String new_string(char const *text) {
    String str = {
        .data = text,
        .len = string_len(text),
    };
    return str;
}

u32 string_len(char const *text) {
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

bool strings_are_equal(String *s1, String *s2) {
    assert(s1 != NULL);
    assert(s2 != NULL);

    if (s1->len != s2->len) {
        return false;
    }
    
    for (u32 i = 0; i < s1->len; i++) {
        if (s1->data[i] != s2->data[i]) {
            return false;
        }
    }
    return true;
}

String substring(Allocator *allocator, const char *orig, u32 start, u32 end) {
    assert(orig != NULL);
    assert(start <= end);

    u32 len = end - start + 1;

    char *dest = (char *)alloc(allocator, len);
    dest = memcpy(dest, orig + start, len);

    String str = {
        .data = dest,
        .len = len
    };

    return str;
}
