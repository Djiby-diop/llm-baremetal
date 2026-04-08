#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void usage(const char *prog) {
    fprintf(stderr, "Usage: %s [normal|degraded|safe]\n", prog ? prog : "oo-sim");
    fprintf(stderr, "  If no mode is provided, defaults to 'normal'.\n");
}

/*
 * oo-sim: Operating Organism environment simulator.
 *
 * v1 additions (OO Architecture Evolution):
 *   - Memory pressure simulation: a shared RAM counter fills over time.
 *     When pressure crosses thresholds, the mode auto-degrades (NORMAL → DEGRADED → SAFE).
 *     This mirrors the firmware's pressure.c pain-signal subsystem.
 *
 *   - SplitBrain simulation: each organism has two concurrent sub-schedulers.
 *     RATIONAL: conservative, deadline-first, picks lowest (urgency) score.
 *     CREATIVE: exploratory, prefers "experimental" tasks, highest energy first.
 *     When the two schedulers pick different tasks, a divergence event is logged.
 *     CREATIVE is blocked under SAFE mode and when pressure > STRESSED.
 *
 *   - D+ policy gates: experimental tasks always require DPLUS_ALLOW verdict.
 *     In SAFE mode all D+ verdicts are DENY for experimental class.
 *
 * Log format remains backward compatible with oo-lab --sim-summary:
 *   tick=N mode=M org=X task=T class=C energy=N deadline=N done=N [splitbrain=rational|creative|both diverge=0|1 pressure=N]
 */

typedef enum {
    SIM_MODE_SAFE = 0,
    SIM_MODE_DEGRADED = 1,
    SIM_MODE_NORMAL = 2
} SimMode;

/* ============================================================
 * Memory Pressure
 * ============================================================ */
typedef enum {
    PRESSURE_CALM     = 0, /* < 30% */
    PRESSURE_AWARE    = 1, /* 30-50% */
    PRESSURE_STRESSED = 2, /* 50-70% */
    PRESSURE_CRITICAL = 3, /* 70-90% */
    PRESSURE_DYING    = 4  /* > 90% */
} PressureLevel;

typedef struct {
    int used;   /* simulated bytes used (0..max) */
    int max;    /* simulated total bytes */
    PressureLevel level;
} SimPressure;

static const char *pressure_to_string(PressureLevel p) {
    switch (p) {
    case PRESSURE_CALM:     return "CALM";
    case PRESSURE_AWARE:    return "AWARE";
    case PRESSURE_STRESSED: return "STRESSED";
    case PRESSURE_CRITICAL: return "CRITICAL";
    case PRESSURE_DYING:    return "DYING";
    default:                return "UNKNOWN";
    }
}

static PressureLevel pressure_compute_level(const SimPressure *p) {
    int pct = (p->used * 100) / (p->max > 0 ? p->max : 1);
    if (pct < 30) return PRESSURE_CALM;
    if (pct < 50) return PRESSURE_AWARE;
    if (pct < 70) return PRESSURE_STRESSED;
    if (pct < 90) return PRESSURE_CRITICAL;
    return PRESSURE_DYING;
}

/* Simulate memory allocation: each task tick consumes a bit of RAM. */
static void pressure_alloc(SimPressure *p, int bytes) {
    p->used += bytes;
    if (p->used > p->max) p->used = p->max;
    p->level = pressure_compute_level(p);
}

/* Simulate periodic GC that frees some RAM. */
static void pressure_gc(SimPressure *p, int freed) {
    p->used -= freed;
    if (p->used < 0) p->used = 0;
    p->level = pressure_compute_level(p);
}

/* Convert pressure level → recommended mode downgrade. */
static SimMode pressure_recommended_mode(PressureLevel pl) {
    if (pl >= PRESSURE_DYING)    return SIM_MODE_SAFE;
    if (pl >= PRESSURE_CRITICAL) return SIM_MODE_DEGRADED;
    return SIM_MODE_NORMAL;
}

/* ============================================================
 * D+ Policy gate (simplified simulation)
 * ============================================================ */
typedef enum { DPLUS_ALLOW = 0, DPLUS_DENY = 1 } DplusVerdict;

