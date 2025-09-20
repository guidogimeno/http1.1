#include "gg_stdlib.h"

#include "http.h"

#include "http.c"

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

static void foo_handler_2(Request *req, Response *res) {
    // TODO: Como manejo los allocadores? Les doy yo uno?
    // Scratches?
    Allocator *allocator = allocator_make(1 * MB);

    String body = string("{ \"tu\": \"vieja\" }");

    response_add_header(res, string_lit("hola"), string_lit("mundo"));
    response_set_status(res, 200);

    response_write(res, allocator, (u8 *)body.data, body.size);
}

static void foo_handler_1(Request *req, Response *res) {
    Allocator *allocator = allocator_make(1 * MB);

    String body = string("{ \"otra\": \"cosa\" }");

    response_add_header(res, string_lit("hola"), string_lit("mundo"));
    response_set_status(res, 200);

    response_write(res, allocator, (u8 *)body.data, body.size);
}

int main(int argc, char *argv[], char *env[]) {
    set_process_name(argc, argv, env, "HTTP_SERVER_GG");

    Allocator *allocator = allocator_make(8 * MB);

    Server *server = http_server_make(allocator);

    http_server_handle(server, "GET /foo", &foo_handler_1);
    http_server_handle(server, "GET /foo/{bar}/baz", &foo_handler_2);

    return http_server_start(server, 8080, "127.0.0.1");
}

