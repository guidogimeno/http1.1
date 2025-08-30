#include "main.h"

static volatile bool main_running = true;

static void headers_map_init(Headers_Map *headers_map) {
    headers_map->length = 0;
    headers_map->capacity = MAX_HEADERS_CAPACITY;
    memset(headers_map->headers, 0, headers_map->capacity);
}

static void headers_put(Headers_Map *headers_map, String field_name, String field_value) {
    u32 headers_cap = headers_map->capacity;

    // TODO: Esto deberia crecer
    assert(headers_map->length < (0.75 * headers_cap));

    u32 header_index = hash_string(field_name) % headers_cap; 
    Header *headers = headers_map->headers;
    Header *header = headers + header_index;

    bool is_occupied = header->occupied;
    bool different_field_name = !string_eq(header->field_name, field_name); 

    if (is_occupied && different_field_name) {
        // tiene que pegar la vuelta
        // y el peor escenario es iterar el array entero
        
        u32 iterations = 0;

        while (iterations < headers_cap && header->occupied) {
            iterations++;

            header_index++;
            if (header_index >= headers_cap) {
                header_index = 0;
            }

            header = &headers[header_index];
        }

        assert(!header->occupied && "el map no puede estar lleno");
    }

    header->field_name = field_name;
    header->field_value = field_value;
    header->occupied = true;

    headers_map->length++;
}

static String *headers_get(Headers_Map *headers_map, String field_name) {
    u32 headers_cap = headers_map->capacity;
    u32 header_index = hash_string(field_name) % headers_cap; 

    Header *headers = headers_map->headers;
    Header *header = headers + header_index;

    if (string_eq(header->field_name, field_name)) {
        return &header->field_value;
    }

    u32 iterations = 0;

    while (iterations < headers_cap && header->occupied) {
        iterations++;

        header_index++;
        if (header_index >= headers_cap) {
            header_index = 0;
        }

        header = &headers[header_index];
        if (string_eq(header->field_name, field_name)) {
            return &header->field_value;
        }
    }

    return NULL;
}

static String http_status_reason(u16 status) {
    switch (status) {
        case 200: return string_lit("Ok");
        case 201: return string_lit("Created");
        case 400: return string_lit("Bad Request");
        default: return string_lit("Unknown");
    }
}

static String encode_response(Allocator *allocator, Response response) {
    String line_separator = string_lit("\r\n");
    String colon_separator = string_lit(": ");
    String space_separator = string_lit(" ");

    String version = string_lit("HTTP/1.1 ");
    String status = string_from_int(allocator, response.status);
    String status_description = http_status_reason(response.status);
    String body = string_with_len((char *)response.body.data, response.body.length);

    // calculo de capacidad
    u32 response_minimum_size = version.size +
        status.size +
        status_description.size +
        body.size +
        1 + 2 + 2; // space + first line \r\n + headers \r\n

    for (u32 i = 0; i < MAX_HEADERS_CAPACITY; i++) {
        Header header = response.headers.headers[i];
        if (header.occupied) {
            // + 4 por colon + salto de linea (2 c/u)
            response_minimum_size += header.field_name.size + header.field_value.size + 4;
        }
    }

    String_Builder builder = {0};
    sbuilder_init_cap(&builder, allocator, response_minimum_size);
    sbuilder_append(&builder, version);
    sbuilder_append(&builder, status);
    sbuilder_append(&builder, space_separator);
    sbuilder_append(&builder, status_description);
    sbuilder_append(&builder, line_separator);

    for (u32 i = 0; i < MAX_HEADERS_CAPACITY; i++) {
        Header header = response.headers.headers[i];
        if (header.occupied) {
            sbuilder_append(&builder, header.field_name);
            sbuilder_append(&builder, colon_separator);
            sbuilder_append(&builder, header.field_value);
            sbuilder_append(&builder, line_separator);
        }
    }

    sbuilder_append(&builder, line_separator);

    if (response.body.length > 0) {
        sbuilder_append(&builder, body);
    }

    return sbuilder_to_string(&builder);
}

static void response_write(Allocator *allocator, Response *response, u8 *content, u32 length) {
    headers_put(&response->headers, string_lit("content-length"), string_from_int(allocator, length));

    response->body.data = content;
    response->body.length = length;
}

