#include "gg_stdlib.h"

#include "http.h"

#include "http.c"

static void set_process_name(int argc, char *argv[], char *env[], char *name);

int main(int argc, char *argv[], char *env[]) {
    set_process_name(argc, argv, env, "HTTP_SERVER_GG");

    return http_listen_and_serve(8080, "127.0.0.1");
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


