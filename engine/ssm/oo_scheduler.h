#pragma once
#include <stdint.h>

/**
 * OO Scheduler - Preemptive Multitasking
 * 
 * Manages CPU time slices and context switching between organs
 * and user tasks using the APIC timer.
 */

typedef struct {
    uint64_t r15, r14, r13, r12, r11, r10, r9, r8;
    uint64_t rbp, rdi, rsi, rdx, rcx, rbx, rax;
    uint64_t rip, cs, rflags, rsp, ss; // Pushed by CPU on interrupt
} CpuContext;

typedef enum {
    TASK_STATE_READY,
    TASK_STATE_RUNNING,
    TASK_STATE_SLEEPING,
    TASK_STATE_ZOMBIE
} TaskState;

typedef struct {
    uint32_t id;
    CpuContext context;
    TaskState state;
    uint32_t priority;
    uint32_t time_slice;
    uint8_t stack[4096];
} OoTask;

// Initialize the scheduler and setup APIC timer
void oo_scheduler_init(void);

// Yield CPU to the next ready task
void oo_scheduler_yield(void);

// Create a new task
int oo_create_task(void (*entry_point)(void), uint32_t priority);
