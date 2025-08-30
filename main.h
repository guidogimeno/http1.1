#include <arpa/inet.h>
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
#define MAX_EPOLL_EVENTS 32
#define MAX_HEADERS_CAPACITY 32

typedef struct Server Server;
typedef struct Connection Connection;
typedef struct Request Request;
typedef struct Response Response;
typedef struct Header Header;
typedef struct Headers_Map Headers_Map;
typedef struct Body Body;
typedef struct Lexer Lexer;

typedef enum Method Method;
typedef enum Parse_Error Parse_Error;

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

    bool is_active;

    Request request;
};

struct Server {
    i32 file_descriptor;

    u32 connections_count;
    Connection *connections;
};

struct Lexer {
    i32 fd;

    u8 *buf;
    u32 capacity;
    u32 size;

    u32 buf_position;
    u32 read_position;
    char current_char;
};

enum Parse_Error {
    PARSE_ERROR_NO_ERROR,

    // request line errors
    PARSE_ERROR_MALFORMED_REQUEST_LINE,
    PARSE_ERROR_INVALID_METHOD,
    PARSE_ERROR_INVALID_URI,
    PARSE_ERROR_INVALID_VERSION,

    // headers errors
    PARSE_ERROR_MALFORMED_HEADER,

    // body errors
    PARSE_ERROR_BODY_TOO_LARGE,

    PARSE_ERROR_COUNT
};

static const char *parse_error_messages[PARSE_ERROR_COUNT + 1] = {
    "",

    "parse error malformed request line",
    "parse error invalid method",
    "parse error invalid uri",
    "parse error invalid version",

    "parse error malformed header",

    "parse error body too large",

    "count"
};
