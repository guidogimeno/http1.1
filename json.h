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

    String key; // Exclusivo de los JSON_TYPE_OBJECT
    JSON_Value value;
};

enum JSON_Parser_State{
    JSON_STATUS_FAILED,
    JSON_STATUS_SUCCESS
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

String json_to_string(Allocator *allocator, JSON_Element *json);

b32 json_is_object(JSON_Element *element);
b32 json_is_array(JSON_Element *element);
b32 json_is_number(JSON_Element *element);
b32 json_is_string(JSON_Element *element);
b32 json_is_boolean(JSON_Element *element);
b32 json_is_null(JSON_Element *element);

f64 json_get_number(JSON_Element *element);
b32 json_get_boolean(JSON_Element *element);
String json_get_string(JSON_Element *element);
JSON_Element *json_object_get(JSON_Element *object, String key);
#define json_for_each(element, parent) \
    for (JSON_Element *element = parent->child; element != NULL; element = element->next) \

JSON_Element *json_create_object();
JSON_Element *json_create_array();
JSON_Element *json_create_null();
JSON_Element *json_create_number(f64 number);
JSON_Element *json_create_string(String string);
JSON_Element *json_create_boolean(b32 boolean);

void json_object_add(JSON_Element *object, String key, JSON_Element *value);
void json_object_add_string(JSON_Element *object, String key, String value);
void json_object_add_number(JSON_Element *object, String key, f64 value);
void json_object_add_boolean(JSON_Element *object, String key, b32 value);
void json_object_add_null(JSON_Element *object, String key);

void json_array_add(JSON_Element *array, JSON_Element *element);
void json_array_add_string(JSON_Element *array, String string);
void json_array_add_number(JSON_Element *array, f64 number);
void json_array_add_boolean(JSON_Element *array, b32 boolean);
void json_array_add_null(JSON_Element *array);

JSON_Element *json_object_push_object(JSON_Element *object, String key);
JSON_Element *json_object_push_array(JSON_Element *object, String key);
JSON_Element *json_object_push_string(JSON_Element *object, String key, String value);
JSON_Element *json_object_push_number(JSON_Element *object, String key, f64 value);
JSON_Element *json_object_push_boolean(JSON_Element *object, String key, b32 value);
JSON_Element *json_object_push_null(JSON_Element *object, String key);

JSON_Element *json_array_push_object(JSON_Element *array);
JSON_Element *json_array_push_array(JSON_Element *array);
JSON_Element *json_array_push_string(JSON_Element *array, String string);
JSON_Element *json_array_push_number(JSON_Element *array, f64 number);
JSON_Element *json_array_push_boolean(JSON_Element *array, b32 boolean);
JSON_Element *json_array_push_null(JSON_Element *array);

