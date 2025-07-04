typedef struct String {
    char const *data;
    u32 size;
} String;

String string(char const *text);
u32 string_size(char const *text);
bool string_eq(String *s1, String *s2);
bool string_eq_cstr(String *s1, char *s2);

String substring(Allocator *allocator, char const *text, u32 start, u32 end); 

bool cstr_eq(char *s1, char *s2);

