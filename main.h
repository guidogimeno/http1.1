#ifndef MAIN_H
#define MAIN_H

#include <stdlib.h>

typedef struct Allocator {
    void *data;
    size_t size;
    size_t capacity;
} Allocator;

#endif

