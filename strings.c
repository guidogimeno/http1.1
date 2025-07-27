String string(const char *text) {
    return string_with_len(text, string_size(text));
}

String string_with_len(const char *text, u32 len) {
    String str = {
        .data = text,
        .size = len,
    };
    return str;
}

u32 string_size(const char *text) {
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
    static const u64 powers_of_10[] = {
        1ULL, 10ULL, 100ULL, 1000ULL, 10000ULL, 100000ULL, 1000000ULL,
        10000000ULL, 100000000ULL, 1000000000ULL, 10000000000ULL,
        100000000000ULL, 1000000000000ULL, 10000000000000ULL,
        100000000000000ULL, 1000000000000000ULL, 10000000000000000ULL,
        100000000000000000ULL, 1000000000000000000ULL, 10000000000000000000ULL
    };

    static const u8 digit_counts[] = {1, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19};

    static const char two_digit_table[100][2] = {
        {'0', '0'}, {'0', '1'}, {'0', '2'}, {'0', '3'}, {'0', '4'}, {'0', '5'},
        {'0', '6'}, {'0', '7'}, {'0', '8'}, {'0', '9'}, {'1', '0'}, {'1', '1'},
        {'1', '2'}, {'1', '3'}, {'1', '4'}, {'1', '5'}, {'1', '6'}, {'1', '7'}, 
        {'1', '8'}, {'1', '9'}, {'2', '0'}, {'2', '1'}, {'2', '2'}, {'2', '3'},
        {'2', '4'}, {'2', '5'}, {'2', '6'}, {'2', '7'}, {'2', '8'}, {'2', '9'},
        {'3', '0'}, {'3', '1'}, {'3', '2'}, {'3', '3'}, {'3', '4'}, {'3', '5'}, 
        {'3', '6'}, {'3', '7'}, {'3', '8'}, {'3', '9'}, {'4', '0'}, {'4', '1'}, 
        {'4', '2'}, {'4', '3'}, {'4', '4'}, {'4', '5'}, {'4', '6'}, {'4', '7'},
        {'4', '8'}, {'4', '9'}, {'5', '0'}, {'5', '1'}, {'5', '2'}, {'5', '3'},
        {'5', '4'}, {'5', '5'}, {'5', '6'}, {'5', '7'}, {'5', '8'}, {'5', '9'},
        {'6', '0'}, {'6', '1'}, {'6', '2'}, {'6', '3'}, {'6', '4'}, {'6', '5'},
        {'6', '6'}, {'6', '7'}, {'6', '8'}, {'6', '9'}, {'7', '0'}, {'7', '1'},
        {'7', '2'}, {'7', '3'}, {'7', '4'}, {'7', '5'}, {'7', '6'}, {'7', '7'},
        {'7', '8'}, {'7', '9'}, {'8', '0'}, {'8', '1'}, {'8', '2'}, {'8', '3'},
        {'8', '4'}, {'8', '5'}, {'8', '6'}, {'8', '7'}, {'8', '8'}, {'8', '9'},
        {'9', '0'}, {'9', '1'}, {'9', '2'}, {'9', '3'}, {'9', '4'}, {'9', '5'},
        {'9', '6'}, {'9', '7'}, {'9', '8'}, {'9', '9'}
    };

    static const char int64_min_str[] = "-9223372036854775808";
    static const char int64_max_str[] = "9223372036854775807";

    if (num == 0) {
        char *buf = (char *)alloc(allocator, 1);
        buf[0] = '0';
        String str = { .data = buf, .size = 1 };
        return str;
    }
    if (num == INT64_MIN) {
        char *buf = (char *)alloc(allocator, 20);
        for (int i = 0; i < 20; i++) buf[i] = int64_min_str[i];
        String str = { .data = buf, .size = 20 };
        return str;
    }
    if (num == INT64_MAX) {
        char *buf = (char *)alloc(allocator, 19);
        for (int i = 0; i < 19; i++) buf[i] = int64_max_str[i];
        String str = { .data = buf, .size = 19 };
        return str;
    }

    bool is_negative = num < 0;
    u64 abs_num = (u64)llabs(num);
    u32 length = is_negative ? 1 : 0;
    for (u32 i = 0; i < 20; i++) {
        if (abs_num < powers_of_10[i]) {
            length += digit_counts[i];
            break;
        }
    }

    char *buf = (char *)alloc(allocator, length);
    u32 i = length - 1;

    while (abs_num >= 100) {
        u32 pair = abs_num % 100;
        abs_num /= 100;
        buf[i] = two_digit_table[pair][1];
        buf[i - 1] = two_digit_table[pair][0];
        i -= 2;
    }

    if (abs_num > 0) {
        if (abs_num < 10) {
            buf[i] = '0' + (char)abs_num;
        } else {
            buf[i] = two_digit_table[abs_num][1];
            buf[i - 1] = two_digit_table[abs_num][0];
            i--;
        }
    }

    if (is_negative) {
        buf[0] = '-';
    }

    String str = { .data = buf, .size = length };
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

    if (total_length > builder->capacity) {
        u32 new_capacity = builder->capacity;

        while (builder->capacity < total_length) {
            new_capacity *= 2;
        }

        printf("realocacion - capacidad anterior: %d capacidad nueva: %d\n", builder->capacity, new_capacity);

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

