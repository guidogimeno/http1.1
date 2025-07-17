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

s64 string_to_int(String str) {
    u32 i = 0;

    s32 sign;
    if (str.data[i] == '-') {
        sign = -1;
        i++;
    } else {
        sign = 1;
    }

    s64 result = 0;
    while (i < str.size) { 
        char c = str.data[i];

        u32 num;
        if (c >= '0' && c <= '9') {
            num = c - '0';
        } else if (c == ' ') {
            return result * sign;
        } else {
            return 0; // error
        }

        // validar overflow
        if (sign == 1) {
            if (result > INT64_MAX / 10 || 
                (result == INT64_MAX / 10 && num > INT64_MAX % 10)) {
                return 0; // overflow
            }
        } else {
            if (result > -(INT64_MIN / 10) || 
                (result == -(INT64_MIN / 10) && num > -(INT64_MIN % 10))) {
                return 0; // overflow
            }
        }

        result = result * 10 + num;

        i++;
    }
    
    return result * sign;
}

String string_from_int(Allocator *allocator, s64 num) {
    s64 temp = num;

    u32 length = 1;
    while (temp != 0) {
        temp /= 10;
        length++;
    }

    char *buf = (char *)alloc(allocator, length);

    if (num < 0) {
        buf[0] = '-';
        length++;
    }

    temp = num;
    u32 i = length - 1;
    while (temp != 0) {
        s64 num_int = (s64)(temp / 10);
        u32 num_rest = temp % 10;
        temp = num_int;

        buf[i] = '0' + num_rest;

        i--;
    }

    String str = {
        .data = buf,
        .size = length
    };

    return str;
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


void sbuilder_init_cap(String_Builder *builder, Allocator *allocator, u32 capacity) {
    builder->length = 0;
    builder->capacity = capacity;
    builder->allocator = allocator;

    if (capacity > 0) {
        builder->data = alloc(allocator, capacity);
    }
}

void sbuilder_init(String_Builder *builder, Allocator *allocator) {
    sbuilder_init_cap(builder, allocator, STRING_BUILDER_DEFAULT_CAPACITY);
}

void sbuilder_append(String_Builder *builder, String str) {
    u32 total_length = builder->length + str.size;

    if (total_length >= builder->capacity) {
        u32 new_capacity = total_length * 2;
        u8 *new_data = alloc(builder->allocator, new_capacity);

        memcpy(new_data, builder->data, builder->length);

        builder->data = new_data;
        builder->capacity = new_capacity;
    }

    memcpy(builder->data + builder->length, str.data, str.size);

    builder->length = total_length;
}

String sbuilder_to_string(String_Builder *sb) {
    String str = {
        .data = (char *)sb->data,
        .size = sb->length,
    };
    return str;
}

