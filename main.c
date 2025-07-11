#include "main.h"

#include "strings.h"

#include "strings.c"

typedef struct Server {
    u32 sockfd;
    u32 clients[1];
} Server;

typedef enum Method {
    METHOD_GET,
    METHOD_PUT,
    METHOD_POST,
    METHOD_DELETE
} Method;

typedef struct Lexer {
    char *buf;
    u32 capacity;
    u32 size;
    u32 buf_position;
    u32 read_position;
    char current_char;
} Lexer;

typedef struct Header {
    String field_name;
    String field_value;
    bool occupied;
} Header;

#define MAX_HEADERS_CAPACITY 16 // osea que solo acepta hastas 12 headers
typedef struct Headers_Map {
    Header headers[MAX_HEADERS_CAPACITY];
    u32 length;
    u32 capacity;
} Headers_Map;

typedef struct Request {
    Method method;
    String uri;
    String version;
    Headers_Map headers_map;
    u8 *body;
} Request;

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

static void init_lexer(Lexer *lexer, char *buf, u32 capacity) {
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

static void parse_request_line(Allocator *allocator, Lexer *lexer, Request *request) {
    do {
        read_char(lexer);
    } while (is_letter(lexer->current_char));

    request->version = string("HTTP/1.1");

    // Method
    String method_str = string_sub_cstr(allocator, lexer->buf, 0, lexer->buf_position - 1);
    if (string_eq_cstr(&method_str, "GET")) {
        request->method = METHOD_GET;
    } else if (string_eq_cstr(&method_str, "PUT")) {
        request->method = METHOD_PUT;
    } else if (string_eq_cstr(&method_str, "POST")) {
        request->method = METHOD_POST;
    } else if (string_eq_cstr(&method_str, "DELETE")) {
        request->method = METHOD_DELETE;
    } else {
        // TODO: responder un mensaje de error
        assert(false && "el Method no existe");
        return;
    }

    // Space
    if (lexer->current_char != ' ') {
        // TODO: responder un mensaje de error
        assert(false && "tiene que haber un espacio entre el method y la uri");
    }

    // URI
    u32 uri_start = lexer->read_position;
    do {
        read_char(lexer);
    } while(is_alphanum(lexer->current_char) || lexer->current_char == '/');

    if (lexer->buf_position == uri_start) {
        // TODO: responder un mensaje de error porque no puede ser vacio
        assert(false && "la uri esta vacia");
    }

    request->uri = string_sub_cstr(allocator, lexer->buf, uri_start, lexer->buf_position - 1);

    // Space
    if (lexer->current_char != ' ') {
        // TODO: responder un mensaje de error
        assert(false && "tiene que haber un espacio entre la uri y la version");
    }

    for (u32 i = 0; i < 8; i++) { // len(HTTP/1.1)
        read_char(lexer);
        if (request->version.data[i] != lexer->current_char) {
            // TODO: responder un mensaje de error
            assert(false && "la version no coincide");
        }
    }

    read_char(lexer);
    if (lexer->current_char != '\r') {
        // TODO: responder un mensaje de error
        assert(false && "el request-line tiene que terminar con \\r");
    }

    read_char(lexer);
    if (lexer->current_char != '\n') {
        // TODO: responder un mensaje de error
        assert(false && "el request-line tiene que terminar con \\n");
    }

    printf(
        "Resultado -> Method=%d URI=%.*s Version:%.*s\n",
        request->method,
        string_print(request->uri),
        string_print(request->version)
    );
}

static void parse_headers(Allocator *allocator, Lexer *lexer, Request *request) {
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
                assert(0 && "headers estan mal formados");
            }
            read_char(lexer);
            if (lexer->current_char != '\n') {
                assert(0 && "headers estan mal formados");
            }
            return;
        }
        
        if (lexer->current_char == ':') {

            // substring to lower case
            u32 field_name_size = (lexer->read_position - 1) - field_name_start;
            String substring = string_with_len(&lexer->buf[field_name_start], field_name_size); 
            String field_name = string_to_lower(allocator, substring);

            // espacio
            read_char(lexer);
            if (lexer->current_char != ' ') {
                assert(0 && "aca deberia haber un espacio");
            }

            // field value
            u32 field_value_start = lexer->read_position;
            u32 field_value_end;

            do {
                read_char(lexer);
            } while (lexer->current_char != '\r');

            field_value_end = lexer->read_position - 1;

            String field_value = string_sub_cstr(allocator, lexer->buf, field_value_start, field_value_end);
            headers_put(&request->headers_map, field_name, field_value);

            // \r\n
            read_char(lexer);
            if (lexer->current_char != '\n') {
                assert(0 && "headers estan mal formados");
            }
        } else {
            assert(0 && "headers estan mal formados");
        }
    } while (lexer->current_char != '\r');

    read_char(lexer);
    if (lexer->current_char != '\n') {
        assert(0 && "headers tienen que terminar con el \\n");
    }
}

