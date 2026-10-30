#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <limits.h>

#include "zenora/buffer_pool.h"
#include "zenora/page_manager.h"
#include "zenora/wal_page_codec.h"

typedef struct
{
    uint64_t page_id;
    uint64_t last_used;
    uint32_t pin_count;
    int valid;
    int dirty;
    unsigned char *data;
    ZenoraPageMetadata metadata;
} ZenoraBufferFrame;

struct ZenoraBufferPool
{
    size_t capacity;
    uint64_t clock;
    ZenoraBufferFrame *frames;
    unsigned char *memory;
    ZenoraPageManager *manager;
    ZenoraWalV6 *wal;
    int failed;
    ZenoraBufferPoolStats stats;
};

static ZenoraBufferFrame *find_frame(
    ZenoraBufferPool *pool,
    uint64_t page_id
)
{
    for (size_t i = 0; i < pool->capacity; i++)
    {
        if (pool->frames[i].valid &&
            pool->frames[i].page_id == page_id)
            return &pool->frames[i];
    }

    return NULL;
}

static void update_usage(
    ZenoraBufferPool *pool,
    ZenoraBufferFrame *frame
)
{
    if (pool->clock == UINT64_MAX)
    {
        uint64_t minimum = UINT64_MAX;

        for (size_t i = 0; i < pool->capacity; i++)
        {
            ZenoraBufferFrame *current = &pool->frames[i];

            if (current->valid && current->last_used < minimum)
                minimum = current->last_used;
        }

        if (minimum == UINT64_MAX)
            minimum = 0;

        for (size_t i = 0; i < pool->capacity; i++)
        {
            ZenoraBufferFrame *current = &pool->frames[i];

            if (current->valid)
                current->last_used -= minimum;
        }

        pool->clock -= minimum;
    }

    frame->last_used = ++pool->clock;
}

static int flush_frame(
    ZenoraBufferPool *pool,
    ZenoraBufferFrame *frame
)
{
    if (!frame->valid || !frame->dirty)
        return 0;

    if (pool->failed)
        return -1;

    if (pool->wal != NULL)
    {
        uint64_t durable_lsn = 0;

        if (frame->metadata.page_lsn == 0 ||
            zenora_wal_v6_get_durable_lsn(
                pool->wal,
                &durable_lsn) != 0 ||
            durable_lsn < frame->metadata.page_lsn)
            return -1;

        if (zenora_page_manager_write_with_metadata(
                pool->manager,
                frame->page_id,
                frame->data,
                &frame->metadata) != 0)
            return -1;
    }
    else
    {
        if (zenora_page_manager_write(
                pool->manager,
                frame->page_id,
                frame->data) != 0)
            return -1;
    }

    frame->dirty = 0;

    return 0;
}

static ZenoraBufferFrame *select_frame(ZenoraBufferPool *pool)
{
    ZenoraBufferFrame *candidate = NULL;

    for (size_t i = 0; i < pool->capacity; i++)
    {
        ZenoraBufferFrame *frame = &pool->frames[i];

        if (!frame->valid)
            return frame;

        if (frame->pin_count != 0)
            continue;

        if (candidate == NULL ||
            frame->last_used < candidate->last_used)
            candidate = frame;
    }

    return candidate;
}

int zenora_buffer_pool_create(
    const char *path,
    size_t capacity,
    ZenoraBufferPool **pool
)
{
    if (path == NULL || path[0] == '\0' ||
        capacity == 0 || pool == NULL)
        return -1;

    *pool = NULL;

    if (capacity > SIZE_MAX / ZENORA_PAGE_SIZE ||
        capacity > SIZE_MAX / sizeof(ZenoraBufferFrame))
        return -2;

    ZenoraBufferPool *instance = calloc(1, sizeof(*instance));

    if (instance == NULL)
        return -3;

    instance->frames = calloc(capacity, sizeof(ZenoraBufferFrame));
    instance->memory = calloc(capacity, ZENORA_PAGE_SIZE);

    if (instance->frames == NULL || instance->memory == NULL)
    {
        free(instance->frames);
        free(instance->memory);
        free(instance);
        return -3;
    }

    if (zenora_page_manager_open(path, 1, &instance->manager) != 0)
    {
        free(instance->frames);
        free(instance->memory);
        free(instance);
        return -4;
    }

    instance->capacity = capacity;

    for (size_t i = 0; i < capacity; i++)
        instance->frames[i].data =
            instance->memory + i * ZENORA_PAGE_SIZE;

    *pool = instance;

    return 0;
}

