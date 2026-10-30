#ifndef ZENORA_PAGE_CODEC_H
#define ZENORA_PAGE_CODEC_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define ZENORA_PAGE_HEADER_SIZE 32U
#define ZENORA_PAGE_PAYLOAD_SIZE 4096U
#define ZENORA_PHYSICAL_PAGE_SIZE (ZENORA_PAGE_HEADER_SIZE + ZENORA_PAGE_PAYLOAD_SIZE)
#define ZENORA_PAGE_FORMAT_VERSION 1U

#define ZENORA_PAGE_TYPE_DATA 1U
#define ZENORA_PAGE_TYPE_METADATA 2U

typedef struct
{
    uint64_t page_id;
    uint64_t page_lsn;
    uint16_t page_type;
    uint32_t flags;
} ZenoraPageMetadata;

int zenora_page_codec_encode(
    void *physical_page,
    const ZenoraPageMetadata *metadata,
    const void *payload
);

int zenora_page_codec_decode(
    const void *physical_page,
    ZenoraPageMetadata *metadata,
    void *payload
);

uint32_t zenora_page_codec_checksum(const void *physical_page);
uint32_t zenora_page_codec_crc32c(const void *data, uint32_t length);

#ifdef __cplusplus
}
#endif

#endif