static DplusVerdict dplus_eval_task(SimMode mode, const char *safety_class, PressureLevel pressure) {
    /* Experimental tasks: blocked in SAFE mode or under critical pressure. */
    if (safety_class && strcmp(safety_class, "experimental") == 0) {
        if (mode == SIM_MODE_SAFE) return DPLUS_DENY;
        if (pressure >= PRESSURE_CRITICAL) return DPLUS_DENY;
    }
    return DPLUS_ALLOW;
}

typedef struct {
    const char *name;
    const char *safety_class; /* e.g. "normal", "recovery", "experimental" */
    int remaining_energy;
    int deadline;
    int done;
} SimTask;

typedef struct {
    const char *id; /* e.g. "A", "B", "C" */
    SimTask *tasks;
    int task_count;
    int time;
    SimMode mode;
    SimPressure *pressure; /* shared pressure across all organisms */
} SimWorld;

static void world_init(SimWorld *w, const char *id, SimTask *tasks, int count, SimMode mode, SimPressure *pressure) {
    int i;
    w->id = id;
    w->tasks = tasks;
    w->task_count = count;
    w->time = 0;
    w->mode = mode;
    w->pressure = pressure;
    for (i = 0; i < count; i++) {
        w->tasks[i].done = 0;
    }
}

static int world_all_done(const SimWorld *w) {
    int i;
    for (i = 0; i < w->task_count; i++) {
        if (!w->tasks[i].done) {
            return 0;
        }
    }
    return 1;
}

static const char *mode_to_string(SimMode m) {
    switch (m) {
    case SIM_MODE_SAFE: return "SAFE";
    case SIM_MODE_DEGRADED: return "DEGRADED";
    case SIM_MODE_NORMAL: return "NORMAL";
    default: return "UNKNOWN";
    }
}

/* ============================================================
 * SplitBrain: two concurrent task selectors per organism.
 * RATIONAL: deadline-first (same as original pick_next_task).
 * CREATIVE: prefers experimental/high-energy tasks.
 * ============================================================ */

/* RATIONAL scheduler: lowest urgency score, respects mode gates + D+ */
static int pick_rational(const SimWorld *w) {
    int best_idx = -1;
    int best_score = 2147483647;
    int i;
    PressureLevel pl = w->pressure ? w->pressure->level : PRESSURE_CALM;

    for (i = 0; i < w->task_count; i++) {
        const SimTask *t = &w->tasks[i];
        if (t->done || t->remaining_energy <= 0) continue;
        if (dplus_eval_task(w->mode, t->safety_class, pl) == DPLUS_DENY) continue;

        /* original mode gates */
        if (w->mode == SIM_MODE_SAFE) {
            if (t->safety_class && strcmp(t->safety_class, "experimental") == 0) continue;
            if (t->safety_class && strcmp(t->safety_class, "recovery") != 0) {
                if (t->deadline - w->time > 5) continue;
            }
        } else if (w->mode == SIM_MODE_DEGRADED) {
            if (t->safety_class && strcmp(t->safety_class, "experimental") == 0) continue;
            if (t->deadline - w->time > 15) continue;
        }

        int score = (t->deadline - w->time) + t->remaining_energy;
        if (score < best_score) { best_score = score; best_idx = i; }
    }
    return best_idx;
}

/* CREATIVE scheduler: prefers experimental + high energy; blocked under SAFE / high pressure. */
static int pick_creative(const SimWorld *w) {
    PressureLevel pl = w->pressure ? w->pressure->level : PRESSURE_CALM;

    /* CREATIVE is blocked under SAFE mode or severe pressure. */
    if (w->mode == SIM_MODE_SAFE) return -1;
    if (pl >= PRESSURE_STRESSED) return -1; /* under memory stress, no creativity */

    int best_idx = -1;
    int best_score = -1;
    int i;

    for (i = 0; i < w->task_count; i++) {
        const SimTask *t = &w->tasks[i];
        if (t->done || t->remaining_energy <= 0) continue;
        if (dplus_eval_task(w->mode, t->safety_class, pl) == DPLUS_DENY) continue;

        /* Prefer experimental; use remaining_energy as score (higher = more creative). */
        int is_experimental = (t->safety_class && strcmp(t->safety_class, "experimental") == 0) ? 1 : 0;
        int score = t->remaining_energy + is_experimental * 100;
        if (score > best_score) { best_score = score; best_idx = i; }
    }
    return best_idx;
}

