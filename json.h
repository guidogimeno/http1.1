typedef struct JSON_Element JSON_Element;
typedef enum JSON_Type JSON_Type;
typedef union JSON_Value JSON_Value;

typedef struct JSON_Parser JSON_Parser;
typedef enum JSON_Parser_State JSON_Parser_State;
typedef struct JSON_Token JSON_Token;
typedef enum JSON_Token_Type JSON_Token_Type;

enum JSON_Type {
    JSON_TYPE_OBJECT,
    JSON_TYPE_ARRAY,
    JSON_TYPE_PAIR,
    JSON_TYPE_NUMBER,
    JSON_TYPE_STRING,
    JSON_TYPE_BOOLEAN,
    JSON_TYPE_NULL
};

union JSON_Value{
    String string;
    f64 number;
    b32 boolean;
    void *null;
};

struct JSON_Element {
    JSON_Element *child;
    JSON_Element *next;
    JSON_Element *prev;

    JSON_Type type;
    JSON_Value value;

    String name; // Exclusivo de los JSON_TYPE_OBJECT
};

enum JSON_Parser_State{
    JSON_STATUS_FAILED,
    JSON_STATUS_SUCCESS,
    JSON_STATUS_PARSING
};

struct JSON_Parser {
    JSON_Parser_State state;
    String json_str;
    u32 at;
};

enum JSON_Token_Type {
    JSON_TOKEN_OPEN_BRACE,
    JSON_TOKEN_CLOSE_BRACE,
    JSON_TOKEN_OPEN_BRACKET,
    JSON_TOKEN_CLOSE_BRACKET,
    JSON_TOKEN_STRING,
    JSON_TOKEN_NUMBER,
    JSON_TOKEN_BOOLEAN,
    JSON_TOKEN_NULL,
    JSON_TOKEN_COLON,
    JSON_TOKEN_COMMA,
    JSON_TOKEN_UNKNOWN,
    JSON_TOKEN_EOF
};

struct JSON_Token{
    JSON_Token_Type type;
    String value;
};

JSON_Parser_State json_parse(Allocator *allocator, String json_str, JSON_Element *json);
JSON_Parser_State json_parse_cstr(Allocator *allocator, char *json_str, size_t json_size, JSON_Element *json);

