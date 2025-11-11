#if APP_INTERNAL

typedef enum {
    DEBUG_CYCLE_COUNTER_update_and_render,
    DEBUG_CYCLE_COUNTER_render_to_output,
    DEBUG_CYCLE_COUNTER_render_draw_rectangle,
    DEBUG_CYCLE_COUNTER_COUNT
} Debug_Cycle_Counter_Func; 

typedef struct {
    u64 cycle_count;
    u32 hit_count;
} Debug_Cycle_Counter;

static u64 cpu_timer() {
    u64 cntvct;
    __asm__ volatile("mrs %0, cntvct_el0" : "=r"(cntvct) :: "memory");
    return cntvct;
}

#define BEGIN_TIMED_BLOCK(id) u64 start_cycle_count##id = cpu_timer();
#define END_TIMED_BLOCK(id) debug_global_memory->counters[DEBUG_CYCLE_COUNTER_##id].cycle_count \
                            += cpu_timer() - start_cycle_count##id; \
debug_global_memory->counters[DEBUG_CYCLE_COUNTER_##id].hit_count++;\

#else

#define BEGIN_TIMED_BLOCK(id)
#define END_TIMED_BLOCK(id)

#endif
