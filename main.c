#include "main.h"

#include "strings.h"

#include "strings.c"

typedef struct Server {
    u32 sockfd;
    u32 clients[1];
} Server;

typedef struct Header {
    String field_name;
    String field_value;
    bool occupied;
} Header;

#define MAX_HEADERS_CAPACITY 32 // osea que solo acepta hastas 12 headers
typedef struct Headers_Map {
    Header headers[MAX_HEADERS_CAPACITY];
    u32 length;
    u32 capacity;
} Headers_Map;

static void init_headers_map(Headers_Map *headers_map) {
    headers_map->length = 0;
    headers_map->capacity = MAX_HEADERS_CAPACITY;
    memset(headers_map->headers, 0, headers_map->capacity);
}

// djb2
static u32 header_hash(String s) {
    u32 hash = 5381; // numero primo
    for (u32 i = 0; i < s.size; i++) {
        // (hash x 33) + ch = ((hash x 32) + hash) + ch
        hash = ((hash << 5) + hash) + s.data[i];
    }
    return hash;
}

static void headers_put(Headers_Map *headers_map, String field_name, String field_value) {
    u32 headers_cap = headers_map->capacity;

    // TODO: Esto deberia crecer
    assert(headers_map->length < (0.75 * headers_cap));

    u32 header_index = header_hash(field_name) % headers_cap; 
    Header *headers = headers_map->headers;
    Header *header = headers + header_index;

    bool is_occupied = header->occupied;
    bool different_field_name = !string_eq(&header->field_name, &field_name); 

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
    u32 header_index = header_hash(field_name) % headers_cap; 

    Header *headers = headers_map->headers;
    Header *header = headers + header_index;

    if (string_eq(&header->field_name, &field_name)) {
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
        if (string_eq(&header->field_name, &field_name)) {
            return &header->field_value;
        }
    }

    return NULL;
}

typedef enum Method {
    METHOD_GET,
    METHOD_PUT,
    METHOD_POST,
    METHOD_DELETE
} Method;

typedef struct Body {
    u8 *data;
    u32 length;
} Body;

typedef struct Request {
    Method method;
    String uri;
    String version;
    Headers_Map headers_map;
    Body body;
} Request;

typedef struct Response {
    u16 status;
    Headers_Map headers;
    Body body;
} Response;

static String http_status_reason(u16 status) {
    switch (status) {
        case 200: return string("Ok");
        case 201: return string("Created");
        case 400: return string("Bad Request");
        default: return string("Unknown");
    }
}

static String encode_response(Allocator *allocator, Response response) {
    String line_separator = string("\r\n");
    String colon_separator = string(": ");
    String space_separator = string(" ");

    String version = string("HTTP/1.1 ");
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
    headers_put(&response->headers, string("content-length"), string_from_int(allocator, length));

    response->body.data = content;
    response->body.length = length;
}

typedef struct Lexer {
    s32 fd;

    u8 *buf;
    u32 capacity;
    u32 size;

    u32 buf_position;
    u32 read_position;
    char current_char;
} Lexer;

