#include "gg_stdlib.h"

#include <arm_neon.h>

#include "macos_layer.h"
#include "app.h"

#include "app.c"

#include <CoreGraphics/CoreGraphics.h>
#include <objc/NSObjCRuntime.h>
#include <objc/objc-runtime.h>
#include <mach/mach_time.h>

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

static mach_timebase_info_data_t macos_timebase;

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

static u64 macos_clock_time(void) {
    return mach_absolute_time();
}

static u64 macos_perf_frequency(void) {
    return (u64)macos_timebase.numer * 1000000000ULL / macos_timebase.denom;
}

static f32 macos_delta_seconds(u64 start, u64 end) {
    u64 delta_ticks = end - start;
    return (f32)((f64)delta_ticks / (f64)macos_perf_frequency()) * 1000.0f;
}

static void macos_sleep(u64 ms) {
    u64 ns = ms * 1000000ULL;
    u64 now = mach_absolute_time();
    u64 deadline = now + (ns * macos_timebase.denom / macos_timebase.numer);
    mach_wait_until(deadline);
}

static void process_keyboard_state(Key_State *key, bool key_down) {
    if (key->ended_down != key_down) {
        key->ended_down = key_down;
        key->half_transition_count++;
    }
}

static u32 macos_monitor_hz() {
    u32 result = 0;

    CGDirectDisplayID main_display = CGMainDisplayID();
    CGDisplayModeRef mode = CGDisplayCopyDisplayMode(main_display);
   
    if (mode) {
        result = (u32)CGDisplayModeGetRefreshRate(mode);
        CGDisplayModeRelease(mode);
    } else {
        // TODO: log
    }

    return result;
}

static void handle_cycle_counters(void) {
#if APP_INTERNAL
    for (u32 i = 0; i < array_size(debug_global_memory->counters); i++) {
        Debug_Cycle_Counter *counter = debug_global_memory->counters + i;
        if (counter->hit_count) {
            printf("  %d: cycles=%llu hits=%d cycles/hit=%llu\n",
                    i,
                    counter->cycle_count,
                    counter->hit_count,
                    counter->cycle_count / counter->hit_count);
            counter->hit_count = 0;
            counter->cycle_count = 0;
        }
    }
#endif
};

i32 main() {
    mach_timebase_info(&macos_timebase);

    App_Memory app_memory = {0};
    app_memory.permanent_size = 32 * MB;
    app_memory.transient_size = 1 * GB;

    u64 total_size = app_memory.permanent_size + app_memory.transient_size;
    void *total_memory = mmap(0, total_size, PROT_READ|PROT_WRITE,
                              MAP_PRIVATE|MAP_ANONYMOUS, -1, 0);
    app_memory.permanent = (u8 *)total_memory;
    app_memory.transient = (u8 *)total_memory + app_memory.permanent_size;

    Thread_Context thread = {0};

    Input inputs[2] = {0};
    Input *new_input = &inputs[0];
    Input *old_input = &inputs[1];

    Arena *arena = arena_make(1 * GB);

    u32 window_width = 800;
    u32 window_height = 600;
    u32 bytes_per_pixel = 4;
    u32 buffer_size = window_width * window_height * bytes_per_pixel;

    Bitmap_Buffer bitmap_buffer = {0};
    bitmap_buffer.width = window_width;
    bitmap_buffer.height = window_height;
    bitmap_buffer.bytes_per_pixel = bytes_per_pixel;
    bitmap_buffer.pitch = window_width * bytes_per_pixel;
    bitmap_buffer.memory = arena_alloc(arena, buffer_size);

    msg(id, cls("NSApplication"), "sharedApplication");
    msg1(void, NSApp, "setActivationPolicy:", NSInteger, 0);
    id window = msg4(id, msg(id, cls("NSWindow"), "alloc"),
                     "initWithContentRect:styleMask:backing:defer:", CGRect,
                     CGRectMake(0, 0, bitmap_buffer.width, bitmap_buffer.height),
                     NSUInteger, 7, NSUInteger, 2, BOOL, NO);
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

    i32 monitor_hz = macos_monitor_hz();
    f32 target_seconds_per_frame = 1.0f / monitor_hz;

    u64 last_time = macos_clock_time();
    while (true) {

        *new_input = (Input){0};

        for (u32 i = 0; i < array_size(new_input->keys); i++) {
            new_input->keys[i].ended_down = old_input->keys[i].ended_down;
        }

        for (u32 i = 0; i < array_size(new_input->mouse_buttons); i++) {
            new_input->mouse_buttons[i].ended_down = old_input->mouse_buttons[i].ended_down;
        }

        new_input->mouse_x = old_input->mouse_x;
        new_input->mouse_y = old_input->mouse_y;
        new_input->mouse_z = old_input->mouse_z;
        new_input->dt = target_seconds_per_frame;

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
                case 1: { // Left Mouse Down
                    process_keyboard_state(&new_input->mouse_buttons[0], true);
                } break;
                case 2: { // Left Mouse Up
                    process_keyboard_state(&new_input->mouse_buttons[0], false);
                } break;
                case 3: { // Right Mouse Down
                    process_keyboard_state(&new_input->mouse_buttons[1], true);
                } break;
                case 4: { // Right Mouse Up
                    process_keyboard_state(&new_input->mouse_buttons[1], false);
                } break;
                case 5:
                case 6: { // NSEventTypeMouseMoved
                    CGPoint xy = msg(CGPoint, event, "locationInWindow");
                    new_input->mouse_x = (i32)xy.x;
                    new_input->mouse_y = (i32)(window_height - xy.y);
                    prevent_default = true;
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

        update_and_render(&app_memory, &thread, &bitmap_buffer, new_input);
        handle_cycle_counters();

        f32 seconds_elapsed = macos_delta_seconds(last_time, macos_clock_time()); 

        if (seconds_elapsed < target_seconds_per_frame) {
            while (seconds_elapsed < target_seconds_per_frame) {
                i64 sleep_ms = (i64)((target_seconds_per_frame - seconds_elapsed) * 1000.0f);
                if (sleep_ms > 0) {
                    macos_sleep(sleep_ms);
                }
                seconds_elapsed = macos_delta_seconds(last_time, macos_clock_time()); 
            }
        } else {
            // TODO: missed frame rate
            // TODO: logging
        }

        u64 end_time = macos_clock_time();
#if 0
        f32 seconds_per_frame = macos_delta_seconds(last_time, end_time); 
        printf("%.02f ms/f\n", seconds_per_frame * 1000.0f);
#endif
        last_time = end_time;

        Input *temp = new_input;
        new_input = old_input;
        old_input = temp;
    }

    msg(void, window, "close");
    return 0;
}

