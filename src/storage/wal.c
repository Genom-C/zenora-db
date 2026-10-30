#define _POSIX_C_SOURCE 200809L
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "zenora/wal.h"

#define ZENORA_WAL_HEADER_SIZE 24
#define ZENORA_WAL_RECORD_HEADER_SIZE 44
#define ZENORA_WAL_MAX_PAYLOAD 16777216
#define ZENORA_WAL_VERSION 3

typedef struct ZenoraWalTransaction
{
    uint64_t transaction_id;
    uint64_t last_lsn;
    int active;
    struct ZenoraWalTransaction *next;
} ZenoraWalTransaction;

struct ZenoraWal
{
    int fd;
    uint64_t next_lsn;
    uint64_t durable_lsn;
    int failed;
    uint32_t format_version;
    ZenoraWalTransaction *transactions;
};

static void free_transactions(ZenoraWalTransaction *transaction)
{
    while (transaction != NULL)
    {
        ZenoraWalTransaction *next = transaction->next;
        free(transaction);
        transaction = next;
    }
}

static ZenoraWalTransaction *find_transaction(
    ZenoraWalTransaction *transactions,
    uint64_t transaction_id
)
{
    while (transactions != NULL)
    {
        if (transactions->transaction_id == transaction_id)
            return transactions;

        transactions = transactions->next;
    }

    return NULL;
}

static int apply_record(
    ZenoraWalTransaction **transactions,
    uint64_t transaction_id,
    uint64_t lsn,
    uint64_t prev_lsn,
    uint32_t type
)
{
    if (transaction_id == 0 || lsn == 0)
        return -1;

    ZenoraWalTransaction *transaction =
        find_transaction(*transactions, transaction_id);

    if (type == ZENORA_WAL_RECORD_BEGIN)
    {
        if (prev_lsn != 0 || transaction != NULL)
            return -1;

        transaction = calloc(1, sizeof(ZenoraWalTransaction));

        if (transaction == NULL)
            return -1;

        transaction->transaction_id = transaction_id;
        transaction->last_lsn = lsn;
        transaction->active = 1;
        transaction->next = *transactions;
        *transactions = transaction;

        return 0;
    }

    if (transaction == NULL ||
        !transaction->active ||
        transaction->last_lsn != prev_lsn)
        return -1;

    transaction->last_lsn = lsn;

    if (type == ZENORA_WAL_RECORD_COMMIT ||
        type == ZENORA_WAL_RECORD_ABORT)
        transaction->active = 0;

    return 0;
}

static void encode_u32(unsigned char *buffer, uint32_t value)
{
    buffer[0] = (unsigned char)value;
    buffer[1] = (unsigned char)(value >> 8);
    buffer[2] = (unsigned char)(value >> 16);
    buffer[3] = (unsigned char)(value >> 24);
}

static void encode_u64(unsigned char *buffer, uint64_t value)
{
    encode_u32(buffer, (uint32_t)value);
    encode_u32(buffer + 4, (uint32_t)(value >> 32));
}

static uint32_t decode_u32(const unsigned char *buffer)
{
    return (uint32_t)buffer[0]
        | ((uint32_t)buffer[1] << 8)
        | ((uint32_t)buffer[2] << 16)
        | ((uint32_t)buffer[3] << 24);
}

static uint64_t decode_u64(const unsigned char *buffer)
{
    return (uint64_t)decode_u32(buffer)
        | ((uint64_t)decode_u32(buffer + 4) << 32);
}

static uint32_t checksum_update(
    uint32_t hash,
    const unsigned char *data,
    size_t length
)
{
    for (size_t i = 0; i < length; i++)
    {
        hash ^= data[i];
        hash *= 16777619u;
    }

    return hash;
}

static uint32_t checksum(const unsigned char *data, size_t length)
{
    return checksum_update(2166136261u, data, length);
}

static int write_all(int fd, const unsigned char *data, size_t length)
{
    size_t offset = 0;

    while (offset < length)
    {
        ssize_t written = write(fd, data + offset, length - offset);

        if (written < 0)
        {
            if (errno == EINTR)
                continue;

            return -1;
        }

        if (written == 0)
            return -1;

        offset += (size_t)written;
    }

    return 0;
}

static int read_all(
    int fd,
    unsigned char *data,
    size_t length,
    off_t position
)
{
    size_t offset = 0;

    while (offset < length)
    {
        ssize_t received = pread(
            fd,
            data + offset,
            length - offset,
            position + (off_t)offset
        );

        if (received < 0)
        {
            if (errno == EINTR)
                continue;

            return -1;
        }

        if (received == 0)
            return -1;

        offset += (size_t)received;
    }

    return 0;
}

static int valid_page_type(uint16_t page_type)
{
    return page_type == 1U || page_type == 2U;
}

