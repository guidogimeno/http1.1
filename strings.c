#include "strings.h"
#include <stdlib.h>

String new_string(char const *text) {
    String str = {
        .data = text,
        .len = string_len(text),
    };
    return str;
}

uint32_t string_len(char const *text) {
    if (text == NULL) return 0;

    int i = 0;
    int keep_going = 1;

    while(keep_going) {
        if (text[i] == '\0') {
            keep_going = 0;
        } else {
            i++;
        }
    }
    return i;
}
