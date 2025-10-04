static void json_parse_object(JSON_Parser *parser, Allocator *allocator, JSON_Element *parent);
static void json_parse_array(JSON_Parser *parser, Allocator *allocator, JSON_Element *parent);

static JSON_Token json_get_token(JSON_Parser *parser) {
    JSON_Token token = {0};
    token.type = JSON_TOKEN_UNKNOWN;

    char c = parser->json_str.data[parser->at];

    while (c == ' ' || c == '\t' || c == '\n') {
        parser->at++;
        c = parser->json_str.data[parser->at];
    }

    switch (c) {
        case '{': {
            token.type = JSON_TOKEN_OPEN_BRACE;
            break;
        }
        case '}': {
            token.type = JSON_TOKEN_CLOSE_BRACE;
            break;
        }
        case '[': {
            token.type = JSON_TOKEN_OPEN_BRACKET;
            break;
        }
        case ']': {
            token.type = JSON_TOKEN_CLOSE_BRACKET;
            break;
        }
        case ':': {
            token.type = JSON_TOKEN_COLON;
            break;
        }
        case ',': {
            token.type = JSON_TOKEN_COMMA;
            break;
        }
        case '\0': {
            token.type = JSON_TOKEN_EOF;
            break;
        }
        case 'n': {
            if ((parser->json_str.size - 1) - parser->at > 3) {
                if (parser->json_str.data[parser->at + 1] == 'u' &&
                    parser->json_str.data[parser->at + 2] == 'l' &&
                    parser->json_str.data[parser->at + 3] == 'l') {
                    token.type = JSON_TOKEN_NULL;
                }
                parser->at += 3;
            }
            break;
        }
        case 't': {
            if ((parser->json_str.size - 1) - parser->at > 3) {
                if (parser->json_str.data[parser->at + 1] == 'r' &&
                    parser->json_str.data[parser->at + 2] == 'u' &&
                    parser->json_str.data[parser->at + 3] == 'e') {
                    token.type = JSON_TOKEN_BOOLEAN;
                    token.value.data = &parser->json_str.data[parser->at];
                    token.value.size = 1;
                }
                parser->at += 3;
            }
            break;
        }
        case 'f': {
            if ((parser->json_str.size - 1) - parser->at > 4) {
                if (parser->json_str.data[parser->at + 1] == 'a' &&
                    parser->json_str.data[parser->at + 2] == 'l' &&
                    parser->json_str.data[parser->at + 3] == 's' &&
                    parser->json_str.data[parser->at + 4] == 'e') {
                    token.type = JSON_TOKEN_BOOLEAN;
                    token.value.data = &parser->json_str.data[parser->at];
                    token.value.size = 1;
                }
                parser->at += 4;
            }
            break;
        }
        case '"': {
            if (parser->at + 1 < parser->json_str.size) {
                parser->at++;

                u32 start = parser->at;

                if (is_letter(parser->json_str.data[parser->at])) {

                    // TODO: Soportar escaped chars
                    while (parser->at < parser->json_str.size &&
                           (is_alphanum(parser->json_str.data[parser->at]) ||
                           parser->json_str.data[parser->at] == '_'        ||
                           parser->json_str.data[parser->at] == ' ')) {
                        parser->at++;
                    }
                }

                if (parser->json_str.data[parser->at] == '"') {
                    token.type = JSON_TOKEN_STRING;
                    token.value.data = &parser->json_str.data[start];
                    token.value.size = parser->at - start;
                }
            }
            break;
        }
        default: {
            if (is_digit(c) || c == '-') {
                u32 start = parser->at;

                parser->at++;

                // TODO: soportar exponenciales positivos y negativos (1e3, 1e-3, -1e-3)
                while (parser->at < parser->json_str.size &&
                        (is_digit(parser->json_str.data[parser->at]) || 
                         parser->json_str.data[parser->at] == '.')) {
                    parser->at++;
                }

                token.type = JSON_TOKEN_NUMBER;
                token.value.data = &parser->json_str.data[start];
                token.value.size = parser->at - start;

                parser->at--;
            }
            break;
        }
    }

    parser->at++;

    return token;
}

static bool json_require_token(JSON_Parser *parser, JSON_Token_Type type) {
    JSON_Token token = json_get_token(parser);
    if (token.type == type) {
        return true;
    } else {
        parser->state = JSON_STATUS_FAILED;
        return false;
    }
}

