#ifndef ZENORA_BUFFER_POOL_H
#define ZENORA_BUFFER_POOL_H

#include <stddef.h>
#include <stdint.h>

#include "zenora/storage.h"
#include "zenora/wal_v6.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct ZenoraBufferPool ZenoraBufferPool;

typedef struct
{
    uint64_t hits;
    uint64_t misses;
    uint64_t evictions;
} ZenoraBufferPoolStats;

int zenora_buffer_pool_create(
    const char *path,
    size_t capacity,
    ZenoraBufferPool **pool
);

int zenora_buffer_pool_allocate_page(
    ZenoraBufferPool *pool,
    uint64_t *page_id
);

int zenora_buffer_pool_fetch(
    ZenoraBufferPool *pool,
    uint64_t page_id,
    void **data
);

int zenora_buffer_pool_unpin(
    ZenoraBufferPool *pool,
    uint64_t page_id
);

int zenora_buffer_pool_create_with_wal(
    const char *path,
    size_t capacity,
    ZenoraWalV6 *wal,
    ZenoraBufferPool **pool
);

int zenora_buffer_pool_write_transactional(
    ZenoraBufferPool *pool,
    ZenoraWalV6 *wal,
    uint64_t transaction_id,
    uint64_t *prev_lsn,
    uint64_t page_id,
    const void *new_data,
    uint64_t *record_lsn
);

int zenora_buffer_pool_mark_dirty(
    ZenoraBufferPool *pool,
    uint64_t page_id
);

int zenora_buffer_pool_flush(
    ZenoraBufferPool *pool,
    uint64_t page_id
);

int zenora_buffer_pool_flush_all(ZenoraBufferPool *pool);

int zenora_buffer_pool_get_stats(
    const ZenoraBufferPool *pool,
    ZenoraBufferPoolStats *stats
);

int zenora_buffer_pool_destroy(ZenoraBufferPool *pool);

#ifdef __cplusplus
}
#endif

#endif
