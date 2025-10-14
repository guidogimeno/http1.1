#include "gg_stdlib.h"
#include "app.h"

#include "app.c"

#include <CoreGraphics/CoreGraphics.h>
#include <objc/NSObjCRuntime.h>
#include <objc/objc-runtime.h>

#define cls(x) ((id)objc_getClass(x))
#define sel(name) sel_getUid(name)

#define msg(type, target, name) ((type(*)(id, SEL))objc_msgSend)(target, sel(name))
#define msg1(type, target, name, arg1_type, a1) \
    ((type(*)(id, SEL, arg1_type))objc_msgSend)(target, sel(name), a1)
#define msg2(type, target, name, arg1_type, a1, arg2_type, a2) \
    ((type(*)(id, SEL, arg1_type, arg2_type))objc_msgSend)(target, sel(name), a1, a2)
#define msg3(type, target, name, arg1_type, a1, arg2_type, a2, arg3_type, a3) \
    ((type(*)(id, SEL, arg1_type, arg2_type, arg3_type))objc_msgSend)(target, sel(name), a1, a2, a3)
#define msg4(type, target, name, arg1_type, a1, arg2_type, a2, arg3_type, a3, arg4_type, a4) \
    ((type(*)(id, SEL, arg1_type, arg2_type, arg3_type, arg4_type))objc_msgSend)(target, sel(name), a1, a2, a3, a4)

extern id const NSDefaultRunLoopMode;
extern id const NSApp;

static void macos_draw_rect(id v, SEL s, CGRect r) {
    (void)r, (void)s;

    Bitmap_Buffer *bitmap_buffer = (Bitmap_Buffer *)objc_getAssociatedObject(v,
                                                            "bitmap_buffer");

    id graphics_context = msg(id, cls("NSGraphicsContext"), "currentContext");
    CGContextRef context = msg(CGContextRef, graphics_context, "graphicsPort");

    CGColorSpaceRef space = CGColorSpaceCreateDeviceRGB();

    size_t buffer_size = (size_t)bitmap_buffer->width *
                                 bitmap_buffer->height *
                                 bitmap_buffer->bytes_per_pixel;
    CGDataProviderRef provider = CGDataProviderCreateWithData(
        NULL, bitmap_buffer->memory, buffer_size, NULL);

    size_t bits_per_component = 8;
    size_t bits_per_pixel = bitmap_buffer->bytes_per_pixel * 8;
    size_t bytes_per_row = bitmap_buffer->width *
                           bitmap_buffer->bytes_per_pixel;
    CGImageRef image = CGImageCreate(
        bitmap_buffer->width, bitmap_buffer->height, bits_per_component,
        bits_per_pixel, bytes_per_row, space, 
        kCGImageAlphaNoneSkipFirst | kCGBitmapByteOrder32Little, provider,
        NULL, false, kCGRenderingIntentDefault);

    CGColorSpaceRelease(space);
    CGDataProviderRelease(provider);
    CGContextDrawImage(context, CGRectMake(0, 0, bitmap_buffer->width,
                                           bitmap_buffer->height), image);
    CGImageRelease(image);
}

static BOOL macos_should_close(id v, SEL s, id w) {
  (void)v, (void)s, (void)w;
  msg1(void, NSApp, "terminate:", id, NSApp);
  return YES;
}

static i64 macos_time(void) {
    struct timespec time;
    clock_gettime(CLOCK_REALTIME, &time);
    return time.tv_sec * 1000 + (time.tv_nsec / 1000000);
}

static void process_keyboard_state(Key_State *key, bool key_down) {
    if (key->ended_down != key_down) {
        key->ended_down = key_down;
        key->half_transition_count++;
    }
}

