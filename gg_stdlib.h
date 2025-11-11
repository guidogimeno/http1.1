#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <string.h>
#include <sys/mman.h>

typedef uint8_t  u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;

typedef int8_t  i8;
typedef int16_t i16;
typedef int32_t i32;
typedef int64_t i64;

typedef int32_t b32;
typedef int64_t b64;

typedef float  f32;
typedef double f64;


#define KB 1024
#define MB 1024 * KB
#define GB 1024 * MB



// ###################################
// ### Utils #########################
// ###################################

#define panic_with_msg(msg) do { \
    fprintf(stderr, "panic at %s:%d - %s\n", __FILE__, __LINE__, msg); \
    exit(EXIT_FAILURE); \
} while (0);

#define array_size(arr) (sizeof(arr)/sizeof((arr)[0]))

#define debug_assert(expression) if (!(expression)) {*(i32 *)0 = 0;}

// ###################################
// ### Math  #########################
// ###################################

typedef union {
    struct {
        f32 x;
        f32 y;
    };
    f32 v[2];
} Vec2_F32;

inline Vec2_F32 vec2_f32(f32 x, f32 y) {
    Vec2_F32 result;

    result.x = x;
    result.y = y;

    return result;
}

inline Vec2_F32 vec2_f32_add(Vec2_F32 a, Vec2_F32 b) {
    Vec2_F32 result = {
        .x = a.x + b.x,
        .y = a.y + b.y,
    };
    return result;
}

inline Vec2_F32 vec2_f32_mult(Vec2_F32 a, Vec2_F32 b) {
    Vec2_F32 result = {
        .x = a.x * b.x,
        .y = a.y * b.y,
    };
    return result;
}

inline Vec2_F32 vec2_f32_sub(Vec2_F32 a, Vec2_F32 b) {
    Vec2_F32 result = {
        .x = a.x - b.x,
        .y = a.y - b.y,
    };
    return result;
}

typedef struct {
    struct {
        f32 x;
        f32 y;
        f32 z;
        f32 w;
    };
    f32 v[4];
} Vec4_F32;

inline Vec4_F32 vec4_f32(f32 x, f32 y, f32 z, f32 w) {
    Vec4_F32 result;

    result.x = x;
    result.y = y;
    result.z = z;
    result.w = w;

    return result;
}

f32 math_exp(f32 a) {
    union { f32 f; f32 i; } u, v;
    u.i = (i32)(6051102.0f * a + 1056478197.0f);
    v.i = (i32)(1056478197.0f - 6051102.0f * a);
    return u.f / v.f;
}

inline f32 math_sin(f32 radians) {
    return sinf(radians);
}

inline f32 math_cos(f32 radians) {
    return cosf(radians);
}

f32 math_pow(f32 a, f32 b) {
    i32 flipped = 0, e;
    f32 f, r = 1.0f;
    if (b < 0) {
        flipped = 1;
        b = -b;
    }

    e = (i32)b;
    f = math_exp(b - e);

    while (e) {
        if (e & 1) r *= a;
        a *= a;
        e >>= 1;
    }

    r *= f;
    return flipped ? 1.0f/r : r;
}

i32 math_round_f32_to_i32(f32 num) {
    return (i32)(num + 0.5f);
}


// ###################################
// ### Arenas ####################
// ###################################

#define MAX_SCRATCH_COUNT 2

#define DEFAULT_ALIGNMENT (2*sizeof(void *))

typedef struct Arena Arena;
typedef struct Arena_Temp Arena_Temp;

struct Arena {
    u8  *data;
    u64 size;
    u64 capacity;
};

struct Arena_Temp {
    Arena *arena;
    u32       position;
};

__thread Arena *thread_local_arenas_pool[MAX_SCRATCH_COUNT] = {0, 0};

Arena *arena_make(u64 capacity);
void arena_init(Arena *arena, u8 *data, u64 capacity);
void *arena_alloc(Arena *arena, u64 size);
void *arena_alloc_aligned(Arena *arena, u64 size, size_t align);
void arena_reset(Arena *arena);
void arena_destroy(Arena *arena);

Arena_Temp arena_temp_begin(Arena *arena);
void arena_temp_end(Arena_Temp arena_temp);

Arena_Temp get_scratch(Arena **conflicts, u64 conflict_count);
#define release_scratch(t) arena_temp_end(t)

