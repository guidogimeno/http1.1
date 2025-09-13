#include "main.h"

static volatile bool main_running = true;

static void set_process_name(int argc, char *argv[], char *env[], char *name);

static i32 signals_init(void);
static void signal_handler(i32 signal_number);

static i32 start_listening(void);

static void server_accept_client(Server *server);
static i32 set_nonblocking(i32 file_descriptor);

static Connection *server_find_connection(Server *server, i32 file_descriptor);
static Connection *server_find_free_connection(Server *server);

static i32 epoll_events_add_file_descriptor(i32 epoll_file_descriptor, i32 fd, u32 epoll_events);
static i32 epoll_events_remove_file_descriptor(i32 epoll_file_descriptor, i32 file_descriptor);

static void connection_init(Connection *connection, i32 file_descriptor, struct sockaddr_in address);
static bool connection_handle(Connection *connection);
static i32 connection_write(Connection *connection, Response response);

static String encode_response(Allocator *allocator, Response response);

static void parser_init(Parser *parser, Allocator * allocator, i32 file_descriptor);
static char parser_get_char(Parser *parser);
static Parser_Buffer *parser_push_buffer(Parser *parser);
static u32 parser_parse_request(Parser *parser, Request *request);

static void http_handler(Allocator *allocator, Request *req, Response *res);
static String http_status_reason(u16 status);

static void response_init(Response *response);
static void response_set_status(Response *response, u32 status);
static void response_add_header(Response *response, String key, String value);

static void headers_init(Headers_Map *headers_map);
static void headers_put(Headers_Map *headers_map, String field_name, String field_value);
static String *headers_get(Headers_Map *headers_map, String field_name);


