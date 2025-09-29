#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <signal.h>
#include <sys/socket.h>
#include <unistd.h>

#if defined(__linux__) || defined(__gnu_linux__)
    #define OS_LINUX 1
#elif defined(__APPLE__) && defined(__MACH__)
    #define OS_MAC 1
#else
    #error Systema Operativo no soportado.
#endif

#if !defined(OS_LINUX)
    #define OS_LINUX 0
#endif
#if !defined(OS_MAC)
    #define OS_MAC 0
#endif

#ifdef __APPLE__
#include <sys/types.h>
#include <sys/event.h>
#else
#include <sys/epoll.h>
#endif

#define HTTP_VERSION_10 string_lit("HTTP/1.0")
#define HTTP_VERSION_11 string_lit("HTTP/1.1")

#define MAX_CONNECTIONS 512
#define MAX_EVENTS 100
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
typedef struct Pattern_Parser Pattern_Parser;
typedef struct Segment_Pattern Segment_Pattern;
typedef struct Query_Param Query_Param;

typedef enum Method Method;
typedef enum Connection_State Connection_State;
typedef enum Parse_Error Parse_Error;
typedef enum Parser_State Parser_State;
typedef enum Pattern_Parser_State Pattern_Parser_State;

typedef void (Http_Handler)(Request *req, Response *res);

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

    PARSER_STATE_PARSING_BODY_BEGIN,
    PARSER_STATE_PARSING_BODY,

    PARSER_STATE_FINISHED,
    PARSER_STATE_FAILED
};

enum Pattern_Parser_State {
    PATTERN_PARSER_STATE_STARTED,
    PATTERN_PARSER_STATE_PARSING_SLASH,
    PATTERN_PARSER_STATE_PARSING_PATH_PARAM,
    PATTERN_PARSER_STATE_PARSING_PATH_SEGMENT,
    PATTERN_PARSER_STATE_FAILED,
    PATTERN_PARSER_STATE_FINISHED
};

struct Segment_Pattern {
    Http_Handler *handler;

    Segment_Pattern *next_segment;
    Segment_Pattern *first_segment;
    Segment_Pattern *last_segment;
    Segment_Pattern *child_segments;

    String segment;
    String path_param_name;

    bool is_path_param;
};

struct Pattern_Parser {
    Pattern_Parser_State state;

    Segment_Pattern *first_segment;
    Segment_Pattern *last_segment;
};

struct Parser_Buffer {
    Parser_Buffer *next;
    u8 *data;
    u32 size;
};

struct Parser {
    Allocator *allocator;

    Parser_State state;

    i32 bytes_read;
    Parser_Buffer *first_buffer;
    Parser_Buffer *last_buffer;
    Parser_Buffer *current_buffer;
    u32 at;

    Parser_Buffer *marked_buffer;
    u32 marked_at;
    u32 marked_distance;

    String header_name;
    u32 body_size;
    u32 body_parsed;
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

struct Query_Param {
    Query_Param *next;
    String key;
    String value;
};

struct Request {
    String method;

    String uri;
    Segment_Pattern *first_segment;
    Segment_Pattern *last_segment;
    Query_Param *first_query_param;
    Query_Param *last_query_param;

    String version;
    Headers_Map headers_map;
    Body body;
};

struct Response {
    u16 status;
    Headers_Map headers;
    Body body;
};

enum Connection_State {
    CONNECTION_STATE_ACTIVE,
    CONNECTION_STATE_FAILED
};

struct Connection {
    Allocator *allocator;

    Connection_State state;

    i32 fd;
    String host;
    u16 port;
    struct sockaddr_in address;

    bool is_active;
    bool keep_alive;

    Request request;

    Parser parser;
};

struct Server {
    Allocator *allocator;

    i32 fd;

    i32 events_fd;

    u32 connections_count;
    Connection *connections;

    Segment_Pattern *patterns_tree;
};

Server *http_server_make(Allocator *allocator);
void http_server_handle(Server *server, char *pattern, Http_Handler *handler);
i32 http_server_start(Server *server, u32 port, char *host);
String http_get_path_param(Request *request, String name);
String http_get_query_param(Request *request, String name);

