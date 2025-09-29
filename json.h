

typedef struct {
    String foo;
} JSONObject;

typedef struct {
    String foo;
} JSONArray;

typedef struct {
    String foo;
} JSONString;

void json_parse(char *json, size_t jsonc);

