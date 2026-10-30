#define _POSIX_C_SOURCE 200809L
#define _FILE_OFFSET_BITS 64

#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <limits.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>

#include "zenora/storage_handle.h"
#include "zenora/storage.h"
#include "zenora/header_codec.h"
#include "zenora/page_codec.h"

struct ZenoraStorageHandle
{
    int fd;
    int writable;
    int failed;
    uint32_t format_version;
    uint64_t page_count;
};

static const char ZENORA_MAGIC[8] = {
    'Z', 'E', 'N', 'O', 'R', 'A', 'D', 'B'
};

static uint64_t storage_page_size(uint32_t format_version)
{
    if (format_version == ZENORA_STORAGE_FORMAT_V1)
        return ZENORA_PAGE_SIZE;

    if (format_version == ZENORA_STORAGE_FORMAT_V2)
        return ZENORA_PHYSICAL_PAGE_SIZE;

    return 0;
}

static int pread_full(
    int fd,
    void *buffer,
    size_t length,
    off_t offset
)
{
    unsigned char *output = buffer;
    size_t completed = 0;

    while (completed < length)
    {
        ssize_t result = pread(
            fd,
            output + completed,
            length - completed,
            offset + (off_t)completed
        );

        if (result < 0)
        {
            if (errno == EINTR)
                continue;

            return -1;
        }

        if (result == 0)
            return -1;

        completed += (size_t)result;
    }

    return 0;
}

static int pwrite_full(
    int fd,
    const void *buffer,
    size_t length,
    off_t offset
)
{
    const unsigned char *input = buffer;
    size_t completed = 0;

    while (completed < length)
    {
        ssize_t result = pwrite(
            fd,
            input + completed,
            length - completed,
            offset + (off_t)completed
        );

        if (result < 0)
        {
            if (errno == EINTR)
                continue;

            return -1;
        }

        if (result == 0)
            return -1;

        completed += (size_t)result;
    }

    return 0;
}

static int page_offset(
    uint32_t format_version,
    uint64_t page_id,
    off_t *offset
)
{
    if (offset == NULL || page_id == 0)
        return -1;

    if (format_version == ZENORA_STORAGE_FORMAT_V1)
    {
        if (page_id > (uint64_t)INT64_MAX / ZENORA_PAGE_SIZE)
            return -1;

        *offset = (off_t)(page_id * ZENORA_PAGE_SIZE);
        return 0;
    }

    if (format_version == ZENORA_STORAGE_FORMAT_V2)
    {
        uint64_t maximum =
            ((uint64_t)INT64_MAX - ZENORA_PAGE_SIZE) /
            ZENORA_PHYSICAL_PAGE_SIZE;

        if (page_id - 1 > maximum)
            return -1;

        *offset = (off_t)(
            ZENORA_PAGE_SIZE +
            (page_id - 1) * ZENORA_PHYSICAL_PAGE_SIZE
        );

        return 0;
    }

    return -1;
}

static int validate_header(
    int fd,
    ZenoraHeader *header
)
{
    unsigned char page[ZENORA_PAGE_SIZE];

    if (pread_full(fd, page, sizeof(page), 0) != 0)
        return -1;

    if (zenora_header_decode(page, header) != 0)
        return -1;

    if (memcmp(header->magic, ZENORA_MAGIC, sizeof(ZENORA_MAGIC)) != 0)
        return -1;

    if (header->page_size != ZENORA_PAGE_SIZE)
        return -1;

    uint64_t stride = storage_page_size(header->format_version);

    if (stride == 0)
        return -1;

    if (header->page_count >
        ((uint64_t)INT64_MAX - ZENORA_PAGE_SIZE) / stride)
        return -1;

    struct stat status;

    if (fstat(fd, &status) != 0 || status.st_size < 0)
        return -1;

    uint64_t expected_size =
        ZENORA_PAGE_SIZE + header->page_count * stride;

    if ((uint64_t)status.st_size != expected_size)
        return -1;

    return 0;
}

int zenora_storage_handle_open(
    const char *path,
    int writable,
    ZenoraStorageHandle **handle
)
{
    if (path == NULL || path[0] == '\0' ||
        handle == NULL || (writable != 0 && writable != 1))
        return -1;

    *handle = NULL;

    int flags = writable ? O_RDWR : O_RDONLY;
    int fd = open(path, flags);

    if (fd < 0)
        return -2;

    ZenoraHeader header = {0};

    if (validate_header(fd, &header) != 0)
    {
        close(fd);
        return -3;
    }

    ZenoraStorageHandle *instance = calloc(1, sizeof(*instance));

    if (instance == NULL)
    {
        close(fd);
        return -4;
    }

    instance->fd = fd;
    instance->writable = writable;
    instance->format_version = header.format_version;
    instance->page_count = header.page_count;

    *handle = instance;

    return 0;
}

