String string(char const *text) {
    return string_with_len(text, string_size(text));
}

String string_with_len(char const *text, u32 len) {
    String str = {
        .data = text,
        .size = len,
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

String string_sub(Allocator *a, String *str, u32 start, u32 end) {
    return string_sub_cstr(a, str->data, start, end);
}

String string_sub_cstr(Allocator *a, const char *text, u32 start, u32 end) {
    assert(text != NULL);
    assert(start >= 0);
    assert(end > 0);
    assert(start <= end);

    u32 len = end - start + 1;

    char *dest = (char *)alloc(a, len);
    dest = memcpy(dest, text + start, len);

    String str = {
        .data = dest,
        .size = len
    };

    return str;
}

String string_slice(String *str, u32 start, u32 offset) {
    assert(str != NULL);
    assert(start >= 0);
    assert(start + offset <= str->size);

    String new_str = {
        .data = str->data + start,
        .size = offset
    };

    return new_str;
}

String string_to_lower(Allocator *a, String str) {

    char *dest = (char *)alloc(a, str.size);

    for (u32 i = 0; i < str.size; i++) {
        char c = str.data[i];
        if (c >= 'A' && c <= 'Z') {
            dest[i] = 'a' + (c - 'A');
        } else {
            dest[i] = str.data[i];
        }
    }

    String new_str = {
        .data = dest,
        .size = str.size,
    };

    return new_str;
}

String string_to_upper(Allocator *a, String str) {

    char *dest = (char *)alloc(a, str.size);

    for (u32 i = 0; i < str.size; i++) {
        char c = str.data[i];
        if (c >= 'a' && c <= 'z') {
            dest[i] = 'A' + (c - 'a');
        } else {
            dest[i] = str.data[i];
        }
    }

    String new_str = {
        .data = dest,
        .size = str.size,
    };

    return new_str;
}

char char_to_lower(char c) {
    if (c >= 'A' && c <= 'Z') {
        return 'a' + (c - 'A');
    }
    return c;
}

char char_to_upper(char c) {
    if (c >= 'a' && c <= 'z') {
        return 'A' + (c - 'a');
    }
    return c;
}
