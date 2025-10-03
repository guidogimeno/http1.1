// TODO:
// Revisar por que en MAC se traba en cierto punto cuando mando muchos requests
// Comando a probar: ab -k -c 10 -n 10000 http://127.0.0.1:8080/foo

static i32 signals_init(void);
static void signal_handler(i32 signal_number);
static i32 start_listening(u32 port, char *host);
static i32 set_nonblocking(i32 fd);

static void server_accept_client(Server *server);
static Connection *server_find_connection(Server *server, i32 fd);
static Connection *server_find_free_connection(Server *server);
static bool server_handle_connection(Server *server, Connection *connection);

static void patterns_tree_add(Segment_Pattern **tree, Segment_Pattern *segment);
static Http_Handler *find_handler_while_adding_path_params(Segment_Pattern **request_patterns,
                                                           Segment_Pattern *server_patterns);
static i32 events_add_fd(i32 events_fd, i32 fd);
static i32 events_remove_fd(i32 events_fd, i32 fd);

static void connection_init(Connection *connection, i32 fd, struct sockaddr_in address);
static i32 connection_write(Connection *connection, Response response);

static String encode_response(Allocator *allocator, Response response);

static void parser_init(Parser *parser, Allocator *allocator);
static char parser_get_char(Parser *parser);
static Parser_Buffer *parser_push_buffer(Parser *parser);
static u32 parser_parse_request(Parser *parser, Request *request);

static void pattern_parser_parse(Pattern_Parser *pattern_parser, Allocator *allocator, String pattern_str);
static void pattern_parser_add_segment(Pattern_Parser *parser, Allocator *allocator, String segment, bool is_path_param);

static String http_status_reason(u16 status);

static void request_init(Request *request);
static void request_add_uri_segments(Request *request, Allocator *allocator, String uri);
static void request_add_segment_literal(Request *request, Allocator *allocator, String literal);
static void request_add_query_param(Request *request, Allocator *allocator, String key, String value);

static void response_init(Response *response);

static void headers_init(Headers_Map *headers_map);
static void headers_put(Headers_Map *headers_map, String field_name, String field_value);


static volatile bool main_running = true;

Server *http_server_make(Allocator *allocator) {
    Server *server = allocator_alloc(allocator, sizeof(Server));

    *server = (Server){0};
    server->allocator = allocator;

    return server;
}

void http_server_handle(Server *server, char *pattern, Http_Handler *handler) {
    if (pattern == NULL || handler == NULL) {
        panic_with_msg("http_server_handle args {pattern} and {handler} cannot be null" );
    }

    String pattern_str = string(pattern);
    if (pattern_str.size == 0) {
        panic_with_msg("http_server_handle arg {pattern} cannot be empty" );
    }

    Pattern_Parser parser = {0}; 
    pattern_parser_parse(&parser, server->allocator, pattern_str);

    if (parser.state == PATTERN_PARSER_STATE_FAILED || parser.state != PATTERN_PARSER_STATE_FINISHED) {
        panic_with_msg("http_server_handle failed to parse pattern" );
    }

    parser.last_segment->handler = handler;

    patterns_tree_add(&server->patterns_tree, parser.first_segment);
}

/*
 * Parsea el patron del path.
 * Estos path tienen el formato:
 *
 * [GET|POST|PUT] /foo/{bar}/baz
 *
 * Esto crea una lista de segmentos dentro del parser.
 */