static void init_lexer(Lexer *lexer, u8 *buffer, u32 capacity) {
    lexer->buf = buffer;
    lexer->capacity = capacity; // Note: It is RECOMMENDED that all HTTP senders and recipients support, at a minimum, request-line lengths of 8000 octets.
    lexer->size = 0; 
    lexer->buf_position = 0;
    lexer->read_position = 0;
    lexer->current_char = 0;
}

static bool is_letter(char ch) {
    return (ch >= 'a' && ch <= 'z') ||
        (ch >= 'A' && ch <= 'Z');
}

static bool is_alphanum(char ch) {
    return (ch >= '0' && ch <= '9') ||
           (ch >= 'A' && ch <= 'Z') ||
           (ch >= 'a' && ch <= 'z');
}

static void read_char(Lexer *lexer) {
    if (lexer->read_position > lexer->size) {
        lexer->current_char = 0;
    } else {
        lexer->current_char = lexer->buf[lexer->read_position];
    }
    lexer->buf_position = lexer->read_position;
    lexer->read_position++;
}

static Parse_Error parse_request_line(Allocator *allocator, Lexer *lexer, Request *request) {
    do {
        read_char(lexer);
    } while (is_letter(lexer->current_char));

    // Method
    String method_str = string_sub_cstr(allocator, (char *)lexer->buf, 0, lexer->buf_position - 1);
    if (string_eq(method_str, string_lit("GET"))) {
        request->method = METHOD_GET;
    } else if (string_eq(method_str, string_lit("PUT"))) {
        request->method = METHOD_PUT;
    } else if (string_eq(method_str, string_lit("POST"))) {
        request->method = METHOD_POST;
    } else if (string_eq(method_str, string_lit("DELETE"))) {
        request->method = METHOD_DELETE;
    } else {
        return PARSE_ERROR_INVALID_METHOD;
    }

    // Space
    if (lexer->current_char != ' ') {
        return PARSE_ERROR_MALFORMED_REQUEST_LINE;
    }

    // URI
    u32 uri_start = lexer->read_position;
    do {
        read_char(lexer);
    } while (is_alphanum(lexer->current_char) || 
             lexer->current_char == '/' ||
             lexer->current_char == '.'
    );

    if (lexer->buf_position == uri_start) {
        return PARSE_ERROR_INVALID_URI;
    }

    request->uri = string_sub_cstr(allocator, (char *)lexer->buf, uri_start, lexer->buf_position - 1);

    // Space
    if (lexer->current_char != ' ') {
        return PARSE_ERROR_MALFORMED_REQUEST_LINE;
    }

    // Version
    u32 version_size = 8; // len(HTTP/x.y)
    char *version_data = allocator_alloc(allocator, version_size);
    for (u32 i = 0; i < version_size; i++) { 
        read_char(lexer);
        version_data[i] = lexer->current_char;
    }

    request->version = string_with_len(version_data, version_size);

    if (!string_eq(request->version, HTTP_VERSION_10) && 
        !string_eq(request->version, HTTP_VERSION_11)
    ) {
        return PARSE_ERROR_INVALID_VERSION;
    }

    // EOL
    read_char(lexer);
    if (lexer->current_char != '\r') {
        return PARSE_ERROR_MALFORMED_REQUEST_LINE;
    }

    read_char(lexer);
    if (lexer->current_char != '\n') {
        return PARSE_ERROR_MALFORMED_REQUEST_LINE;
    }

    return PARSE_ERROR_NO_ERROR;
}

