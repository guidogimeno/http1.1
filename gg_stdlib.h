#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef uint8_t  u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;

typedef int8_t  i8;
typedef int16_t i16;
typedef int32_t i32;
typedef int64_t i64;

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
void      *allocator_alloc(Allocator *allocator, u64 size);
void      *alloctor_alloc_aligned(Allocator *allocator, u64 size, size_t align);
void      allocator_reset(Allocator *allocator);

AllocatorTemp allocator_temp_begin(Allocator *allocator);
void          allocator_temp_end(AllocatorTemp allocatorTemp);

AllocatorTemp get_scratch(Allocator **conflicts, u64 conflict_count);
#define       release_scratch(t) allocator_temp_end(t)

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

void *allocator_alloc(Allocator *allocator, u64 size) {
    return alloctor_alloc_aligned(allocator, size, DEFAULT_ALIGNMENT);
}

void *alloctor_alloc_aligned(Allocator *allocator, u64 size, size_t align) {
    uintptr_t current_ptr = (uintptr_t)allocator->data + (uintptr_t)allocator->size;
    uintptr_t offset = align_forward(current_ptr, align);
    offset -= (uintptr_t)allocator->data;

    assert(offset + size <= allocator->capacity);

    void *result = &allocator->data[offset];
    allocator->size = offset + size;

    memset(result, 0, size);

    return result;
}

void allocator_reset(Allocator *allocator) {
    allocator->size = 0;
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

typedef struct String {
    const char *data;
    u32 size;
} String;

typedef struct String_Builder {
    u8 *data;
    u32 length;
    u32 capacity;
    Allocator *allocator;
} String_Builder;

#define STRING_BUILDER_DEFAULT_CAPACITY 32


// String functions

#define string_lit(char_pointer) string_with_len(char_pointer, sizeof(char_pointer) - 1)

// Como no son NULL terminated, esto hace que se puedan printear haciendo:
// printf("%.*s", string_print(string));
#define string_print(str) str.size, str.data

String string(const char *text);
String string_with_len(const char *text, u32 len);
u32    string_size(const char *text);

bool cstr_eq(char *s1, char *s2);
bool string_eq(String s1, String s2);
bool string_eq_cstr(String s1, char *s2);

String string_to_lower(Allocator *a, String str);
String string_to_upper(Allocator *a, String str);
i64    string_to_int(String str);
String string_from_int(Allocator *a, i64 num);

String string_sub(Allocator *a, String *str, u32 start, u32 end); 
String string_sub_cstr(Allocator *a, const char *text, u32 start, u32 end); 
String string_slice(String *str, u32 start, u32 offset);

char char_to_lower(char c);
char char_to_upper(char c);

// String_Builder functions
void   sbuilder_init(String_Builder *builder, Allocator *allocator);
void   sbuilder_init_cap(String_Builder *builder, Allocator *allocator, u32 capacity); 
void   sbuilder_append(String_Builder *builder, String str);
String sbuilder_to_string(String_Builder *sb);

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

bool string_eq(String s1, String s2) {
    if (s1.size != s2.size) {
        return false;
    }
    
    for (u32 i = 0; i < s1.size; i++) {
        if (s1.data[i] != s2.data[i]) {
            return false;
        }
    }
    return true;
}

bool string_eq_cstr(String s1, char *s2) {
    if (s1.size != string_size(s2)) {
        return false;
    }
    
    for (u32 i = 0; i < s1.size; i++) {
        if (s1.data[i] != s2[i]) {
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

    char *dest = (char *)allocator_alloc(a, len);
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

    char *dest = (char *)allocator_alloc(a, str.size);

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

    char *dest = (char *)allocator_alloc(a, str.size);

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

i64 string_to_int(String str) {
    u32 i = 0;

    i32 sign;
    if (str.data[i] == '-') {
        sign = -1;
        i++;
    } else {
        sign = 1;
    }

    i64 result = 0;
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

String string_from_int(Allocator *allocator, i64 num) {
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
        char *buf = (char *)allocator_alloc(allocator, 1);
        buf[0] = '0';
        String str = { .data = buf, .size = 1 };
        return str;
    }
    if (num == INT64_MIN) {
        char *buf = (char *)allocator_alloc(allocator, 20);
        for (int i = 0; i < 20; i++) buf[i] = int64_min_str[i];
        String str = { .data = buf, .size = 20 };
        return str;
    }
    if (num == INT64_MAX) {
        char *buf = (char *)allocator_alloc(allocator, 19);
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

    char *buf = (char *)allocator_alloc(allocator, length);
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
        builder->data = allocator_alloc(allocator, capacity);
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

        u8 *new_data = allocator_alloc(builder->allocator, new_capacity);

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
        alloctor_alloc_aligned(allocator, dynamic_array->capacity * item_size, 1);
    } else {
        void *data = allocator_alloc(allocator, 2 * dynamic_array->capacity * item_size);
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
// ### Hashes ########################
// ###################################

// hash: djb2 - util para strings
u64 hash_string(String s) {
    u64 hash = 5381; // numero primo
    for (u64 i = 0; i < s.size; i++) {
        // (hash x 33) + ch = ((hash x 32) + hash) + ch
        hash = ((hash << 5) + hash) + s.data[i];
    }
    return hash;
}

// hash: fnv1a - TODO: averiguar en QUE es bueno
u64 hash_generic(void *data, size_t size) {
    u64 hash = 14695981039346656037ULL;
    u8 *bytes = (u8 *)data;
    for (u64 i = 0; i < size; i++) {
        hash ^= bytes[i];
        hash *= 1099511628211ULL; // numero primo
    }
    return hash;
}

