typedef struct String {
    char const *data;
    u32 length;
} String;

String string(char const *text);

String substring(Allocator *allocator, char const *text, u32 start, u32 end); 

u32 string_len(char const *text);

bool string_eq(String *s1, String *s2);
bool string_eq_cstr(String *s1, char *s2);
bool cstr_eq(char *s1, char *s2);

