#include "../gg_stdlib.h"
#include <stdio.h>

#define MAX_TEST_BUFFER_CAPACITY 8

typedef struct TestParser TestParser;
typedef struct TestConnection TestConnection;
typedef struct Buffer Buffer;
typedef enum TestParserState TestParserState;

enum TestParserState {
    TEST_PARSER_STATE_STARTED,
    TEST_PARSER_STATE_PARSING,
    TEST_PARSER_STATE_FAILED,
    TEST_PARSER_STATE_FINISHED
};

struct TestParser {
    Allocator *allocator;

    FILE *file;

    u32 bytes_read;
    Buffer *first_buffer;
    Buffer *last_buffer;
    Buffer *current_buffer;
    u32 at;

    Buffer *marked_buffer;
    u32 marked_at;
    u32 marked_distance;

    TestParserState state;
};

struct Buffer {
    Buffer *next;
    u8 *data;
    u32 size;
};

struct TestConnection {
    String header;
    String body;
};

/*
 * Header=abc\n
 * Body=abd\n\n
 *
 */

static String parser_extract_block(TestParser *parser, u32 last_buffer_offset) {
    Buffer *first_buffer = parser->marked_buffer;
    Buffer *last_buffer = parser->current_buffer;

    u8 *first_buffer_offset = first_buffer->data + parser->marked_at;

    if (first_buffer == last_buffer) {
        u32 total_size = last_buffer_offset - parser->marked_at;

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
    u32 surplus_buffer_size = buffer_max_size - last_buffer_offset;

    u32 total_size = first_buffer_remaining_size + middle_buffers_size - surplus_buffer_size;

    void *data = allocator_alloc(parser->allocator, total_size);
    void *next_memcpy = data;

    memcpy(next_memcpy, first_buffer_offset, first_buffer_remaining_size);
    next_memcpy += first_buffer_remaining_size;

    Buffer *buffer;

    for (buffer = first_buffer->next; buffer != last_buffer; buffer = buffer->next) {
        memcpy(next_memcpy, buffer->data, buffer_max_size);
        next_memcpy += buffer_max_size;
    }

    memcpy(next_memcpy, buffer->data, last_buffer_offset);

    String result = {
        .data = data,
        .size = total_size
    };

    return result;
}

static bool parser_keep_going(TestParser *parser) {
    return parser->state == TEST_PARSER_STATE_PARSING;
}

static void parser_read_char(TestParser *parser) {
    if (parser->at + 1 < parser->bytes_read) {
        parser->at++;
        return;
    } 

    void *memory = allocator_alloc(parser->allocator, sizeof(Buffer) + MAX_TEST_BUFFER_CAPACITY);
    Buffer *new_buffer = (Buffer *)memory;
    new_buffer->size = MAX_TEST_BUFFER_CAPACITY;
    new_buffer->data = memory + sizeof(Buffer);
    new_buffer->next = NULL;

    u32 bytes_read = fread(new_buffer->data, 1, new_buffer->size, parser->file);

    if (bytes_read == -1) {
        parser->state = TEST_PARSER_STATE_FAILED;
        return;
    } else if (bytes_read == 0) {
        parser->state = TEST_PARSER_STATE_FINISHED;
        return;
    }

    parser->at = 0;
    parser->current_buffer = new_buffer;
    parser->bytes_read = bytes_read;
    parser->marked_distance++;

    if (parser->first_buffer == NULL && parser->last_buffer == NULL) {
        parser->first_buffer = new_buffer;
        parser->last_buffer = new_buffer;
    }

    parser->last_buffer->next = new_buffer;
    parser->last_buffer = new_buffer;
}

static void parser_mark(TestParser *parser, u32 at) {
    parser->marked_buffer = parser->current_buffer;
    parser->marked_distance = 0;
    parser->marked_at = at;
}

static bool is_letter(char ch) {
    return (ch >= 'a' && ch <= 'z') ||
        (ch >= 'A' && ch <= 'Z');
}

static void parser_parse(TestParser *parser) {
    parser->state = TEST_PARSER_STATE_PARSING;

    parser_read_char(parser);

    parser_mark(parser, parser->at);

    while (is_letter(parser->current_buffer->data[parser->at]) && parser_keep_going(parser)) {
        parser_read_char(parser);
    }

    if (parser->current_buffer->data[parser->at] != ':') {
        parser->state = TEST_PARSER_STATE_FAILED;
        return;
    }

    String header = parser_extract_block(parser, parser->at);

    printf("HEADER: %.*s\n", string_print(header));

    parser->state = TEST_PARSER_STATE_FINISHED;
}

int main() {
    FILE *file = fopen("/home/ggimeno/http1.1/experimentos/request.txt", "r");
    if (file == NULL) {
        printf("Error opening file!\n");
        return 1;
    }

    Allocator *allocator = allocator_make(1 * GB); 

    TestParser parser = {0};
    parser.allocator = allocator;
    parser.file = file;
    parser.bytes_read = 0;
    parser.first_buffer = NULL;
    parser.last_buffer = NULL;
    parser.current_buffer = NULL;
    parser.at = 0;
    parser.marked_buffer = NULL;
    parser.marked_at = 0;
    parser.marked_distance = 0;
    parser.state = TEST_PARSER_STATE_STARTED;

    parser_parse(&parser);

    if (parser.state == TEST_PARSER_STATE_FINISHED) {
        printf("termino con exito\n");
    } else if (parser.state == TEST_PARSER_STATE_FAILED) {
        printf("termino con errores\n");
        fclose(file);
    }

    return EXIT_SUCCESS;
}
