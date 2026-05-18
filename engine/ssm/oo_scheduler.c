#include "oo_scheduler.h"
#include <string.h>

#define MAX_TASKS 16

static OoTask g_tasks[MAX_TASKS];
static int    g_current_task = 0;
static int    g_task_count = 0;

void oo_scheduler_init(void) {
    memset(g_tasks, 0, sizeof(g_tasks));
    g_current_task = 0;
    g_task_count = 0;
    
    // The main execution flow becomes Task 0
    g_tasks[0].id = 0;
    g_tasks[0].state = TASK_STATE_RUNNING;
    g_tasks[0].priority = 1;
    g_task_count = 1;
}

int oo_create_task(void (*entry_point)(void), uint32_t priority) {
    if (g_task_count >= MAX_TASKS) return -1;
    
    int id = g_task_count++;
    OoTask *task = &g_tasks[id];
    
    task->id = id;
    task->state = TASK_STATE_READY;
    task->priority = priority;
    
    // Setup stack and entry point for the context switch
    // We simulate a stack frame as if the CPU pushed it during an interrupt
    uint64_t *stack_top = (uint64_t *)&task->stack[4096];
    
    // Fake interrupt frame
    *(--stack_top) = 0x10; // SS (Kernel Data Segment)
    *(--stack_top) = (uint64_t)&task->stack[4096]; // RSP
    *(--stack_top) = 0x202; // RFLAGS (Interrupts enabled)
    *(--stack_top) = 0x08; // CS (Kernel Code Segment)
    *(--stack_top) = (uint64_t)entry_point; // RIP
    
    // Fake general purpose registers
    for (int i = 0; i < 15; i++) {
        *(--stack_top) = 0;
    }
    
    task->context.rsp = (uint64_t)stack_top;
    
    return id;
}

void oo_scheduler_yield(void) {
    int prev_task = g_current_task;
    int next_task = -1;
    
    // Find next ready task (Round Robin)
    for (int i = 1; i <= MAX_TASKS; i++) {
        int idx = (prev_task + i) % MAX_TASKS;
        if (idx < g_task_count && g_tasks[idx].state == TASK_STATE_READY) {
            next_task = idx;
            break;
        }
    }
    
    if (next_task == -1) return; // No other task ready
    
    g_tasks[prev_task].state = TASK_STATE_READY;
    g_tasks[next_task].state = TASK_STATE_RUNNING;
    g_current_task = next_task;
    
    // Inline assembly for context switch
    // Saves current registers, switches stack, and restores registers
    __asm__ volatile (
        "pushfq\n"
        "push %%rax\n"
        "push %%rbx\n"
        "push %%rcx\n"
        "push %%rdx\n"
        "push %%rsi\n"
        "push %%rdi\n"
        "push %%rbp\n"
        "push %%r8\n"
        "push %%r9\n"
        "push %%r10\n"
        "push %%r11\n"
        "push %%r12\n"
        "push %%r13\n"
        "push %%r14\n"
        "push %%r15\n"
        
        // Save old RSP
        "mov %%rsp, %0\n"
        
        // Load new RSP
        "mov %1, %%rsp\n"
        
        "pop %%r15\n"
        "pop %%r14\n"
        "pop %%r13\n"
        "pop %%r12\n"
        "pop %%r11\n"
        "pop %%r10\n"
        "pop %%r9\n"
        "pop %%r8\n"
        "pop %%rbp\n"
        "pop %%rdi\n"
        "pop %%rsi\n"
        "pop %%rdx\n"
        "pop %%rcx\n"
        "pop %%rbx\n"
        "pop %%rax\n"
        "popfq\n"
        : "=m"(g_tasks[prev_task].context.rsp)
        : "r"(g_tasks[next_task].context.rsp)
        : "memory"
    );
}
