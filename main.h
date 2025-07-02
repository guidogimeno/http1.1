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

typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;

typedef int8_t s8;
typedef int16_t s16;
typedef int32_t s32;
typedef int64_t s64;

typedef struct Allocator {
    char *data;
    u32 size;
    u32 capacity;
} Allocator;

void *alloc(Allocator *allocator, u32 size) {
    assert(allocator->size + size <= allocator->capacity);
    void *result = &allocator->data[allocator->size];
    allocator->size += size;
    return result;
}

