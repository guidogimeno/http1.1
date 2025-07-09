typedef struct String {
    char const *data;
    u32 size;
} String;

// Como no son NULL terminated, esto hace que se puedan printear haciendo:
// printf("%.*s", string_print(string));
#define string_print(str) str.size, str.data

String string(char const *text);
u32 string_size(char const *text);
bool string_eq(String *s1, String *s2);
bool string_eq_cstr(String *s1, char *s2);
String string_sub(Allocator *allocator, char const *text, u32 start, u32 end); 
bool cstr_eq(char *s1, char *s2);