static uint16_t decode_u16(const unsigned char *buffer)
{
    return (uint16_t)buffer[0] | ((uint16_t)buffer[1] << 8);
}

static int validate_v5_record(
    uint32_t type,
    uint64_t page_id,
    const unsigned char *payload,
    uint32_t payload_length
)
{
    if (type == ZENORA_WAL_RECORD_BEGIN ||
        type == ZENORA_WAL_RECORD_COMMIT ||
        type == ZENORA_WAL_RECORD_ABORT)
        return page_id == 0 && payload_length == 0 ? 0 : -1;

    if (page_id == 0 || payload == NULL)
        return -1;

    if (type == ZENORA_WAL_RECORD_PAGE_UPDATE)
    {
        if (payload_length != ZENORA_WAL_V5_PAGE_UPDATE_PAYLOAD_SIZE ||
            decode_u32(payload) != 1U ||
            !valid_page_type(decode_u16(payload + 4)) ||
            !valid_page_type(decode_u16(payload + 6)) ||
            decode_u32(payload + 16) != ZENORA_WAL_V5_IMAGE_SIZE ||
            decode_u32(payload + 20) != 0U)
            return -1;

        return 0;
    }

    if (type == ZENORA_WAL_RECORD_CLR)
    {
        if (payload_length != ZENORA_WAL_V5_CLR_PAYLOAD_SIZE ||
            decode_u32(payload) != 1U ||
            !valid_page_type(decode_u16(payload + 4)) ||
            decode_u16(payload + 6) != 0U ||
            decode_u32(payload + 12) != ZENORA_WAL_V5_IMAGE_SIZE)
            return -1;

        return 0;
    }

    if (type == ZENORA_WAL_RECORD_PAGE_ALLOCATE)
    {
        if (payload_length != ZENORA_WAL_V5_PAGE_ALLOCATE_PAYLOAD_SIZE ||
            decode_u32(payload) != 1U ||
            !valid_page_type(decode_u16(payload + 4)) ||
            decode_u16(payload + 6) != 0U)
            return -1;

        return 0;
    }

    return -1;
}

static int validate_records(
    ZenoraWal *wal,
    ZenoraWalVisitor visitor,
    void *context
)
{
    struct stat status;

    if (fstat(wal->fd, &status) != 0)
        return -1;

    uint32_t record_header_size = wal->format_version == 2 ? 40U : 44U;
    off_t offset = ZENORA_WAL_HEADER_SIZE;
    uint64_t previous_lsn = 0;
    ZenoraWalTransaction *transactions = NULL;

    while (offset < status.st_size)
    {
        unsigned char header[ZENORA_WAL_RECORD_HEADER_SIZE];

        if (status.st_size - offset < (off_t)record_header_size)
            goto failure;

        if (read_all(wal->fd, header, record_header_size, offset) != 0)
            goto failure;

        uint64_t lsn = decode_u64(header);
        uint64_t transaction_id = decode_u64(header + 8);
        uint64_t prev_lsn = decode_u64(header + 16);
        uint32_t type = decode_u32(header + 24);
        uint64_t page_id = wal->format_version == 2
            ? decode_u32(header + 28)
            : decode_u64(header + 28);
        uint32_t payload_length = decode_u32(
            header + (wal->format_version == 2 ? 32 : 36)
        );
        uint32_t stored_checksum = decode_u32(
            header + (wal->format_version == 2 ? 36 : 40)
        );

        if (lsn == 0 ||
            (wal->format_version == 5
                ? (previous_lsn == UINT64_MAX || lsn != previous_lsn + 1)
                : lsn <= previous_lsn) ||
            type < ZENORA_WAL_RECORD_BEGIN ||
            type > (wal->format_version == 5
                ? ZENORA_WAL_RECORD_PAGE_ALLOCATE
                : ZENORA_WAL_RECORD_ABORT))
            goto failure;

        if (payload_length > ZENORA_WAL_MAX_PAYLOAD)
            goto failure;

        if ((uint64_t)(status.st_size - offset - record_header_size)
            < payload_length)
            goto failure;

        unsigned char *payload = NULL;

        if (payload_length > 0)
        {
            payload = malloc(payload_length);

            if (payload == NULL)
                goto failure;

            if (read_all(
                    wal->fd,
                    payload,
                    payload_length,
                    offset + record_header_size
                ) != 0)
            {
                free(payload);
                goto failure;
            }
        }

        uint32_t calculated = checksum(
            header,
            record_header_size - 4
        );
        calculated = checksum_update(calculated, payload, payload_length);

        if (calculated != stored_checksum)
        {
            free(payload);
            goto failure;
        }

        if ((wal->format_version == 5 &&
             validate_v5_record(type, page_id, payload, payload_length) != 0) ||
            apply_record(
                &transactions,
                transaction_id,
                lsn,
                prev_lsn,
                type
            ) != 0)
        {
            free(payload);
            goto failure;
        }

        ZenoraWalRecord record;
        record.lsn = lsn;
        record.transaction_id = transaction_id;
        record.prev_lsn = prev_lsn;
        record.type = type;
        record.page_id = page_id;
        record.payload_length = payload_length;
        record.payload = payload;

        if (visitor != NULL && visitor(&record, context) != 0)
        {
            free(payload);
            goto failure;
        }

        free(payload);

        previous_lsn = lsn;
        offset += record_header_size + payload_length;
    }

    if (previous_lsn == UINT64_MAX)
        goto failure;

    free_transactions(wal->transactions);
    wal->transactions = transactions;
    wal->next_lsn = previous_lsn + 1;

    return 0;

failure:
    free_transactions(transactions);
    return -1;
}

