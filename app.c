static void render(Bitmap_Buffer *buffer, u32 xoffset, u32 yoffset, f32 dt) {
    u32 pitch = buffer->width * buffer->bytes_per_pixel;
    u8 *row = (u8 *)buffer->memory;
    for (u32 y = 0; y < buffer->height; y++) {

        u32 *pixel = (u32 *)row;
        for (u32 x = 0; x < buffer->width; x++) {
            u8 r = 0;
            u8 g = y + yoffset;
            u8 b = x + xoffset;
            u8 a = 0xFF;

            // Pixel en memoria: xx RR GG BB
            *pixel = (r << 24) | (g << 16) | (b << 8) | a;

            pixel++;
        }

        row += pitch;
    }
}

void update_and_render(Bitmap_Buffer *buffer, Input *input, f32 dt) {
    static u32 xoffset;
    static u32 yoffset;

    if (input->up.ended_down) {
        yoffset -= 1;
    }
    if (input->down.ended_down) {
        yoffset += 1;
    }
    if (input->left.ended_down) {
        xoffset -= 1;
    }
    if (input->right.ended_down) {
        xoffset += 1;
    }

    render(buffer, xoffset, yoffset, dt);
}
