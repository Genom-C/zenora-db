#ifndef ZENORA_STORAGE_H
#define ZENORA_STORAGE_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define ZENORA_FILE_EXTENSION ".zdb"
#define ZENORA_PAGE_SIZE 4096U
#define ZENORA_STORAGE_FORMAT_V1 1U
#define ZENORA_STORAGE_FORMAT_V2 2U

typedef struct
{
    char magic[8];
    uint32_t format_version;
    uint32_t page_size;
    uint64_t page_count;
} ZenoraHeader;

int zenora_storage_create(const char *path);
int zenora_storage_open(const char *path);

#ifdef __cplusplus
}
#endif

#endif
