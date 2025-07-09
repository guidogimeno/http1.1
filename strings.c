String string(char const *text) {
    String str = {
        .data = text,
        .size = string_size(text),
    };
    return str;
}

u32 string_size(char const *text) {
    if (text == NULL) return 0;

    u32 i = 0;
    u32 keep_going = true;

    while(keep_going) {
        if (text[i] == '\0') {
            keep_going = false;
        } else {
            i++;
        }
    }
    return i;
}

bool string_eq(String *s1, String *s2) {
    assert(s1 != NULL);
    assert(s2 != NULL);

    if (s1->size != s2->size) {
        return false;
    }
    
    for (u32 i = 0; i < s1->size; i++) {
        if (s1->data[i] != s2->data[i]) {
            return false;
        }
    }
    return true;
}

bool string_eq_cstr(String *s1, char *s2) {
    assert(s1 != NULL);
    assert(s2 != NULL);

    if (s1->size != string_size(s2)) {
        return false;
    }
    
    for (u32 i = 0; i < s1->size; i++) {
        if (s1->data[i] != s2[i]) {
            return false;
        }
    }
    return true;
}

bool cstr_eq(char *s1, char *s2) {
    assert(s1 != NULL);
    assert(s2 != NULL);

    u32 len = string_size(s1);
    if (len != string_size(s2)) {
        return false;
    }
    
    for (u32 i = 0; i < len; i++) {
        if (s1[i] != s2[i]) {
            return false;
        }
    }
    return true;
}

String string_sub(Allocator *allocator, const char *orig, u32 start, u32 end) {
    assert(orig != NULL);
    assert(start <= end);

    u32 len = end - start + 1;

    char *dest = (char *)alloc(allocator, len);
    dest = memcpy(dest, orig + start, len);

    String str = {
        .data = dest,
        .size = len
    };

    return str;
}
