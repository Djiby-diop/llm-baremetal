/* oo_scheduler.c — OO Cooperative Scheduler  Phase 5E
 * =====================================================
 * Cooperative multitasking with LAPIC preemption stub.
 * Freestanding C11. No libc.
 */
#include "oo_scheduler.h"

OoScheduler g_sched;

/* Static stacks for all tasks */
static UINT8 _stacks[OO_SCHED_MAX_TASKS][OO_TASK_STACK_SIZE]
    __attribute__((aligned(16)));

/* ── Context switch (cooperative) ─────────────────────────────────────── */
static void _task_wrapper(void) {
    OoScheduler *s = &g_sched;
    OoTask *t = &s->tasks[s->current];
    t->fn(t->arg);
    t->state = OO_TASK_DONE;
    /* Yield back to scheduler */
    oo_yield();
    /* Should never reach here */
    while(1) __asm__("hlt");
}

/* ── Save/restore context in pure C (simplified) ──────────────────────── */
/* Real-world would use full register save in ASM.
   Here we use setjmp-style via inline ASM for the RSP only. */

static int _task_started[OO_SCHED_MAX_TASKS];

/* Call task entry — set up initial stack frame */
static __attribute__((noinline)) void _launch_task(OoTask *t) {
    /* Switch to task stack and call wrapper */
    __asm__ volatile(
        "mov %0, %%rsp\n\t"
        "call _task_wrapper\n\t"
        :: "r"(t->stack_top) : "memory"
    );
}

/* ── Init ─────────────────────────────────────────────────────────────── */
EFI_STATUS oo_sched_init(OoScheduler *s, UINT32 quantum_ms) {
    for (int i = 0; i < sizeof(*s); i++) ((UINT8*)s)[i] = 0;
    s->current    = -1;
    s->quantum_ms = quantum_ms ? quantum_ms : 10;
    s->initialized = 1;

    /* Pre-initialize stacks */
    for (int i = 0; i < OO_SCHED_MAX_TASKS; i++) {
        s->tasks[i].stack_base = (UINT64)(UINTN)_stacks[i];
        s->tasks[i].stack_top  = (UINT64)(UINTN)(_stacks[i] + OO_TASK_STACK_SIZE - 16);
        s->tasks[i].state      = OO_TASK_EMPTY;
    }
    Print(L"[sched] Initialized — %d task slots, quantum=%ums\r\n",
          OO_SCHED_MAX_TASKS, s->quantum_ms);
    return EFI_SUCCESS;
}

/* ── Spawn a task ─────────────────────────────────────────────────────── */
int oo_sched_spawn(OoScheduler *s, OoTaskFn fn, void *arg, const char *name) {
    if (!s->initialized) return -1;
    for (int i = 0; i < OO_SCHED_MAX_TASKS; i++) {
        if (s->tasks[i].state == OO_TASK_EMPTY ||
            s->tasks[i].state == OO_TASK_DONE) {
            OoTask *t  = &s->tasks[i];
            t->fn      = fn;
            t->arg     = arg;
            t->ticks   = 0;
            t->state   = OO_TASK_READY;
            _task_started[i] = 0;
            /* Copy name */
            int j;
            for (j = 0; name && name[j] && j < 31; j++) t->name[j] = name[j];
            t->name[j] = 0;
            /* Reset RSP to top of stack */
            t->rsp = t->stack_top;
            Print(L"[sched] Task[%d] spawned: '%a'\r\n", i, t->name);
            return i;
        }
    }
    Print(L"[sched] ⚠ No free task slot\r\n");
    return -1;
}

/* ── Cooperative yield — switch to next READY task ───────────────────── */
static int _next_ready(OoScheduler *s) {
    int start = (s->current >= 0) ? s->current : 0;
    for (int i = 1; i <= OO_SCHED_MAX_TASKS; i++) {
        int idx = (start + i) % OO_SCHED_MAX_TASKS;
        if (s->tasks[idx].state == OO_TASK_READY) return idx;
    }
    return -1;
}

