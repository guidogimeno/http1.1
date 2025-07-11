typedef struct String {
    char const *data;
    u32 size;
} String;

// Como no son NULL terminated, esto hace que se puedan printear haciendo:
// printf("%.*s", string_print(string));
#define string_print(str) str.size, str.data

String string(char const *text);
String string_with_len(char const *text, u32 len);

u32 string_size(char const *text);

bool cstr_eq(char *s1, char *s2);
bool string_eq(String *s1, String *s2);
bool string_eq_cstr(String *s1, char *s2);

String string_to_lower(Allocator *a, String str);
String string_to_upper(Allocator *a, String str);
s64 string_to_int(String str);

String string_sub(Allocator *a, String *str, u32 start, u32 end); 
String string_sub_cstr(Allocator *a, char const *text, u32 start, u32 end); 
String string_slice(String *str, u32 start, u32 offset);

char char_to_lower(char c);
char char_to_upper(char c);
