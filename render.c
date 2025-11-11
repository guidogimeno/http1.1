static void render_draw_rectangle(Draw_Buffer *buf, Vec2_F32 offset,
                                  Vec2_F32 dimension, Vec4_F32 color) {
    BEGIN_TIMED_BLOCK(render_draw_rectangle);

    i32 min_x_int = math_round_f32_to_i32(offset.x);
    i32 min_y_int = math_round_f32_to_i32(offset.y); 
    i32 max_x_int = math_round_f32_to_i32(dimension.x); 
    i32 max_y_int = math_round_f32_to_i32(dimension.y); 

    if (min_x_int < 0) {
        min_x_int = 0;
    }

    if (min_y_int < 0) {
        min_y_int = 0;
    }

    if (max_x_int > buf->width) {
        max_x_int = buf->width;
    }

    if (max_y_int > buf->height) {
        max_y_int = buf->height;
    }

    u32 color_u32 = ((math_round_f32_to_i32(color.w * 255.0f) << 24) |
                     (math_round_f32_to_i32(color.x * 255.0f) << 16) |
                     (math_round_f32_to_i32(color.y * 255.0f) << 8)  |
                     (math_round_f32_to_i32(color.z * 255.0f) << 0));
    uint32x4_t color_vec = vdupq_n_u32(color_u32);

    u8 *row = (u8 *)(buf->memory + 
               min_x_int * buf->bytes_per_pixel +
               min_y_int * buf->pitch);
    for (i32 y = min_y_int; y < max_y_int; y++) {
        u32 *pixel = (u32 *)row;
        for (i32 x = min_x_int; x < max_x_int; x += 4) {
            vst1q_u32(pixel, color_vec);
            pixel += 4;
        }
        row += buf->pitch;
    }

    END_TIMED_BLOCK(render_draw_rectangle);
}

static void *render_push_element(Render_Group *render, u32 size, Render_Entry_Type type) {
    Render_Entry_Header *result = 0;

    if ((render->push_buffer_size + size) < render->max_push_buffer_size) {
        result = (Render_Entry_Header *)(render->push_buffer + render->push_buffer_size);
        result->type = type;
        render->push_buffer_size += size;
    } else {
        assert(false && "render se quedo sin memoria");
    }

    return result;
}

static void render_add_rect(Render_Group *render, Vec2_F32 offset,
                            Vec2_F32 dimension, Vec4_F32 color) {
    Render_Entry_Rectangle *entry = render_push_element(
        render, sizeof(Render_Entry_Rectangle), RENDER_ENTRY_TYPE_RECTANGLE);
    if (entry) {
        entry->base = render->base;
        entry->offset = offset;
        entry->dimension = dimension;
        entry->color = color;
    }
}

static void render_add_rect_outline(Render_Group *render, Vec2_F32 offset,
                                    Vec2_F32 dimension, Vec4_F32 color) {
    f32 thickness = 10.0f;

    // TODO: Arreglar porque esto esta como el ojete
    Vec2_F32 half_dimension_x = vec2_f32(0, 0.5f * dimension.x);
    Vec2_F32 half_dimension_y = vec2_f32(0, 0.5f * dimension.y);

    render_add_rect(render, vec2_f32_sub(offset, half_dimension_y),
                    vec2_f32(dimension.x, thickness), color);
    render_add_rect(render, vec2_f32_add(offset, half_dimension_y),
                    vec2_f32(dimension.x, thickness), color);
    
    render_add_rect(render, vec2_f32_sub(offset, half_dimension_x),
                    vec2_f32(thickness, dimension.y), color);
    render_add_rect(render, vec2_f32_add(offset, half_dimension_x),
                    vec2_f32(thickness, dimension.y), color);
}

static void render_clear(Render_Group *render, Vec4_F32 color) {
    Render_Entry_Clear *entry = render_push_element(
        render, sizeof(Render_Entry_Clear), RENDER_ENTRY_TYPE_CLEAR);
    if (entry) {
        entry->color = color;
    }
}

static void render_coordinates(Render_Group *render, Vec2_F32 origin,
                               Vec2_F32 x_axis, Vec2_F32 y_axis,
                               Vec4_F32 color) {
    Render_Entry_Coordinate_System *entry = render_push_element(
        render, sizeof(Render_Entry_Coordinate_System),
        RENDER_ENTRY_TYPE_COORDINATES);
    if (entry) {
        entry->origin = origin;
        entry->x_axis = x_axis;
        entry->y_axis = y_axis;
        entry->color = color;
    }
}

static void render_to_output(Render_Group *render, Draw_Buffer *output) {
    BEGIN_TIMED_BLOCK(render_to_output);

    u32 base = 0;
    while (base < render->push_buffer_size) {
        Render_Entry_Header *header = (Render_Entry_Header *)&render->push_buffer[base];
        switch (header->type) {
            case RENDER_ENTRY_TYPE_COORDINATES: {
                Render_Entry_Coordinate_System *entry = (Render_Entry_Coordinate_System *)header;

                Vec2_F32 dim = vec2_f32(10.0f, 10.0f);

                Vec2_F32 p = vec2_f32_add(entry->origin, entry->x_axis);
                render_draw_rectangle(output, vec2_f32_sub(p, dim), vec2_f32_add(p, dim), entry->color);

                p = vec2_f32_add(entry->origin, entry->y_axis);
                render_draw_rectangle(output, vec2_f32_sub(p, dim), vec2_f32_add(p, dim), entry->color);

                base += sizeof(*entry);
            } break;
            case RENDER_ENTRY_TYPE_CLEAR: {
                Render_Entry_Clear *entry = (Render_Entry_Clear *)header;
                Vec2_F32 offset = vec2_f32(0.0f, 0.0f);
                Vec2_F32 dimension = vec2_f32(output->width, output->height);
                render_draw_rectangle(output, offset, dimension, entry->color);
                base += sizeof(*entry);
            } break;
            case RENDER_ENTRY_TYPE_RECTANGLE_OUTLINE:
            case RENDER_ENTRY_TYPE_RECTANGLE: {
                Render_Entry_Rectangle *entry = (Render_Entry_Rectangle *)header;
                render_draw_rectangle(output, entry->offset, entry->dimension, entry->color);
                base += sizeof(*entry);
            } break;
            default: { debug_assert(false && "falta implementar") };
        }
    }

    END_TIMED_BLOCK(render_to_output);
}