Arena *arena_make(u64 capacity) {
    void *memory = mmap(0, sizeof(Arena) + capacity, PROT_READ|PROT_WRITE, MAP_PRIVATE|MAP_ANONYMOUS, -1, 0);
    if (memory == (void *)-1) {
        panic_with_msg("mmap failed");
    }

    Arena *arena = (Arena *)memory;
    arena->data = memory + sizeof(Arena);
    arena->capacity = capacity;
    arena->size = 0;

    return arena;
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

void arena_init(Arena *arena, u8 *data, u64 capacity) {
    *arena = (Arena){0};
    arena->data = data;
    arena->size = 0;
    arena->capacity = capacity;
}

void *arena_alloc(Arena *arena, u64 size) {
    return arena_alloc_aligned(arena, size, DEFAULT_ALIGNMENT);
}

void *arena_alloc_aligned(Arena *arena, u64 size, size_t align) {
    uintptr_t current_ptr = (uintptr_t)arena->data + (uintptr_t)arena->size;
    uintptr_t offset = align_forward(current_ptr, align);
    offset -= (uintptr_t)arena->data;

    assert(offset + size <= arena->capacity);

    void *result = &arena->data[offset];
    arena->size = offset + size;

    memset(result, 0, size);

    return result;
}

void arena_reset(Arena *arena) {
    arena->size = 0;
}

void arena_destroy(Arena *arena) {
    munmap(arena, arena->capacity);
}

Arena_Temp arena_temp_begin(Arena *arena) {
    Arena_Temp arena_temp = {
        .arena = arena,
        .position = arena->size,
    };
    return arena_temp;
}

void arena_temp_end(Arena_Temp arena_temp) {
    arena_temp.arena->size = arena_temp.position;
}

Arena_Temp get_scratch(Arena **conflicts, u64 conflict_count) {
    if (thread_local_arenas_pool[0] == 0) {
        for (u32 i = 0; i < MAX_SCRATCH_COUNT; i++) {
            thread_local_arenas_pool[i] = arena_make(1 * MB); 
        }
    }

    if (conflict_count == 0) {
        return arena_temp_begin(thread_local_arenas_pool[0]);
    }

    for (u32 pool_index = 0; pool_index < MAX_SCRATCH_COUNT; pool_index++) {
        Arena *arena = thread_local_arenas_pool[pool_index];

        bool is_free = true;
        for (u32 conflict_index = 0; conflict_index < conflict_count; conflict_index++) {
            if (arena == conflicts[conflict_index]) {
                is_free = false;
                break;
            }
        }

        if (is_free) {
            return arena_temp_begin(arena);
        }
    }

    return (Arena_Temp){0};
}





// ###################################
// ### Strings #######################
// ###################################

#define STRING_BUILDER_DEFAULT_CAPACITY 32

typedef struct String String;
typedef struct String_Builder String_Builder;

struct String {
    const char *data;
    u32 size;
};

struct String_Builder {
    u8 *data;
    u32 length;
    u32 capacity;
    Arena *arena;
};

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
bool string_is_empty(String str);

String string_to_lower(Arena *a, String str);
String string_to_upper(Arena *a, String str);
String string_from_b32(Arena *a, b32 boolean);
String string_from_i64(Arena *a, i64 num);
String string_from_f64(Arena *a, f64 num, i32 precision);
i32 string_to_i32(String str);
i64 string_to_i64(String str);
f32 string_to_f32(String str);
f64 string_to_f64(String str);

String string_sub(Arena *a, String *str, u32 start, u32 end); 
String string_sub_cstr(Arena *a, const char *text, u32 start, u32 end); 
String string_slice(String *str, u32 start, u32 offset);

char char_to_lower(char c);
char char_to_upper(char c);

bool is_space(char c);
bool is_letter(char c);
bool is_digit(char c);
bool is_alphanum(char c);

// String_Builder functions
void   sbuilder_init(String_Builder *builder, Arena *arena);
void   sbuilder_init_cap(String_Builder *builder, Arena *arena, u32 capacity); 
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

bool string_is_empty(String str) {
    return str.size <= 0;
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

String string_sub(Arena *a, String *str, u32 start, u32 end) {
    return string_sub_cstr(a, str->data, start, end);
}

String string_sub_cstr(Arena *a, const char *text, u32 start, u32 end) {
    assert(text != NULL);
    assert(start >= 0);
    assert(end > 0);
    assert(start <= end);

    u32 len = end - start + 1;

    char *dest = (char *)arena_alloc(a, len);
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

String string_to_lower(Arena *a, String str) {

    char *dest = (char *)arena_alloc(a, str.size);

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

String string_to_upper(Arena *a, String str) {

    char *dest = (char *)arena_alloc(a, str.size);

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

i64 string_to_i64(String str) {
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

i32 string_to_i32(String str) {
    return (i32)string_to_i64(str);
}

f64 string_to_f64(String str) {
    char *data = (char *)str.data;
    u32 size = str.size;
    u32 i = 0;
    i32 sign = 1.0;
    f64 scale = 1.0;
	i32 frac = 0;
    f64 result, value = 0.0;
	
	while (i < size && is_space(data[i])) {
		i++;
	}

	if (data[i] == '-') {
		sign = -1.0;
		i++;
	} else if (data[i] == '+') {
		i++;
	}

    while (i < size && is_digit(data[i])) {
		value = value * 10.0 + (data[i]-'0');
        i++;
    }

    if (data[i] == '.') {
        f64 decimal = 0.0;
        int dec_places = 0;
        i++;
        while (i < size && is_digit(data[i])) {
            decimal = decimal * 10.0 + (data[i] - '0');
            dec_places++;
            i++;
        }
        if (dec_places > 0) {
            value += decimal / math_pow(10.0, dec_places);
        }
    }

    if (i + 1 < size) {
        if ((data[i] == 'e') || (data[i] == 'E')) {
            u32 exp = 0;

            i++;
            if (data[i] == '-') {
                frac = 1;
                i++;
            } else if (data[i] == '+') {
                i++;
            }

            while (i < size && is_digit(data[i])) {
                exp = exp * 10 + (data[i]-'0');
                i++;
            }

            if (exp > 308) {
                exp = 308;
            }

            while (exp >= 50) { scale *= 1e50; exp -= 50; }
            while (exp >=  8) { scale *= 1e8;  exp -=  8; }
            while (exp >   0) { scale *= 10.0; exp -=  1; }
        }
    }
	
	result = sign * (frac ? (value / scale) : (value * scale));
	
	return result;
}

f32 string_to_f32(String string) {
    return (f32)string_to_f64(string);
}

String string_from_b32(Arena *arena, b32 boolean) {
    String result;
    char *buff;
    u32 size;

    if (boolean) {
        size = 4;
        buff = arena_alloc(arena, size);
        memcpy(buff, "true", size);
    } else {
        size = 5;
        buff = arena_alloc(arena, size);
        memcpy(buff, "false", size);
    }

    result.data = buff;
    result.size = size;

    return result;
}

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

String string_from_i64(Arena *arena, i64 num) {
    if (num == 0) {
        char *buf = (char *)arena_alloc(arena, 1);
        buf[0] = '0';
        String str = { .data = buf, .size = 1 };
        return str;
    }
    if (num == INT64_MIN) {
        char *buf = (char *)arena_alloc(arena, 20);
        for (int i = 0; i < 20; i++) buf[i] = int64_min_str[i];
        String str = { .data = buf, .size = 20 };
        return str;
    }
    if (num == INT64_MAX) {
        char *buf = (char *)arena_alloc(arena, 19);
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

    char *buf = (char *)arena_alloc(arena, length);
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

String string_from_f64(Arena *a, f64 num, i32 precision) {
    if (precision < 0) {
        precision = 0;
    }

    f64 abs_num = num;
    bool is_negative = false;
    if (num < 0) {
        abs_num = -num;
        is_negative = true;
    }

    u64 int_part = (u64) abs_num;
    f64 frac_part = abs_num - (f64)int_part;

    u32 int_len = 0;
    for (u32 i = 0; i < 20; i++) {
        if (int_part < powers_of_10[i]) {
            int_len = digit_counts[i];
            break;
        }
    }
    if (int_part == 0) {
        int_len = 1;
    }

    u32 frac_total = (u32)(precision > 0 ? 1 + precision : 0);
    u32 length = (is_negative ? 1u : 0u) + int_len + frac_total;

    char *buf = (char *)arena_alloc_aligned(a, length, 1);
    u32 curr = 0;

    if (is_negative) {
        buf[curr++] = '-';
    }

    u32 int_digit_start = curr;
    u32 num_int_digits = int_len;
    u32 i = int_digit_start + num_int_digits - 1;
    u64 abs_int = int_part;

    if (abs_int == 0) {
        buf[int_digit_start] = '0';
    } else {
        while (abs_int >= 100) {
            u32 pair = (u32)(abs_int % 100);
            abs_int /= 100;
            buf[i] = two_digit_table[pair][1];
            buf[i - 1] = two_digit_table[pair][0];
            i -= 2;
        }
        if (abs_int > 0) {
            if (abs_int < 10) {
                buf[i] = (char)('0' + abs_int);
            } else {
                u32 pair = (u32)abs_int;
                buf[i] = two_digit_table[pair][1];
                buf[i - 1] = two_digit_table[pair][0];
                i -= 1;
            }
        }
    }

    curr = int_digit_start + num_int_digits;
    if (precision > 0) {
        buf[curr++] = '.';
        f64 f = frac_part;
        for (i32 d = 0; d < precision; ++d) {
            f *= 10.0;
            i64 dig = (i64)f;
            f -= (f64)dig;
            buf[curr + (u32)d] = (char)('0' + (u8)dig);
        }
        curr += (u32)precision;
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

bool is_space(char c) {
    return (c == ' '  ||
            c == '\t' ||
            c == '\n' ||
            c == '\r' ||
            c == '\f' ||
            c == '\v');
}

bool is_letter(char c) {
    return (c >= 'a' && c <= 'z') ||
        (c >= 'A' && c <= 'Z');
}

bool is_digit(char c) {
    return (c >= '0' && c <= '9');
}

bool is_alphanum(char c) {
    return (c >= '0' && c <= '9') ||
           (c >= 'A' && c <= 'Z') ||
           (c >= 'a' && c <= 'z');
}

void sbuilder_init_cap(String_Builder *builder, Arena *arena, u32 capacity) {
    builder->length = 0;
    builder->capacity = capacity;
    builder->arena = arena;

    if (capacity > 0) {
        builder->data = arena_alloc(arena, capacity);
    }
}

void sbuilder_init(String_Builder *builder, Arena *arena) {
    sbuilder_init_cap(builder, arena, STRING_BUILDER_DEFAULT_CAPACITY);
}

void sbuilder_append(String_Builder *builder, String str) {
    u32 total_length = builder->length + str.size;

    if (total_length > builder->capacity) {
        u32 new_capacity = builder->capacity;

        while (builder->capacity < total_length) {
            new_capacity *= 2;
        }

        printf("realocacion - capacidad anterior: %d capacidad nueva: %d\n", builder->capacity, new_capacity);

        u8 *new_data = arena_alloc(builder->arena, new_capacity);

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

typedef struct DynamicArray DynamicArray;

struct DynamicArray {
    void *items;
    u64 length;
    u64 capacity;
};

void dynamic_array_grow(Arena *arena, void *dynamic_array_ptr, size_t item_size) {
    DynamicArray *dynamic_array = (DynamicArray *)dynamic_array_ptr;

    if (dynamic_array->capacity <= 0) {
        dynamic_array->capacity = 1;
    }

    uintptr_t items_offset = dynamic_array->length * item_size;

    if (arena->data + arena->size == dynamic_array->items + items_offset) {
        arena_alloc_aligned(arena, dynamic_array->capacity * item_size, 1);
    } else {
        void *data = arena_alloc(arena, 2 * dynamic_array->capacity * item_size);
        if (dynamic_array->length > 0) {
            memcpy(data, dynamic_array->items, items_offset);
        }
        dynamic_array->items = data;
    }

    dynamic_array->capacity *= 2;
}

#define dynamic_array_append(arena, dynamic_array) \
    ((dynamic_array)->length >= (dynamic_array)->capacity \
     ? dynamic_array_grow(arena, dynamic_array, sizeof(*(dynamic_array)->items)), \
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

// hash: fnv1a
u64 hash_generic(void *data, size_t size) {
    u64 hash = 14695981039346656037ULL;
    u8 *bytes = (u8 *)data;
    for (u64 i = 0; i < size; i++) {
        hash ^= bytes[i];
        hash *= 1099511628211ULL; // numero primo
    }
    return hash;
}