int main(int argc, char *argv[], char *env[]) {
    set_process_name(argc, argv, env, "HTTP_SERVER_GG");

    if (signals_init() == -1) {
        perror("error al iniciar las signals");
        exit(EXIT_FAILURE);
    }

    Allocator *allocator = allocator_make(1 * GB);

    i32 server_file_descriptor = start_listening();    

    i32 epoll_file_descriptor = epoll_create1(0);
    if (epoll_file_descriptor == -1) {
        exit(EXIT_FAILURE);
    }

    if (epoll_events_add_file_descriptor(epoll_file_descriptor, 
            server_file_descriptor, EPOLLIN|EPOLLET) == -1) {
        exit(EXIT_FAILURE);
    }
    

    Server server = {0};
    server.file_descriptor = server_file_descriptor;
    server.epoll_file_descriptor = epoll_file_descriptor;
    server.connections_count = MAX_CONNECTIONS;
    server.connections = allocator_alloc(allocator, 
                            sizeof(Connection) * server.connections_count);

    struct epoll_event epoll_events[MAX_EPOLL_EVENTS];

    while (main_running) {
        i32 epoll_events_count = epoll_wait(epoll_file_descriptor,
                                    epoll_events, MAX_EPOLL_EVENTS, -1);

        if (epoll_events_count == -1 && errno != EINTR) {
            continue;
        }

        for (u32 i = 0; i < epoll_events_count; i++) {
            i32 event_file_descriptor = epoll_events[i].data.fd;

            if (event_file_descriptor == server.file_descriptor) {
                server_accept_client(&server);
                continue;
            }

            Connection *connection = server_find_connection(&server,
                                        event_file_descriptor);
            if (connection == NULL) {
                printf("error: no se logro encontrar la conexion\n");
                epoll_events_remove_file_descriptor(epoll_file_descriptor, event_file_descriptor);
                continue;
            }

            bool remove_connection = connection_handle(connection);
            if (remove_connection) {
                epoll_events_remove_file_descriptor(epoll_file_descriptor, connection->file_descriptor);
                connection->is_active = false;
            }
        }
    }

    close(server.epoll_file_descriptor);
    close(server.file_descriptor);
    
    return EXIT_SUCCESS;
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

static void signal_handler(i32 signal_number) {
    switch (signal_number) {
        case SIGINT:
            main_running = false;
            break;
        default: break;
    }
}

static i32 signals_init(void) {
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

static i32 start_listening(void) {
    // creacion del socket
    i32 file_descriptor = socket(AF_INET, SOCK_STREAM, 0);
    if (file_descriptor == -1) {
        perror("error al crear socket");
        exit(EXIT_FAILURE);
    }

    // address reutilizable, no hace falta esperar al TIME_WAIT
    i32 reuse = 1;
    if (setsockopt(file_descriptor, SOL_SOCKET, SO_REUSEADDR, &reuse, 
                    sizeof(reuse)) == -1) {
        perror("error al realizar setsockopt(SO_REUSEADDR)");
        exit(EXIT_FAILURE);
    }
    
    // bind address al socket
    struct sockaddr_in server_addr = {
        .sin_family = AF_INET,
        .sin_port = htons(8080),
        .sin_addr.s_addr = inet_addr("127.0.0.1"),
    };
    
    if (bind(file_descriptor, (struct sockaddr *)&server_addr, 
                sizeof(server_addr)) == -1) {
        perror("error al realizar el bind");
        exit(EXIT_FAILURE);
    }
    
    // escuchar a traves del socket
    if (listen(file_descriptor, MAX_CONNECTIONS) == -1) {
        perror("error al realizar el listen");
        exit(EXIT_FAILURE);
    }

    String server_host = string(inet_ntoa(server_addr.sin_addr));
    u16 server_port = ntohs(server_addr.sin_port);

    printf("Servidor escuchando en: %.*s:%d\n",
            string_print(server_host), server_port);

    return file_descriptor;
}

static void server_accept_client(Server *server) {

    struct sockaddr_in client_addr;
    socklen_t client_addr_len = sizeof(client_addr);

    i32 client_fd = accept(server->file_descriptor, (struct sockaddr *)&client_addr, &client_addr_len);
    if (client_fd == -1) {
        perror("error al aceptar cliente");
        return;
    }
    
    if (set_nonblocking(client_fd) == -1) {
        perror("error al setear el nonblocking");
        close(client_fd);
        return;
    }

    Connection *connection = server_find_free_connection(server);
    if (connection == NULL) {
        perror("no hay conexiones libres");
        close(client_fd);
        return;
    }

    if (epoll_events_add_file_descriptor(server->epoll_file_descriptor, client_fd, EPOLLIN|EPOLLET) == -1) {
        perror("error al agregar client_fd al epoll events");
        close(client_fd);
        return;
    }

    connection_init(connection, client_fd, client_addr);

    printf("Nuevo cliente aceptado: %.*s:%d\n", string_print(connection->host),
            connection->port);
}

static i32 set_nonblocking(i32 file_descriptor) {
    i32 flags = fcntl(file_descriptor, F_GETFL);
    if (flags == -1) {
        return -1;
    }

    if (fcntl(file_descriptor, F_SETFL, flags | O_NONBLOCK) == -1) {
        return -1;
    }

    return 0;
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

static void connection_init(Connection *connection, i32 file_descriptor, struct sockaddr_in address) {
    if (connection->allocator == NULL) {
        connection->allocator = allocator_make(1 * MB);
    } else {
        allocator_reset(connection->allocator);
    }

    connection->file_descriptor = file_descriptor;
    connection->host = string(inet_ntoa(address.sin_addr));
    connection->port = ntohs(address.sin_port); 
    connection->address = address; 
    connection->is_active = true;
    connection->error_ocurred = false;
    connection->request = (Request){0};
    headers_init(&connection->request.headers_map);
    parser_init(&connection->parser, connection->allocator, file_descriptor);
}

static bool connection_handle(Connection *connection) {
    Parser *parser = &connection->parser;
    Request *request = &connection->request;

    while (true) {

        Parser_Buffer *buffer = parser_push_buffer(parser); 

        parser->bytes_read = recv(connection->file_descriptor, buffer->data, buffer->size, 0);

        if (parser->bytes_read == -1) {
            if (errno != EAGAIN && errno != EWOULDBLOCK) {
                return true;
            }
            break;
        }

        if (parser->bytes_read == 0) {
            return true;
        }

        u32 bytes_parsed = 0;

        while (bytes_parsed < parser->bytes_read) {
            
            bytes_parsed += parser_parse_request(parser, request);
            
            if (parser->state == PARSER_STATE_FINISHED) {
                Response response;
                response_init(&response);

                http_handler(connection->allocator, request, &response);

                if (connection_write(connection, response) == -1) {
                    return true;
                }

                connection_init(connection, connection->file_descriptor, connection->address);
            }

            if (parser->state == PARSER_STATE_FAILED) {
                return true;
            }
        }

        if (parser->bytes_read < buffer->size) {
            break;
        }
    }

    if (parser->state != PARSER_STATE_FINISHED) {
        return false;
    }

    String *connection_value = headers_get(&request->headers_map, string_lit("connection"));
    if (connection_value != NULL) {
        if (string_eq(*connection_value, string_lit("Keep-Alive"))) {
            connection_init(connection, connection->file_descriptor, connection->address);
            return false;
        }
    } 

    return true;
}

static i32 connection_write(Connection *connection, Response response) {
    String encoded_response = encode_response(connection->allocator, response);

    i32 bytes_sent = send(connection->file_descriptor, encoded_response.data, encoded_response.size, 0);

    if (bytes_sent == -1) {
        return -1;
    }

    if (bytes_sent != encoded_response.size) {
        return -1;
    }

    return 0;
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


static i32 epoll_events_add_file_descriptor(i32 epoll_file_descriptor, i32 fd, u32 epoll_events) {

    struct epoll_event event;
    event.events = epoll_events;
    event.data.fd = fd;
    return epoll_ctl(epoll_file_descriptor, EPOLL_CTL_ADD, fd, &event);
}

static i32 epoll_events_remove_file_descriptor(i32 epoll_file_descriptor, i32 file_descriptor) {

    i32 res = epoll_ctl(epoll_file_descriptor, EPOLL_CTL_DEL, file_descriptor, 
                        NULL);
    if (res == -1) {
        return -1;
    }

    return close(file_descriptor);
}

static void parser_init(Parser *parser, Allocator * allocator, i32 file_descriptor) {
    *parser = (Parser){0};
    parser->allocator = allocator;
    parser->bytes_read = 0;
    parser->first_buffer = NULL;
    parser->last_buffer = NULL;
    parser->current_buffer = NULL;
    parser->at = 0;
    parser->marked_buffer = NULL;
    parser->marked_at = 0;
    parser->marked_distance = 0;
    parser->state = PARSER_STATE_STARTED;
}

static char parser_get_char(Parser *parser) {
    return parser->current_buffer->data[parser->at];
}

static void parser_mark(Parser *parser, u32 at) {
    parser->marked_buffer = parser->current_buffer;
    parser->marked_distance = 0;
    parser->marked_at = at;
}

static String parser_extract_block(Parser *parser, u32 last_buffer_offset) {

    Parser_Buffer *first_buffer = parser->marked_buffer;
    Parser_Buffer *last_buffer = parser->current_buffer;

    u8 *first_buffer_offset = first_buffer->data + parser->marked_at;

    if (first_buffer == last_buffer) {
        u32 total_size = last_buffer_offset - parser->marked_at + 1;

        void *data = allocator_alloc(parser->allocator, total_size);

        memcpy(data, first_buffer_offset, total_size);

        String result = {
            .data = data, 
            .size = total_size
        };

        return result;
    }

    u32 buffer_max_size = first_buffer->size;
    u32 first_buffer_remaining_size = buffer_max_size - parser->marked_at;
    u32 middle_buffers_size = parser->marked_distance * buffer_max_size;
    u32 surplus_buffer_size = buffer_max_size - last_buffer_offset - 1;

    u32 total_size = first_buffer_remaining_size + middle_buffers_size - surplus_buffer_size;

    void *data = allocator_alloc(parser->allocator, total_size);
    void *next_memcpy = data;

    memcpy(next_memcpy, first_buffer_offset, first_buffer_remaining_size);
    next_memcpy += first_buffer_remaining_size;

    Parser_Buffer *buffer;

    for (buffer = first_buffer->next; buffer != last_buffer; buffer = buffer->next) {
        memcpy(next_memcpy, buffer->data, buffer_max_size);
        next_memcpy += buffer_max_size;
    }

    memcpy(next_memcpy, buffer->data, last_buffer_offset + 1);

    String result = {
        .data = data,
        .size = total_size
    };

    return result;
}

static Parser_Buffer *parser_push_buffer(Parser *parser) {
    void *memory = allocator_alloc(parser->allocator, 
                        sizeof(Parser_Buffer) + MAX_PARSER_BUFFER_CAPACITY);

    Parser_Buffer *new_buffer = (Parser_Buffer *)memory;
    new_buffer->size = MAX_PARSER_BUFFER_CAPACITY;
    new_buffer->data = memory + sizeof(Parser_Buffer);
    new_buffer->next = NULL;

    parser->at = 0;
    parser->current_buffer = new_buffer;
    parser->marked_distance++;

    if (parser->first_buffer == NULL && parser->last_buffer == NULL) {
        parser->first_buffer = new_buffer;
        parser->last_buffer = new_buffer;
    }

    parser->last_buffer->next = new_buffer;
    parser->last_buffer = new_buffer;

    return new_buffer;
}

static u32 parser_parse_request(Parser *parser, Request *request) {
    u32 parser_start_position = parser->at;

    while (parser->at < parser->bytes_read) {

        char c = parser_get_char(parser);

        switch (parser->state) {

            case PARSER_STATE_STARTED: 

                parser_mark(parser, parser->at);
                parser->state = PARSER_STATE_PARSING_METHOD;

                break;

            case PARSER_STATE_PARSING_METHOD:

                if (is_letter(c)) {
                    break;
                }

                if (c != ' ') {
                    parser->state = PARSER_STATE_FAILED;
                    break;
                }

                String method = parser_extract_block(parser, parser->at - 1);

                if (string_eq(method, string_lit("GET"))) {
                    request->method = METHOD_GET;
                } else if (string_eq(method, string_lit("PUT"))) {
                    request->method = METHOD_PUT;
                } else if (string_eq(method, string_lit("POST"))) {
                    request->method = METHOD_POST;
                } else if (string_eq(method, string_lit("DELETE"))) {
                    request->method = METHOD_DELETE;
                } else {
                    parser->state = PARSER_STATE_FAILED;
                    break;
                }

                parser->state = PARSER_STATE_PARSING_SPACE_BEFORE_URI;

                break;

            case PARSER_STATE_PARSING_SPACE_BEFORE_URI:

                parser_mark(parser, parser->at);
                parser->state = PARSER_STATE_PARSING_URI;

                break;

            case PARSER_STATE_PARSING_URI:

                if (is_alphanum(c) || c == '/' || c == '.') {
                    break;
                }

                if (c != ' ') {
                    parser->state = PARSER_STATE_FAILED;
                    break;
                }

                String uri = parser_extract_block(parser, parser->at - 1);
                
                request->uri = uri;

                parser->state = PARSER_STATE_PARSING_SPACE_BEFORE_VERSION;

                break;

            case PARSER_STATE_PARSING_SPACE_BEFORE_VERSION:

                parser_mark(parser, parser->at);
                parser->state = PARSER_STATE_PARSING_VERSION;

                break;

            case PARSER_STATE_PARSING_VERSION:

                if (c == 'H' ||
                    c == 'T' ||
                    c == 'P' ||
                    c == '/' ||
                    c == '1' ||
                    c == '0' ||
                    c == '.'
                ) {
                    break;
                }

                if (c != '\r') {
                    parser->state = PARSER_STATE_FAILED;
                    break;
                }

                String version = parser_extract_block(parser, parser->at - 1);

                if (!string_eq(version, HTTP_VERSION_10) && 
                    !string_eq(version, HTTP_VERSION_11)
                ) {
                    parser->state = PARSER_STATE_FAILED;
                    break;
                }

                request->version = version;

                parser->state = PARSER_STATE_PARSING_END_OF_REQUEST_LINE;

                break;

            case PARSER_STATE_PARSING_END_OF_REQUEST_LINE:

                if (c != '\n') {
                    parser->state = PARSER_STATE_FAILED;
                    break;
                }

                parser->state = PARSER_STATE_PARSING_HEADER_KEY_BEGIN;

                break;

            case PARSER_STATE_PARSING_HEADER_KEY_BEGIN:

                if (c == '\r') {
                    parser->state = PARSER_STATE_PARSING_HEADERS_END;
                    break;
                }

                if (!is_alphanum(c)) {
                    parser->state = PARSER_STATE_FAILED;
                    break;
                }

                parser_mark(parser, parser->at);
                parser->state = PARSER_STATE_PARSING_HEADER_KEY;

                break;

            case PARSER_STATE_PARSING_HEADER_KEY:
                
                if (is_alphanum(c) || c == '-' || c == '_') {
                    break;
                }

                if (c != ':') {
                    parser->state = PARSER_STATE_FAILED;
                    break;
                }

                String key = parser_extract_block(parser, parser->at - 1);
                parser->header_name = string_to_lower(parser->allocator, key);

                parser->state = PARSER_STATE_PARSING_HEADER_SPACE;

                break;

            case PARSER_STATE_PARSING_HEADER_SPACE:

                if (c != ' ') {
                    parser->state = PARSER_STATE_FAILED;
                    break;
                }

                parser->state = PARSER_STATE_PARSING_HEADER_VALUE_BEGIN;

                break;

            case PARSER_STATE_PARSING_HEADER_VALUE_BEGIN:

                if (c == '\r') {
                    parser->state = PARSER_STATE_FAILED;
                    break;
                }

                parser_mark(parser, parser->at);
                parser->state = PARSER_STATE_PARSING_HEADER_VALUE;

                break;

            case PARSER_STATE_PARSING_HEADER_VALUE:

                if (c != '\r') {
                    break;
                }

                String value = parser_extract_block(parser, parser->at - 1);
                
                headers_put(&request->headers_map, parser->header_name, value);

                parser->state = PARSER_STATE_PARSING_HEADER_VALUE_END;

                break;

            case PARSER_STATE_PARSING_HEADER_VALUE_END:
                
                if (c != '\n') {
                    parser->state = PARSER_STATE_FAILED;
                    break;
                }

                parser->state = PARSER_STATE_PARSING_HEADER_KEY_BEGIN;

                break;
                
            case PARSER_STATE_PARSING_HEADERS_END:

                if (c != '\n') {
                    parser->state = PARSER_STATE_PARSING_HEADER_VALUE;
                    break;
                }

                String *content_length = headers_get(&request->headers_map, 
                                            string_lit("content-length"));

                if (content_length != NULL) {

                    i64 body_size = string_to_int(*content_length);

                    if (body_size < 0 || body_size > 4 * KB) { 
                        parser->state = PARSER_STATE_FAILED;
                        break;
                    } 

                    if (body_size == 0) {
                        parser->state = PARSER_STATE_FINISHED;
                        break;
                    }

                    parser->body_size = (u32) body_size;
                    parser->state = PARSER_STATE_PARSING_BODY_BEGIN;
                    break;
                }

                parser->state = PARSER_STATE_FINISHED;

                break;

            case PARSER_STATE_PARSING_BODY_BEGIN:

                parser_mark(parser, parser->at);
                parser->body_parsed++;

                if (parser->body_size == 1) {
                    String body = parser_extract_block(parser, parser->at);

                    request->body.length = body.size;
                    request->body.data = (u8 *)body.data;

                    parser->state = PARSER_STATE_FINISHED;
                    break;
                }

                parser->state = PARSER_STATE_PARSING_BODY;

                break;

            case PARSER_STATE_PARSING_BODY: {

                u32 pending = parser->body_size - parser->body_parsed;
                u32 remaining = parser->bytes_read - parser->at;

                if (pending > remaining) {
                    parser->at = parser->bytes_read;
                    parser->body_parsed += remaining;
                    break;
                }

                parser->at += pending - 1;
                parser->body_parsed += pending;

                String body = parser_extract_block(parser, parser->at);

                request->body.length = body.size;
                request->body.data = (u8 *)body.data;

                parser->state = PARSER_STATE_FINISHED;

                break;
            }

            case PARSER_STATE_FAILED:
            case PARSER_STATE_FINISHED:

                return parser->at - parser_start_position;

            default: assert(1 && "assert: parser_parse_request: caso no contemplado");
        }

        parser->at++;
    }

    return parser->at - parser_start_position;
}

static void http_handler(Allocator *allocator, Request *req, Response *res) {
    String body = string("{ \"foo\": \"bar\" }");

    response_add_header(res, string_lit("hola"), string_lit("mundo"));
    response_set_status(res, 200);

    response_write(allocator, res, (u8 *)body.data, body.size);
}

static String http_status_reason(u16 status) {
    switch (status) {
        case 200: return string_lit("Ok");
        case 201: return string_lit("Created");
        case 400: return string_lit("Bad Request");
        default: return string_lit("Unknown");
    }
}

static void response_init(Response *response) {
    *response = (Response){0};
    response->status = 500;
    headers_init(&response->headers);
}

static void response_set_status(Response *response, u32 status) {
    response->status = 200;
}

static void response_add_header(Response *response, String key, String value) {
    headers_put(&response->headers, key, value);
}

static void response_write(Allocator *allocator, Response *response, u8 *content, u32 length) {
    headers_put(&response->headers, string_lit("content-length"), string_from_int(allocator, length));

    response->body.data = content;
    response->body.length = length;
}

static void headers_init(Headers_Map *headers_map) {
    headers_map->length = 0;
    headers_map->capacity = MAX_HEADERS_CAPACITY;
    memset(headers_map->headers, 0, headers_map->capacity);
}

static void headers_put(Headers_Map *headers_map, String field_name, String field_value) {
    u32 headers_cap = headers_map->capacity;

    // TODO: Esto deberia crecer o devovler algun tipo de error
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

