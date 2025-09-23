#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <signal.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <unistd.h>

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
typedef struct Pattern_Parser Pattern_Parser;
typedef struct Pattern_Segment Pattern_Segment;
typedef struct Segment_Literal Segment_Literal;

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

struct Pattern_Segment {
    Pattern_Segment *next_segment;
    Pattern_Segment *first_segment;
    Pattern_Segment *last_segment;
    Pattern_Segment *child_segments;

    String segment;
    bool is_path_param;

    Http_Handler *handler;
};

struct Pattern_Parser {
    Pattern_Parser_State state;

    String method;

    Pattern_Segment *first_segment;
    Pattern_Segment *last_segment;
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

struct Segment_Literal {
    Segment_Literal *next_segment;
    String segment;
};

struct Request {
    String method;
    String uri;
    Segment_Literal *first_segment;
    Segment_Literal *last_segment;
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

    i32 file_descriptor;
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

    i32 file_descriptor;

    i32 epoll_file_descriptor;

    u32 connections_count;
    Connection *connections;

    Pattern_Segment *segments_tree;
};

Server *http_server_make(Allocator *allocator);
void http_server_handle(Server *server, char *pattern, Http_Handler *handler);
i32 http_server_start(Server *server, u32 port, char *host);

static i32 signals_init(void);
static void signal_handler(i32 signal_number);
static i32 start_listening(u32 port, char *host);
static i32 set_nonblocking(i32 file_descriptor);

static void server_accept_client(Server *server);
static Connection *server_find_connection(Server *server, i32 file_descriptor);
static Connection *server_find_free_connection(Server *server);
static bool server_handle_connection(Server *server, Connection *connection);

static void segments_tree_add(Pattern_Segment **tree, Pattern_Segment *segment);
static Http_Handler *find_handler(Pattern_Segment *pattern_segment, Segment_Literal *segment_literal);

static i32 epoll_events_add_file_descriptor(i32 epoll_file_descriptor, i32 fd, u32 epoll_events);
static i32 epoll_events_remove_file_descriptor(i32 epoll_file_descriptor, i32 file_descriptor);

static void connection_init(Connection *connection, i32 file_descriptor, struct sockaddr_in address);
static i32 connection_write(Connection *connection, Response response);

static String encode_response(Allocator *allocator, Response response);

static void parser_init(Parser *parser, Allocator * allocator);
static char parser_get_char(Parser *parser);
static Parser_Buffer *parser_push_buffer(Parser *parser);
static u32 parser_parse_request(Parser *parser, Request *request);

static void pattern_parser_parse(Pattern_Parser *pattern_parser, Allocator *allocator, String pattern_str);
static void pattern_parser_add_segment(Pattern_Parser *parser, Allocator *allocator, String segment, bool is_path_param);

static String http_status_reason(u16 status);

static void request_init(Request *request);
static void request_add_segment_literal(Request *request, Allocator *allocator, String literal);

static void response_init(Response *response);
static void response_set_status(Response *response, u32 status);
static void response_add_header(Response *response, String key, String value);
static void response_write(Response *response, Allocator *allocator, u8 *content, u32 length);

static void headers_init(Headers_Map *headers_map);
static void headers_put(Headers_Map *headers_map, String field_name, String field_value);
static String *headers_get(Headers_Map *headers_map, String field_name);