static int wal_open_version(
    const char *path,
    ZenoraWal **wal,
    uint32_t requested_version
)
{
    if (path == NULL || wal == NULL)
        return -1;

    *wal = NULL;

    int fd = open(path, O_RDWR | O_CREAT, 0600);

    if (fd < 0)
        return -1;

    struct stat status;

    if (fstat(fd, &status) != 0)
    {
        close(fd);
        return -1;
    }

    uint32_t format_version = requested_version == 5
        ? 5U
        : requested_version == 4
            ? 4U
            : ZENORA_WAL_VERSION;

    if (status.st_size == 0)
    {
        unsigned char header[ZENORA_WAL_HEADER_SIZE] = {0};

        memcpy(header, "ZENORAWL", 8);
        encode_u32(header + 8, format_version);
        encode_u32(header + 12, ZENORA_WAL_RECORD_HEADER_SIZE);
        encode_u32(header + 16, checksum(header, 16));

        if (write_all(fd, header, sizeof(header)) != 0 || fsync(fd) != 0)
        {
            close(fd);
            return -1;
        }
    }
    else
    {
        unsigned char header[ZENORA_WAL_HEADER_SIZE];

        if (status.st_size < ZENORA_WAL_HEADER_SIZE ||
            read_all(fd, header, sizeof(header), 0) != 0 ||
            memcmp(header, "ZENORAWL", 8) != 0 ||
            decode_u32(header + 16) != checksum(header, 16))
        {
            close(fd);
            return -1;
        }

        format_version = decode_u32(header + 8);
        uint32_t stored_record_size = decode_u32(header + 12);

        int supported = requested_version == 5
            ? format_version == 5 && stored_record_size == 44
            : requested_version == 4
                ? format_version == 4 && stored_record_size == 44
                : (format_version == 2 && stored_record_size == 40) ||
                  (format_version == 3 && stored_record_size == 44);

        if (!supported)
        {
            close(fd);
            return -1;
        }
    }

    ZenoraWal *instance = calloc(1, sizeof(ZenoraWal));

    if (instance == NULL)
    {
        close(fd);
        return -1;
    }

    instance->fd = fd;
    instance->next_lsn = 1;
    instance->format_version = format_version;

    if (validate_records(instance, NULL, NULL) != 0)
    {
        close(fd);
        free_transactions(instance->transactions);
        free(instance);
        return -1;
    }

    if ((format_version == 4 || format_version == 5) && fsync(fd) != 0)
    {
        close(fd);
        free_transactions(instance->transactions);
        free(instance);
        return -1;
    }

    instance->durable_lsn = instance->next_lsn - 1;

    *wal = instance;

    return 0;
}

int zenora_wal_open(const char *path, ZenoraWal **wal)
{
    return wal_open_version(path, wal, 0);
}

int zenora_wal_open_v4(const char *path, ZenoraWal **wal)
{
    return wal_open_version(path, wal, 4);
}

int zenora_wal_open_v5(const char *path, ZenoraWal **wal)
{
    return wal_open_version(path, wal, 5);
}