static void pattern_parser_parse(Pattern_Parser *pattern_parser, Allocator *allocator, String pattern_str) {
 
    pattern_parser->state = PATTERN_PARSER_STATE_STARTED;

    u32 slash_pos = 0;
    u32 open_brace_pos = 0;

    for (u32 i = 0; i < pattern_str.size; i++) {

        char c = pattern_str.data[i];

        switch (pattern_parser->state) {

            case PATTERN_PARSER_STATE_STARTED: {

                if (is_letter(c)) {
                    break;
                }

                if (c != ' ') {
                    pattern_parser->state = PATTERN_PARSER_STATE_FAILED;
                    break;
                }

                String method = string_with_len(pattern_str.data, i);

                if (string_eq(method, string_lit("GET"))  ||
                    string_eq(method, string_lit("PUT"))  ||
                    string_eq(method, string_lit("POST")) ||
                    string_eq(method, string_lit("DELETE"))) {

                    pattern_parser_add_segment(pattern_parser, allocator, method, false);
                    pattern_parser->state = PATTERN_PARSER_STATE_PARSING_SLASH;

                } else {

                    pattern_parser->state = PATTERN_PARSER_STATE_FAILED;

                }

                break;
            }

            case PATTERN_PARSER_STATE_PARSING_SLASH: {

                if (c != '/') {
                    pattern_parser->state = PATTERN_PARSER_STATE_FAILED;
                    break;
                }

                slash_pos = i;

                pattern_parser->state = PATTERN_PARSER_STATE_PARSING_PATH_SEGMENT;

                break;
            }

            case PATTERN_PARSER_STATE_PARSING_PATH_SEGMENT: {

                if (is_alphanum(c)) {
                    break;
                }

                if (c == '{') {
                    open_brace_pos = i;
                    pattern_parser->state = PATTERN_PARSER_STATE_PARSING_PATH_PARAM;
                    break;
                }

                if (c != '/') {
                    pattern_parser->state = PATTERN_PARSER_STATE_FAILED;
                    break;
                }

                String segment = string_with_len(pattern_str.data + slash_pos + 1, i - slash_pos - 1);
                pattern_parser_add_segment(pattern_parser, allocator, segment, false);

                slash_pos = i;

                pattern_parser->state = PATTERN_PARSER_STATE_PARSING_PATH_SEGMENT;
                
                break;
            }

            case PATTERN_PARSER_STATE_PARSING_PATH_PARAM: {

                if (is_letter(c)) {
                    break;
                }

                if (c != '}') {
                    pattern_parser->state = PATTERN_PARSER_STATE_FAILED;
                    break;
                }

                String segment = string_with_len(pattern_str.data + open_brace_pos + 1, i - open_brace_pos - 1);
                pattern_parser_add_segment(pattern_parser, allocator, segment, true);

                if (i == pattern_str.size -1) {
                    pattern_parser->state = PATTERN_PARSER_STATE_FINISHED;
                } else {
                    pattern_parser->state = PATTERN_PARSER_STATE_PARSING_SLASH;
                }

                break;
            }

            case PATTERN_PARSER_STATE_FINISHED:
            case PATTERN_PARSER_STATE_FAILED:
                return;
        }
    }

    if (pattern_parser->state == PATTERN_PARSER_STATE_PARSING_PATH_SEGMENT ||
        pattern_parser->state == PATTERN_PARSER_STATE_PARSING_SLASH) {

        String segment = string_with_len(pattern_str.data + slash_pos + 1, pattern_str.size - slash_pos - 1);
        pattern_parser_add_segment(pattern_parser, allocator, segment, false);

        pattern_parser->state = PATTERN_PARSER_STATE_FINISHED;
    }
}

static void pattern_parser_add_segment(Pattern_Parser *parser, Allocator *allocator, String segment, bool is_path_param) {
    Segment_Pattern *segment_pattern = allocator_alloc(allocator, sizeof(Segment_Pattern));
    segment_pattern->segment = segment;
    segment_pattern->is_path_param = is_path_param;
    segment_pattern->handler = NULL;
    segment_pattern->next_segment = NULL;
    segment_pattern->first_segment = segment_pattern;
    segment_pattern->last_segment = segment_pattern;
    segment_pattern->child_segments = NULL;

    if (parser->first_segment == NULL && parser->last_segment == NULL) {
        parser->first_segment = segment_pattern;
    } else {
        parser->last_segment->child_segments = segment_pattern;
    }
    parser->last_segment = segment_pattern;
}