static void init_lexer(Lexer *lexer, u8 *buf, u32 capacity) {
    lexer->buf = buf;
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

typedef enum Parse_Error {
    PARSE_ERROR_NO_ERROR,

    // request line errors
    PARSE_ERROR_MALFORMED_REQUEST_LINE,
    PARSE_ERROR_INVALID_METHOD,
    PARSE_ERROR_INVALID_URI,
    PARSE_ERROR_INVALID_VERSION,

    // headers errors
    PARSE_ERROR_MALFORMED_HEADER,

    // body errors
    PARSE_ERROR_BODY_TOO_LARGE,

    PARSE_ERROR_COUNT
} Parse_Error;

static const char *parse_error_messages[PARSE_ERROR_COUNT + 1] = {
    "",

    "parse error malformed request line",
    "parse error invalid method",
    "parse error invalid uri",
    "parse error invalid version",

    "parse error malformed header",

    "parse error body too large",

    "count"
};

static Parse_Error parse_request_line(Allocator *allocator, Lexer *lexer, Request *request) {
    do {
        read_char(lexer);
    } while (is_letter(lexer->current_char));

    request->version = string("HTTP/1.1");

    // Method
    String method_str = string_sub_cstr(allocator, (char *)lexer->buf, 0, lexer->buf_position - 1);
    if (string_eq_cstr(&method_str, "GET")) {
        request->method = METHOD_GET;
    } else if (string_eq_cstr(&method_str, "PUT")) {
        request->method = METHOD_PUT;
    } else if (string_eq_cstr(&method_str, "POST")) {
        request->method = METHOD_POST;
    } else if (string_eq_cstr(&method_str, "DELETE")) {
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
    } while(is_alphanum(lexer->current_char) || lexer->current_char == '/');

    if (lexer->buf_position == uri_start) {
        return PARSE_ERROR_INVALID_URI;
    }

    request->uri = string_sub_cstr(allocator, (char *)lexer->buf, uri_start, lexer->buf_position - 1);

    // Space
    if (lexer->current_char != ' ') {
        return PARSE_ERROR_MALFORMED_REQUEST_LINE;
    }

    for (u32 i = 0; i < 8; i++) { // len(HTTP/1.1)
        read_char(lexer);
        if (request->version.data[i] != lexer->current_char) {
            return PARSE_ERROR_INVALID_VERSION;
        }
    }

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
    String *content_length = headers_get(&request->headers_map, string("content-length"));

    if (content_length != NULL) {
        s64 len = string_to_int(*content_length);
        if (len > 4 * KB) { 
            return PARSE_ERROR_BODY_TOO_LARGE;
        }

        request->body.length = len;
        request->body.data = &lexer->buf[lexer->read_position];
    }

    return PARSE_ERROR_NO_ERROR;
}

static void connection_write(Allocator *allocator, s32 fd, Response response) {
    String encoded_response = encode_response(allocator, response);

    u32 bytes_written = write(fd, encoded_response.data, encoded_response.size);
    if (bytes_written != encoded_response.size) {
        perror("error al escribir al cliente");
    }
}

static void handle_parse_error(Allocator *allocator, s32 fd, Parse_Error err) {
    Response response = {0};
    response.status = 400;
    init_headers_map(&response.headers);

    const char *error_message = parse_error_messages[err];
    response_write(allocator, &response, (u8 *)error_message, string_size(error_message));

    connection_write(allocator, fd, response);
}

static void http_handler(Allocator *allocator, Request req, Response *res) {
    String body = string("{ \"foo\": \"bar\" }");

    res->status = 200;
    response_write(allocator, res, (u8 *)body.data, body.size);
}

typedef struct Connection {
    s32 fd;
    String host;
    u16 port;
} Connection;

typedef struct ThreadArgs {
    Connection connection;
} ThreadArgs;

// TODO:
// - Lectura Larga -> Keep Alive
// - Lectura Corta
void *handle_connection(void *arg) {
    AllocatorTemp scratch = get_scratch(0, 0);
    Allocator *allocator = scratch.allocator;

    ThreadArgs *thread_args = (ThreadArgs *)arg;
    Connection connection = thread_args->connection;
    s32 client_fd = connection.fd;

    u8 buf[8 * KB];
    Lexer lexer = {0};
    init_lexer(&lexer, buf, 8 * KB);

    while (true) { // TODO: Deberia ser un while(1) si fuera un keep alive? o siempre?
        s32 bytes_read = read(client_fd, lexer.buf, lexer.capacity);
        if (bytes_read == 0) {
            printf("el cliente cerro la conexion:%.*s\n", string_print(connection.host));
            break;
        } else if (bytes_read == -1) {
            perror("error al leer del cliente\n");
            break;
        }
        lexer.size = bytes_read - 1;

        Request request = {0};
        init_headers_map(&request.headers_map);

        Parse_Error err = parse_request_line(allocator, &lexer, &request);
        if (err) {
            handle_parse_error(allocator, client_fd, err);
            break;
        }

        err = parse_headers(allocator, &lexer, &request);
        if (err) {
            handle_parse_error(allocator, client_fd, err);
            break;
        }

        err = parse_body(allocator, &lexer, &request);
        if (err) {
            handle_parse_error(allocator, client_fd, err);
            break;
        }

        Response response = {0};
        init_headers_map(&response.headers);

        http_handler(allocator, request, &response);

        connection_write(allocator, client_fd, response);
    }

    release_scratch(scratch);

    if (close(client_fd) == -1) {
        perror("error al cerrar el client_fd");
        // TODO: ver como manejar este error
        return NULL;
    }

    return NULL;
}

int main(int argc, char *argv[]) {
    printf("Iniciando servidor..\n");

    // creacion del socket
    u32 sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd == -1) {
        perror("error al crear socket");
        return -1;
    }
    
    // address reutilizable, no hace falta esperar al TIME_WAIT
    u32 reuse = 1;
    u32 res = setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, (const char *)&reuse, sizeof(reuse));
    if (res == -1) {
        perror("error al realizar setsockopt(SO_REUSEADDR)");
        return -1;
    }
    
    Server server = {
        .sockfd = sockfd
    };
    
    // bind address al socket
    struct sockaddr_in server_addr = {
        .sin_family = AF_INET,
        .sin_port = htons(8080),
        .sin_addr.s_addr = inet_addr("127.0.0.1"),
    };
    
    res = bind(sockfd, (struct sockaddr *)&server_addr, sizeof(server_addr));
    if (res == -1) {
        perror("error al realizar el bind");
        return -1;
    }
    
    // escuchar a traves del socket
    res = listen(sockfd, 1); // tamanio maximo de la cola de conexiones pendientes
    if (res == -1) {
        perror("error al realizar el listen");
        return -1;
    }
    
    String server_host = string(inet_ntoa(server_addr.sin_addr));
    u16 server_port = ntohs(server_addr.sin_port);
    printf("Servidor escuchando en: %.*s:%d\n", string_print(server_host), server_port);
    
    // aceptar conexiones
    while (true) {
        struct sockaddr_in client_addr;
        socklen_t client_addr_len = sizeof(client_addr);
    
        s32 client_fd = accept(sockfd, (struct sockaddr *)&client_addr, &client_addr_len); // TODO: despues probar de hacer nonblocking
        if (client_fd == -1) {
            perror("error al aceptar cliente");
            return -1; // TODO: manejar mejor este error
        }
        server.clients[0] = client_fd;

        Connection connection = {0};
        connection.fd = client_fd;
        connection.host = string(inet_ntoa(client_addr.sin_addr));
        connection.port = ntohs(client_addr.sin_port); 

        printf("Nuevo cliente aceptado: %.*s:%d\n", string_print(connection.host), connection.port);

        pthread_t thread;
        ThreadArgs thread_args = {0};
        thread_args.connection = connection;

        pthread_create(&thread, NULL, handle_connection, &thread_args);
        pthread_join(thread, NULL);
    }
      
    close(sockfd);
    
    return 0;
}

