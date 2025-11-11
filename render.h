typedef enum {
    RENDER_ENTRY_TYPE_CLEAR,
    RENDER_ENTRY_TYPE_COORDINATES,
    RENDER_ENTRY_TYPE_RECTANGLE,
    RENDER_ENTRY_TYPE_RECTANGLE_OUTLINE,
} Render_Entry_Type;

typedef struct {
    Render_Entry_Type type;
} Render_Entry_Header;

typedef struct {
    Render_Entry_Header header;
    Vec2_F32 origin;
    Vec2_F32 x_axis;
    Vec2_F32 y_axis;
    Vec4_F32 color;
} Render_Entry_Coordinate_System;

typedef struct {
    Render_Entry_Header header;
    Vec4_F32 color;
} Render_Entry_Clear;

typedef struct {
    Render_Entry_Header header;
    Vec2_F32 base;
    Vec2_F32 offset;
    Vec2_F32 dimension;
    Vec4_F32 color;
} Render_Entry_Rectangle;

typedef struct {
    Vec2_F32 base;
    u64 max_push_buffer_size;
    u64 push_buffer_size;
    u8 *push_buffer;
} Render_Group;

static void render_clear(Render_Group *render, Vec4_F32 color);
static void render_coordinates(Render_Group *render, Vec2_F32 origin,
                               Vec2_F32 x_axis, Vec2_F32 y_axis,
                               Vec4_F32 color);
static void render_add_rect(Render_Group *render, Vec2_F32 offset,
                            Vec2_F32 dimension, Vec4_F32 color);
static void render_add_rect_outline(Render_Group *render, Vec2_F32 offset,
                                    Vec2_F32 dimension, Vec4_F32 color);
static void render_to_output(Render_Group *render, Draw_Buffer *output);

// TODO: Esto creo que tendria que eventualmente irse a una seccion de "draw"
static void render_draw_rectangle(Draw_Buffer *buf, Vec2_F32 offset,
                                  Vec2_F32 dimension, Vec4_F32 color);


