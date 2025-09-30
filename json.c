static JSON_Token json_get_token(JSON_Parser *parser) {
    JSON_Token token = {0};
    token.type = JSON_TOKEN_UNKNOWN;

    char c = parser->json_str.data[parser->at];
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
        case '(': {
            token.type = JSON_TOKEN_OPEN_PAREN;
            break;
        }
        case ')': {
            token.type = JSON_TOKEN_CLOSE_PAREN;
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
            token.type =  JSON_TOKEN_EOF;
            break;
        }
        case '\n':
        case ' ':
        case '\t': {
            break;
        }
        case 't': {
            if ((parser->json_str.size - 1) - parser->at > 3) {
                if (parser->json_str.data[parser->at + 1] == 'r' &&
                    parser->json_str.data[parser->at + 2] == 'u' &&
                    parser->json_str.data[parser->at + 3] == 'e') {
                    token.type = JSON_TOKEN_BOOLEAN;
                    token.value.data = &parser->json_str.data[parser->at];
                    token.value.size = 4;
                }
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
                    token.value.size = 5;
                }
            }
            break;
        }
        case '"': {
            if (is_letter(c)) {
                u32 start = parser->at;

                while (parser->at < parser->json_str.size &&
                        is_alphanum(parser->json_str.data[parser->at])) {
                    parser->at++;
                }

                if (parser->json_str.data[parser->at] == '"') {
                    token.type = JSON_TOKEN_STRING;
                    token.value.data = &parser->json_str.data[start + 1];
                    token.value.size = start - parser->at - 1;
                }
            }
            break;
        }
        default: {
            if (is_digit(c)) {
                u32 start = parser->at;

                while (parser->at < parser->json_str.size &&
                        (is_digit(parser->json_str.data[parser->at]) || 
                         parser->json_str.data[parser->at] == '.')) {
                    parser->at++;
                }

                token.type = JSON_TOKEN_NUMBER;
                token.value.data = &parser->json_str.data[start + 1];
                token.value.size = start - parser->at - 1;
            } else {
                token.type = JSON_TOKEN_UNKNOWN;
            }
            break;
        }
    }

    return token;
}

JSON_Parser_State json_parse(String json_str, JSON *json) {
    JSON_Parser parser = {0};
    parser.json_str = json_str;

    bool parsing = true;

    while (parsing) {

        JSON_Token token = json_get_token(&parser);
        switch (token.type) {
            case JSON_TOKEN_EOF:
            case JSON_TOKEN_UNKNOWN: {
                parsing = false;
                break;
            }

            case JSON_TOKEN_OPEN_BRACKET:
            case JSON_TOKEN_CLOSE_BRACKET:
            default: break;
        }
    }

    return JSON_STATUS_SUCCESS;
};

JSON_Parser_State json_parse_cstr(char *json_cstr, size_t json_size, JSON *json) {
    String json_str = string_with_len(json_cstr, json_size);
    return json_parse(json_str, json);
};