static void world_tick(SimWorld *w) {
    static FILE *logf = NULL;
    if (!logf) {
        logf = fopen("OOSIM.LOG", "a");
    }

    /* Pressure: update mode if pressure recommends downgrade. */
    if (w->pressure) {
        SimMode rec = pressure_recommended_mode(w->pressure->level);
        if (rec < w->mode) {
            printf("  [PRESSURE] org=%s mode %s → %s (pressure=%s)\n",
                   w->id ? w->id : "X",
                   mode_to_string(w->mode),
                   mode_to_string(rec),
                   pressure_to_string(w->pressure->level));
            if (logf) {
                fprintf(logf, "tick=%d mode_change org=%s old=%s new=%s pressure=%s\n",
                        w->time, w->id ? w->id : "X",
                        mode_to_string(w->mode), mode_to_string(rec),
                        pressure_to_string(w->pressure->level));
            }
            w->mode = rec;
        }
    }

    int rational_idx = pick_rational(w);
    int creative_idx = pick_creative(w);

    /* Determine which scheduler wins this tick (RATIONAL takes precedence). */
    int chosen_idx = rational_idx;
    int splitbrain_diverge = (rational_idx != creative_idx && creative_idx >= 0) ? 1 : 0;
    const char *scheduler_used = "rational";
    if (rational_idx < 0 && creative_idx >= 0) {
        chosen_idx = creative_idx;
        scheduler_used = "creative";
    } else if (rational_idx >= 0 && creative_idx >= 0 && rational_idx != creative_idx) {
        /* Both schedulers active but disagree: log divergence, use RATIONAL. */
        scheduler_used = "rational(diverge)";
    } else if (rational_idx >= 0 && creative_idx == rational_idx) {
        scheduler_used = "both";
        splitbrain_diverge = 0;
    }

    int pressure_pct = 0;
    if (w->pressure) {
        pressure_pct = (w->pressure->used * 100) / (w->pressure->max > 0 ? w->pressure->max : 1);
    }

    if (chosen_idx >= 0) {
        SimTask *t = &w->tasks[chosen_idx];
        t->remaining_energy -= 1;
        if (t->remaining_energy <= 0) {
            t->remaining_energy = 0;
            t->done = 1;
        }
        /* Simulated memory allocation per task tick. */
        if (w->pressure) {
            pressure_alloc(w->pressure, 512);
        }
        printf("tick %d [%s] org=%s: %-12s  class=%-11s  energy=%d  sched=%-16s  diverge=%d  pressure=%d%%\n",
               w->time, mode_to_string(w->mode), w->id ? w->id : "X", t->name,
               t->safety_class ? t->safety_class : "unknown",
               t->remaining_energy, scheduler_used, splitbrain_diverge, pressure_pct);
        if (logf) {
            fprintf(logf,
                    "tick=%d mode=%s org=%s task=%s class=%s energy=%d deadline=%d done=%d"
                    " splitbrain=%s diverge=%d pressure=%d\n",
                    w->time, mode_to_string(w->mode), w->id ? w->id : "X", t->name,
                    t->safety_class ? t->safety_class : "unknown",
                    t->remaining_energy, t->deadline, t->done,
                    scheduler_used, splitbrain_diverge, pressure_pct);
        }
    } else {
        /* No runnable task — GC happens during idle. */
        if (w->pressure) {
            pressure_gc(w->pressure, 1024);
            pressure_pct = (w->pressure->used * 100) / (w->pressure->max > 0 ? w->pressure->max : 1);
        }
        printf("tick %d [%s] org=%s: idle  pressure=%d%% (gc)\n",
               w->time, mode_to_string(w->mode), w->id ? w->id : "X", pressure_pct);
        if (logf) {
            fprintf(logf,
                    "tick=%d mode=%s org=%s idle=1 pressure=%d gc=1\n",
                    w->time, mode_to_string(w->mode), w->id ? w->id : "X", pressure_pct);
        }
    }

    w->time += 1;
}

static void print_summary(const SimWorld *w) {
    int i;
    int pressure_pct = 0;
    if (w->pressure) {
        pressure_pct = (w->pressure->used * 100) / (w->pressure->max > 0 ? w->pressure->max : 1);
    }
    printf("\nfinal summary at time %d (mode=%s, pressure=%d%%):\n",
           w->time, mode_to_string(w->mode), pressure_pct);
    for (i = 0; i < w->task_count; i++) {
        const SimTask *t = &w->tasks[i];
        printf("  - %-12s  class=%-11s  done=%d  remaining_energy=%d  deadline=%d\n",
               t->name,
               t->safety_class ? t->safety_class : "unknown",
               t->done, t->remaining_energy, t->deadline);
    }
}

