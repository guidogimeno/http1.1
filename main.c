#include "main.h"

#include "strings.h"

#include "strings.c"

// static void append_token(Allocator *allocator, Token_Array *arr, Token token) {
//     // si la capacidad llega al maximo se realoca y se utiliza el doble de capacidad
//     if (arr->size >= arr->capacity) {
//         printf("old size=%d cap=%d\n", arr->size, arr->capacity);
//         u32 capacity_bytes = sizeof(Token) * arr->capacity;
//         Token *new_memory = (Token *)alloc(allocator, capacity_bytes * 2);
//         arr->tokens = memcpy(new_memory, arr->tokens, capacity_bytes);
//         arr->capacity *= 2;
//         printf("new size=%d cap=%d\n", arr->size, arr->capacity);
//     }
//
//     arr->tokens[arr->size] = token;
//     arr->size++;
// }
//

static void init_lexer(Lexer *lexer, char *buf, u32 capacity) {
    lexer->buf = buf;
    lexer->capacity = capacity; // Note: It is RECOMMENDED that all HTTP senders and recipients support, at a minimum, request-line lengths of 8000 octets.
    lexer->length = 0; 
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
    if (lexer->read_position >= lexer->length) {
        lexer->current_char = 0;
    } else {
        lexer->current_char = lexer->buf[lexer->read_position];
    }
    lexer->buf_position = lexer->read_position;
    lexer->read_position++;
}

static void parse_request_line(Allocator *allocator, Lexer *lexer) {
    do {
        read_char(lexer);
    } while (is_letter(lexer->current_char));

    Method method;
    String uri;
    String version = string("HTTP/1.1");

    // Method
    String method_str = substring(allocator, lexer->buf, 0, lexer->buf_position - 1);
    if (string_eq_cstr(&method_str, "GET")) {
        method = GET;
    } else if (string_eq_cstr(&method_str, "PUT")) {
        method = PUT;
    } else if (string_eq_cstr(&method_str, "POST")) {
        method = POST;
    } else if (string_eq_cstr(&method_str, "DELETE")) {
        method = DELETE;
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

    uri = substring(allocator, lexer->buf, uri_start, lexer->buf_position - 1);

    // Space
    if (lexer->current_char != ' ') {
        // TODO: responder un mensaje de error
        assert(false && "tiene que haber un espacio entre la uri y la version");
    }

    for (u32 i = 0; i < 8; i++) { // len(HTTP/1.1)
        read_char(lexer);
        if (version.data[i] != lexer->current_char) {
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

    printf("Resultado: Method=%d URI=%s Version:%s\n", method, uri.data, version.data);
}

int main(int argc, char *argv[]) {
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
    u16 port = server_addr.sin_port;
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
        u16 port = client_addr.sin_port;
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
            lexer.length = bytes_read - 1;

            parse_request_line(&allocator, &lexer);
        }

        if (close(client_fd) == -1) {
            perror("error al cerrar el client_fd");
            return -1;
        }
    }
   
    close(sockfd);

    return 0;
}