int zenora_buffer_pool_fetch(
    ZenoraBufferPool *pool,
    uint64_t page_id,
    void **data
)
{
    if (pool == NULL || data == NULL || page_id == 0)
        return -1;

    *data = NULL;

    ZenoraBufferFrame *frame = find_frame(pool, page_id);

    if (frame != NULL)
    {
        if (frame->pin_count == UINT32_MAX)
            return -2;

        frame->pin_count++;
        update_usage(pool, frame);
        pool->stats.hits++;

        *data = frame->data;

        return 0;
    }

    pool->stats.misses++;

    frame = select_frame(pool);

    if (frame == NULL)
        return -3;

    if (frame->valid && flush_frame(pool, frame) != 0)
        return -4;

    unsigned char page[ZENORA_PAGE_SIZE];

    ZenoraPageMetadata metadata = {0};

    if (pool->wal != NULL)
    {
        if (zenora_page_manager_read_with_metadata(
                pool->manager,
                page_id,
                page,
                &metadata) != 0)
            return -5;
    }
    else
    {
        if (zenora_page_manager_read(
                pool->manager,
                page_id,
                page) != 0)
            return -5;
    }

    if (frame->valid)
        pool->stats.evictions++;

    memcpy(frame->data, page, ZENORA_PAGE_SIZE);

    frame->page_id = page_id;
    frame->pin_count = 1;
    frame->valid = 1;
    frame->dirty = 0;
    frame->metadata = metadata;

    update_usage(pool, frame);

    *data = frame->data;

    return 0;
}

int zenora_buffer_pool_unpin(
    ZenoraBufferPool *pool,
    uint64_t page_id
)
{
    if (pool == NULL || page_id == 0)
        return -1;

    ZenoraBufferFrame *frame = find_frame(pool, page_id);

    if (frame == NULL || frame->pin_count == 0)
        return -2;

    frame->pin_count--;

    return 0;
}


int zenora_buffer_pool_create_with_wal(
    const char *path,
    size_t capacity,
    ZenoraWalV6 *wal,
    ZenoraBufferPool **pool
)
{
    if (wal == NULL || pool == NULL)
        return -1;

    int result = zenora_buffer_pool_create(path, capacity, pool);

    if (result != 0)
        return result;

    uint32_t format_version = 0;

    if (zenora_page_manager_get_format(
            (*pool)->manager,
            &format_version) != 0 ||
        format_version != ZENORA_STORAGE_FORMAT_V2)
    {
        zenora_buffer_pool_destroy(*pool);
        *pool = NULL;
        return -2;
    }

    (*pool)->wal = wal;

    return 0;
}

