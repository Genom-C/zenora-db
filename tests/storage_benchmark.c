#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <errno.h>

#include "zenora/storage.h"
#include "zenora/page_manager.h"
#include "zenora/buffer_pool.h"

static volatile uint64_t benchmark_sink = 0;

static uint64_t random_state = UINT64_C(0x9E3779B97F4A7C15);

static uint64_t random_next(void)
{
    random_state ^= random_state << 13;
    random_state ^= random_state >> 7;
    random_state ^= random_state << 17;
    return random_state;
}

static uint64_t elapsed_ns(struct timespec start, struct timespec end)
{
    return (uint64_t)(end.tv_sec - start.tv_sec) * UINT64_C(1000000000)
         + (uint64_t)(end.tv_nsec - start.tv_nsec);
}

static int compare_u64(const void *a, const void *b)
{
    uint64_t x = *(const uint64_t *)a;
    uint64_t y = *(const uint64_t *)b;

    return (x > y) - (x < y);
}

static int run_workload(
    const char *path,
    uint64_t page_count,
    size_t capacity,
    size_t iterations,
    int random_access,
    int write_mode
)
{
    ZenoraBufferPool *pool = NULL;
    ZenoraBufferPoolStats before = {0};
    ZenoraBufferPoolStats after = {0};

    uint64_t *latencies = malloc(iterations * sizeof(uint64_t));

    if (latencies == NULL)
        return -1;

    if (zenora_buffer_pool_create(path, capacity, &pool) != 0)
    {
        free(latencies);
        return -2;
    }

    zenora_buffer_pool_get_stats(pool, &before);

    struct timespec total_start;
    struct timespec total_end;

    if (clock_gettime(CLOCK_MONOTONIC, &total_start) != 0)
        goto failure;

    for (size_t i = 0; i < iterations; i++)
    {
        uint64_t page_id = random_access
            ? random_next() % page_count + 1
            : (uint64_t)(i % page_count) + 1;

        struct timespec operation_start;
        struct timespec operation_end;

        if (clock_gettime(CLOCK_MONOTONIC, &operation_start) != 0)
            goto failure;

        void *data = NULL;

        if (zenora_buffer_pool_fetch(pool, page_id, &data) != 0)
            goto failure;

        if (write_mode)
        {
            unsigned char *bytes = data;
            bytes[i % ZENORA_PAGE_SIZE] = (unsigned char)(i + page_id);

            if (zenora_buffer_pool_mark_dirty(pool, page_id) != 0)
            {
                zenora_buffer_pool_unpin(pool, page_id);
                goto failure;
            }
        }
        else
        {
            const unsigned char *bytes = data;
            benchmark_sink += bytes[i % ZENORA_PAGE_SIZE];
        }

        if (zenora_buffer_pool_unpin(pool, page_id) != 0)
            goto failure;

        if (clock_gettime(CLOCK_MONOTONIC, &operation_end) != 0)
            goto failure;

        latencies[i] = elapsed_ns(operation_start, operation_end);
    }

    if (clock_gettime(CLOCK_MONOTONIC, &total_end) != 0)
        goto failure;

    uint64_t total_time = elapsed_ns(total_start, total_end);

    struct timespec flush_start;
    struct timespec flush_end;

    if (clock_gettime(CLOCK_MONOTONIC, &flush_start) != 0)
        goto failure;

    if (zenora_buffer_pool_flush_all(pool) != 0)
        goto failure;

    if (clock_gettime(CLOCK_MONOTONIC, &flush_end) != 0)
        goto failure;

    uint64_t flush_time = elapsed_ns(flush_start, flush_end);

    if (zenora_buffer_pool_get_stats(pool, &after) != 0)
        goto failure;

    qsort(latencies, iterations, sizeof(uint64_t), compare_u64);

    size_t p95_index = (iterations * 95 + 99) / 100 - 1;

    long double sum = 0;

    for (size_t i = 0; i < iterations; i++)
        sum += latencies[i];

    long double average_ns = sum / iterations;
    long double throughput =
        (long double)iterations * 1000000000.0L / total_time;

    const char *access_name = random_access ? "RANDOM" : "SEQUENTIAL";
    const char *operation_name = write_mode ? "WRITE" : "READ";

    printf("\n%s %s\n", access_name, operation_name);
    printf("OPERATIONS: %zu\n", iterations);
    printf("THROUGHPUT: %.2Lf OPS/SEC\n", throughput);
    printf("AVERAGE LATENCY: %.2Lf US\n", average_ns / 1000.0L);
    printf("P95 LATENCY: %.2Lf US\n", latencies[p95_index] / 1000.0L);
    printf("TOTAL TIME: %.3Lf MS\n", total_time / 1000000.0L);
    printf("FLUSH TIME: %.3Lf MS\n", flush_time / 1000000.0L);
    printf("CACHE HITS: %llu\n", (unsigned long long)(after.hits - before.hits));
    printf("CACHE MISSES: %llu\n", (unsigned long long)(after.misses - before.misses));
    printf("EVICTIONS: %llu\n", (unsigned long long)(after.evictions - before.evictions));

    if (zenora_buffer_pool_destroy(pool) != 0)
    {
        free(latencies);
        return -3;
    }

    free(latencies);

    return 0;

failure:
    if (pool != NULL)
        zenora_buffer_pool_destroy(pool);

    free(latencies);

    return -4;
}

