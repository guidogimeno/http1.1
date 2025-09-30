typedef enum {
    JSON_STATUS_SUCCESS,
    JSON_STATUS_FAILED
} JSON_Parser_State;

typedef struct {
    JSON_Parser_State state;
    String json_str;
    u32 at;
} JSON_Parser;

typedef enum {
    JSON_TOKEN_OPEN_BRACE,
    JSON_TOKEN_CLOSE_BRACE,
    JSON_TOKEN_OPEN_BRACKET,
    JSON_TOKEN_CLOSE_BRACKET,
    JSON_TOKEN_OPEN_PAREN,
    JSON_TOKEN_CLOSE_PAREN,
    JSON_TOKEN_STRING,
    JSON_TOKEN_NUMBER,
    JSON_TOKEN_BOOLEAN,
    JSON_TOKEN_COLON,
    JSON_TOKEN_COMMA,
    JSON_TOKEN_UNKNOWN,
    JSON_TOKEN_EOF
} JSON_Token_Type;

typedef struct {
    JSON_Token_Type type;
    String value;
} JSON_Token;

typedef struct {
    String foo;
} JSON_Object;

typedef struct {
    String foo;
} JSON_Array;

typedef struct {
    String foo;
} JSON_String;

typedef enum {
    JSON_TYPE_OBJECT,
    JSON_TYPE_ARRAY,
    JSON_TYPE_NUMBER,
    JSON_TYPE_STRING,
    JSON_TYPE_BOOLEAN,
    JSON_TYPE_NULL
} JSON_Type;

typedef union {
    JSON_Object object;
    JSON_Array array;
    JSON_String string;
    f64 number;
    b32 boolean;
    i32 null;
} JSON_Value;

typedef struct {
    JSON_Type type;
    JSON_Value value;
} JSON;

JSON_Parser_State json_parse(String json_str, JSON *json);
JSON_Parser_State json_parse_cstr(char *json_str, size_t json_size, JSON *json);

