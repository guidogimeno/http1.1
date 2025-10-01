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

static void json_object_add_pair(Allocator *allocator, JSON_Object *object, String key, JSON_Element *element) {
    JSON_Pair *pair = allocator_alloc(allocator, sizeof(JSON_Pair));
    pair->key = key;
    pair->element = element;
    pair->next = NULL;

    if (object->first_pair == NULL && object->last_pair == NULL) {
        object->first_pair = pair;
    } else {
        object->last_pair->next = pair;
    }
    object->last_pair = pair;
}

static JSON_Object *json_parse_object(JSON_Parser *parser, Allocator *allocator) {
    JSON_Object *object = allocator_alloc(allocator, sizeof(JSON_Object));
    JSON_Element *element = allocator_alloc(allocator, sizeof(JSON_Element));

    JSON_Token token_key;

    do {
        token_key = json_get_token(parser);
        if (token_key.type != JSON_TOKEN_STRING) {
            printf("Error. Esperaba un String y recibi: %s, %.*s\n", detalles[token_key.type], string_print(token_key.value));
            return NULL;
        }

        JSON_Token token_colon = json_get_token(parser);
        if (token_colon.type != JSON_TOKEN_COLON) {
            printf("Error. Esperaba un ':' y recibi: %s, %.*s\n", detalles[token_colon.type], string_print(token_colon.value));
            return NULL;
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
                element->type = JSON_TYPE_OBJECT;
                element->value.object = *json_parse_object(parser, allocator); 
                break;
            }
            case JSON_TOKEN_OPEN_BRACKET: {
                element->type = JSON_TYPE_ARRAY;
                // element->value.array = json_parse_object(parser, allocator);  // TODO: json_parse_array(parser, allocator);
                break;
            }
            default: { 
                printf("Error. Recibi: %s, %.*s\n", detalles[token_value.type], string_print(token_value.value));
                return NULL;
            }
        }

        json_object_add_pair(allocator, object, token_key.value, element);

    } while (token_key.type == JSON_TOKEN_COMMA);

    if (token_key.type != JSON_TOKEN_CLOSE_BRACE) {
        printf("Error. Esperaba un '}' y recibi: %s, %.*s\n", detalles[token_key.type], string_print(token_key.value));
        return NULL;
    }

    return object;
}

JSON_Parser_State json_parse(String json_str, JSON_Element *json) {
    Allocator *allocator = allocator_make(1 * MB);

    JSON_Parser parser = {0};
    parser.json_str = json_str;

    // JSON_Token token;
    // do {
    //     token = json_get_token(&parser);
    //     printf("Token Type: %s\nToken Value:%.*s\n\n", detalles[token.type], string_print(token.value));
    // } while (token.type != JSON_TOKEN_EOF && token.type != JSON_TOKEN_UNKNOWN);

    JSON_Token token = json_get_token(&parser);
    switch (token.type) {
        default: {
            printf("No implementado: %s, %.*s\n", detalles[token.type], string_print(token.value));
            break;
        }
        case JSON_TOKEN_EOF: {
            printf("EOF\n");
            break;
        }
        case JSON_TOKEN_UNKNOWN: {
            printf("Token desconocido: %.*s\n", string_print(token.value));
            break;
        }
        case JSON_TOKEN_OPEN_BRACKET: {
           
            break;
        }
        case JSON_TOKEN_OPEN_BRACE: {
            parser.json.type = JSON_TYPE_OBJECT;
            parser.json.value.object = *json_parse_object(&parser, allocator);
            break;
        }
    }

    return JSON_STATUS_SUCCESS;
};

JSON_Parser_State json_parse_cstr(char *json_cstr, size_t json_size, JSON_Element *json) {
    String json_str = string_with_len(json_cstr, json_size);
    return json_parse(json_str, json);
};

