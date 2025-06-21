#include "main.h"

#include "strings.h"

#include "strings.c"

typedef struct Server {
    u32 sockfd;
    u32 clients[1];
} Server;

typedef struct Lexer {
    char *buf;
    u32 size;
    u32 pos;
} Lexer;

typedef enum Token_Type {
    SPACE,
    WORD,
    CRLF
} Token_Type;

typedef struct Token {
    Token_Type type;
    String value;
} Token;

static bool is_letter(char ch) {
    return (ch >= 'a' && ch <= 'z') ||
        (ch >= 'A' && ch <= 'Z');
}

static Token *next_token(Allocator *allocator, Lexer *lexer) {
    while (lexer->pos < lexer->size) {
        char ch = lexer->buf[lexer->pos];

        if (is_letter(ch)) {
            u32 temp_pos = lexer->pos;
            while (is_letter(lexer->buf[++temp_pos]));

            String word = substring(allocator, lexer->buf, lexer->pos, temp_pos - 1);
            Token *token = (Token *)alloc(allocator, sizeof(Token));
            token->type = WORD;
            token->value = word;

            lexer->pos = temp_pos;

            return token;
        } 

        lexer->pos++;
    }

    assert(false);
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

        char buf[10];
        Lexer lexer = {
            .buf = buf,
            .size = 10, // Note: It is RECOMMENDED that all HTTP senders and recipients support, at a minimum, request-line lengths of 8000 octets.
            .pos = 0,
        };

        Token tokens[10];

        while (true) {
            // TODO: Necesito leer linea por linea
            s16 bytes_read = read(client_fd, lexer.buf, lexer.size);
            if (bytes_read == 0) {
                printf("el cliente cerro la conexion:%s\n", host);
                break;
            } 
            if (bytes_read == -1) {
                perror("error al leer del cliente\n");
                break;
            }

            // start line
            while (lexer.pos < lexer.size) {
                Token *token = next_token(&allocator, &lexer);
                printf("token.type=%d token.data=%s\n", token->type, token->value.data);
            }
        }

        if (close(client_fd) == -1) {
            perror("error al cerrar el client_fd");
            return -1;
        }
    }
   
    close(sockfd);

    return 0;
}

