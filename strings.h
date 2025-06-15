#ifndef STRINGS_H
#define STRINGS_H

#include <stdint.h>

typedef struct {
    char const *data;
    uint32_t len;
} String;

String new_string(char const *text);

uint32_t string_len(char const *text);

#endif