static int parse_u64(const char *value, uint64_t *result)
{
    if (value == NULL || result == NULL || value[0] == '\0')
        return -1;

    errno = 0;
    char *end = NULL;
    unsigned long long parsed = strtoull(value, &end, 10);

    if (errno != 0 || end == value || *end != '\0' || parsed == 0)
        return -1;

    *result = (uint64_t)parsed;

    return 0;
}

int main(int argc, char **argv)
{
    uint64_t page_count = 32;
    uint64_t capacity_value = 16;
    uint64_t iterations_value = 128;

    if (argc > 4)
        return 1;

    if (argc > 1 && parse_u64(argv[1], &page_count) != 0)
        return 2;

    if (argc > 2 && parse_u64(argv[2], &capacity_value) != 0)
        return 3;

    if (argc > 3 && parse_u64(argv[3], &iterations_value) != 0)
        return 4;

    if (page_count < 2 ||
        capacity_value > SIZE_MAX ||
        iterations_value > SIZE_MAX / sizeof(uint64_t))
        return 5;

    char path[] = "build/zenora-benchmark-XXXXXX";

    int fd = mkstemp(path);

    if (fd < 0)
        return 6;

    if (close(fd) != 0 || unlink(path) != 0)
        return 7;

    if (zenora_storage_create(path) != 0)
        return 8;

    printf("ZENORA STORAGE BENCHMARK\n");
    printf("VERSION: %s\n", ZENORA_VERSION);
    printf("PAGES: %llu\n", (unsigned long long)page_count);
    printf("BUFFER CAPACITY: %llu\n", (unsigned long long)capacity_value);
    printf("ITERATIONS: %llu\n", (unsigned long long)iterations_value);

    for (uint64_t i = 0; i < page_count; i++)
    {
        uint64_t page_id = 0;

        if (zenora_page_allocate(path, &page_id) != 0 ||
            page_id != i + 1)
        {
            unlink(path);
            return 9;
        }
    }

    int result = 0;

    if (run_workload(path, page_count, (size_t)capacity_value,
                     (size_t)iterations_value, 0, 0) != 0)
        result = 10;

    if (result == 0 &&
        run_workload(path, page_count, (size_t)capacity_value,
                     (size_t)iterations_value, 1, 0) != 0)
        result = 11;

    if (result == 0 &&
        run_workload(path, page_count, (size_t)capacity_value,
                     (size_t)iterations_value, 0, 1) != 0)
        result = 12;

    if (result == 0 &&
        run_workload(path, page_count, (size_t)capacity_value,
                     (size_t)iterations_value, 1, 1) != 0)
        result = 13;

    unlink(path);

    if (result != 0)
        return result;

    printf("\nBENCHMARK COMPLETED\n");

    return 0;
}