int zenora_storage_handle_close(ZenoraStorageHandle *handle)
{
    if (handle == NULL)
        return -1;

    int result = close(handle->fd);

    free(handle);

    return result == 0 ? 0 : -2;
}

int zenora_storage_handle_read_page(
    ZenoraStorageHandle *handle,
    uint64_t page_id,
    void *buffer
)
{
    if (handle == NULL || buffer == NULL || page_id == 0)
        return -1;

    if (handle->failed)
        return -2;

    if (page_id > handle->page_count)
        return -3;

    off_t offset;

    if (page_offset(handle->format_version, page_id, &offset) != 0)
        return -4;

    if (handle->format_version == ZENORA_STORAGE_FORMAT_V1)
        return pread_full(
            handle->fd,
            buffer,
            ZENORA_PAGE_SIZE,
            offset
        );

    unsigned char physical_page[ZENORA_PHYSICAL_PAGE_SIZE];
    ZenoraPageMetadata metadata = {0};

    if (pread_full(
            handle->fd,
            physical_page,
            sizeof(physical_page),
            offset) != 0)
        return -5;

    if (zenora_page_codec_decode(
            physical_page,
            &metadata,
            buffer) != 0 ||
        metadata.page_id != page_id)
        return -6;

    return 0;
}

int zenora_storage_handle_write_page(
    ZenoraStorageHandle *handle,
    uint64_t page_id,
    const void *buffer
)
{
    if (handle == NULL || buffer == NULL || page_id == 0)
        return -1;

    if (!handle->writable || handle->failed)
        return -2;

    if (page_id > handle->page_count)
        return -3;

    off_t offset;

    if (page_offset(handle->format_version, page_id, &offset) != 0)
        return -4;

    int result;

    if (handle->format_version == ZENORA_STORAGE_FORMAT_V1)
    {
        result = pwrite_full(
            handle->fd,
            buffer,
            ZENORA_PAGE_SIZE,
            offset
        );
    }
    else
    {
        unsigned char existing_page[ZENORA_PHYSICAL_PAGE_SIZE];
        unsigned char physical_page[ZENORA_PHYSICAL_PAGE_SIZE];
        unsigned char existing_payload[ZENORA_PAGE_SIZE];
        ZenoraPageMetadata metadata = {0};

        if (pread_full(
                handle->fd,
                existing_page,
                sizeof(existing_page),
                offset) != 0)
        {
            handle->failed = 1;
            return -5;
        }

        if (zenora_page_codec_decode(
                existing_page,
                &metadata,
                existing_payload) != 0 ||
            metadata.page_id != page_id)
        {
            handle->failed = 1;
            return -6;
        }

        if (zenora_page_codec_encode(
                physical_page,
                &metadata,
                buffer) != 0)
            return -7;

        result = pwrite_full(
            handle->fd,
            physical_page,
            sizeof(physical_page),
            offset
        );
    }

    if (result != 0)
    {
        handle->failed = 1;
        return -8;
    }

    if (fdatasync(handle->fd) != 0)
    {
        handle->failed = 1;
        return -9;
    }

    return 0;
}

int zenora_storage_handle_read_page_with_metadata(
    ZenoraStorageHandle *handle,
    uint64_t page_id,
    void *buffer,
    ZenoraPageMetadata *metadata
)
{
    if (handle == NULL || buffer == NULL || metadata == NULL || page_id == 0)
        return -1;

    if (handle->failed)
        return -2;

    if (handle->format_version != ZENORA_STORAGE_FORMAT_V2)
        return -3;

    if (page_id > handle->page_count)
        return -4;

    off_t offset;

    if (page_offset(handle->format_version, page_id, &offset) != 0)
        return -5;

    unsigned char physical_page[ZENORA_PHYSICAL_PAGE_SIZE];
    ZenoraPageMetadata decoded = {0};

    if (pread_full(
            handle->fd,
            physical_page,
            sizeof(physical_page),
            offset) != 0)
    {
        handle->failed = 1;
        return -6;
    }

    if (zenora_page_codec_decode(
            physical_page,
            &decoded,
            buffer) != 0 ||
        decoded.page_id != page_id)
    {
        handle->failed = 1;
        return -7;
    }

    *metadata = decoded;

    return 0;
}

