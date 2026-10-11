#ifndef WINDOW_MANAGER_H
#define WINDOW_MANAGER_H

#include <stdbool.h>
#include <stdint.h>
#include "x86_64/syscall.h"

uint64_t window_manager_syscall(const syscall_window_request_t* request,
                                uint64_t caller_pid, bool is_window_manager);
void window_manager_process_exit(uint64_t pid);

#endif
