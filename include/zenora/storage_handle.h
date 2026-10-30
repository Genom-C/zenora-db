#ifndef ZENORA_STORAGE_HANDLE_H
#define ZENORA_STORAGE_HANDLE_H

#include <stdint.h>
#include "zenora/page_codec.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct ZenoraStorageHandle ZenoraStorageHandle;

int zenora_storage_handle_open(
    const char *path,
    int writable,
    ZenoraStorageHandle **handle
);

int zenora_storage_handle_close(ZenoraStorageHandle *handle);

int zenora_storage_handle_read_page(
    ZenoraStorageHandle *handle,
    uint64_t page_id,
    void *buffer
);

int zenora_storage_handle_write_page(
    ZenoraStorageHandle *handle,
    uint64_t page_id,
    const void *buffer
);

int zenora_storage_handle_read_page_with_metadata(
    ZenoraStorageHandle *handle,
    uint64_t page_id,
    void *buffer,
    ZenoraPageMetadata *metadata
);

int zenora_storage_handle_write_page_with_metadata(
    ZenoraStorageHandle *handle,
    uint64_t page_id,
    const void *buffer,
    const ZenoraPageMetadata *metadata
);

int zenora_storage_handle_allocate_page(
    ZenoraStorageHandle *handle,
    uint64_t *page_id
);

int zenora_storage_handle_sync(ZenoraStorageHandle *handle);

int zenora_storage_handle_get_format(
    const ZenoraStorageHandle *handle,
    uint32_t *format_version
);

int zenora_storage_handle_get_page_count(
    const ZenoraStorageHandle *handle,
    uint64_t *page_count
);

#ifdef __cplusplus
}
#endif

#endif