static void parse_body(Allocator *allocator, Lexer *lexer, Request *request) {
    String *content_length = headers_get(&request->headers_map, string("content-length"));
    if (content_length != NULL) {
        printf("Este es el valor del header 'content-lenght': %.*s\n", string_print((*content_length)));
        // s64 bytes = string_to_int(*content_length);
    }
}

int main(int argc, char *argv[]) {

    String use_cases[17] = {
        string("   1"),
        string("   1 "),
        string("   -1 "),
        string("   -0001 "),
        string("   -11234 "),
        string("   11204 "),
        string("01"),
        string("00004"),
        string("40000"),
        string("0000"),
        string("00300"),
        string("-00300"),
        string(" -00304 "),
        string("9223372036854775807"),
        string("-9223372036854775808"),
        string("9223372036854775808"),
        string("-9223372036854775809")
    };

    for (u32 i = 0; i < 17; i++) {
        String str = use_cases[i];
        printf("input: %.*s -> ", string_print(str));
        s64 result = string_to_int(str);
        printf("resultado: %ld\n", result);
    }





    printf("iniciando servidor..\n");

    u32 allocator_capacity = 1024 * 1024;
    Allocator allocator = {
        .data = malloc(allocator_capacity),
        .capacity = allocator_capacity,
        .size = 0,
    };
    
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
    
    char *host = inet_ntoa(server_addr.sin_addr);
    u16 port = ntohs(server_addr.sin_port);
    printf("servidor escuchando en: %s:%d\n", host, port);
    
    // aceptar conexiones
    while (true) {
        struct sockaddr_in client_addr;
        socklen_t client_addr_len = sizeof(client_addr);
    
        s32 client_fd = accept(sockfd, (struct sockaddr *)&client_addr, &client_addr_len); // TODO: despues probar de hacer nonblocking
        if (client_fd == -1) {
            perror("error al aceptar cliente");
            return -1;
        }
        server.clients[0] = client_fd;
    
        char *host = inet_ntoa(client_addr.sin_addr);
        u16 port = ntohs(client_addr.sin_port);
        printf("nuevo cliente aceptado: %s:%d\n", host, port);
    
        char buf[1024];
        Lexer lexer = {0};
        init_lexer(&lexer, buf, 1024);
    
        while (true) {
            s32 bytes_read = read(client_fd, lexer.buf, lexer.capacity);
            if (bytes_read == 0) {
                printf("el cliente cerro la conexion:%s\n", host);
                break;
            } else if (bytes_read == -1) {
                perror("error al leer del cliente\n");
                break;
            }
            lexer.size = bytes_read - 1;
    
            Headers_Map headers_map;
            init_headers_map(&headers_map);

            Request request = {0};
            request.headers_map = headers_map;

            parse_request_line(&allocator, &lexer, &request);
            parse_headers(&allocator, &lexer, &request);
            parse_body(&allocator, &lexer, &request);

            printf("cantidad alocada: %d\n", allocator.size);
        }
    
        if (close(client_fd) == -1) {
            perror("error al cerrar el client_fd");
            return -1;
        }
    }
      
    close(sockfd);
    
    return 0;
}