i32 main() {
    u32 window_width = 800;
    u32 window_height = 600;
    u32 bytes_per_pixel = 4;
    u32 buffer_size = window_width * window_height * bytes_per_pixel;

    // TODO: Hacer permanente y transient
    Arena *arena = arena_make(512 * MB);
    void *memory = arena_alloc(arena, buffer_size);

    Bitmap_Buffer bitmap_buffer;
    bitmap_buffer.arena = arena;
    bitmap_buffer.width = window_width;
    bitmap_buffer.height = window_height;
    bitmap_buffer.bytes_per_pixel = bytes_per_pixel;
    bitmap_buffer.memory = memory;

    Input inputs[2] = {0};
    Input *new_input = &inputs[0];
    Input *old_input = &inputs[1];

    msg(id, cls("NSApplication"), "sharedApplication");
    msg1(void, NSApp, "setActivationPolicy:", NSInteger, 0);
    id window = msg4(id, msg(id, cls("NSWindow"), "alloc"),
                     "initWithContentRect:styleMask:backing:defer:", CGRect,
                     CGRectMake(0, 0, bitmap_buffer.width, 
                     bitmap_buffer.height), NSUInteger, 3, NSUInteger, 2, 
                     BOOL, NO);
    Class windelegate = objc_allocateClassPair((Class)cls("NSObject"),
                                               "Delegate", 0);
    class_addMethod(windelegate, sel_getUid("windowShouldClose:"),
                    (IMP)macos_should_close, "c@:@");
    objc_registerClassPair(windelegate);
    msg1(void, window, "setDelegate:", id,
       msg(id, msg(id, (id)windelegate, "alloc"), "init"));
    Class view_class = objc_allocateClassPair((Class)cls("NSView"), "View", 0);
    class_addMethod(view_class, sel_getUid("drawRect:"),
                    (IMP)macos_draw_rect, "i@:@@");
    objc_registerClassPair(view_class);

    id view = msg(id, msg(id, (id)view_class, "alloc"), "init");
    msg1(void, window, "setContentView:", id, view);
    objc_setAssociatedObject(view, "bitmap_buffer", (id)&bitmap_buffer,
                             OBJC_ASSOCIATION_ASSIGN);

    id title = msg1(id, cls("NSString"), "stringWithUTF8String:", const char *,
                    "TODO: Cambiar este titulo");
    msg1(void, window, "setTitle:", id, title);
    msg1(void, window, "makeKeyAndOrderFront:", id, nil);
    msg(void, window, "center");
    msg1(void, NSApp, "activateIgnoringOtherApps:", BOOL, YES);

    i64 last_time = macos_time();
    while (true) {

        *new_input = (Input){0};

        for (u32 i = 0; i < array_size(new_input->keys); i++) {
            new_input->keys[i].ended_down = old_input->keys[i].ended_down;
        }

        id content_view = msg(id, window, "contentView");
        msg1(void, content_view, "setNeedsDisplay:", BOOL, YES);

        id event = msg4(id, NSApp,
                        "nextEventMatchingMask:untilDate:inMode:dequeue:",
                        NSUInteger, NSUIntegerMax, id, NULL, id,
                        NSDefaultRunLoopMode, BOOL, YES);
        if (event) {
            bool prevent_default = false;
            NSUInteger event_type = msg(NSUInteger, event, "type");
            switch (event_type) {
                case 1: { // NSEventTypeMouseDown
                          // f->mouse |= 1;
                } break;
                case 2: { // NSEventTypeMouseUp
                          // f->mouse &= ~1;
                } break;
                case 5:
                case 6: { // NSEventTypeMouseMoved
                          // CGPoint xy = msg(CGPoint, ev, "locationInWindow");
                          // f->x = (int)xy.x;
                          // f->y = (int)(f->height - xy.y);
                } break;
                case 10: // NSEventTypeKeyDown
                case 11: { //NSEventTypeKeyUp
                    bool key_down = event_type == 10;
                    NSUInteger key_code = msg(NSUInteger, event, "keyCode");
                    if (key_code == 123) {
                        process_keyboard_state(&new_input->left, key_down);
                    } else if (key_code == 124) {
                        process_keyboard_state(&new_input->right, key_down);
                    } else if (key_code == 125) {
                        process_keyboard_state(&new_input->down, key_down);
                    } else if (key_code == 126) {
                        process_keyboard_state(&new_input->up, key_down);
                    }
                    prevent_default = true;
                } break;
            }

            if (!prevent_default) {
                msg1(void, NSApp, "sendEvent:", id, event);
            }
        }

        i64 end_time = macos_time();
        i64 elapsed_time = end_time - last_time;

        update_and_render(&bitmap_buffer, new_input, elapsed_time);

        last_time = end_time;

        Input *temp = new_input;
        new_input = old_input;
        old_input = temp;

        // printf("fps: %ff/ms\n", (1000 / (f64)elapsed_time));
        // printf("elapsed: %lldms/f\n", elapsed_time);
    }

    msg(void, window, "close");
    return 0;
}

