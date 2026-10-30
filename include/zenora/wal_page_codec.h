#ifndef ZENORA_WAL_PAGE_CODEC_H
#define ZENORA_WAL_PAGE_CODEC_H

#include <stdint.h>
#include "zenora/wal_v6.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct
{
    uint16_t before_page_type;
    uint16_t after_page_type;
    const uint8_t *before_image;
    const uint8_t *after_image;
} ZenoraWalPageUpdateView;

typedef struct
{
    uint16_t page_type;
    uint64_t undo_next_lsn;
    const uint8_t *image;
} ZenoraWalClrView;

int zenora_wal_page_update_encode(
    void *payload,
    uint32_t payload_length,
    const ZenoraWalPageUpdateView *view
);

int zenora_wal_page_update_decode(
    const void *payload,
    uint32_t payload_length,
    ZenoraWalPageUpdateView *view
);

int zenora_wal_clr_encode(
    void *payload,
    uint32_t payload_length,
    const ZenoraWalClrView *view
);

int zenora_wal_clr_decode(
    const void *payload,
    uint32_t payload_length,
    ZenoraWalClrView *view
);

#ifdef __cplusplus
}
#endif

#endif
