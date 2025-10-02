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

static void json_parse_array(JSON_Parser *parser, Allocator *allocator, JSON_Element *parent) {
    parent->type = JSON_TYPE_ARRAY;

    JSON_Element *current = parent->child;

    JSON_Token token = json_get_token(parser);
    if (token.type == JSON_TOKEN_CLOSE_BRACKET) {
        return;
    }

    while (true) {
        JSON_Element *element = allocator_alloc(allocator, sizeof(JSON_Element));
        *element = (JSON_Element){0};

        switch (token.type) {
            case JSON_TOKEN_STRING: {
                element->type = JSON_TYPE_STRING;
                element->value.string = token.value;
                break;
            }
            case JSON_TOKEN_NUMBER: {
                element->type = JSON_TYPE_NUMBER;
                element->value.number = string_to_float(token.value);
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
    String element_name;

    JSON_Token token_key = json_get_token(parser);
    if (token_key.type == JSON_TOKEN_CLOSE_BRACE) {
        return;
    } else if (token_key.type == JSON_TOKEN_STRING) {
        element_name = token_key.value;
    } else {
        parser->state = JSON_STATUS_FAILED;
        return;
    }

    while (true) {
        JSON_Element *element = allocator_alloc(allocator, sizeof(JSON_Element));
        *element = (JSON_Element){0};
        element->name = element_name;

        JSON_Token token_colon = json_get_token(parser);
        if (token_colon.type != JSON_TOKEN_COLON) {
            parser->state = JSON_STATUS_FAILED;
            break;
        }

        JSON_Token token_value = json_get_token(parser);
        switch (token_value.type) {
            case JSON_TOKEN_STRING: {
                element->type = JSON_TYPE_STRING;
                element->value.string = token_value.value;
                break;
            }
            case JSON_TOKEN_NUMBER: {
                element->type = JSON_TYPE_NUMBER;
                element->value.number = string_to_float(token_value.value);
                break;
            }
            case JSON_TOKEN_NULL: {
                element->type = JSON_TYPE_NULL;
                element->value.null = NULL;
                break;
            }
            case JSON_TOKEN_BOOLEAN: {
                element->type = JSON_TYPE_NUMBER;
                element->value.boolean = *token_value.value.data == 't' ? true : false;
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
            element_name = token_key.value;
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

    JSON_Token token = json_get_token(&parser);
    switch (token.type) {
        case JSON_TOKEN_OPEN_BRACKET: {
            json_parse_array(&parser, allocator, json);
            break;
        }
        case JSON_TOKEN_OPEN_BRACE: {
            json_parse_object(&parser, allocator, json);
            break;
        }
        default: {
            parser.state = JSON_STATUS_FAILED;
            break;
        }
    }

    return parser.state;
};

JSON_Parser_State json_parse_cstr(Allocator *allocator, char *json_cstr, size_t json_size, JSON_Element *json) {
    String json_str = string_with_len(json_cstr, json_size);
    return json_parse(allocator, json_str, json);
};