static Parse_Error parse_headers(Allocator *allocator, Lexer *lexer, Request *request) {
    do {
        u32 field_name_start = lexer->read_position;

        do {
            read_char(lexer);
        } while (is_alphanum(lexer->current_char) || 
                 lexer->current_char == '-'       ||
                 lexer->current_char == '_');

        // si ya no quedan field names
        if (lexer->read_position - field_name_start == 1) {
            if (lexer->current_char != '\r') {
                return PARSE_ERROR_MALFORMED_HEADER;
            }
            read_char(lexer);
            if (lexer->current_char != '\n') {
                return PARSE_ERROR_MALFORMED_HEADER;
            }
            return PARSE_ERROR_NO_ERROR;
        }
        
        if (lexer->current_char != ':') {
            return PARSE_ERROR_MALFORMED_HEADER;
        }

        // substring to lower case
        u32 field_name_size = (lexer->read_position - 1) - field_name_start;
        String substring = string_with_len((char *)&lexer->buf[field_name_start], field_name_size); 
        String field_name = string_to_lower(allocator, substring);

        // espacio
        read_char(lexer);
        if (lexer->current_char != ' ') {
            return PARSE_ERROR_MALFORMED_HEADER;
        }

        // field value
        u32 field_value_start = lexer->read_position;
        u32 field_value_end;

        do {
            read_char(lexer);
        } while (lexer->current_char != '\r');

        field_value_end = lexer->buf_position - 1;
        String field_value = string_sub_cstr(allocator, (char *)lexer->buf, field_value_start, field_value_end);
        headers_put(&request->headers_map, field_name, field_value);

        // \r\n
        read_char(lexer);
        if (lexer->current_char != '\n') {
            return PARSE_ERROR_MALFORMED_HEADER;
        }

    } while (lexer->current_char != '\r');

    read_char(lexer);
    if (lexer->current_char != '\n') {
        return PARSE_ERROR_MALFORMED_HEADER;
    }

    return PARSE_ERROR_NO_ERROR;
}

static Parse_Error parse_body(Allocator *allocator, Lexer *lexer, Request *request) {
    String *content_length = headers_get(&request->headers_map, string_lit("content-length"));

    if (content_length != NULL) {
        i64 len = string_to_int(*content_length);
        if (len > 4 * KB) { 
            return PARSE_ERROR_BODY_TOO_LARGE;
        }

        request->body.length = len;
        request->body.data = &lexer->buf[lexer->read_position];
    }

    return PARSE_ERROR_NO_ERROR;
}

static void connection_write(Allocator *allocator, i32 fd, Response response) {
    String encoded_response = encode_response(allocator, response);

    u32 bytes_written = send(fd, encoded_response.data, encoded_response.size, 0);
    if (bytes_written != encoded_response.size) {
        perror("error al escribir al cliente");
        // TODO: Retornar un error o algo
    }
}

static void handle_parse_error(Allocator *allocator, i32 fd, Parse_Error err) {
    Response response = {0};
    response.status = 400;
    headers_map_init(&response.headers);

    const char *error_message = parse_error_messages[err];
    response_write(allocator, &response, (u8 *)error_message, string_size(error_message));

    connection_write(allocator, fd, response);
}

static void http_handler(Allocator *allocator, Request *req, Response *res) {
    String body = string("{ \"foo\": \"bar\" }");

    res->status = 200;
    response_write(allocator, res, (u8 *)body.data, body.size);
}

static i32 epoll_events_add_file_descriptor(i32 epoll_fd, i32 fd, u32 events) {
    struct epoll_event event;
    event.events = events;
    event.data.fd = fd;
    return epoll_ctl(epoll_fd, EPOLL_CTL_ADD, fd, &event);
}

static i32 epoll_events_remove_file_descriptor(i32 epoll_fd, i32 fd) {
    return epoll_ctl(epoll_fd, EPOLL_CTL_DEL, fd, NULL);
}

static i32 set_nonblocking(i32 fd) {
    i32 flags = fcntl(fd, F_GETFL, 0);
    if (flags == -1) {
        return -1;
    }

    if (fcntl(fd, F_SETFL, flags | O_NONBLOCK) == -1) {
        return -1;
    }

    return 0;
}


static void server_init(Server *server, i32 file_descriptor, Connection *connections, u32 max_connections) {
    server->file_descriptor = file_descriptor;
    server->connections_count = max_connections;
    server->connections = connections;
}

static void connection_init(Connection *connection, i32 file_descriptor, struct sockaddr_in address) {
    if (connection->allocator == NULL) {
        connection->allocator = allocator_make(1 * MB) ;
    } else {
        allocator_reset(connection->allocator);
    }

    connection->file_descriptor = file_descriptor;
    connection->host = string(inet_ntoa(address.sin_addr));
    connection->port = ntohs(address.sin_port); 
    connection->is_active = false;
    connection->request = (Request){0};
    headers_map_init(&connection->request.headers_map);
    connection->parser = (Parser){0};
    connection->parser.allocator = connection->allocator;
}