int main(int argc, char **argv) {
    SimTask tasksA[3];
    SimTask tasksB[3];
    SimTask tasksC[3];
    SimWorld worlds[3];
    SimPressure pressure; /* shared pressure across all organisms */
    int max_ticks = 30;   /* extended to 30 to show pressure effects */
    int i;

    /* Initialize shared pressure: simulated 8 MB arena, starts empty. */
    pressure.used  = 0;
    pressure.max   = 8 * 1024 * 1024; /* 8 MB simulated */
    pressure.level = PRESSURE_CALM;

    /* Initialize three organisms with slightly different energy/deadline profiles. */
    tasksA[0].name = "baseline";
    tasksA[0].safety_class = "normal";
    tasksA[0].remaining_energy = 5;
    tasksA[0].deadline = 8;
    tasksA[0].done = 0;
    tasksA[1].name = "urgent_fix";
    tasksA[1].safety_class = "recovery";
    tasksA[1].remaining_energy = 3;
    tasksA[1].deadline = 4;
    tasksA[1].done = 0;
    tasksA[2].name = "long_term";
    tasksA[2].safety_class = "experimental";
    tasksA[2].remaining_energy = 10;
    tasksA[2].deadline = 20;
    tasksA[2].done = 0;

    tasksB[0] = tasksA[0];
    tasksB[1] = tasksA[1];
    tasksB[2] = tasksA[2];
    tasksB[0].remaining_energy = 7;  /* organism B has more baseline work */

    tasksC[0] = tasksA[0];
    tasksC[1] = tasksA[1];
    tasksC[2] = tasksA[2];
    tasksC[2].remaining_energy = 6;  /* organism C is less experimental-heavy */

    SimMode base_mode = SIM_MODE_NORMAL;
    if (argc >= 2) {
        if (strcmp(argv[1], "--help") == 0 || strcmp(argv[1], "-h") == 0) {
            usage(argv[0]);
            return EXIT_SUCCESS;
        }
        if (strcmp(argv[1], "safe") == 0) {
            base_mode = SIM_MODE_SAFE;
        } else if (strcmp(argv[1], "degraded") == 0) {
            base_mode = SIM_MODE_DEGRADED;
        } else if (strcmp(argv[1], "normal") == 0) {
            base_mode = SIM_MODE_NORMAL;
        } else {
            fprintf(stderr, "error: unknown mode '%s'\n\n", argv[1]);
            usage(argv[0]);
            return EXIT_FAILURE;
        }
    }

    world_init(&worlds[0], "A", tasksA, 3, base_mode, &pressure);
    world_init(&worlds[1], "B", tasksB, 3, base_mode, &pressure);
    world_init(&worlds[2], "C", tasksC, 3, base_mode, &pressure);

    printf("oo-sim: multi-organism world v2 (base_mode=%s, splitbrain+pressure enabled)\n", mode_to_string(base_mode));
    for (int w = 0; w < 3; w++) {
        SimWorld *sw = &worlds[w];
        printf("organism %s tasks:\n", sw->id ? sw->id : "X");
        for (i = 0; i < sw->task_count; i++) {
            printf("  - %-12s  class=%-11s  energy=%d  deadline=%d\n",
                   sw->tasks[i].name,
                   sw->tasks[i].safety_class ? sw->tasks[i].safety_class : "unknown",
                   sw->tasks[i].remaining_energy, sw->tasks[i].deadline);
        }
        printf("\n");
    }

    for (i = 0; i < max_ticks; i++) {
        int all_done = 1;
        for (int w = 0; w < 3; w++) {
            SimWorld *sw = &worlds[w];
            if (!world_all_done(sw)) {
                world_tick(sw);
                all_done = 0;
            }
        }
        if (all_done) {
            printf("all organisms done by global tick %d\n", i);
            break;
        }
    }

    for (int w = 0; w < 3; w++) {
        print_summary(&worlds[w]);
    }

    return EXIT_SUCCESS;
}