int zenora_wal_append_ex(
    ZenoraWal *wal,
    uint64_t transaction_id,
    uint64_t prev_lsn,
    uint32_t type,
    uint64_t page_id,
    const void *payload,
    uint32_t payload_length,
    uint64_t *lsn
)
{
    if (wal == NULL ||
        wal->failed ||
        (wal->format_version != 3 &&
         wal->format_version != 4 &&
         wal->format_version != 5))
        return -1;

    if (type < ZENORA_WAL_RECORD_BEGIN ||
        type > (wal->format_version == 5
            ? ZENORA_WAL_RECORD_PAGE_ALLOCATE
            : ZENORA_WAL_RECORD_ABORT))
        return -1;

    if (transaction_id == 0 ||
        payload_length > ZENORA_WAL_MAX_PAYLOAD ||
        (payload_length > 0 && payload == NULL) ||
        wal->next_lsn == UINT64_MAX)
        return -1;

    if (wal->format_version == 5 &&
        validate_v5_record(
            type,
            page_id,
            (const unsigned char *)payload,
            payload_length
        ) != 0)
        return -1;

    ZenoraWalTransaction *transaction =
        find_transaction(wal->transactions, transaction_id);

    if (type == ZENORA_WAL_RECORD_BEGIN)
    {
        if (prev_lsn != 0 || transaction != NULL)
            return -1;
    }
    else
    {
        if (transaction == NULL ||
            !transaction->active ||
            transaction->last_lsn != prev_lsn)
            return -1;
    }

    ZenoraWalTransaction *new_transaction = NULL;

    if (type == ZENORA_WAL_RECORD_BEGIN)
    {
        new_transaction = calloc(1, sizeof(ZenoraWalTransaction));

        if (new_transaction == NULL)
            return -1;
    }

    unsigned char header[ZENORA_WAL_RECORD_HEADER_SIZE] = {0};

    encode_u64(header, wal->next_lsn);
    encode_u64(header + 8, transaction_id);
    encode_u64(header + 16, prev_lsn);
    encode_u32(header + 24, type);
    encode_u64(header + 28, page_id);
    encode_u32(header + 36, payload_length);

    uint32_t hash = checksum(header, 40);
    hash = checksum_update(hash, payload, payload_length);
    encode_u32(header + 40, hash);

    if (lseek(wal->fd, 0, SEEK_END) < 0)
    {
        free(new_transaction);
        wal->failed = 1;
        return -1;
    }

    if (write_all(wal->fd, header, sizeof(header)) != 0)
    {
        free(new_transaction);
        wal->failed = 1;
        return -1;
    }

    if (payload_length > 0 &&
        write_all(wal->fd, (const unsigned char *)payload, payload_length) != 0)
    {
        free(new_transaction);
        wal->failed = 1;
        return -1;
    }

    if (wal->format_version == 3 && fsync(wal->fd) != 0)
    {
        free(new_transaction);
        wal->failed = 1;
        return -1;
    }

    uint64_t appended_lsn = wal->next_lsn;

    if (wal->format_version == 3)
        wal->durable_lsn = appended_lsn;

    if (type == ZENORA_WAL_RECORD_BEGIN)
    {
        new_transaction->transaction_id = transaction_id;
        new_transaction->last_lsn = appended_lsn;
        new_transaction->active = 1;
        new_transaction->next = wal->transactions;
        wal->transactions = new_transaction;
    }
    else
    {
        transaction->last_lsn = appended_lsn;

        if (type == ZENORA_WAL_RECORD_COMMIT ||
            type == ZENORA_WAL_RECORD_ABORT)
            transaction->active = 0;
    }

    if (lsn != NULL)
        *lsn = appended_lsn;

    wal->next_lsn++;

    return 0;
}

int zenora_wal_append(
    ZenoraWal *wal,
    uint64_t transaction_id,
    uint32_t type,
    uint64_t page_id,
    const void *payload,
    uint32_t payload_length,
    uint64_t *lsn
)
{
    if (wal == NULL)
        return -1;

    uint64_t prev_lsn = 0;

    if (type != ZENORA_WAL_RECORD_BEGIN)
    {
        ZenoraWalTransaction *transaction =
            find_transaction(wal->transactions, transaction_id);

        if (transaction == NULL || !transaction->active)
            return -1;

        prev_lsn = transaction->last_lsn;
    }

    return zenora_wal_append_ex(
        wal,
        transaction_id,
        prev_lsn,
        type,
        page_id,
        payload,
        payload_length,
        lsn
    );
}

int zenora_wal_sync(ZenoraWal *wal)
{
    if (wal == NULL || wal->failed || wal->format_version == 2)
        return -1;

    if (fsync(wal->fd) != 0)
    {
        wal->failed = 1;
        return -1;
    }

    wal->durable_lsn = wal->next_lsn - 1;

    return 0;
}

int zenora_wal_get_durable_lsn(
    const ZenoraWal *wal,
    uint64_t *durable_lsn
)
{
    if (wal == NULL || durable_lsn == NULL)
        return -1;

    *durable_lsn = wal->durable_lsn;

    return 0;
}

int zenora_wal_scan(
    ZenoraWal *wal,
    ZenoraWalVisitor visitor,
    void *context
)
{
    if (wal == NULL)
        return -1;

    return validate_records(wal, visitor, context);
}

void zenora_wal_close(ZenoraWal *wal)
{
    if (wal == NULL)
        return;

    close(wal->fd);
    free_transactions(wal->transactions);
    free(wal);
}
