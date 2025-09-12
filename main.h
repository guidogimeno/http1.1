#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <signal.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <unistd.h>

#include "gg_stdlib.h"

#define HTTP_VERSION_10 string_lit("HTTP/1.0")
#define HTTP_VERSION_11 string_lit("HTTP/1.1")

#define MAX_CONNECTIONS 1024
#define MAX_EPOLL_EVENTS 256
#define MAX_PARSER_BUFFER_CAPACITY 4
#define MAX_HEADERS_CAPACITY 32

typedef struct Server Server;
typedef struct Connection Connection;
typedef struct Request Request;
typedef struct Response Response;
typedef struct Header Header;
typedef struct Headers_Map Headers_Map;
typedef struct Body Body;
typedef struct Parser_Buffer Parser_Buffer;
typedef struct Parser Parser;

typedef enum Method Method;
typedef enum Parse_Error Parse_Error;
typedef enum Parser_State Parser_State;

enum Parser_State {
    PARSER_STATE_STARTED,

    PARSER_STATE_PARSING_METHOD,
    PARSER_STATE_PARSING_SPACE_BEFORE_URI,
    PARSER_STATE_PARSING_URI,
    PARSER_STATE_PARSING_SPACE_BEFORE_VERSION,
    PARSER_STATE_PARSING_VERSION,
    PARSER_STATE_PARSING_END_OF_REQUEST_LINE,

    PARSER_STATE_PARSING_HEADER_KEY_BEGIN,
    PARSER_STATE_PARSING_HEADER_KEY,
    PARSER_STATE_PARSING_HEADER_SPACE,
    PARSER_STATE_PARSING_HEADER_VALUE_BEGIN,
    PARSER_STATE_PARSING_HEADER_VALUE,
    PARSER_STATE_PARSING_HEADER_VALUE_END,
    PARSER_STATE_PARSING_HEADERS_END,

    PARSER_STATE_PARSING_BODY,
    PARSER_STATE_FINISHED,
    PARSER_STATE_FAILED
};

struct Parser_Buffer {
    Parser_Buffer *next;
    u8 *data;
    u32 size;
};

struct Parser {
    Allocator *allocator;

    i32 file_descriptor;

    u32 bytes_read;
    Parser_Buffer *first_buffer;
    Parser_Buffer *last_buffer;
    Parser_Buffer *current_buffer;
    u32 at;

    Parser_Buffer *marked_buffer;
    u32 marked_at;
    u32 marked_distance;

    String header_name;

    Parser_State state;
};

struct Header {
    String field_name;
    String field_value;
    bool occupied;
};

struct Headers_Map {
    Header headers[MAX_HEADERS_CAPACITY];
    u32 length;
    u32 capacity;
};

enum Method {
    METHOD_GET,
    METHOD_PUT,
    METHOD_POST,
    METHOD_DELETE
};

struct Body {
    u8 *data;
    u32 length;
};

struct Request {
    Method method;
    String uri;
    String version;
    Headers_Map headers_map;
    Body body;
};

struct Response {
    u16 status;
    Headers_Map headers;
    Body body;
};

struct Connection {
    Allocator *allocator;

    i32 file_descriptor;
    String host;
    u16 port;
    struct sockaddr_in address;

    bool is_active;

    Request request;

    Parser parser;
};

struct Server {
    i32 file_descriptor;

    i32 epoll_file_descriptor;

    u32 connections_count;
    Connection *connections;
};

