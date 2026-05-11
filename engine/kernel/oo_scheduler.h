/* oo_scheduler.h — OO Cooperative + Preemptive Scheduler  Phase 5E
 * =================================================================
 * Phase 1: cooperative yield (8 task slots)
 * Phase 2: LAPIC timer preemption (10ms quantum)
 * Freestanding C11.
 */
#pragma once
#include <efi.h>

#define OO_SCHED_MAX_TASKS   8
#define OO_TASK_STACK_SIZE   (64 * 1024)   /* 64 KB per task */

typedef enum {
    OO_TASK_EMPTY   = 0,
    OO_TASK_READY   = 1,
    OO_TASK_RUNNING = 2,
    OO_TASK_BLOCKED = 3,
    OO_TASK_DONE    = 4,
} OoTaskState;

typedef void (*OoTaskFn)(void *arg);

typedef struct {
    UINT64       rsp;         /* saved stack pointer */
    UINT64       stack_base;  /* bottom of stack */
    UINT64       stack_top;   /* initial RSP */
    OoTaskState  state;
    OoTaskFn     fn;
    void        *arg;
    char         name[32];
    UINT64       ticks;       /* times scheduled */
    UINT64       yield_at;    /* tick when yielded */
} OoTask;

typedef struct {
    int      initialized;
    OoTask   tasks[OO_SCHED_MAX_TASKS];
    int      current;         /* index of running task (-1 = none) */
    UINT64   tick_count;      /* total scheduler ticks */
    UINT32   quantum_ms;      /* LAPIC timer quantum */
    int      preemptive;      /* 1 = LAPIC timer active */
} OoScheduler;

EFI_STATUS oo_sched_init(OoScheduler *s, UINT32 quantum_ms);
int        oo_sched_spawn(OoScheduler *s, OoTaskFn fn, void *arg, const char *name);
void       oo_yield(void);             /* cooperative yield */
void       oo_sched_tick(void);        /* called by LAPIC timer IRQ */
void       oo_sched_run(OoScheduler *s);  /* start scheduler loop */
void       oo_sched_print(const OoScheduler *s);
int        oo_sched_repl_cmd(OoScheduler *s, const char *cmd);

extern OoScheduler g_sched;