i32 http_server_start(Server *server, u32 port, char *host) {
    if (signals_init() == -1) {
        perror("error al iniciar las signals");
        return EXIT_FAILURE;
    }

    i32 server_fd = start_listening(port, host);    

    i32 events_fd;
#if OS_MAC
    struct kevent eventlist[MAX_EVENTS];
    events_fd = kqueue();
#else
    struct epoll_event epoll_events[MAX_EVENTS];
    events_fd = epoll_create1(0);
#endif

    if (events_fd == -1) {
        return EXIT_FAILURE;
    }

    if (events_add_fd(events_fd, server_fd) == -1) {
        perror("error al agregar server_fd al epoll events");
        return EXIT_FAILURE;
    }

    server->fd = server_fd;
    server->events_fd = events_fd;
    server->connections_count = MAX_CONNECTIONS;
    server->connections = allocator_alloc(server->allocator, sizeof(Connection) * server->connections_count);

    while (main_running) {

        i32 events_count;
#if OS_MAC
        events_count = kevent(events_fd, NULL, 0, eventlist, MAX_EVENTS,	NULL);
#else
        events_count = epoll_wait(events_fd, epoll_events, MAX_EVENTS, -1);
#endif
        if (events_count == -1) {
            continue;
        }

        for (u32 i = 0; i < events_count; i++) {

            i32 event_fd;
#if OS_MAC
            event_fd = eventlist[i].ident;
#else
            event_fd = epoll_events[i].data.fd;
#endif
            if (event_fd == server->fd) {
                server_accept_client(server);
                continue;
            }

            Connection *connection = server_find_connection(server, event_fd);
            if (connection == NULL) {
                printf("error: no se logro encontrar la conexion\n");
                events_remove_fd(events_fd, event_fd);
                continue;
            }

            bool remove_connection = server_handle_connection(server, connection);
            if (remove_connection) {
                events_remove_fd(events_fd, connection->fd);
                connection->is_active = false;
            }
        }
    }

    close(server->events_fd);
    close(server->fd);

    return EXIT_SUCCESS;
}

Body http_request_get_body(Request *request) {
    return request->body;
}

String http_request_get_header(Request *request, String name) {
    String *value = http_headers_get(&request->headers_map, name);
    if (value) {
        return *value;
    }
    return string_lit("");
}

Headers_Map http_request_get_headers(Request *request) {
    return request->headers_map;
}

/*
 * Busca el path param en base al nombre de la variable definido en la URL
 * Ejemplo: `/foo/{bar}`
 * En este caso el nombre de la variable seria "bar".
 * En caso de no encontrar el atributo, retorna un string vacio.
 */
String http_request_get_path_param(Request *request, String name) {
    
    for (Segment_Pattern *segment = request->first_segment;
         segment != NULL;
         segment = segment->next_segment) {

        if (segment->is_path_param && 
                string_eq(segment->path_param_name, name)) {

            return segment->segment;
        }

    }

    return string_lit("");
}

String http_request_get_query_param(Request *request, String name) {
    for (Query_Param *qparam = request->first_query_param;
         qparam != NULL;
         qparam = qparam->next) {

        if (string_eq(qparam->key, name)) {
            return qparam->value;
        }
    }

    return string_lit("");
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

static i32 start_listening(u32 port, char *host) {
    // creacion del socket
    i32 fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd == -1) {
        perror("error al crear socket");
        exit(EXIT_FAILURE);
    }

    // address reutilizable, no hace falta esperar al TIME_WAIT
    i32 reuse = 1;
    if (setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse)) == -1) {
        perror("error al realizar setsockopt(SO_REUSEADDR)");
        exit(EXIT_FAILURE);
    }
    
    // bind address al socket
    struct sockaddr_in server_addr = {
        .sin_family = AF_INET,
        .sin_port = htons(port),
        .sin_addr.s_addr = inet_addr(host),
    };
    
    if (bind(fd, (struct sockaddr *)&server_addr, sizeof(server_addr)) == -1) {
        perror("error al realizar el bind");
        exit(EXIT_FAILURE);
    }
    
    // escuchar a traves del socket
    if (listen(fd, MAX_CONNECTIONS) == -1) {
        perror("error al realizar el listen");
        exit(EXIT_FAILURE);
    }

    String server_host = string(inet_ntoa(server_addr.sin_addr));
    u16 server_port = ntohs(server_addr.sin_port);

    printf("Servidor escuchando en: %.*s:%d\n", string_print(server_host), server_port);

    return fd;
}

static i32 set_nonblocking(i32 fd) {
    i32 flags = fcntl(fd, F_GETFL);
    if (flags == -1) {
        return -1;
    }

    if (fcntl(fd, F_SETFL, flags | O_NONBLOCK) == -1) {
        return -1;
    }

    return 0;
}

static void server_accept_client(Server *server) {

    struct sockaddr_in client_addr;
    socklen_t client_addr_len = sizeof(client_addr);

    i32 client_fd = accept(server->fd, (struct sockaddr *)&client_addr, &client_addr_len);
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

    if (events_add_fd(server->events_fd, client_fd) == -1) {
        perror("error al agregar client_fd al epoll events");
        close(client_fd);
        return;
    }

    connection_init(connection, client_fd, client_addr);

    printf("Nuevo cliente aceptado: %.*s:%d\n", string_print(connection->host),
            connection->port);
}


