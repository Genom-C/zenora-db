#ifndef ZENORA_PAGE_MANAGER_H
#define ZENORA_PAGE_MANAGER_H

#include <stdint.h>
#include "zenora/storage.h"
#include "zenora/page_codec.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct ZenoraPageManager ZenoraPageManager;

int zenora_page_manager_open(
    const char *path,
    int writable,
    ZenoraPageManager **manager
);

int zenora_page_manager_close(ZenoraPageManager *manager);

int zenora_page_manager_allocate(
    ZenoraPageManager *manager,
    uint64_t *page_id
);

int zenora_page_manager_read(
    ZenoraPageManager *manager,
    uint64_t page_id,
    void *buffer
);

int zenora_page_manager_write(
    ZenoraPageManager *manager,
    uint64_t page_id,
    const void *buffer
);

int zenora_page_manager_read_with_metadata(
    ZenoraPageManager *manager,
    uint64_t page_id,
    void *buffer,
    ZenoraPageMetadata *metadata
);

int zenora_page_manager_write_with_metadata(
    ZenoraPageManager *manager,
    uint64_t page_id,
    const void *buffer,
    const ZenoraPageMetadata *metadata
);

int zenora_page_manager_sync(ZenoraPageManager *manager);

int zenora_page_manager_get_format(
    const ZenoraPageManager *manager,
    uint32_t *format_version
);

int zenora_page_manager_get_page_count(
    const ZenoraPageManager *manager,
    uint64_t *page_count
);

int zenora_page_allocate(const char *path, uint64_t *page_id);

int zenora_page_read(
    const char *path,
    uint64_t page_id,
    void *buffer
);

int zenora_page_write(
    const char *path,
    uint64_t page_id,
    const void *buffer
);

#ifdef __cplusplus
}
#endif

#endif
