#include <stdlib.h>

#include "zenora/page_manager.h"
#include "zenora/storage_handle.h"

struct ZenoraPageManager
{
    ZenoraStorageHandle *handle;
};

int zenora_page_manager_open(
    const char *path,
    int writable,
    ZenoraPageManager **manager
)
{
    if (path == NULL || manager == NULL)
        return -1;

    *manager = NULL;

    ZenoraPageManager *instance = calloc(1, sizeof(*instance));

    if (instance == NULL)
        return -2;

    if (zenora_storage_handle_open(
            path,
            writable,
            &instance->handle) != 0)
    {
        free(instance);
        return -3;
    }

    *manager = instance;

    return 0;
}

int zenora_page_manager_close(ZenoraPageManager *manager)
{
    if (manager == NULL)
        return -1;

    int result = zenora_storage_handle_close(manager->handle);

    free(manager);

    return result == 0 ? 0 : -2;
}

int zenora_page_manager_allocate(
    ZenoraPageManager *manager,
    uint64_t *page_id
)
{
    if (manager == NULL)
        return -1;

    return zenora_storage_handle_allocate_page(
        manager->handle,
        page_id
    );
}

int zenora_page_manager_read(
    ZenoraPageManager *manager,
    uint64_t page_id,
    void *buffer
)
{
    if (manager == NULL)
        return -1;

    return zenora_storage_handle_read_page(
        manager->handle,
        page_id,
        buffer
    );
}

int zenora_page_manager_write(
    ZenoraPageManager *manager,
    uint64_t page_id,
    const void *buffer
)
{
    if (manager == NULL)
        return -1;

    return zenora_storage_handle_write_page(
        manager->handle,
        page_id,
        buffer
    );
}

int zenora_page_manager_sync(ZenoraPageManager *manager)
{
    if (manager == NULL)
        return -1;

    return zenora_storage_handle_sync(manager->handle);
}

int zenora_page_manager_get_format(
    const ZenoraPageManager *manager,
    uint32_t *format_version
)
{
    if (manager == NULL)
        return -1;

    return zenora_storage_handle_get_format(
        manager->handle,
        format_version
    );
}

int zenora_page_manager_get_page_count(
    const ZenoraPageManager *manager,
    uint64_t *page_count
)
{
    if (manager == NULL)
        return -1;

    return zenora_storage_handle_get_page_count(
        manager->handle,
        page_count
    );
}

int zenora_page_allocate(const char *path, uint64_t *page_id)
{
    if (path == NULL || page_id == NULL)
        return -1;

    ZenoraPageManager *manager = NULL;

    if (zenora_page_manager_open(path, 1, &manager) != 0)
        return -2;

    int result = zenora_page_manager_allocate(manager, page_id);
    int close_result = zenora_page_manager_close(manager);

    return result == 0 && close_result == 0 ? 0 : -3;
}

int zenora_page_read(
    const char *path,
    uint64_t page_id,
    void *buffer
)
{
    if (path == NULL || buffer == NULL || page_id == 0)
        return -1;

    ZenoraPageManager *manager = NULL;

    if (zenora_page_manager_open(path, 0, &manager) != 0)
        return -2;

    int result = zenora_page_manager_read(manager, page_id, buffer);
    int close_result = zenora_page_manager_close(manager);

    return result == 0 && close_result == 0 ? 0 : -3;
}

int zenora_page_write(
    const char *path,
    uint64_t page_id,
    const void *buffer
)
{
    if (path == NULL || buffer == NULL || page_id == 0)
        return -1;

    ZenoraPageManager *manager = NULL;

    if (zenora_page_manager_open(path, 1, &manager) != 0)
        return -2;

    int result = zenora_page_manager_write(
        manager,
        page_id,
        buffer
    );

    int close_result = zenora_page_manager_close(manager);

    return result == 0 && close_result == 0 ? 0 : -3;
}

int zenora_page_manager_read_with_metadata(
    ZenoraPageManager *manager,
    uint64_t page_id,
    void *buffer,
    ZenoraPageMetadata *metadata
)
{
    if (manager == NULL)
        return -1;

    return zenora_storage_handle_read_page_with_metadata(
        manager->handle,
        page_id,
        buffer,
        metadata
    );
}

int zenora_page_manager_write_with_metadata(
    ZenoraPageManager *manager,
    uint64_t page_id,
    const void *buffer,
    const ZenoraPageMetadata *metadata
)
{
    if (manager == NULL)
        return -1;

    return zenora_storage_handle_write_page_with_metadata(
        manager->handle,
        page_id,
        buffer,
        metadata
    );
}
