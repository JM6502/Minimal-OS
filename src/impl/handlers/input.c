#include "input.h"
#include "graphics.h"
#include "time.h"
#include "x86_64/spinlock.h"

static syscall_input_event_t g_events[SYSCALL_INPUT_QUEUE_CAPACITY];
static uint32_t g_head;
static uint32_t g_count;
static uint64_t g_dropped;
static spinlock_t g_input_lock = SPINLOCK_INIT;

void input_event_push(uint8_t type, uint8_t key, uint8_t mods,
                      uint8_t buttons, int32_t dx, int32_t dy,
                      int32_t wheel) {
    if (!graphics_has_screen_owner()) return;

    uint64_t flags = spinlock_acquire(&g_input_lock);
    if (g_count == SYSCALL_INPUT_QUEUE_CAPACITY) {
        g_dropped++;
        spinlock_release(&g_input_lock, flags);
        return;
    }

    uint32_t index = (g_head + g_count) % SYSCALL_INPUT_QUEUE_CAPACITY;
    g_events[index] = (syscall_input_event_t){
        .type = type,
        .key = key,
        .mods = mods,
        .buttons = buttons,
        .dx = dx,
        .dy = dy,
        .wheel = wheel,
        .time_ms = (uint32_t)time_get_uptime_ms()
    };
    g_count++;
    spinlock_release(&g_input_lock, flags);
}

uint32_t input_event_drain(syscall_input_event_t* events, uint32_t capacity) {
    if (!events || !capacity) return 0;
    if (capacity > SYSCALL_INPUT_MAX_READ) capacity = SYSCALL_INPUT_MAX_READ;

    uint64_t flags = spinlock_acquire(&g_input_lock);
    uint32_t count = g_count < capacity ? g_count : capacity;
    for (uint32_t i = 0; i < count; ++i) {
        events[i] = g_events[g_head];
        g_head = (g_head + 1u) % SYSCALL_INPUT_QUEUE_CAPACITY;
    }
    g_count -= count;
    spinlock_release(&g_input_lock, flags);
    return count;
}

uint64_t input_event_dropped_count(void) {
    uint64_t flags = spinlock_acquire(&g_input_lock);
    uint64_t dropped = g_dropped;
    spinlock_release(&g_input_lock, flags);
    return dropped;
}
