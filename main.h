#include <arpa/inet.h>
#include <assert.h>
#include <netinet/in.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/epoll.h>
#include <fcntl.h>
#include <unistd.h>

typedef uint8_t  u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;

typedef int8_t  s8;
typedef int16_t s16;
typedef int32_t s32;
typedef int64_t s64;

typedef float  f32;
typedef double f64;

#define KB 1024
#define MB 1024 * KB
#define GB 1024 * MB



// ###################################
// ### Allocators ####################
// ###################################

#define DEFAULT_ALIGNMENT (2*sizeof(void *))

typedef struct Allocator {
    u8  *data;
    u64 size;
    u64 capacity;
} Allocator;

typedef struct AllocatorTemp {
    Allocator *allocator;
    u32       position;
} AllocatorTemp;

#define MAX_SCRATCH_COUNT 2

__thread Allocator *thread_local_allocators_pool[MAX_SCRATCH_COUNT] = {0, 0};


Allocator *allocator_make(u64 capacity);
void *alloc(Allocator *allocator, u64 size);
void *alloc_aligned(Allocator *allocator, u64 size, size_t align);

AllocatorTemp allocator_temp_begin(Allocator *allocator);
void allocator_temp_end(AllocatorTemp allocatorTemp);

AllocatorTemp get_scratch(Allocator **conflicts, u64 conflict_count);
#define release_scratch(t) allocator_temp_end(t)

Allocator *allocator_make(u64 capacity) {
    void *memory = malloc(sizeof(Allocator) + capacity);
    Allocator *allocator = (Allocator *)memory;
    allocator->data = memory + sizeof(Allocator);
    allocator->capacity = capacity;
    allocator->size = 0;
    return allocator;
}

static bool is_power_of_two(uintptr_t x) {
	return (x & (x-1)) == 0;
}

static uintptr_t align_forward(uintptr_t ptr, size_t align) {
	uintptr_t p, a, modulo;

	assert(is_power_of_two(align));

	p = ptr;
	a = (uintptr_t)align;
	modulo = p & (a-1);

	if (modulo != 0) {
		p += a - modulo;
	}
	return p;
}

void *alloc(Allocator *allocator, u64 size) {
    return alloc_aligned(allocator, size, DEFAULT_ALIGNMENT);
}

void *alloc_aligned(Allocator *allocator, u64 size, size_t align) {
    uintptr_t current_ptr = (uintptr_t)allocator->data + (uintptr_t)allocator->size;
    uintptr_t offset = align_forward(current_ptr, align);
    offset -= (uintptr_t)allocator->data;

    assert(offset + size <= allocator->capacity);

    void *result = &allocator->data[offset];
    allocator->size = offset + size;

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



// ###################################
// ### Strings #######################
// ###################################




// ###################################
// ### Dynamic Arrays ################
// ###################################

typedef struct DynamicArray {
    void *items;
    u64 length;
    u64 capacity;
} DynamicArray;

void dynamic_array_grow(Allocator *allocator, void *dynamic_array_ptr, size_t item_size) {
    DynamicArray *dynamic_array = (DynamicArray *)dynamic_array_ptr;

    if (dynamic_array->capacity <= 0) {
        dynamic_array->capacity = 1;
    }

    uintptr_t items_offset = dynamic_array->length * item_size;

    if (allocator->data + allocator->size == dynamic_array->items + items_offset) {
        alloc_aligned(allocator, dynamic_array->capacity * item_size, 1);
    } else {
        void *data = alloc(allocator, 2 * dynamic_array->capacity * item_size);
        if (dynamic_array->length > 0) {
            memcpy(data, dynamic_array->items, items_offset);
        }
        dynamic_array->items = data;
    }

    dynamic_array->capacity *= 2;
}

#define dynamic_array_append(allocator, dynamic_array) \
    ((dynamic_array)->length >= (dynamic_array)->capacity \
     ? dynamic_array_grow(allocator, dynamic_array, sizeof(*(dynamic_array)->items)), \
       (dynamic_array)->items + (dynamic_array)->length++ \
     : (dynamic_array)->items + (dynamic_array)->length++)



// ###################################
// ### Maps ##########################
// ###################################

// typedef struct Map {
//     void *data[];
//     u64 length;
//     u64 capacity;
// }