static void json_parse_element_value(JSON_Parser *parser, Allocator *allocator, JSON_Element *element, JSON_Token token) {
    switch (token.type) {
        case JSON_TOKEN_STRING: {
            element->type = JSON_TYPE_STRING;
            element->value.string = token.value;
            break;
        }
        case JSON_TOKEN_NUMBER: {
            element->type = JSON_TYPE_NUMBER;
            element->value.number = string_to_f64(token.value);
            break;
        }
        case JSON_TOKEN_NULL: {
            element->type = JSON_TYPE_NULL;
            element->value.null = NULL;
            break;
        }
        case JSON_TOKEN_BOOLEAN: {
            element->type = JSON_TYPE_NUMBER;
            element->value.boolean = *token.value.data == 't' ? true : false;
            break;
        }
        case JSON_TOKEN_OPEN_BRACE: {
            json_parse_object(parser, allocator, element);
            break;
        }
        case JSON_TOKEN_OPEN_BRACKET: {
            json_parse_array(parser, allocator, element);
            break;
        }
        default: { 
            parser->state = JSON_STATUS_FAILED;
            break;
        }
    }
}

static void json_element_init(JSON_Element *element) {
    *element = (JSON_Element){0};
}

static void json_parse_array(JSON_Parser *parser, Allocator *allocator, JSON_Element *parent) {
    parent->type = JSON_TYPE_ARRAY;

    JSON_Element *current = parent->child;

    JSON_Token token = json_get_token(parser);
    if (token.type == JSON_TOKEN_CLOSE_BRACKET) {
        return;
    }

    while (true) {
        JSON_Element *element = allocator_alloc(allocator, sizeof(JSON_Element));

        json_parse_element_value(parser, allocator, element, token);

        if (parser->state == JSON_STATUS_FAILED) {
            break;
        }

        if (current) {
            current->next = element;
            element->prev = current;
        } else {
            parent->child = element;
        }
        current = element;

        JSON_Token last_token = json_get_token(parser);
        if (last_token.type == JSON_TOKEN_CLOSE_BRACKET) {
            break;
        } else if (last_token.type != JSON_TOKEN_COMMA) {
            parser->state = JSON_STATUS_FAILED;
            break;
        };

        token = json_get_token(parser);
    }
}

static void json_parse_object(JSON_Parser *parser, Allocator *allocator, JSON_Element *parent) {
    parent->type = JSON_TYPE_OBJECT;

    JSON_Element *current = parent->child;
    String element_key;

    JSON_Token token_key = json_get_token(parser);
    if (token_key.type == JSON_TOKEN_CLOSE_BRACE) {
        return;
    } else if (token_key.type == JSON_TOKEN_STRING) {
        element_key = token_key.value;
    } else {
        parser->state = JSON_STATUS_FAILED;
        return;
    }

    while (true) {
        JSON_Element *element = allocator_alloc(allocator, sizeof(JSON_Element));
        element->key = element_key;
        
        if (!json_require_token(parser, JSON_TOKEN_COLON)) {
            break;
        }

        JSON_Token token_value = json_get_token(parser);
        json_parse_element_value(parser, allocator, element, token_value);

        if (parser->state == JSON_STATUS_FAILED) {
            break;
        }

        if (current) {
            current->next = element;
            element->prev = current;
        } else {
            parent->child = element;
        }
        current = element;

        JSON_Token last_token = json_get_token(parser);
        if (last_token.type == JSON_TOKEN_CLOSE_BRACE) {
            break;
        } else if (last_token.type != JSON_TOKEN_COMMA) {
            parser->state = JSON_STATUS_FAILED;
            break;
        };

        token_key = json_get_token(parser);
        if (token_key.type == JSON_TOKEN_STRING) {
            element_key = token_key.value;
        } else {
            parser->state = JSON_STATUS_FAILED;
            break;
        }
    }
}

JSON_Parser_State json_parse(Allocator *allocator, String json_str, JSON_Element *json) {
    JSON_Parser parser = {0};
    parser.state = JSON_STATUS_SUCCESS;
    parser.json_str = json_str;

    json_element_init(json);

    JSON_Token token = json_get_token(&parser);
    if (token.type == JSON_TOKEN_OPEN_BRACKET) {
        json_parse_array(&parser, allocator, json);
    } else if (token.type == JSON_TOKEN_OPEN_BRACE) {
        json_parse_object(&parser, allocator, json);
    } else {
        parser.state = JSON_STATUS_FAILED;
    }

    return parser.state;
};

