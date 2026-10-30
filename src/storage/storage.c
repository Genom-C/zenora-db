#define _POSIX_C_SOURCE 200809L
#define _FILE_OFFSET_BITS 64

#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <limits.h>
#include <fcntl.h>
#include <unistd.h>

#include "zenora/storage.h"
#include "zenora/header_codec.h"
#include "zenora/page_codec.h"

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

static int validate_file_size(
    FILE *file,
    uint64_t page_count,
    uint32_t format_version
)
{
    uint64_t stride = storage_page_size(format_version);

    if (stride == 0)
        return -1;

    if (page_count >
        ((uint64_t)INT64_MAX - ZENORA_PAGE_SIZE) / stride)
        return -1;

    if (fseeko(file, 0, SEEK_END) != 0)
        return -1;

    off_t actual_size = ftello(file);

    if (actual_size < 0)
        return -1;

    uint64_t expected_size =
        ZENORA_PAGE_SIZE + page_count * stride;

    return (uint64_t)actual_size == expected_size ? 0 : -1;
}

int zenora_storage_create(const char *path)
{
    if (path == NULL || path[0] == '\0')
        return -1;

    int fd = open(path, O_WRONLY | O_CREAT | O_EXCL, 0600);

    if (fd < 0)
        return -2;

    FILE *file = fdopen(fd, "wb");

    if (file == NULL)
    {
        close(fd);
        unlink(path);
        return -3;
    }

    unsigned char page[ZENORA_PAGE_SIZE];
    ZenoraHeader header = {0};

    memcpy(header.magic, ZENORA_MAGIC, sizeof(ZENORA_MAGIC));

    header.format_version = ZENORA_STORAGE_FORMAT_V2;
    header.page_size = ZENORA_PAGE_SIZE;
    header.page_count = 0;

    zenora_header_encode(page, &header);

    int failed = 0;

    if (fwrite(page, 1, sizeof(page), file) != sizeof(page))
        failed = 1;

    if (!failed && fflush(file) != 0)
        failed = 1;

    if (!failed && fsync(fileno(file)) != 0)
        failed = 1;

    if (fclose(file) != 0)
        failed = 1;

    if (failed)
    {
        unlink(path);
        return -4;
    }

    return 0;
}

int zenora_storage_open(const char *path)
{
    if (path == NULL || path[0] == '\0')
        return -1;

    FILE *file = fopen(path, "rb");

    if (file == NULL)
        return -2;

    unsigned char page[ZENORA_PAGE_SIZE];

    size_t read_count = fread(page, 1, sizeof(page), file);

    if (read_count != sizeof(page))
    {
        fclose(file);
        return -3;
    }

    ZenoraHeader header = {0};

    int decode_result = zenora_header_decode(page, &header);

    int valid =
        decode_result == 0 &&
        memcmp(header.magic, ZENORA_MAGIC, sizeof(ZENORA_MAGIC)) == 0 &&
        (header.format_version == ZENORA_STORAGE_FORMAT_V1 ||
         header.format_version == ZENORA_STORAGE_FORMAT_V2) &&
        header.page_size == ZENORA_PAGE_SIZE &&
        validate_file_size(
            file,
            header.page_count,
            header.format_version
        ) == 0;

    int close_result = fclose(file);

    if (!valid || close_result != 0)
        return -4;

    return 0;
}
