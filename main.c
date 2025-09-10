#include "main.h"

static volatile bool main_running = true;

static i32 start_listening(void);

static void server_accept_client(Server *server, 
    struct sockaddr_in *client_addr, socklen_t *client_addr_len);

static Connection *server_find_connection(Server *server, i32 file_descriptor);
static Connection *server_find_free_connection(Server *server);

static i32 epoll_events_add_file_descriptor(i32 epoll_file_descriptor, i32 fd, 
    u32 epoll_events);
static i32 epoll_events_remove_file_descriptor(i32 epoll_file_descriptor, 
    i32 file_descriptor);

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

static void headers_init(Headers_Map *headers_map) {
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

static bool is_letter(char ch) {
    return (ch >= 'a' && ch <= 'z') ||
        (ch >= 'A' && ch <= 'Z');
}

static bool is_alphanum(char ch) {
    return (ch >= '0' && ch <= '9') ||
           (ch >= 'A' && ch <= 'Z') ||
           (ch >= 'a' && ch <= 'z');
}


// Parser

static void parser_init(Parser *parser, Allocator * allocator, i32 file_descriptor) {
    *parser = (Parser){0};
    parser->file_descriptor = file_descriptor;
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

static bool parser_keep_going(Parser *parser) {
    return parser->state != PARSER_STATE_FAILED;
}

static void parser_recv_from_socket(Parser *parser) {
    void *memory = allocator_alloc(parser->allocator, sizeof(Parser_Buffer) + MAX_PARSER_BUFFER_CAPACITY);
    Parser_Buffer *new_buffer = (Parser_Buffer *)memory;
    new_buffer->size = MAX_PARSER_BUFFER_CAPACITY;
    new_buffer->data = memory + sizeof(Parser_Buffer);
    new_buffer->next = NULL;

    i32 bytes_read = recv(parser->file_descriptor, new_buffer->data, new_buffer->size, 0);

    if (bytes_read == -1 && errno == EAGAIN) {
        // TODO: Ver que hago
        assert(1 && "ver que hacer en estos casos");
    }

    if (bytes_read == 0 || bytes_read == -1) {
        parser->state = PARSER_STATE_FAILED;
        return;
    }

    parser->at = 0;
    parser->current_buffer = new_buffer;
    parser->bytes_read = bytes_read;
    parser->marked_distance++;

    if (parser->first_buffer == NULL && parser->last_buffer == NULL) {
        parser->first_buffer = new_buffer;
        parser->last_buffer = new_buffer;
    }

    parser->last_buffer->next = new_buffer;
    parser->last_buffer = new_buffer;
}

static void parser_read_char(Parser *parser) {
    if (parser->at + 1 < parser->bytes_read) {
        parser->at++;
        return;
    } 

    parser_recv_from_socket(parser);
}

static void parser_read_bytes(Parser *parser, u64 bytes_count) {
    u64 first_buffer_pending_bytes = parser->bytes_read - parser->at;
    u64 total_pending_bytes = first_buffer_pending_bytes;

    while (total_pending_bytes < bytes_count) {
        parser_recv_from_socket(parser);
        total_pending_bytes += parser->bytes_read;
    }

    u64 other_buffers_pending_bytes = bytes_count - first_buffer_pending_bytes;
    u64 parser_at = (other_buffers_pending_bytes - 1) % parser->current_buffer->size;

    parser->at += parser_at;
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

static void parser_parse(Parser *parser, Request *request) {
    parser->state = PARSER_STATE_PARSING_REQUEST_LINE;

    parser_read_char(parser);
    parser_mark(parser, parser->at);

    // Parse Request Line
    while (is_letter(parser_get_char(parser)) && parser_keep_going(parser)) {
        parser_read_char(parser);
    }

    // Method
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
        return;
    }

    // Space
    if (parser_get_char(parser) != ' ') {
        parser->state = PARSER_STATE_FAILED;
        return;
    }

    // URI
    parser_read_char(parser);
    parser_mark(parser, parser->at);

    while (is_alphanum(parser_get_char(parser)) || 
        parser_get_char(parser) == '/' ||
        parser_get_char(parser) == '.') {
        parser_read_char(parser);
    }

    String uri = parser_extract_block(parser, parser->at - 1);
    if (string_eq(uri, string_lit(""))) {
        parser->state = PARSER_STATE_FAILED;
        return;
    }

    request->uri = uri;

    // Space
    if (parser_get_char(parser) != ' ') {
        parser->state = PARSER_STATE_FAILED;
        return;
    }

    // Version
    parser_read_char(parser);
    parser_mark(parser, parser->at);
    for (u32 i = 0; i < 7; i++) { // len(HTTP/x.y)
        parser_read_char(parser);
    }

    request->version = parser_extract_block(parser, parser->at);

    if (!string_eq(request->version, HTTP_VERSION_10) && 
        !string_eq(request->version, HTTP_VERSION_11)
    ) {
        parser->state = PARSER_STATE_FAILED;
        return;
    }

    // EOL
    parser_read_char(parser);
    if (parser_get_char(parser) != '\r') {
        parser->state = PARSER_STATE_FAILED;
        return;
    }

    parser_read_char(parser);
    if (parser_get_char(parser) != '\n') {
        parser->state = PARSER_STATE_FAILED;
        return;
    }


    // Parse Headers

    parser->state = PARSER_STATE_PARSING_HEADERS;

    while (parser->state == PARSER_STATE_PARSING_HEADERS) {
        parser_read_char(parser);
        parser_mark(parser, parser->at);

        while (parser_keep_going(parser) && 
                (is_alphanum(parser_get_char(parser)) || 
                parser_get_char(parser) == '-'        ||
                parser_get_char(parser) == '_')
        ) {
            parser_read_char(parser);
        }

        // si ya no quedan field names
        if (parser->marked_at == parser->at 
                && parser->marked_buffer == parser->current_buffer) {
            if (parser_get_char(parser) != '\r') {
                parser->state = PARSER_STATE_FAILED;
                return;
            }
            parser_read_char(parser);
            if (parser_get_char(parser) != '\n') {
                parser->state = PARSER_STATE_FAILED;
                return;
            }

            parser->state = PARSER_STATE_PARSING_BODY;
            break;
        }
        
        if (parser_get_char(parser) != ':') {
            parser->state = PARSER_STATE_FAILED;
            return;
        }

        // substring to lower case
        String field_name = parser_extract_block(parser, parser->at - 1);
        field_name = string_to_lower(parser->allocator, field_name);

        // espacio
        parser_read_char(parser);
        if (parser_get_char(parser) != ' ') {
            parser->state = PARSER_STATE_FAILED;
            return;
        }

        // field value + \r\n
        parser_read_char(parser);
        parser_mark(parser, parser->at);

        while (parser_keep_going(parser) && parser_get_char(parser) != '\r') {
            parser_read_char(parser);
        }

        String field_value = parser_extract_block(parser, parser->at - 1);
        headers_put(&request->headers_map, field_name, field_value);

        parser_read_char(parser);

        if (parser_get_char(parser) != '\n') {
            parser->state = PARSER_STATE_FAILED;
            return;
        } 
    }

    // Parse Body
    
    String *content_length = headers_get(&request->headers_map, 
                                            string_lit("content-length"));

    if (content_length != NULL) {
        i64 body_length = string_to_int(*content_length);
        if (body_length > 4 * KB) { 
            parser->state = PARSER_STATE_FAILED;
            return;
        }

        parser_read_char(parser);
        parser_mark(parser, parser->at);
        parser_read_bytes(parser, body_length);

        String body = parser_extract_block(parser, parser->at);

        request->body.length = body.size;
        request->body.data = (u8 *)body.data;
        printf("Body recibido: %.*s\n", string_print(body));
    }

    parser->state = PARSER_STATE_FINISHED;
}


// Connection

static void connection_init(Connection *connection, i32 file_descriptor,
    struct sockaddr_in address) {
    if (connection->allocator == NULL) {
        connection->allocator = allocator_make(1 * MB);
    } else {
        allocator_reset(connection->allocator);
    }

    connection->file_descriptor = file_descriptor;
    connection->host = string(inet_ntoa(address.sin_addr));
    connection->port = ntohs(address.sin_port); 
    connection->address = address; 
    connection->is_active = false;
    connection->request = (Request){0};
    headers_init(&connection->request.headers_map);
    parser_init(&connection->parser, connection->allocator, file_descriptor);
}

static void connection_write(Allocator *allocator, i32 fd, Response response) {
    String encoded_response = encode_response(allocator, response);

    u32 bytes_written = send(fd, encoded_response.data, 
                                encoded_response.size, 0);

    if (bytes_written != encoded_response.size) {
        perror("error al escribir al cliente");
        // TODO: Retornar un error o algo
    }
}

// TODO: ver que hacer con esto
// static void handle_parse_error(Allocator *allocator) {
//     Response response = {0};
//     response.status = 400;
//     headers_init(&response.headers);
//
//     const char *error_message = "request malformed";
//     response_write(allocator, &response, (u8 *)error_message, string_size(error_message));
//
//     connection_write(allocator, fd, response);
// }

static void http_handler(Allocator *allocator, Request *req, Response *res) {
    String body = string("{ \"foo\": \"bar\" }");

    res->status = 200;
    response_write(allocator, res, (u8 *)body.data, body.size);
}

static i32 epoll_events_add_file_descriptor(i32 epoll_file_descriptor, i32 fd, 
    u32 epoll_events) {

    struct epoll_event event;
    event.events = epoll_events;
    event.data.fd = fd;
    return epoll_ctl(epoll_file_descriptor, EPOLL_CTL_ADD, fd, &event);
}

static i32 epoll_events_remove_file_descriptor(i32 epoll_file_descriptor, 
    i32 file_descriptor) {

    i32 res = epoll_ctl(epoll_file_descriptor, EPOLL_CTL_DEL, file_descriptor, 
                        NULL);
    if (res == -1) {
        return -1;
    }

    return close(file_descriptor);
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


// Server

static void server_accept_client(Server *server, 
    struct sockaddr_in *client_addr, socklen_t *client_addr_len) {

    i32 client_fd = accept(server->file_descriptor, 
                            (struct sockaddr *)client_addr, client_addr_len);

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

    if (epoll_events_add_file_descriptor(server->epoll_file_descriptor, 
                                            client_fd,
                                            EPOLLIN|EPOLLET) == -1) {
        perror("error al agregar client_fd al epoll events");
        close(client_fd);
        return;
    }

    connection_init(connection, client_fd, *client_addr);
    connection->is_active = true;

    printf("Nuevo cliente aceptado: %.*s:%d\n", string_print(connection->host),
            connection->port);
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



// Signals

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

    struct sockaddr_in client_addr;
    socklen_t client_addr_len = sizeof(client_addr);

    struct epoll_event epoll_events[MAX_EPOLL_EVENTS];

    // aceptar conexiones
    while (main_running == true) {
        i32 epoll_events_count = epoll_wait(epoll_file_descriptor,
                                    epoll_events, MAX_EPOLL_EVENTS, -1);

        if (epoll_events_count == -1 && errno != EINTR) {
            continue;
        }

        for (u32 i = 0; i < epoll_events_count; i++) {
            i32 event_file_descriptor = epoll_events[i].data.fd;

            if (event_file_descriptor == server.file_descriptor) {
                server_accept_client(&server, &client_addr, &client_addr_len);
                continue;
            }

            Connection *connection = server_find_connection(&server,
                                        event_file_descriptor);
            if (connection == NULL) {
                printf("error: no se logro encontrar la conexion\n");

                epoll_events_remove_file_descriptor(epoll_file_descriptor,
                                                        event_file_descriptor);

                continue;
            }

            parser_parse(&connection->parser, &connection->request);

            if (!(connection->parser.state == PARSER_STATE_FINISHED)) {
                printf("error: no se pudo parsear bien el request\n");

                epoll_events_remove_file_descriptor(epoll_file_descriptor,
                                                connection->file_descriptor);
                connection->is_active = false;

                continue;
            }

            Response response = {0};
            headers_init(&response.headers);

            http_handler(connection->allocator, &connection->request,
                            &response);

            connection_write(connection->allocator, 
                                connection->file_descriptor, response);

            // String *connection_header = headers_get(
            //         &connection->request.headers_map,
            //         string_lit("connection"));
            //
            // if (connection_header != NULL) {
            //     if (string_eq(*connection_header, string_lit("Keep-Alive"))) {
            //         allocator_reset(connection->allocator);
            //         connection->is_active = true;
            //         continue;
            //     }
            // } 


            epoll_events_remove_file_descriptor(epoll_file_descriptor,
                                                connection->file_descriptor);
            connection->is_active = false;

            printf("Conexion cerrada porque ya se envio la respuesta\n");
        }
    }

    close(server.epoll_file_descriptor);
    close(server.file_descriptor);
    
    return EXIT_SUCCESS;
}

