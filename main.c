#include "gg_stdlib.h"

#include "http.h"
#include "json.h"

#include "http.c"
#include "json.c"

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

static void handler(Request *request, Response *response) {
    Allocator_Temp scratch = get_scratch(0, 0);
    // Allocator *allocator = scratch.allocator;


    String path_param = http_request_get_path_param(request, string_lit("bar"));
    String query_param = http_request_get_query_param(request, string_lit("foo"));

    printf("path_param: %.*s\n", string_print(path_param));
    printf("query_param: %.*s\n", string_print(query_param));

    String body = string("{ \"DIEZ\": \"DIEZ\" }\n");

    http_response_add_header(response, string_lit("content-type"), string_lit("application/json"));
    http_response_set_status(response, 200);
    http_response_write(response, (u8 *)body.data, body.size);

    release_scratch(scratch);
}

char* read_file_to_string(const char* filename) {
    FILE* file = fopen(filename, "r");  // Use "rb" for binary mode if needed
    if (!file) {
        perror("Failed to open file");
        return NULL;
    }

    // Get file size
    if (fseek(file, 0, SEEK_END) != 0) {
        perror("Failed to seek file");
        fclose(file);
        return NULL;
    }
    long file_size = ftell(file);
    if (file_size == -1) {
        perror("Failed to get file size");
        fclose(file);
        return NULL;
    }
    if (fseek(file, 0, SEEK_SET) != 0) {
        perror("Failed to rewind file");
        fclose(file);
        return NULL;
    }

    // Allocate buffer (+1 for null terminator)
    char* buffer = malloc((size_t)file_size + 1);
    if (!buffer) {
        perror("Failed to allocate memory");
        fclose(file);
        return NULL;
    }

    // Read the file
    size_t bytes_read = fread(buffer, 1, (size_t)file_size, file);
    if (bytes_read != (size_t)file_size) {
        perror("Failed to read file");
        free(buffer);
        fclose(file);
        return NULL;
    }

    // Null-terminate
    buffer[file_size] = '\0';

    fclose(file);
    return buffer;
}

int main(int argc, char *argv[], char *env[]) {
    set_process_name(argc, argv, env, "HTTP_SERVER_GG");

    char *content = read_file_to_string("test.json");

    JSON_Element element;
    String json_str = string(content);
    json_parse(json_str, &element);

    Allocator *allocator = allocator_make(8 * MB);

    Server *server = http_server_make(allocator);

    http_server_handle(server, "GET /foo/{bar}/baz", &handler);

    return http_server_start(server, 8080, "127.0.0.1");
}

