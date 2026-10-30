#ifndef ZENORA_HEADER_CODEC_H
#define ZENORA_HEADER_CODEC_H

#include <stdint.h>
#include <string.h>
#include "zenora/storage.h"

#define ZENORA_HEADER_CHECKSUM_OFFSET 24
#define ZENORA_HEADER_SIZE 28

static inline void zenora_encode_u32(unsigned char *buffer, uint32_t value)
{
    buffer[0] = (unsigned char)value;
    buffer[1] = (unsigned char)(value >> 8);
    buffer[2] = (unsigned char)(value >> 16);
    buffer[3] = (unsigned char)(value >> 24);
}

static inline void zenora_encode_u64(unsigned char *buffer, uint64_t value)
{
    for (unsigned int i = 0; i < 8; i++)
        buffer[i] = (unsigned char)(value >> (i * 8));
}

static inline uint32_t zenora_decode_u32(const unsigned char *buffer)
{
    return (uint32_t)buffer[0] |
           ((uint32_t)buffer[1] << 8) |
           ((uint32_t)buffer[2] << 16) |
           ((uint32_t)buffer[3] << 24);
}

static inline uint64_t zenora_decode_u64(const unsigned char *buffer)
{
    uint64_t value = 0;

    for (unsigned int i = 0; i < 8; i++)
        value |= (uint64_t)buffer[i] << (i * 8);

    return value;
}

static inline uint32_t zenora_header_checksum(const unsigned char *buffer)
{
    uint32_t hash = 2166136261u;

    for (unsigned int i = 0; i < 24; i++)
    {
        hash ^= buffer[i];
        hash *= 16777619u;
    }

    return hash;
}

static inline void zenora_header_encode(
    unsigned char *page,
    const ZenoraHeader *header
)
{
    memset(page, 0, ZENORA_PAGE_SIZE);

    memcpy(page, header->magic, 8);
    zenora_encode_u32(page + 8, header->format_version);
    zenora_encode_u32(page + 12, header->page_size);
    zenora_encode_u64(page + 16, header->page_count);

    zenora_encode_u32(
        page + ZENORA_HEADER_CHECKSUM_OFFSET,
        zenora_header_checksum(page)
    );
}

static inline int zenora_header_decode(
    const unsigned char *page,
    ZenoraHeader *header
)
{
    uint32_t stored_checksum =
        zenora_decode_u32(page + ZENORA_HEADER_CHECKSUM_OFFSET);

    uint32_t calculated_checksum = zenora_header_checksum(page);

    if (stored_checksum != 0 &&
        stored_checksum != calculated_checksum)
        return -1;

    memcpy(header->magic, page, 8);
    header->format_version = zenora_decode_u32(page + 8);
    header->page_size = zenora_decode_u32(page + 12);
    header->page_count = zenora_decode_u64(page + 16);

    return 0;
}

#endif
