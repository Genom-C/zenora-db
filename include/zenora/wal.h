#ifndef ZENORA_WAL_H
#define ZENORA_WAL_H

#include <stddef.h>
#include <stdint.h>

typedef struct ZenoraWal ZenoraWal;

typedef enum
{
    ZENORA_WAL_RECORD_BEGIN = 1,
    ZENORA_WAL_RECORD_UPDATE = 2,
    ZENORA_WAL_RECORD_COMMIT = 3,
    ZENORA_WAL_RECORD_ABORT = 4,
    ZENORA_WAL_RECORD_PAGE_UPDATE = 5,
    ZENORA_WAL_RECORD_CLR = 6,
    ZENORA_WAL_RECORD_PAGE_ALLOCATE = 7
} ZenoraWalRecordType;

#define ZENORA_WAL_V5_VERSION 5U
#define ZENORA_WAL_V5_RECORD_HEADER_SIZE 44U
#define ZENORA_WAL_V5_IMAGE_SIZE 4096U
#define ZENORA_WAL_V5_PAGE_UPDATE_PAYLOAD_SIZE 8216U
#define ZENORA_WAL_V5_CLR_PAYLOAD_SIZE 4120U
#define ZENORA_WAL_V5_PAGE_ALLOCATE_PAYLOAD_SIZE 4108U

typedef struct
{
    uint64_t lsn;
    uint64_t transaction_id;
    uint64_t prev_lsn;
    uint32_t type;
    uint64_t page_id;
    uint32_t payload_length;
    const unsigned char *payload;
} ZenoraWalRecord;

typedef int (*ZenoraWalVisitor)(const ZenoraWalRecord *record, void *context);

int zenora_wal_open(const char *path, ZenoraWal **wal);
int zenora_wal_open_v4(const char *path, ZenoraWal **wal);
int zenora_wal_open_v5(const char *path, ZenoraWal **wal);
int zenora_wal_append_ex(
    ZenoraWal *wal,
    uint64_t transaction_id,
    uint64_t prev_lsn,
    uint32_t type,
    uint64_t page_id,
    const void *payload,
    uint32_t payload_length,
    uint64_t *lsn
);
int zenora_wal_append(
    ZenoraWal *wal,
    uint64_t transaction_id,
    uint32_t type,
    uint64_t page_id,
    const void *payload,
    uint32_t payload_length,
    uint64_t *lsn
);
int zenora_wal_sync(ZenoraWal *wal);
int zenora_wal_get_durable_lsn(const ZenoraWal *wal, uint64_t *durable_lsn);
int zenora_wal_scan(ZenoraWal *wal, ZenoraWalVisitor visitor, void *context);
void zenora_wal_close(ZenoraWal *wal);

#endif
