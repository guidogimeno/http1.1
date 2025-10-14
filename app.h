typedef struct {
    u32 half_transition_count;
    b32 ended_down;
} Key_State;

typedef struct {
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
    Arena *arena;
} Bitmap_Buffer;

void update_and_render(Bitmap_Buffer *buffer, Input *input, f32 dt);