static Connection *server_find_connection(Server *server, i32 file_descriptor) {
    for (u32 i = 0; i < server->connections_count; i++) {
        if (server->connections[i].file_descriptor == file_descriptor) {
            return &server->connections[i];
        }
    }

    return NULL;
}

static Connection *server_find_free_connection(Server *server) {
    for (u32 i = 0; i < server->connections_count; i++) {
        if (server->connections[i].is_active == false) {
            return &server->connections[i];
        }
    }

    return NULL;
}

static void signal_handler(i32 signal_number) {
    switch (signal_number) {
        case SIGINT:
            main_running = false;
            break;
        default: break;
    }
}

static i32 init_signals(void) {
    if (signal(SIGINT, &signal_handler) == SIG_ERR) {
        return -1;
    }

    if (signal(SIGCHLD, SIG_IGN) == SIG_ERR) {
        return -1;
    }

    if (signal(SIGPIPE, SIG_IGN) == SIG_ERR) {
        return -1;
    }
    
    return 0;
}

static void set_process_name(int argc, char *argv[], char *env[], char *name) {
    size_t available = 0, length = strlen(name) + 1;

    if (argc >= 1) {
        available += strlen(argv[0]);
    }

    for (i32 n = 0; env[n] != NULL; n ++) {
        available += strlen(env[0]);
    }

    if (length > available) {
        return;
    }

    memcpy(argv[0], name, length);
}

