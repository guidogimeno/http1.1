#include "render.h"

#include "render.c"

static f32 x, y = 50.0f;

void update_and_render(App_Memory *memory, Thread_Context *thread,
                       Bitmap_Buffer *buffer, Input *input) {
    App_State *app = (App_State *)memory->permanent;
    Transient_State *transient = (Transient_State *)memory->transient;

#if APP_INTERNAL
    debug_global_memory = memory;
#endif

    BEGIN_TIMED_BLOCK(update_and_render);

    if (!app->initialized) {
        arena_init(&app->arena, (u8 *)(memory->permanent) + sizeof(App_State),
                   memory->permanent_size - sizeof(App_State));
        app->initialized = true;
        app->time = 0;
    }

    if (!transient->initialized) {
        arena_init(&transient->arena, 
                   (u8 *)(memory->transient) + sizeof(Transient_State),
                   memory->transient_size - sizeof(Transient_State));
        transient->initialized = true;
    }

    app->time += input->dt;

    Draw_Buffer draw_buffer = {0};
    draw_buffer.width = buffer->width;
    draw_buffer.height = buffer->height;
    draw_buffer.memory = buffer->memory;
    draw_buffer.pitch = buffer->pitch;
    draw_buffer.bytes_per_pixel = buffer->bytes_per_pixel;

    Arena_Temp arena_temp = arena_temp_begin(&transient->arena);
    
    Render_Group *render_group = arena_alloc(&transient->arena, 
                                             sizeof(Render_Group));
    render_group->base = vec2_f32(0, 0);
    render_group->max_push_buffer_size = 1 * MB;
    render_group->push_buffer_size = 0;
    render_group->push_buffer = arena_alloc(&transient->arena,
                                        render_group->max_push_buffer_size);

    if (input->up.ended_down) {
        y -= 128 * input->dt;
    }
    if (input->down.ended_down) {
        y += 128 * input->dt;
    }
    if (input->left.ended_down) {
        x -= 128 * input->dt;
    }
    if (input->right.ended_down) {
        x += 128 * input->dt;
    }

    render_clear(render_group, vec4_f32(0.0f, 0.0f, 0.0f, 0.0f));

    render_add_rect(render_group, vec2_f32(x, y), 
                    vec2_f32(x + 10.0f, y + 10.0f), 
                    vec4_f32(1.0f, 0.0f, 0.0f, 0.0f));

    render_add_rect(render_group, vec2_f32(x + 100.0f, y + 100.0f), 
                    vec2_f32(x + 120.0f, y + 120.0f), 
                    vec4_f32(0.0f, 1.0f, 0.0f, 0.0f));

    render_add_rect_outline(render_group, vec2_f32(100.0f, 100.0f), 
                    vec2_f32(100.0f + 20.0f, 100.0f + 20.0f), 
                    vec4_f32(0.0f, 0.5f, 0.5f, 0.0f));

    f32 angle = app->time;
    Vec2_F32 x_axis = vec2_f32(50.0f * math_cos(angle), 50.0f * math_sin(angle));
    Vec2_F32 y_axis = vec2_f32(-x_axis.y, x_axis.x);
    render_coordinates(render_group, vec2_f32(400.0f, 400.0f), x_axis, y_axis,
                       vec4_f32(1.0f, 0.0f, 1.0f, 0.0f));

    render_to_output(render_group, &draw_buffer);

    arena_temp_end(arena_temp);

    END_TIMED_BLOCK(update_and_render);
}
