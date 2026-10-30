#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "zenora/wal_v6.h"

#define WAL_HEADER_SIZE 24U
#define WAL_RECORD_HEADER_SIZE 44U
#define WAL_MAX_PAYLOAD 16777216U

typedef struct WalV6History
{
    uint64_t lsn;
    uint64_t prev_lsn;
    uint64_t page_id;
    uint32_t type;
    uint16_t page_type;
    struct WalV6History *next;
} WalV6History;

typedef struct WalV6Transaction
{
    uint64_t transaction_id;
    uint64_t last_lsn;
    uint64_t undo_next_lsn;
    ZenoraWalV6TransactionState state;
    WalV6History *history;
    struct WalV6Transaction *next;
} WalV6Transaction;

struct ZenoraWalV6
{
    int fd;
    uint64_t next_lsn;
    uint64_t durable_lsn;
    uint32_t format_version;
    int failed;
    WalV6Transaction *transactions;
};

static void put_u16(unsigned char *p, uint16_t v)
{
    p[0] = (unsigned char)v;
    p[1] = (unsigned char)(v >> 8);
}

static void put_u32(unsigned char *p, uint32_t v)
{
    p[0] = (unsigned char)v;
    p[1] = (unsigned char)(v >> 8);
    p[2] = (unsigned char)(v >> 16);
    p[3] = (unsigned char)(v >> 24);
}

static void put_u64(unsigned char *p, uint64_t v)
{
    put_u32(p, (uint32_t)v);
    put_u32(p + 4, (uint32_t)(v >> 32));
}

static uint16_t get_u16(const unsigned char *p)
{
    return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}

static uint32_t get_u32(const unsigned char *p)
{
    return (uint32_t)p[0]
        | ((uint32_t)p[1] << 8)
        | ((uint32_t)p[2] << 16)
        | ((uint32_t)p[3] << 24);
}

static uint64_t get_u64(const unsigned char *p)
{
    return (uint64_t)get_u32(p)
        | ((uint64_t)get_u32(p + 4) << 32);
}

static uint32_t hash_update(
    uint32_t hash,
    const unsigned char *data,
    size_t length
)
{
    for (size_t i = 0; i < length; i++)
    {
        hash ^= data[i];
        hash *= 16777619U;
    }

    return hash;
}

static uint32_t hash_data(const unsigned char *data, size_t length)
{
    return hash_update(2166136261U, data, length);
}

static int write_all(int fd, const unsigned char *data, size_t length)
{
    size_t offset = 0;

    while (offset < length)
    {
        ssize_t n = write(fd, data + offset, length - offset);

        if (n < 0)
        {
            if (errno == EINTR)
                continue;
            return -1;
        }

        if (n == 0)
            return -1;

        offset += (size_t)n;
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
        ssize_t n = pread(
            fd,
            data + offset,
            length - offset,
            position + (off_t)offset
        );

        if (n < 0)
        {
            if (errno == EINTR)
                continue;
            return -1;
        }

        if (n == 0)
            return -1;

        offset += (size_t)n;
    }

    return 0;
}

static void free_history(WalV6History *history)
{
    while (history != NULL)
    {
        WalV6History *next = history->next;
        free(history);
        history = next;
    }
}

static void free_transactions(WalV6Transaction *transaction)
{
    while (transaction != NULL)
    {
        WalV6Transaction *next = transaction->next;
        free_history(transaction->history);
        free(transaction);
        transaction = next;
    }
}