int zenora_storage_handle_write_page_with_metadata(
    ZenoraStorageHandle *handle,
    uint64_t page_id,
    const void *buffer,
    const ZenoraPageMetadata *metadata
)
{
    if (handle == NULL || buffer == NULL || metadata == NULL || page_id == 0)
        return -1;

    if (!handle->writable || handle->failed)
        return -2;

    if (handle->format_version != ZENORA_STORAGE_FORMAT_V2)
        return -3;

    if (page_id > handle->page_count || metadata->page_id != page_id)
        return -4;

    if (metadata->page_type != ZENORA_PAGE_TYPE_DATA &&
        metadata->page_type != ZENORA_PAGE_TYPE_METADATA)
        return -5;

    off_t offset;

    if (page_offset(handle->format_version, page_id, &offset) != 0)
        return -6;

    unsigned char existing_page[ZENORA_PHYSICAL_PAGE_SIZE];
    unsigned char existing_payload[ZENORA_PAGE_SIZE];
    unsigned char physical_page[ZENORA_PHYSICAL_PAGE_SIZE];
    ZenoraPageMetadata existing_metadata = {0};

    if (pread_full(
            handle->fd,
            existing_page,
            sizeof(existing_page),
            offset) != 0)
    {
        handle->failed = 1;
        return -7;
    }

    if (zenora_page_codec_decode(
            existing_page,
            &existing_metadata,
            existing_payload) != 0 ||
        existing_metadata.page_id != page_id)
    {
        handle->failed = 1;
        return -8;
    }

    if (zenora_page_codec_encode(
            physical_page,
            metadata,
            buffer) != 0)
        return -9;

    if (pwrite_full(
            handle->fd,
            physical_page,
            sizeof(physical_page),
            offset) != 0)
    {
        handle->failed = 1;
        return -10;
    }

    if (fdatasync(handle->fd) != 0)
    {
        handle->failed = 1;
        return -11;
    }

    return 0;
}

int zenora_storage_handle_allocate_page(
    ZenoraStorageHandle *handle,
    uint64_t *page_id
)
{
    if (handle == NULL || page_id == NULL)
        return -1;

    if (!handle->writable || handle->failed)
        return -2;

    uint64_t stride = storage_page_size(handle->format_version);

    if (stride == 0 ||
        handle->page_count >=
        ((uint64_t)INT64_MAX - ZENORA_PAGE_SIZE) / stride)
        return -3;

    uint64_t next_page = handle->page_count + 1;
    off_t offset;

    if (page_offset(handle->format_version, next_page, &offset) != 0)
        return -4;

    unsigned char payload[ZENORA_PAGE_SIZE] = {0};
    int result;

    if (handle->format_version == ZENORA_STORAGE_FORMAT_V1)
    {
        result = pwrite_full(
            handle->fd,
            payload,
            sizeof(payload),
            offset
        );
    }
    else
    {
        unsigned char physical_page[ZENORA_PHYSICAL_PAGE_SIZE];

        ZenoraPageMetadata metadata = {
            next_page,
            0,
            ZENORA_PAGE_TYPE_DATA,
            0
        };

        if (zenora_page_codec_encode(
                physical_page,
                &metadata,
                payload) != 0)
            return -5;

        result = pwrite_full(
            handle->fd,
            physical_page,
            sizeof(physical_page),
            offset
        );
    }

    if (result != 0 || fdatasync(handle->fd) != 0)
    {
        handle->failed = 1;
        return -6;
    }

    unsigned char header_page[ZENORA_PAGE_SIZE];
    ZenoraHeader header = {0};

    memcpy(header.magic, ZENORA_MAGIC, sizeof(ZENORA_MAGIC));
    header.format_version = handle->format_version;
    header.page_size = ZENORA_PAGE_SIZE;
    header.page_count = next_page;

    zenora_header_encode(header_page, &header);

    if (pwrite_full(
            handle->fd,
            header_page,
            sizeof(header_page),
            0) != 0 ||
        fdatasync(handle->fd) != 0)
    {
        handle->failed = 1;
        return -7;
    }

    handle->page_count = next_page;
    *page_id = next_page;

    return 0;
}

int zenora_storage_handle_sync(ZenoraStorageHandle *handle)
{
    if (handle == NULL)
        return -1;

    if (handle->failed)
        return -2;

    if (!handle->writable)
        return 0;

    if (fdatasync(handle->fd) != 0)
    {
        handle->failed = 1;
        return -3;
    }

    return 0;
}

int zenora_storage_handle_get_format(
    const ZenoraStorageHandle *handle,
    uint32_t *format_version
)
{
    if (handle == NULL || format_version == NULL || handle->failed)
        return -1;

    *format_version = handle->format_version;

    return 0;
}

int zenora_storage_handle_get_page_count(
    const ZenoraStorageHandle *handle,
    uint64_t *page_count
)
{
    if (handle == NULL || page_count == NULL || handle->failed)
        return -1;

    *page_count = handle->page_count;

    return 0;
}