int main(int argc, char *argv[], char *env[]) {
    set_process_name(argc, argv, env, "HTTP_SERVER_GG");

    if (init_signals() == -1) {
        perror("error al iniciar las signals");
        exit(EXIT_FAILURE);
    }

    // creacion del socket
    i32 server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd == -1) {
        perror("error al crear socket");
        exit(EXIT_FAILURE);
    }

    Connection connections[MAX_CONNECTIONS] = {0};

    Server server;
    server_init(&server, server_fd, connections, MAX_CONNECTIONS);
    
    // address reutilizable, no hace falta esperar al TIME_WAIT
    i32 reuse = 1;
    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, (const char *)&reuse, sizeof(reuse)) == -1) {
        perror("error al realizar setsockopt(SO_REUSEADDR)");
        exit(EXIT_FAILURE);
    }
    
    // bind address al socket
    struct sockaddr_in server_addr = {
        .sin_family = AF_INET,
        .sin_port = htons(8080),
        .sin_addr.s_addr = inet_addr("127.0.0.1"),
    };
    
    if (bind(server_fd, (struct sockaddr *)&server_addr, sizeof(server_addr)) == -1) {
        perror("error al realizar el bind");
        exit(EXIT_FAILURE);
    }
    
    // escuchar a traves del socket
    if (listen(server_fd, MAX_CONNECTIONS) == -1) {
        perror("error al realizar el listen");
        exit(EXIT_FAILURE);
    }
    
    String server_host = string(inet_ntoa(server_addr.sin_addr));
    u16 server_port = ntohs(server_addr.sin_port);
    printf("Servidor escuchando en: %.*s:%d\n", string_print(server_host), server_port);

    struct sockaddr_in client_addr;
    socklen_t client_addr_len = sizeof(client_addr);

    // epoll
    i32 epoll_events_count = 0;
    struct epoll_event events[MAX_EPOLL_EVENTS];

    i32 epoll_fd = epoll_create1(0);
    if (epoll_fd == -1) {
        exit(EXIT_FAILURE);
    }

    if (epoll_events_add_file_descriptor(epoll_fd, server_fd, EPOLLIN) == -1) {
        perror("error al agregar server_fd al epoll events");
        exit(EXIT_FAILURE);
    }
    
    // aceptar conexiones
    while (main_running) {
        epoll_events_count = epoll_wait(epoll_fd, events, MAX_EPOLL_EVENTS, -1);
        if (epoll_events_count == -1) {
            perror("epoll_wait()");
            continue;
        }

        for (u32 i = 0; i < epoll_events_count; i++) {
            if (events[i].data.fd == server_fd) {
                i32 client_fd = accept(server_fd, (struct sockaddr *)&client_addr, &client_addr_len);
                if (client_fd == -1) {
                    perror("error al aceptar cliente");
                    continue;
                }
                
                if (set_nonblocking(client_fd) == -1) {
                    perror("error al setear el nonblocking");
                    close(client_fd);
                    continue;
                }

                if (epoll_events_add_file_descriptor(epoll_fd, client_fd, EPOLLIN) == -1) {
                    perror("error al agregar client_fd al epoll events");
                    close(client_fd);
                    continue;
                }

                Connection *connection = server_find_free_connection(&server);
                if (connection == NULL) {
                    perror("no hay conexiones libres");
                    close(client_fd);
                    continue;
                }

                connection_init(connection, client_fd, client_addr);
                connection->is_active = true;

                printf("Nuevo cliente aceptado: %.*s:%d\n", string_print(connection->host), connection->port);
            } else {
                Connection *connection = server_find_connection(&server, events[i].data.fd);
                if (connection == NULL) {
                    perror("no se logro encontrar la conexion en el pool de conexiones");

                    if (epoll_events_remove_file_descriptor(epoll_fd, events[i].data.fd) == -1) {
                        perror("error al eliminar un fd del epoll");
                    }

                    close(events[i].data.fd);
                    continue;
                }

                u8 buf[8 * KB];
                Lexer lexer = {0};
                init_lexer(&lexer, buf, 8 * KB);

                i32 bytes_read = recv(connection->file_descriptor, lexer.buf, lexer.capacity, 0);
                if (bytes_read == 0) {
                    printf("el cliente cerro la conexion:%.*s\n", string_print(connection->host));

                    // TODO: ver como no repetir esto para cada error
                    if (epoll_events_remove_file_descriptor(epoll_fd, connection->file_descriptor) == -1) {
                        perror("error al eliminar un fd del epoll");
                    }
                    connection->is_active = false;
                    close(connection->file_descriptor);
                    continue;
                } else if (bytes_read == -1) {
                    perror("error al leer del cliente\n");
                    continue;
                }
                lexer.size = bytes_read - 1;

                Parse_Error err = parse_request_line(connection->allocator, &lexer, &connection->request);
                if (err) {
                    handle_parse_error(connection->allocator, connection->file_descriptor, err);
                    if (epoll_events_remove_file_descriptor(epoll_fd, connection->file_descriptor) == -1) {
                        perror("error al eliminar un fd del epoll");
                    }
                    connection->is_active = false;
                    close(connection->file_descriptor);
                    continue;
                }

                err = parse_headers(connection->allocator, &lexer, &connection->request);
                if (err) {
                    handle_parse_error(connection->allocator, connection->file_descriptor, err);
                    if (epoll_events_remove_file_descriptor(epoll_fd, connection->file_descriptor) == -1) {
                        perror("error al eliminar un fd del epoll");
                    }
                    connection->is_active = false;
                    close(connection->file_descriptor);
                    continue;
                }

                err = parse_body(connection->allocator, &lexer, &connection->request);
                if (err) {
                    handle_parse_error(connection->allocator, connection->file_descriptor, err);
                    if (epoll_events_remove_file_descriptor(epoll_fd, connection->file_descriptor) == -1) {
                        perror("error al eliminar un fd del epoll");
                    }
                    connection->is_active = false;
                    close(connection->file_descriptor);
                    continue;
                }

                Response response = {0};
                headers_map_init(&response.headers);

                http_handler(connection->allocator, &connection->request, &response);

                connection_write(connection->allocator, connection->file_descriptor, response);

                connection->is_active = false;

                if (epoll_events_remove_file_descriptor(epoll_fd, connection->file_descriptor) == -1) {
                    perror("error al eliminar un fd del epoll");
                }

                printf("Conexion cerrada\n");

                close(connection->file_descriptor);
            }
        }
    }
      
    close(epoll_fd);
    close(server_fd);
    
    return EXIT_SUCCESS;
}

