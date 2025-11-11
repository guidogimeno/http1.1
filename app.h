
typedef struct {
    void *memory;
    i32 width;
    i32 height;
    i32 pitch;
    i32 bytes_per_pixel;
} Draw_Buffer;

typedef struct {
    Arena arena;
    b32 initialized;
} Transient_State;

typedef struct {
    Arena arena;
    b32 initialized;
    f32 time;
} App_State;

typedef struct {
    u32 half_transition_count;
    b32 ended_down;
} Key_State;

typedef struct {
    i32 placeholder;
} Thread_Context;

typedef struct {
    Key_State mouse_buttons[3];
    i32 mouse_x;
    i32 mouse_y;
    i32 mouse_z;

    f32 dt;

    union {
        Key_State keys[4];
        struct {
            Key_State up;
            Key_State down;
            Key_State left;
            Key_State right;
        };
    };
} Input;

typedef struct {
    void *memory;
    i32 width;
    i32 height;
    i32 bytes_per_pixel;
    i32 pitch;
} Bitmap_Buffer;

typedef struct {
    u64 permanent_size;
    u8 *permanent;

    u64 transient_size;
    u8 *transient;

#if APP_INTERNAL
    Debug_Cycle_Counter counters[DEBUG_CYCLE_COUNTER_COUNT];
#endif
} App_Memory;

#if APP_INTERNAL
static App_Memory *debug_global_memory;
#endif

void update_and_render(App_Memory *memory, Thread_Context *thread,
                       Bitmap_Buffer *buffer, Input *input);

