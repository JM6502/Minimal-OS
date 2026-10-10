#ifndef INPUT_H
#define INPUT_H

#include <stdbool.h>
#include <stdint.h>
#include "x86_64/syscall.h"

void input_event_push(uint8_t type, uint8_t key, uint8_t mods,
                      uint8_t buttons, int32_t dx, int32_t dy,
                      int32_t wheel);
uint32_t input_event_drain(syscall_input_event_t* events, uint32_t capacity);
uint64_t input_event_dropped_count(void);

#endif