static Connection *server_find_connection(Server *server, i32 fd) {
    for (u32 i = 0; i < server->connections_count; i++) {
        if (server->connections[i].fd == fd) {
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

static void connection_init(Connection *connection, i32 fd, struct sockaddr_in address) {
    if (connection->allocator == NULL) {
        connection->allocator = allocator_make(1 * MB);
    } else {
        allocator_reset(connection->allocator);
    }

    connection->fd = fd;
    connection->state = CONNECTION_STATE_ACTIVE;
    connection->host = string(inet_ntoa(address.sin_addr));
    connection->port = ntohs(address.sin_port); 
    connection->address = address; 
    connection->is_active = true;
    connection->keep_alive = false;
    connection->request = (Request){0};
    headers_init(&connection->request.headers_map);
    parser_init(&connection->parser, connection->allocator);
}

static bool server_handle_connection(Server *server, Connection *connection) {
    Parser *parser = &connection->parser;
    Request *request = &connection->request;

    while (true) {

        Parser_Buffer *buffer = parser_push_buffer(parser); 

        parser->bytes_read = read(connection->fd, buffer->data, buffer->size);
        if (parser->bytes_read < 0) {
            return true;
        }

        u32 bytes_parsed = 0;

        while (bytes_parsed < parser->bytes_read) {
            
            bytes_parsed += parser_parse_request(parser, request);
            
            if (parser->state == PARSER_STATE_FINISHED) {

                Http_Handler *handler = find_handler_while_adding_path_params(&request->first_segment,
                                                                              server->patterns_tree);
                if (handler) {

                    String *connection_value = http_headers_get(&request->headers_map, string_lit("connection"));
                    if (connection_value == NULL) {
                        connection->keep_alive = string_eq(request->version, HTTP_VERSION_11);
                    } else {
                        connection->keep_alive = string_eq(*connection_value, string_lit("Close"));
                    }
                
                    Response response;
                    response_init(&response);
                
                    handler(request, &response);
                
                    if (connection_write(connection, response) == -1) {
                        connection->state = CONNECTION_STATE_FAILED;
                        break;
                    }
                
                    request_init(request); 

                } else {
                    // TODO: responder un 404 o algo
                }
            }

            if (parser->state == PARSER_STATE_FAILED) {
                break;
            }
        }

        if (parser->bytes_read < buffer->size || 
            parser->state == PARSER_STATE_FAILED ||
            connection->state == CONNECTION_STATE_FAILED) {
            break;
        }
    }

    if (!(parser->state == PARSER_STATE_FINISHED || 
                parser->state == PARSER_STATE_STARTED) || 
        connection->state == CONNECTION_STATE_FAILED) {
        return true;
    }

    if (connection->keep_alive) {
        connection_init(connection, connection->fd, connection->address);
        connection->keep_alive = true;
        return false;
    } 

    return true;
}

/*
 * Agrega la lista enlazada de segmentos al arbol de segmentos.
 * Este arbol no es binario, sino que cada nodo puede tener multiples hijos.
 * Con lo cual se comparan los nodos del arbol con los de la lista enlazada hasta
 * encontrar el ancestro en comun mas reciente.
 * Una vez encontrado se agregan los nodos restantes al arbol.
 *
 * De:
 * 
 *          Arbol       Lista Enlazada
 *           (A)            (A)->(B)->(D)
 *          /   \
 *        (B)   (C)
 *
 *
 * A:
 *          Arbol       
 *           (A)     
 *          /   \
 *        (B)   (C)
 *        /
 *      (D)
 *
 */
static void patterns_tree_add(Segment_Pattern **tree, Segment_Pattern *segment) {

    Segment_Pattern *server_segment = NULL;

    for (server_segment = *tree;
         server_segment != NULL;
         server_segment = server_segment->next_segment) {

        if (string_eq(server_segment->segment, segment->segment) &&
                server_segment->is_path_param == segment->is_path_param) {
            break;
        }

    }

    if (server_segment) {

        if (segment->child_segments) {

            if (server_segment->child_segments) {
                patterns_tree_add(&server_segment->child_segments, segment->child_segments);
            } else {
                server_segment->child_segments = segment->child_segments;
            }

        } else {

            if (server_segment->handler) {
                panic_with_msg("http_server_handle failed due tu duplicated paths");
            } else {
                server_segment->handler = segment->handler;
            }

        }
        
    } else {

        if (*tree == NULL) {
            *tree = segment;
        } else {
            (*tree)->last_segment->next_segment = segment;
            (*tree)->last_segment = segment;
        }
    
    }
}

static Http_Handler *find_handler_while_adding_path_params(Segment_Pattern **request_patterns,
                                                           Segment_Pattern *server_patterns) {

    Http_Handler *handler = NULL;
    Segment_Pattern *request_pattern = *request_patterns;

    for (Segment_Pattern *server_pattern = server_patterns->first_segment;
         server_pattern != NULL;
         server_pattern = server_pattern->next_segment) {

        if (!server_pattern->is_path_param && 
                string_eq(server_pattern->segment, request_pattern->segment)) {

            if (!request_pattern->next_segment) {

                if (server_pattern->handler) {
                    handler = server_pattern->handler;
                    request_pattern->is_path_param = false;
                }

            } else if (server_pattern->child_segments) {

                handler = find_handler_while_adding_path_params(&request_pattern->next_segment,
                                                                server_pattern->child_segments);
                if (handler) {
                    request_pattern->is_path_param = false;
                }
                
            }

            break;

        } else if (server_pattern->is_path_param) {

            if (!request_pattern->next_segment) {

                if (server_pattern->handler) {
                    handler = server_pattern->handler;
                    request_pattern->path_param_name = server_pattern->segment;
                    request_pattern->is_path_param = true;
                }

            } else if (server_pattern->child_segments) {

                if (!handler) {

                    handler = find_handler_while_adding_path_params(&request_pattern->next_segment,
                                                                    server_pattern->child_segments);
                    if (handler) {
                        request_pattern->path_param_name = server_pattern->segment;
                        request_pattern->is_path_param = true;
                    }

                }
            }
        }
    }

    return handler;
}

static i32 connection_write(Connection *connection, Response response) {
    Allocator *allocator = connection->allocator;

    String content_lenght_value = string_from_i64(allocator, response.body.size);
    String connection_value;

    if (connection->keep_alive) {
        connection_value = string_lit("keep-alive");
    } else {
        connection_value = string_lit("close");
    }

    headers_put(&response.headers, string_lit("Content-Length"), content_lenght_value);
    headers_put(&response.headers, string_lit("Connection"), connection_value);

    String encoded_response = encode_response(allocator, response);

    i32 bytes_sent = write(connection->fd, encoded_response.data, encoded_response.size);

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
    String status = string_from_i64(allocator, response.status);
    String status_description = http_status_reason(response.status);
    String body = string_with_len((char *)response.body.data, response.body.size);

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

    if (response.body.size > 0) {
        sbuilder_append(&builder, body);
    }

    return sbuilder_to_string(&builder);
}

static i32 events_add_fd(i32 events_fd, i32 fd) {
#if OS_MAC
    struct kevent changelist[1];
    EV_SET(changelist, fd, EVFILT_READ, EV_ADD|EV_ENABLE, 0, 0, 0);
    return kevent(events_fd, changelist, 1, NULL, 0, NULL);
#else
    struct epoll_event event;
    event.events = EPOLLIN;
    event.data.fd = fd;
    return epoll_ctl(events_fd, EPOLL_CTL_ADD, fd, &event);
#endif
}

static i32 events_remove_fd(i32 events_fd, i32 fd) {
#if !OS_MAC
    i32 res = epoll_ctl(events_fd, EPOLL_CTL_DEL, fd, NULL);
    if (res == -1) {
        return -1;
    }
#endif
    return close(fd);
}

static void parser_init(Parser *parser, Allocator * allocator) {
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
    } else {
        parser->last_buffer->next = new_buffer;
    }
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

                if (string_eq(method, string_lit("GET"))  ||
                    string_eq(method, string_lit("PUT"))  ||
                    string_eq(method, string_lit("POST")) ||
                    string_eq(method, string_lit("DELETE"))) {

                    request->method = method;
                    request_add_segment_literal(request, parser->allocator, method);
                    parser->state = PARSER_STATE_PARSING_SPACE_BEFORE_URI;

                } else {

                    parser->state = PARSER_STATE_FAILED;
                    
                }

                break;

            case PARSER_STATE_PARSING_SPACE_BEFORE_URI:

                parser_mark(parser, parser->at);
                parser->state = PARSER_STATE_PARSING_URI;

                break;

            case PARSER_STATE_PARSING_URI:

                if (is_alphanum(c) || c == '/' || c == '.' || c == '=' 
                                   || c == '?' || c == '_' || c == '-'
                                   || c == '&') {

                    break;
                }

                if (c != ' ') {
                    parser->state = PARSER_STATE_FAILED;
                    break;
                }

                String uri = parser_extract_block(parser, parser->at - 1);
                request->uri = uri;

                request_add_uri_segments(request, parser->allocator, uri);

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

                String *content_length = http_headers_get(&request->headers_map, string_lit("content-length"));

                if (content_length != NULL) {

                    i64 body_size = string_to_i64(*content_length);

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

                    request->body.size = body.size;
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

                request->body.size = body.size;
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

static void request_add_uri_segments(Request *request, Allocator *allocator, String uri) {

    u32 i = 1;

    { // Path segments
    
        // Si el path es unicamente: '/'
        if (uri.data[0] == '/' && (uri.size == 1 || (uri.size > 1 && uri.data[1] == '?'))) {

            request_add_segment_literal(request, allocator, string_lit(""));

        } else {

            u32 start = 1;

            while (i < uri.size && uri.data[i] != '?') {

                if (uri.data[i] == '/') {
                    String segment = string_with_len(uri.data + start, i - start);
                    request_add_segment_literal(request, allocator, segment);
                    start = i + 1;
                }

                i++;
            }

            if (uri.size > 1) {

                String segment;

                // Si el ultimo caracter leido fue un '/'
                if (uri.data[i - 1] == '/') {
                    segment = string_lit("");
                } else {
                    // Remanente
                    segment = string_with_len(uri.data + start, i - start);
                }

                request_add_segment_literal(request, allocator, segment);
            }
        }
    }
    
    { // Query params
      
        bool has_query_params = uri.data[i] == '?';
        if (has_query_params) {

            u32 key_pos = i + 1;
            u32 value_pos = i + 1;

            String query_key;

            while (i < uri.size) {

                char c = uri.data[i];

                if (c == '=') {

                    query_key = string_with_len(uri.data + key_pos, i - key_pos);

                    value_pos = i + 1;

                } else if (c == '&') {

                    String query_value = string_with_len(uri.data + value_pos, i - value_pos);
                    request_add_query_param(request, allocator, query_key, query_value);

                    key_pos = i + 1;
                }

                i++;
            }

            // Remanente
            String query_value = string_with_len(uri.data + value_pos, i - value_pos);
            request_add_query_param(request, allocator, query_key, query_value);
        }
    }
}

static void request_add_query_param(Request *request, Allocator *allocator, String key, String value) {
    Query_Param *query_param = allocator_alloc(allocator, sizeof(Query_Param));
    *query_param = (Query_Param){0};
    query_param->next = NULL;
    query_param->key = key;
    query_param->value = value;

    if (request->first_query_param == NULL && request->last_query_param == NULL) {
        request->first_query_param = query_param;
    } else {
        request->last_query_param->next = query_param;
    }
    request->last_query_param = query_param;
}

static void request_add_segment_literal(Request *request, Allocator *allocator, String literal) {
    Segment_Pattern *segment = allocator_alloc(allocator, sizeof(Segment_Pattern));
    *segment = (Segment_Pattern){0};
    segment->segment = literal;
    segment->is_path_param = false;

    if (request->first_segment == NULL && request->last_segment == NULL) {
        request->first_segment = segment;
    } else {
        request->last_segment->next_segment = segment;
    }
    request->last_segment = segment;
}

static String http_status_reason(u16 status) {
    switch (status) {
        case 200: return string_lit("Ok");
        case 201: return string_lit("Created");
        case 400: return string_lit("Bad Request");
        default: return string_lit("Unknown");
    }
}

static void request_init(Request *request) {
    *request = (Request){0};
    headers_init(&request->headers_map);
}

static void response_init(Response *response) {
    *response = (Response){0};
    response->status = 200;
    headers_init(&response->headers);
}

void http_response_set_status(Response *response, u32 status) {
    response->status = 200;
}

void http_response_add_header(Response *response, String key, String value) {
    headers_put(&response->headers, key, value);
}

void http_response_write(Response *response, u8 *content, size_t size) {
    response->body.data = content;
    response->body.size = size;
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

String *http_headers_get(Headers_Map *headers_map, String name) {
    u32 headers_cap = headers_map->capacity;
    u32 header_index = hash_string(name) % headers_cap; 

    Header *headers = headers_map->headers;
    Header *header = headers + header_index;

    if (string_eq(header->field_name, name)) {
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
        if (string_eq(header->field_name, name)) {
            return &header->field_value;
        }
    }

    return NULL;
}