JSON_Parser_State json_parse_cstr(Allocator *allocator, char *json_cstr, size_t json_size, JSON_Element *json) {
    String json_str = string_with_len(json_cstr, json_size);
    return json_parse(allocator, json_str, json);
};

b32 json_is_object(JSON_Element *element) {
    return element->type == JSON_TYPE_OBJECT;
}

b32 json_is_array(JSON_Element *element) {
    return element->type == JSON_TYPE_ARRAY;
}

b32 json_is_number(JSON_Element *element) {
    return element->type == JSON_TYPE_NUMBER;
}

b32 json_is_boolean(JSON_Element *element) {
    return element->type == JSON_TYPE_BOOLEAN;
}

b32 json_is_string(JSON_Element *element) {
    return element->type == JSON_TYPE_STRING;
}

b32 json_is_null(JSON_Element *element) {
    return element->type == JSON_TYPE_NULL;
}

f64 json_get_number(JSON_Element *element) {
    return element->value.number;
}

b32 json_get_boolean(JSON_Element *element) {
    return element->value.boolean;
}

String json_get_string(JSON_Element *element) {
    return element->value.string;
}

JSON_Element *json_object_get(JSON_Element *object, String key) {
    json_for_each(item, object) {
        if (string_eq(key, item->key)) {
            return item;
        }
    }
    return NULL;
}

JSON_Element *json_create_object(Allocator *allocator) {
    JSON_Element *element = allocator_alloc(allocator, sizeof(JSON_Element));
    element->type = JSON_TYPE_OBJECT;
    return element;
}

JSON_Element *json_create_array(Allocator *allocator) {
    JSON_Element *element = allocator_alloc(allocator, sizeof(JSON_Element));
    element->type = JSON_TYPE_ARRAY;
    return element;
}

JSON_Element *json_create_null(Allocator *allocator) {
    JSON_Element *element = allocator_alloc(allocator, sizeof(JSON_Element));
    element->type = JSON_TYPE_NULL;
    return element;
}

JSON_Element *json_create_number(Allocator *allocator, f64 number) {
    JSON_Element *element = allocator_alloc(allocator, sizeof(JSON_Element));
    element->type = JSON_TYPE_NUMBER;
    element->value.number = number;
    return element;
}

JSON_Element *json_create_string(Allocator *allocator, String string) {
    JSON_Element *element = allocator_alloc(allocator, sizeof(JSON_Element));
    element->type = JSON_TYPE_STRING;
    element->value.string = string;
    return element;
}

JSON_Element *json_create_boolean(Allocator *allocator, b32 boolean) {
    JSON_Element *element = allocator_alloc(allocator, sizeof(JSON_Element));
    element->type = JSON_TYPE_BOOLEAN;
    element->value.boolean = boolean;
    return element;
}

void json_object_add(JSON_Element *object, JSON_Element *value) {
    if (!object->child) {
        object->child = value;
    } else {
        JSON_Element *last = object->child;
        while (last->next) {
            last = last->next;
        }
        last->next = value;
    }
}

void json_object_add_string(JSON_Element *object, String key, String value, Allocator *allocator) {
    JSON_Element *new_element = allocator_alloc(allocator, sizeof(JSON_Element));
    new_element->key = key;
    new_element->value.string = value;
    json_object_add(object, new_element);
}

void json_object_add_number(JSON_Element *object, String key, f64 value, Allocator *allocator) {
    JSON_Element *new_element = allocator_alloc(allocator, sizeof(JSON_Element));
    new_element->key = key;
    new_element->value.number = value;
    json_object_add(object, new_element);
}

void json_object_add_boolean(JSON_Element *object, String key, b32 value, Allocator *allocator) {
    JSON_Element *new_element = allocator_alloc(allocator, sizeof(JSON_Element));
    new_element->key = key;
    new_element->value.boolean = value;
    json_object_add(object, new_element);
}

