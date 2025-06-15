#include <assert.h>
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <sys/socket.h>
#include <unistd.h>
#include <netinet/in.h>
#include <arpa/inet.h>

typedef struct Server {
    uint32_t sockfd;
    uint32_t clients[1];
} Server;

int main(int argc, char *argv[]) {
    printf("iniciando servidor..\n");

    // creacion del socket
    uint32_t sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd == -1) {
        perror("error al crear socket");
        return -1;
    }

    // address reutilizable, no hace falta esperar al TIME_WAIT
    uint32_t reuse = 1;
    uint32_t res = setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, (const char *)&reuse, sizeof(reuse));
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
    uint16_t port = server_addr.sin_port;
    printf("servidor escuchando en: %s:%d\n", host, port);

    // aceptar conexiones
    while (true) {
        struct sockaddr_in client_addr;
        socklen_t client_addr_len = sizeof(client_addr);

        int client_fd = accept(sockfd, (struct sockaddr *)&client_addr, &client_addr_len); // TODO: despues probar de hacer nonblocking
        if (client_fd == -1) {
            perror("error al aceptar cliente");
            return -1;
        }
        server.clients[0] = client_fd;

        char *host = inet_ntoa(client_addr.sin_addr);
        uint16_t port = client_addr.sin_port;
        printf("nuevo cliente aceptado: %s:%d\n", host, port);

        uint8_t buf_size = 10;
        char buf[buf_size];
        while (true) {
            int16_t b = read(client_fd, buf, buf_size);
            if (b == 0) {
                printf("el cliente cerro la conexion:%s\n", host);
                break;
            } 
            if (b == -1) {
                perror("error al leer del cliente\n");
                break;
            }

            buf[b] = '\0';
            printf("recibido: %s\n", buf);
        }

        if (close(client_fd) == -1) {
            perror("error al cerrar el client_fd");
            return -1;
        }
    }
   
    close(sockfd);

    return 0;
}