int zenora_buffer_pool_write_transactional(
    ZenoraBufferPool *pool,
    ZenoraWalV6 *wal,
    uint64_t transaction_id,
    uint64_t *prev_lsn,
    uint64_t page_id,
    const void *new_data,
    uint64_t *record_lsn
)
{
    if (pool == NULL || wal == NULL || prev_lsn == NULL ||
        transaction_id == 0 || page_id == 0 ||
        new_data == NULL || record_lsn == NULL)
        return -1;

    if (pool->failed || pool->wal != wal)
        return -2;

    ZenoraBufferFrame *frame = find_frame(pool, page_id);

    if (frame == NULL || frame->pin_count == 0 ||
        !frame->valid || frame->dirty)
        return -3;

    if (frame->metadata.page_type != ZENORA_PAGE_TYPE_DATA &&
        frame->metadata.page_type != ZENORA_PAGE_TYPE_METADATA)
        return -4;

    unsigned char *payload = malloc(
        ZENORA_WAL_V6_PAGE_UPDATE_PAYLOAD_SIZE
    );

    if (payload == NULL)
        return -5;

    ZenoraWalPageUpdateView view = {
        .before_page_type = frame->metadata.page_type,
        .after_page_type = frame->metadata.page_type,
        .before_image = frame->data,
        .after_image = new_data
    };

    if (zenora_wal_page_update_encode(
            payload,
            ZENORA_WAL_V6_PAGE_UPDATE_PAYLOAD_SIZE,
            &view) != 0)
    {
        free(payload);
        return -6;
    }

    uint64_t lsn = 0;

    int result = zenora_wal_v6_append_ex(
        wal,
        transaction_id,
        *prev_lsn,
        ZENORA_WAL_RECORD_PAGE_UPDATE,
        page_id,
        payload,
        ZENORA_WAL_V6_PAGE_UPDATE_PAYLOAD_SIZE,
        &lsn
    );

    free(payload);

    if (result != 0)
        return -7;

    *prev_lsn = lsn;

    if (zenora_wal_v6_sync(wal) != 0)
    {
        pool->failed = 1;
        return -8;
    }

    uint64_t durable_lsn = 0;

    if (zenora_wal_v6_get_durable_lsn(
            wal,
            &durable_lsn) != 0 ||
        durable_lsn < lsn)
    {
        pool->failed = 1;
        return -9;
    }

    memcpy(frame->data, new_data, ZENORA_PAGE_SIZE);
    frame->metadata.page_lsn = lsn;
    frame->dirty = 1;
    *record_lsn = lsn;

    return 0;
}

int zenora_buffer_pool_mark_dirty(
    ZenoraBufferPool *pool,
    uint64_t page_id
)
{
    if (pool == NULL || page_id == 0)
        return -1;

    if (pool->wal != NULL || pool->failed)
        return -3;

    ZenoraBufferFrame *frame = find_frame(pool, page_id);

    if (frame == NULL || frame->pin_count == 0)
        return -2;

    frame->dirty = 1;

    return 0;
}

int zenora_buffer_pool_flush(
    ZenoraBufferPool *pool,
    uint64_t page_id
)
{
    if (pool == NULL || page_id == 0)
        return -1;

    ZenoraBufferFrame *frame = find_frame(pool, page_id);

    if (frame == NULL)
        return -2;

    return flush_frame(pool, frame);
}

int zenora_buffer_pool_flush_all(ZenoraBufferPool *pool)
{
    if (pool == NULL)
        return -1;

    for (size_t i = 0; i < pool->capacity; i++)
    {
        if (flush_frame(pool, &pool->frames[i]) != 0)
            return -2;
    }

    return zenora_page_manager_sync(pool->manager) == 0 ? 0 : -2;
}

int zenora_buffer_pool_get_stats(
    const ZenoraBufferPool *pool,
    ZenoraBufferPoolStats *stats
)
{
    if (pool == NULL || stats == NULL)
        return -1;

    *stats = pool->stats;

    return 0;
}

int zenora_buffer_pool_destroy(ZenoraBufferPool *pool)
{
    if (pool == NULL)
        return -1;

    for (size_t i = 0; i < pool->capacity; i++)
    {
        if (pool->frames[i].pin_count != 0)
            return -2;
    }

    if (zenora_buffer_pool_flush_all(pool) != 0)
        return -3;

    if (zenora_page_manager_close(pool->manager) != 0)
        return -4;

    free(pool->memory);
    free(pool->frames);
    free(pool);

    return 0;
}

int zenora_buffer_pool_allocate_page(
    ZenoraBufferPool *pool,
    uint64_t *page_id
)
{
    if (pool == NULL || page_id == NULL)
        return -1;

    return zenora_page_manager_allocate(pool->manager, page_id);
}