void json_object_add_null(JSON_Element *object, String key, Allocator *allocator) {
    JSON_Element *new_element = allocator_alloc(allocator, sizeof(JSON_Element));
    new_element->key = key;
    new_element->value.null = NULL;
    json_object_add(object, new_element);
}

void json_array_add(JSON_Element *array, JSON_Element *element) {
    json_object_add(array, element);
}

void json_array_add_string(JSON_Element *array, String value, Allocator *allocator) {
    JSON_Element *new_element = allocator_alloc(allocator, sizeof(JSON_Element));
    new_element->value.string = value;
    json_array_add(array, new_element);
}

void json_array_add_number(JSON_Element *array, f64 value, Allocator *allocator) {
    JSON_Element *new_element = allocator_alloc(allocator, sizeof(JSON_Element));
    new_element->value.number = value;
    json_array_add(array, new_element);
}

void json_array_add_boolean(JSON_Element *array, b32 value, Allocator *allocator) {
    JSON_Element *new_element = allocator_alloc(allocator, sizeof(JSON_Element));
    new_element->value.boolean = value;
    json_array_add(array, new_element);
}

void json_array_add_null(JSON_Element *array, Allocator *allocator) {
    JSON_Element *new_element = allocator_alloc(allocator, sizeof(JSON_Element));
    new_element->value.null = NULL;
    json_array_add(array, new_element);
}

static u32 json_append_element_as_string(Allocator *allocator, JSON_Element *element, b32 is_object) {
    u32 object_size = 2;

    char *open = allocator_alloc_aligned(allocator, 1, 1);
    open[0] = is_object ? '{' : '[';

    if (element->child) {

        json_for_each(item, element) {

            if (is_object) {
                // key
                u32 str_size = item->key.size + 3;
                char *str_buff = allocator_alloc_aligned(allocator, str_size, 1);

                str_buff[0] = '"';
                memcpy(str_buff + 1, item->key.data, str_size);
                str_buff[str_size - 2] = '"';
                str_buff[str_size - 1] = ':';

                object_size += str_size;
            }

            // value
            switch (item->type) {
                case JSON_TYPE_STRING: {
                    u32 str_size = item->value.string.size + 2;
                    char *str_buff = allocator_alloc_aligned(allocator, str_size, 1);

                    str_buff[0] = '"';
                    memcpy(str_buff + 1, item->value.string.data, str_size);
                    str_buff[str_size - 1] = '"';

                    object_size += str_size;
                    break;
                }
                case JSON_TYPE_NUMBER: {
                    // -42 rompio
                    String number = string_from_f64(allocator, item->value.number, 2);
                    object_size += number.size;
                    break;
                }
                case JSON_TYPE_NULL: {
                    char *null_buff = allocator_alloc_aligned(allocator, 4, 1);

                    memcpy(null_buff, "null", 4);

                    object_size += 4;
                    break;
                }
                case JSON_TYPE_BOOLEAN: {
                    u32 size;
                    if (item->value.boolean) {
                        size = 4;
                        char *buff = allocator_alloc_aligned(allocator, size, 1);
                        memcpy(buff, "true", size);
                    } else {
                        size = 5;
                        char *buff = allocator_alloc_aligned(allocator, size, 1);
                        memcpy(buff, "false", size);
                    }

                    object_size += size;
                    break;
                }
                case JSON_TYPE_OBJECT: {
                    object_size += json_append_element_as_string(allocator, item, true);
                    break;
                }
                case JSON_TYPE_ARRAY: {
                    object_size += json_append_element_as_string(allocator, item, false);
                    break;
                }
            }

            if (item->next) {
                char *comma_buff = allocator_alloc_aligned(allocator, 1, 1);
                comma_buff[0] = ',';
                object_size += 1;
            }
        }
    }

    char *close = allocator_alloc_aligned(allocator, 1, 1);
    close[0] = is_object ? '}' : ']';

    return object_size;
}

String json_to_string(Allocator *allocator, JSON_Element *json) {
    String result = {0};

    if (json != NULL) {
        result.data = allocator_alloc_aligned(allocator, 0, 1);

        if (json->type == JSON_TYPE_OBJECT) {
            result.size = json_append_element_as_string(allocator, json, true);
        } else if (json->type == JSON_TYPE_ARRAY) {
            result.size = json_append_element_as_string(allocator, json, true);
        }
    }

    return result;
}

