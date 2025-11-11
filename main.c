#include "gg_stdlib.h"

#include "http.h"
#include "json.h"

#include "http.c"
#include "json.c"

static void send_json(Arena *arena, Response *response, u32 status, JSON_Element *json) {
    String json_str = json_to_string(arena, json);
    http_response_add_header(response, string_lit("content-type"), string_lit("application/json"));
    http_response_set_status(response, status);
    http_response_write(response, (u8 *)json_str.data, json_str.size);
}

// TODO: Hacer una version propia en mi STDLIB
static char* read_file_to_string(const char* filename) {
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

static void handle_strange_configs(Request *request, Response *response) {
    Arena_Temp scratch = get_scratch(0, 0);
    Arena *arena = scratch.arena;

    char *content = read_file_to_string("foo.json");
    String json_str = string(content);

    JSON_Element result = {0};
    json_parse(arena, (u8 *)json_str.data, json_str.size, &result);

    send_json(arena, response, 200, &result);
    release_scratch(scratch);
}

int main(int argc, char *argv[], char *env[]) {
    Arena *arena = arena_make(1 * MB);

    Server *server = http_server_make(arena);

    http_server_handle(server, "GET /", &handle_strange_configs);

    return http_server_start(server, 8888, "127.0.0.1");
}
