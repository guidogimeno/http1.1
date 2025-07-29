#include <arpa/inet.h>
#include <assert.h>
#include <netinet/in.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>
#include <pthread.h>

typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;

typedef int8_t s8;
typedef int16_t s16;
typedef int32_t s32;
typedef int64_t s64;

typedef float f32;
typedef double f64;

#define KB 1024
#define MB 1024 * KB
#define GB 1024 * MB

typedef struct Allocator {
    u8 *data;
    u32 size;
    u32 capacity;
} Allocator;

typedef struct AllocatorTemp {
    Allocator *allocator;
    u32 position;
} AllocatorTemp;

#define MAX_SCRATCH_COUNT 2

__thread Allocator *thread_local_allocators_pool[MAX_SCRATCH_COUNT] = {0, 0};

Allocator *allocator_make(u32 capacity) {
    void *memory = malloc(sizeof(Allocator) + capacity);
    Allocator *allocator = (Allocator *)memory;
    allocator->data = memory + sizeof(Allocator);
    allocator->capacity = capacity;
    allocator->size = 0;
    return allocator;
}

void *alloc(Allocator *allocator, u32 size) {
    assert(allocator->size + size <= allocator->capacity);
    void *result = &allocator->data[allocator->size];
    allocator->size += size;
    return result;
}

AllocatorTemp allocator_temp_begin(Allocator *allocator) {
    AllocatorTemp allocatorTemp = {
        .allocator = allocator,
        .position = allocator->size,
    };
    return allocatorTemp;
}

void allocator_temp_end(AllocatorTemp allocatorTemp) {
    allocatorTemp.allocator->size = allocatorTemp.position;
}

AllocatorTemp get_scratch(Allocator **conflicts, u64 conflict_count) {
    if (thread_local_allocators_pool[0] == 0) {
        for (u32 i = 0; i < MAX_SCRATCH_COUNT; i++) {
            thread_local_allocators_pool[i] = allocator_make(1024 * 1024); 
        }
    }

    if (conflict_count == 0) {
        return allocator_temp_begin(thread_local_allocators_pool[0]);
    }

    for (u32 pool_index = 0; pool_index < MAX_SCRATCH_COUNT; pool_index++) {
        Allocator *allocator = thread_local_allocators_pool[pool_index];

        bool is_free = true;
        for (u32 conflict_index = 0; conflict_index < conflict_count; conflict_index++) {
            if (allocator == conflicts[conflict_index]) {
                is_free = false;
                break;
            }
        }

        if (is_free) {
            return allocator_temp_begin(allocator);
        }
    }

    return (AllocatorTemp){0};
}

#define release_scratch(t) allocator_temp_end(t)
