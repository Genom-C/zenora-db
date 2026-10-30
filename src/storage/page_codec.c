#include <stdint.h>
#include <string.h>
#include "zenora/page_codec.h"

static const uint8_t page_magic[4] = {'Z', 'P', 'G', '1'};

static void write_u16_le(uint8_t *buffer, uint16_t value)
{
    buffer[0] = (uint8_t)value;
    buffer[1] = (uint8_t)(value >> 8);
}

static void write_u32_le(uint8_t *buffer, uint32_t value)
{
    buffer[0] = (uint8_t)value;
    buffer[1] = (uint8_t)(value >> 8);
    buffer[2] = (uint8_t)(value >> 16);
    buffer[3] = (uint8_t)(value >> 24);
}

static void write_u64_le(uint8_t *buffer, uint64_t value)
{
    for (uint32_t i = 0; i < 8; ++i)
    {
        buffer[i] = (uint8_t)(value >> (i * 8));
    }
}

static uint16_t read_u16_le(const uint8_t *buffer)
{
    return (uint16_t)buffer[0] |
           ((uint16_t)buffer[1] << 8);
}

static uint32_t read_u32_le(const uint8_t *buffer)
{
    return (uint32_t)buffer[0] |
           ((uint32_t)buffer[1] << 8) |
           ((uint32_t)buffer[2] << 16) |
           ((uint32_t)buffer[3] << 24);
}

static uint64_t read_u64_le(const uint8_t *buffer)
{
    uint64_t value = 0;

    for (uint32_t i = 0; i < 8; ++i)
    {
        value |= (uint64_t)buffer[i] << (i * 8);
    }

    return value;
}

static uint32_t crc32c_update(uint32_t crc, const uint8_t *data, uint32_t length)
{
    for (uint32_t i = 0; i < length; ++i)
    {
        crc ^= data[i];

        for (uint32_t bit = 0; bit < 8; ++bit)
        {
            uint32_t mask = (uint32_t)-(int32_t)(crc & 1U);
            crc = (crc >> 1) ^ (0x82F63B78U & mask);
        }
    }

    return crc;
}

uint32_t zenora_page_codec_crc32c(const void *data, uint32_t length)
{
    if (data == NULL && length != 0)
    {
        return 0;
    }

    uint32_t crc = 0xFFFFFFFFU;

    if (length != 0)
    {
        crc = crc32c_update(crc, (const uint8_t *)data, length);
    }

    return ~crc;
}

uint32_t zenora_page_codec_checksum(const void *physical_page)
{
    if (physical_page == NULL)
    {
        return 0;
    }

    const uint8_t *bytes = (const uint8_t *)physical_page;
    uint32_t crc = 0xFFFFFFFFU;

    crc = crc32c_update(crc, bytes, 28U);
    crc = crc32c_update(
        crc,
        bytes + ZENORA_PAGE_HEADER_SIZE,
        ZENORA_PAGE_PAYLOAD_SIZE
    );

    return ~crc;
}

int zenora_page_codec_encode(
    void *physical_page,
    const ZenoraPageMetadata *metadata,
    const void *payload
)
{
    if (physical_page == NULL || metadata == NULL || payload == NULL)
    {
        return -1;
    }

    if (metadata->page_id == 0 || metadata->page_type == 0)
    {
        return -1;
    }

    uint8_t *bytes = (uint8_t *)physical_page;

    memset(bytes, 0, ZENORA_PHYSICAL_PAGE_SIZE);

    memcpy(bytes, page_magic, sizeof(page_magic));
    write_u16_le(bytes + 4, ZENORA_PAGE_FORMAT_VERSION);
    write_u16_le(bytes + 6, metadata->page_type);
    write_u64_le(bytes + 8, metadata->page_id);
    write_u64_le(bytes + 16, metadata->page_lsn);
    write_u32_le(bytes + 24, metadata->flags);

    memcpy(
        bytes + ZENORA_PAGE_HEADER_SIZE,
        payload,
        ZENORA_PAGE_PAYLOAD_SIZE
    );

    write_u32_le(bytes + 28, zenora_page_codec_checksum(bytes));

    return 0;
}

int zenora_page_codec_decode(
    const void *physical_page,
    ZenoraPageMetadata *metadata,
    void *payload
)
{
    if (physical_page == NULL || metadata == NULL || payload == NULL)
    {
        return -1;
    }

    const uint8_t *bytes = (const uint8_t *)physical_page;

    if (memcmp(bytes, page_magic, sizeof(page_magic)) != 0)
    {
        return -1;
    }

    if (read_u16_le(bytes + 4) != ZENORA_PAGE_FORMAT_VERSION)
    {
        return -1;
    }

    if (read_u16_le(bytes + 6) == 0 || read_u64_le(bytes + 8) == 0)
    {
        return -1;
    }

    uint32_t stored_checksum = read_u32_le(bytes + 28);
    uint32_t calculated_checksum = zenora_page_codec_checksum(bytes);

    if (stored_checksum != calculated_checksum)
    {
        return -1;
    }

    metadata->page_type = read_u16_le(bytes + 6);
    metadata->page_id = read_u64_le(bytes + 8);
    metadata->page_lsn = read_u64_le(bytes + 16);
    metadata->flags = read_u32_le(bytes + 24);

    memcpy(
        payload,
        bytes + ZENORA_PAGE_HEADER_SIZE,
        ZENORA_PAGE_PAYLOAD_SIZE
    );

    return 0;
}
