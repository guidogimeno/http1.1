typedef struct {
    const char *data;
    u32 len;
} String;

String new_string(char const *text);
u32 string_len(char const *text);
bool strings_are_equal(String *s1, String *s2);
String substring(Allocator *allocator, char const *text, u32 start, u32 end);
