#ifndef ZENORA_WAL_V6_H
#define ZENORA_WAL_V6_H

#include <stdint.h>
#include "zenora/wal.h"

typedef struct ZenoraWalV6 ZenoraWalV6;

#define ZENORA_WAL_V6_VERSION 6U
#define ZENORA_WAL_V6_RECORD_HEADER_SIZE 44U
#define ZENORA_WAL_V6_IMAGE_SIZE 4096U
#define ZENORA_WAL_V6_PAGE_UPDATE_PAYLOAD_SIZE 8216U
#define ZENORA_WAL_V6_CLR_PAYLOAD_SIZE 4120U
#define ZENORA_WAL_V6_PAGE_ALLOCATE_PAYLOAD_SIZE 4108U

#define ZENORA_WAL_V6_RECORD_END 8U

typedef enum
{
    ZENORA_WAL_V6_TX_ACTIVE = 1,
    ZENORA_WAL_V6_TX_ABORTING = 2,
    ZENORA_WAL_V6_TX_COMMITTED = 3,
    ZENORA_WAL_V6_TX_ENDED = 4
} ZenoraWalV6TransactionState;

int zenora_wal_v6_open(const char *path, ZenoraWalV6 **wal);

int zenora_wal_v6_append_ex(
    ZenoraWalV6 *wal,
    uint64_t transaction_id,
    uint64_t prev_lsn,
    uint32_t type,
    uint64_t page_id,
    const void *payload,
    uint32_t payload_length,
    uint64_t *lsn
);

int zenora_wal_v6_append(
    ZenoraWalV6 *wal,
    uint64_t transaction_id,
    uint32_t type,
    uint64_t page_id,
    const void *payload,
    uint32_t payload_length,
    uint64_t *lsn
);

int zenora_wal_v6_sync(ZenoraWalV6 *wal);
int zenora_wal_v6_get_durable_lsn(const ZenoraWalV6 *wal, uint64_t *durable_lsn);
int zenora_wal_v6_scan(ZenoraWalV6 *wal, ZenoraWalVisitor visitor, void *context);
int zenora_wal_v6_close(ZenoraWalV6 *wal);

#endif