static WalV6Transaction *find_transaction(
    WalV6Transaction *transactions,
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

static WalV6History *history_find(
    const WalV6Transaction *transaction,
    uint64_t lsn
)
{
    const WalV6History *entry = transaction->history;

    while (entry != NULL)
    {
        if (entry->lsn == lsn)
            return (WalV6History *)entry;

        entry = entry->next;
    }

    return NULL;
}

static int valid_undo_chain(
    const WalV6Transaction *transaction,
    uint64_t lsn
)
{
    while (lsn != 0)
    {
        WalV6History *entry = history_find(transaction, lsn);

        if (entry == NULL)
            return 0;

        if (entry->type == ZENORA_WAL_RECORD_BEGIN)
            return 1;

        if (entry->type != ZENORA_WAL_RECORD_PAGE_UPDATE)
            return 0;

        lsn = entry->prev_lsn;
    }

    return 0;
}

static int valid_page_type(uint16_t type)
{
    return type == 1U || type == 2U;
}

static int validate_payload(
    uint32_t type,
    uint64_t page_id,
    const unsigned char *payload,
    uint32_t length
)
{
    if (type == ZENORA_WAL_RECORD_BEGIN ||
        type == ZENORA_WAL_RECORD_COMMIT ||
        type == ZENORA_WAL_RECORD_ABORT ||
        type == ZENORA_WAL_V6_RECORD_END)
        return page_id == 0 && length == 0 ? 0 : -1;

    if (page_id == 0 || payload == NULL)
        return -1;

    if (type == ZENORA_WAL_RECORD_UPDATE)
        return 0;

    if (type == ZENORA_WAL_RECORD_PAGE_UPDATE)
    {
        if (length != ZENORA_WAL_V6_PAGE_UPDATE_PAYLOAD_SIZE ||
            get_u32(payload) != 1U ||
            !valid_page_type(get_u16(payload + 4)) ||
            !valid_page_type(get_u16(payload + 6)) ||
            get_u32(payload + 16) != ZENORA_WAL_V6_IMAGE_SIZE ||
            get_u32(payload + 20) != 0U)
            return -1;

        return 0;
    }

    if (type == ZENORA_WAL_RECORD_CLR)
    {
        if (length != ZENORA_WAL_V6_CLR_PAYLOAD_SIZE ||
            get_u32(payload) != 1U ||
            !valid_page_type(get_u16(payload + 4)) ||
            get_u16(payload + 6) != 0U ||
            get_u32(payload + 8) != 0U ||
            get_u32(payload + 12) != ZENORA_WAL_V6_IMAGE_SIZE)
            return -1;

        return 0;
    }

    if (type == ZENORA_WAL_RECORD_PAGE_ALLOCATE)
    {
        if (length != ZENORA_WAL_V6_PAGE_ALLOCATE_PAYLOAD_SIZE ||
            get_u32(payload) != 1U ||
            !valid_page_type(get_u16(payload + 4)) ||
            get_u16(payload + 6) != 0U ||
            get_u32(payload + 8) != 0U)
            return -1;

        return 0;
    }

    return -1;
}

static int apply_record(
    WalV6Transaction **transactions,
    uint64_t transaction_id,
    uint64_t lsn,
    uint64_t prev_lsn,
    uint32_t type,
    uint64_t page_id,
    const unsigned char *payload
)
{
    if (transaction_id == 0 || lsn == 0)
        return -1;

    WalV6Transaction *transaction =
        find_transaction(*transactions, transaction_id);

    if (type == ZENORA_WAL_RECORD_BEGIN)
    {
        if (prev_lsn != 0 || transaction != NULL)
            return -1;

        transaction = calloc(1, sizeof(*transaction));
        WalV6History *history = calloc(1, sizeof(*history));

        if (transaction == NULL || history == NULL)
        {
            free(transaction);
            free(history);
            return -1;
        }

        history->lsn = lsn;
        history->type = type;

        transaction->transaction_id = transaction_id;
        transaction->last_lsn = lsn;
        transaction->state = ZENORA_WAL_V6_TX_ACTIVE;
        transaction->history = history;
        transaction->next = *transactions;
        *transactions = transaction;

        return 0;
    }

    if (transaction == NULL ||
        transaction->state == ZENORA_WAL_V6_TX_ENDED ||
        transaction->last_lsn != prev_lsn)
        return -1;

    ZenoraWalV6TransactionState new_state = transaction->state;
    uint64_t new_undo_next = transaction->undo_next_lsn;

    if (transaction->state == ZENORA_WAL_V6_TX_ACTIVE)
    {
        if (type == ZENORA_WAL_RECORD_COMMIT)
        {
            new_state = ZENORA_WAL_V6_TX_COMMITTED;
        }
        else if (type == ZENORA_WAL_RECORD_ABORT)
        {
            if (!valid_undo_chain(transaction, prev_lsn))
                return -1;

            new_state = ZENORA_WAL_V6_TX_ABORTING;
            new_undo_next = prev_lsn;
        }
        else if (type != ZENORA_WAL_RECORD_UPDATE &&
                 type != ZENORA_WAL_RECORD_PAGE_UPDATE &&
                 type != ZENORA_WAL_RECORD_PAGE_ALLOCATE)
        {
            return -1;
        }
    }
    else if (transaction->state == ZENORA_WAL_V6_TX_ABORTING)
    {
        if (type == ZENORA_WAL_RECORD_CLR)
        {
            WalV6History *target =
                history_find(transaction, transaction->undo_next_lsn);

            uint64_t undo_next = get_u64(payload + 16);

            if (target == NULL ||
                target->type != ZENORA_WAL_RECORD_PAGE_UPDATE ||
                target->page_id != page_id ||
                target->page_type != get_u16(payload + 4) ||
                undo_next != target->prev_lsn ||
                undo_next >= target->lsn)
                return -1;

            if (undo_next != 0 &&
                history_find(transaction, undo_next) == NULL)
                return -1;

            new_undo_next = undo_next;
        }
        else if (type == ZENORA_WAL_V6_RECORD_END)
        {
            WalV6History *target =
                history_find(transaction, transaction->undo_next_lsn);

            if (target == NULL ||
                target->type != ZENORA_WAL_RECORD_BEGIN)
                return -1;

            new_state = ZENORA_WAL_V6_TX_ENDED;
        }
        else
        {
            return -1;
        }
    }
    else if (transaction->state == ZENORA_WAL_V6_TX_COMMITTED)
    {
        if (type != ZENORA_WAL_V6_RECORD_END)
            return -1;

        new_state = ZENORA_WAL_V6_TX_ENDED;
    }
    else
    {
        return -1;
    }

    WalV6History *history = calloc(1, sizeof(*history));

    if (history == NULL)
        return -1;

    history->lsn = lsn;
    history->prev_lsn = prev_lsn;
    history->page_id = page_id;
    history->type = type;

    if (type == ZENORA_WAL_RECORD_PAGE_UPDATE)
        history->page_type = get_u16(payload + 6);
    else if (type == ZENORA_WAL_RECORD_PAGE_ALLOCATE)
        history->page_type = get_u16(payload + 4);

    history->next = transaction->history;

    transaction->state = new_state;
    transaction->undo_next_lsn = new_undo_next;
    transaction->history = history;
    transaction->last_lsn = lsn;

    return 0;
}

static int validate_records(
    ZenoraWalV6 *wal,
    ZenoraWalVisitor visitor,
    void *context
)
{
    struct stat status;

    if (fstat(wal->fd, &status) != 0)
        return -1;

    off_t offset = WAL_HEADER_SIZE;
    uint64_t previous_lsn = 0;
    WalV6Transaction *transactions = NULL;

    while (offset < status.st_size)
    {
        unsigned char header[WAL_RECORD_HEADER_SIZE];

        if (status.st_size - offset < WAL_RECORD_HEADER_SIZE)
            goto failure;

        if (read_all(
                wal->fd,
                header,
                sizeof(header),
                offset
            ) != 0)
            goto failure;

        uint64_t lsn = get_u64(header);
        uint64_t transaction_id = get_u64(header + 8);
        uint64_t prev_lsn = get_u64(header + 16);
        uint32_t type = get_u32(header + 24);
        uint64_t page_id = get_u64(header + 28);
        uint32_t payload_length = get_u32(header + 36);
        uint32_t stored_hash = get_u32(header + 40);

        if (previous_lsn == UINT64_MAX ||
            lsn != previous_lsn + 1 ||
            lsn == 0 ||
            type < ZENORA_WAL_RECORD_BEGIN ||
            type > ZENORA_WAL_V6_RECORD_END ||
            payload_length > WAL_MAX_PAYLOAD)
            goto failure;

        if ((uint64_t)(status.st_size - offset - WAL_RECORD_HEADER_SIZE)
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
                    offset + WAL_RECORD_HEADER_SIZE
                ) != 0)
            {
                free(payload);
                goto failure;
            }
        }

        uint32_t calculated = hash_data(header, 40);
        calculated = hash_update(calculated, payload, payload_length);

        if (calculated != stored_hash ||
            validate_payload(type, page_id, payload, payload_length) != 0 ||
            apply_record(
                &transactions,
                transaction_id,
                lsn,
                prev_lsn,
                type,
                page_id,
                payload
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
        offset += WAL_RECORD_HEADER_SIZE + payload_length;
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

int zenora_wal_v6_open(const char *path, ZenoraWalV6 **wal)
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

    if (status.st_size == 0)
    {
        unsigned char header[WAL_HEADER_SIZE] = {0};

        memcpy(header, "ZENORAWL", 8);
        put_u32(header + 8, ZENORA_WAL_V6_VERSION);
        put_u32(header + 12, WAL_RECORD_HEADER_SIZE);
        put_u32(header + 16, hash_data(header, 16));

        if (write_all(fd, header, sizeof(header)) != 0 || fsync(fd) != 0)
        {
            close(fd);
            return -1;
        }
    }
    else
    {
        unsigned char header[WAL_HEADER_SIZE];

        if (status.st_size < WAL_HEADER_SIZE ||
            read_all(fd, header, sizeof(header), 0) != 0 ||
            memcmp(header, "ZENORAWL", 8) != 0 ||
            get_u32(header + 8) != ZENORA_WAL_V6_VERSION ||
            get_u32(header + 12) != WAL_RECORD_HEADER_SIZE ||
            get_u32(header + 16) != hash_data(header, 16))
        {
            close(fd);
            return -1;
        }
    }

    ZenoraWalV6 *instance = calloc(1, sizeof(*instance));

    if (instance == NULL)
    {
        close(fd);
        return -1;
    }

    instance->fd = fd;
    instance->next_lsn = 1;
    instance->format_version = ZENORA_WAL_V6_VERSION;

    if (validate_records(instance, NULL, NULL) != 0 || fsync(fd) != 0)
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

int zenora_wal_v6_append_ex(
    ZenoraWalV6 *wal,
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
        wal->format_version != ZENORA_WAL_V6_VERSION ||
        transaction_id == 0 ||
        wal->next_lsn == UINT64_MAX ||
        payload_length > WAL_MAX_PAYLOAD ||
        (payload_length > 0 && payload == NULL) ||
        type < ZENORA_WAL_RECORD_BEGIN ||
        type > ZENORA_WAL_V6_RECORD_END ||
        type == ZENORA_WAL_RECORD_PAGE_ALLOCATE)
        return -1;

    if (validate_payload(
            type,
            page_id,
            (const unsigned char *)payload,
            payload_length
        ) != 0)
        return -1;

    WalV6Transaction *previous_transaction =
        find_transaction(wal->transactions, transaction_id);

    int created_transaction = type == ZENORA_WAL_RECORD_BEGIN;
    ZenoraWalV6TransactionState previous_state =
        previous_transaction != NULL
            ? previous_transaction->state
            : ZENORA_WAL_V6_TX_ACTIVE;
    uint64_t previous_undo_next =
        previous_transaction != NULL
            ? previous_transaction->undo_next_lsn
            : 0;
    uint64_t previous_last_lsn =
        previous_transaction != NULL
            ? previous_transaction->last_lsn
            : 0;
    WalV6History *previous_history =
        previous_transaction != NULL
            ? previous_transaction->history
            : NULL;

    if (apply_record(
            &wal->transactions,
            transaction_id,
            wal->next_lsn,
            prev_lsn,
            type,
            page_id,
            (const unsigned char *)payload
        ) != 0)
        return -1;

    unsigned char header[WAL_RECORD_HEADER_SIZE] = {0};

    put_u64(header, wal->next_lsn);
    put_u64(header + 8, transaction_id);
    put_u64(header + 16, prev_lsn);
    put_u32(header + 24, type);
    put_u64(header + 28, page_id);
    put_u32(header + 36, payload_length);

    uint32_t hash = hash_data(header, 40);
    hash = hash_update(hash, payload, payload_length);
    put_u32(header + 40, hash);

    if (lseek(wal->fd, 0, SEEK_END) < 0 ||
        write_all(wal->fd, header, sizeof(header)) != 0 ||
        (payload_length > 0 &&
         write_all(wal->fd, payload, payload_length) != 0))
    {
        WalV6Transaction *updated =
            find_transaction(wal->transactions, transaction_id);

        if (created_transaction)
        {
            WalV6Transaction **cursor = &wal->transactions;

            while (*cursor != NULL && *cursor != updated)
                cursor = &(*cursor)->next;

            if (*cursor == updated)
            {
                *cursor = updated->next;
                free_history(updated->history);
                free(updated);
            }
        }
        else if (updated != NULL)
        {
            WalV6History *added_history = updated->history;
            updated->history = previous_history;
            free(added_history);
            updated->state = previous_state;
            updated->undo_next_lsn = previous_undo_next;
            updated->last_lsn = previous_last_lsn;
        }

        wal->failed = 1;
        return -1;
    }

    uint64_t appended_lsn = wal->next_lsn++;

    if (lsn != NULL)
        *lsn = appended_lsn;

    return 0;
}

int zenora_wal_v6_append(
    ZenoraWalV6 *wal,
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
        WalV6Transaction *transaction =
            find_transaction(wal->transactions, transaction_id);

        if (transaction == NULL)
            return -1;

        prev_lsn = transaction->last_lsn;
    }

    return zenora_wal_v6_append_ex(
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

int zenora_wal_v6_sync(ZenoraWalV6 *wal)
{
    if (wal == NULL || wal->failed ||
        wal->format_version != ZENORA_WAL_V6_VERSION)
        return -1;

    if (fsync(wal->fd) != 0)
    {
        wal->failed = 1;
        return -1;
    }

    wal->durable_lsn = wal->next_lsn - 1;
    return 0;
}

int zenora_wal_v6_get_durable_lsn(
    const ZenoraWalV6 *wal,
    uint64_t *durable_lsn
)
{
    if (wal == NULL || durable_lsn == NULL)
        return -1;

    *durable_lsn = wal->durable_lsn;
    return 0;
}

int zenora_wal_v6_scan(
    ZenoraWalV6 *wal,
    ZenoraWalVisitor visitor,
    void *context
)
{
    if (wal == NULL || wal->failed ||
        wal->format_version != ZENORA_WAL_V6_VERSION)
        return -1;

    return validate_records(wal, visitor, context);
}

int zenora_wal_v6_close(ZenoraWalV6 *wal)
{
    if (wal == NULL)
        return -1;

    int result = close(wal->fd);
    free_transactions(wal->transactions);
    free(wal);

    return result == 0 ? 0 : -1;
}
