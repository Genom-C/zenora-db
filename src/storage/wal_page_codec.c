#include <string.h>
#include "zenora/wal_page_codec.h"
#include "zenora/page_codec.h"

static void put_u16(uint8_t *p, uint16_t value)
{
    p[0] = (uint8_t)value;
    p[1] = (uint8_t)(value >> 8);
}

static void put_u32(uint8_t *p, uint32_t value)
{
    p[0] = (uint8_t)value;
    p[1] = (uint8_t)(value >> 8);
    p[2] = (uint8_t)(value >> 16);
    p[3] = (uint8_t)(value >> 24);
}

static void put_u64(uint8_t *p, uint64_t value)
{
    for (uint32_t i = 0; i < 8; ++i)
        p[i] = (uint8_t)(value >> (i * 8));
}

static uint16_t get_u16(const uint8_t *p)
{
    return (uint16_t)p[0] |
           ((uint16_t)p[1] << 8);
}

static uint32_t get_u32(const uint8_t *p)
{
    return (uint32_t)p[0] |
           ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) |
           ((uint32_t)p[3] << 24);
}

static uint64_t get_u64(const uint8_t *p)
{
    uint64_t value = 0;

    for (uint32_t i = 0; i < 8; ++i)
        value |= (uint64_t)p[i] << (i * 8);

    return value;
}

static int valid_page_type(uint16_t type)
{
    return type == ZENORA_PAGE_TYPE_DATA ||
           type == ZENORA_PAGE_TYPE_METADATA;
}

int zenora_wal_page_update_encode(
    void *payload,
    uint32_t payload_length,
    const ZenoraWalPageUpdateView *view
)
{
    if (payload == NULL || view == NULL ||
        view->before_image == NULL ||
        view->after_image == NULL ||
        payload_length != ZENORA_WAL_V6_PAGE_UPDATE_PAYLOAD_SIZE ||
        !valid_page_type(view->before_page_type) ||
        !valid_page_type(view->after_page_type))
        return -1;

    uint8_t *output = (uint8_t *)payload;

    memset(output, 0, payload_length);

    put_u32(output, 1U);
    put_u16(output + 4, view->before_page_type);
    put_u16(output + 6, view->after_page_type);
    put_u32(output + 16, ZENORA_WAL_V6_IMAGE_SIZE);

    memcpy(output + 24, view->before_image, ZENORA_WAL_V6_IMAGE_SIZE);
    memcpy(output + 4120, view->after_image, ZENORA_WAL_V6_IMAGE_SIZE);

    return 0;
}

int zenora_wal_page_update_decode(
    const void *payload,
    uint32_t payload_length,
    ZenoraWalPageUpdateView *view
)
{
    if (payload == NULL || view == NULL ||
        payload_length != ZENORA_WAL_V6_PAGE_UPDATE_PAYLOAD_SIZE)
        return -1;

    const uint8_t *input = (const uint8_t *)payload;

    uint16_t before_type = get_u16(input + 4);
    uint16_t after_type = get_u16(input + 6);

    if (get_u32(input) != 1U ||
        !valid_page_type(before_type) ||
        !valid_page_type(after_type) ||
        get_u32(input + 16) != ZENORA_WAL_V6_IMAGE_SIZE ||
        get_u32(input + 20) != 0U)
        return -1;

    view->before_page_type = before_type;
    view->after_page_type = after_type;
    view->before_image = input + 24;
    view->after_image = input + 4120;

    return 0;
}

int zenora_wal_clr_encode(
    void *payload,
    uint32_t payload_length,
    const ZenoraWalClrView *view
)
{
    if (payload == NULL || view == NULL ||
        view->image == NULL ||
        payload_length != ZENORA_WAL_V6_CLR_PAYLOAD_SIZE ||
        !valid_page_type(view->page_type))
        return -1;

    uint8_t *output = (uint8_t *)payload;

    memset(output, 0, payload_length);

    put_u32(output, 1U);
    put_u16(output + 4, view->page_type);
    put_u32(output + 12, ZENORA_WAL_V6_IMAGE_SIZE);
    put_u64(output + 16, view->undo_next_lsn);

    memcpy(output + 24, view->image, ZENORA_WAL_V6_IMAGE_SIZE);

    return 0;
}

int zenora_wal_clr_decode(
    const void *payload,
    uint32_t payload_length,
    ZenoraWalClrView *view
)
{
    if (payload == NULL || view == NULL ||
        payload_length != ZENORA_WAL_V6_CLR_PAYLOAD_SIZE)
        return -1;

    const uint8_t *input = (const uint8_t *)payload;
    uint16_t page_type = get_u16(input + 4);

    if (get_u32(input) != 1U ||
        !valid_page_type(page_type) ||
        get_u16(input + 6) != 0U ||
        get_u32(input + 12) != ZENORA_WAL_V6_IMAGE_SIZE)
        return -1;

    view->page_type = page_type;
    view->undo_next_lsn = get_u64(input + 16);
    view->image = input + 24;

    return 0;
}