void oo_yield(void) {
    OoScheduler *s = &g_sched;
    if (!s->initialized) return;
    s->tick_count++;

    int prev = s->current;
    int next = _next_ready(s);

    if (next < 0) return; /* No ready tasks */

    if (prev >= 0 && prev != next && s->tasks[prev].state == OO_TASK_RUNNING)
        s->tasks[prev].state = OO_TASK_READY;

    s->current = next;
    s->tasks[next].state = OO_TASK_RUNNING;
    s->tasks[next].ticks++;

    if (!_task_started[next]) {
        _task_started[next] = 1;
        _launch_task(&s->tasks[next]);
    }
}

/* ── LAPIC timer tick (stub — full impl requires LAPIC MMIO) ─────────── */
void oo_sched_tick(void) {
    g_sched.tick_count++;
    oo_yield();
}

/* ── Scheduler run loop (cooperative) ───────────────────────────────── */
void oo_sched_run(OoScheduler *s) {
    if (!s->initialized) return;
    Print(L"[sched] Starting cooperative scheduler loop\r\n");
    while (1) {
        int found = 0;
        for (int i = 0; i < OO_SCHED_MAX_TASKS; i++) {
            if (s->tasks[i].state == OO_TASK_READY) {
                found = 1;
                s->current = i;
                s->tasks[i].state = OO_TASK_RUNNING;
                s->tasks[i].ticks++;
                if (!_task_started[i]) {
                    _task_started[i] = 1;
                    _launch_task(&s->tasks[i]);
                }
            }
        }
        if (!found) break;
    }
    Print(L"[sched] All tasks done. Ticks: %lu\r\n", s->tick_count);
}

/* ── Print ───────────────────────────────────────────────────────────── */
static const char *_state_str(OoTaskState st) {
    switch(st) {
        case OO_TASK_EMPTY:   return "EMPTY";
        case OO_TASK_READY:   return "READY";
        case OO_TASK_RUNNING: return "RUN";
        case OO_TASK_BLOCKED: return "BLOCKED";
        case OO_TASK_DONE:    return "DONE";
        default:              return "?";
    }
}

void oo_sched_print(const OoScheduler *s) {
    Print(L"\r\n  [Scheduler]\r\n");
    Print(L"  Initialized : %s\r\n", s->initialized ? L"YES" : L"NO");
    Print(L"  Tick count  : %lu\r\n", s->tick_count);
    Print(L"  Quantum     : %u ms\r\n", s->quantum_ms);
    Print(L"  Current     : %d\r\n\r\n", s->current);
    for (int i = 0; i < OO_SCHED_MAX_TASKS; i++) {
        const OoTask *t = &s->tasks[i];
        if (t->state == OO_TASK_EMPTY) continue;
        Print(L"  [%d] %-12a %-8a ticks=%lu\r\n",
              i, t->name, _state_str(t->state), t->ticks);
    }
    Print(L"\r\n");
}

/* ── REPL ────────────────────────────────────────────────────────────── */
static int _sched_cmp(const char*a,const char*b,int n){
    for(int i=0;i<n;i++){if(!a[i]&&!b[i])return 0;if(a[i]!=b[i])return 1;}return 0;}

int oo_sched_repl_cmd(OoScheduler *s, const char *cmd) {
    if (!cmd) return 0;
    if (_sched_cmp(cmd, "/sched_status", 13) == 0) { oo_sched_print(s); return 1; }
    if (_sched_cmp(cmd, "/sched_init",   11) == 0) {
        oo_sched_init(s, 10); return 1;
    }
    if (_sched_cmp(cmd, "/sched_run",    10) == 0) {
        oo_sched_run(s); return 1;
    }
    if (_sched_cmp(cmd, "/sched_yield",  12) == 0) {
        oo_yield(); return 1;
    }
    return 0;
}
