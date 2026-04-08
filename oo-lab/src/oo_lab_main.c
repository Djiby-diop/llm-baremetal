#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*
 * oo-lab: minimal C entry point for Operating Organism lab tooling.
 *
 * This is intentionally small. The idea is to grow a set of tiny,
 * testable helpers for:
 * - Reading and printing OO journals (OOJOUR.LOG, OOOUTCOME.LOG, etc.).
 * - Reading JSON / JSONL artifacts from oo-host.
 * - Comparing sovereign and host side views.
 *
 * v0 behaviour:
 *   oo-lab <path-to-log>
 *     - prints a short banner
 *     - prints the last N lines (default 40) of the given text file
 *
 * v0.1 behaviour:
 *   oo-lab --stats <path-to-log>
 *     - prints a short banner
 *     - prints a few basic statistics about the log
 */

static void print_banner(void) {
    puts("oo-lab: Operating Organism Lab (C) v0.1");
}

static void usage(const char *prog) {
    fprintf(stderr, "Usage:\n");
    fprintf(stderr, "  %s <log_file_path>            # print last 40 lines (default OOJOUR.LOG)\n", prog ? prog : "oo-lab");
    fprintf(stderr, "  %s --stats <log_file_path>    # print basic statistics\n", prog ? prog : "oo-lab");
    fprintf(stderr, "  %s --summary <log_file_path>  # summarize OOJOUR-style events\n", prog ? prog : "oo-lab");
    fprintf(stderr, "  %s --grep <token> <log>       # print only lines containing token\n", prog ? prog : "oo-lab");
    fprintf(stderr, "  %s --sim-summary              # summarize OOSIM.LOG (mode/task ticks)\n", prog ? prog : "oo-lab");
    fprintf(stderr, "  %s --train-summary <jsonl_path>   # summarize OO_TRAIN.JSONL training samples\n", prog ? prog : "oo-lab");
    fprintf(stderr, "  %s --kvc-meta <path>               # show OO_KVC.META (KV cache snapshot info)\n", prog ? prog : "oo-lab");
    fprintf(stderr, "  %s --dna-delta <path>              # show OO_DNA.DELTA (hardware drift)\n", prog ? prog : "oo-lab");
    fprintf(stderr, "  %s --bench <log_a> [<log_b>]      # micro-benchmark: Mamba vs llama2 (or single engine)\n", prog ? prog : "oo-lab");
    fprintf(stderr, "    --label-a <name>                 #   label for engine A (default: mamba)\n");
    fprintf(stderr, "    --label-b <name>                 #   label for engine B (default: llama2)\n");
    fprintf(stderr, "    --export-json <path>             #   write bench summary JSON (for host feedback)\n");
    fprintf(stderr, "  %s --profile                  # print a small runtime profile of this build\n", prog ? prog : "oo-lab");
}

static int tail_file(const char *path, int max_lines) {
    if (!path) {
        return -1;
    }

    FILE *f = fopen(path, "rb");
    if (!f) {
        fprintf(stderr, "error: could not open '%s'\n", path);
        return -1;
    }

    int c;
    long total_lines = 0;
    while ((c = fgetc(f)) != EOF) {
        if (c == '\n') {
            total_lines++;
        }
    }

    long start_line = 0;
    if (total_lines > max_lines) {
        start_line = total_lines - max_lines;
    }

    if (fseek(f, 0, SEEK_SET) != 0) {
        fclose(f);
        fprintf(stderr, "error: seek failed on '%s'\n", path);
        return -1;
    }

    long current_line = 0;
    while ((c = fgetc(f)) != EOF) {
        if (current_line >= start_line) {
            if (putchar(c) == EOF) {
                fclose(f);
                return -1;
            }
        }
        if (c == '\n') {
            current_line++;
        }
    }

    fclose(f);
    return 0;
}

static int stats_file(const char *path) {
    if (!path) {
        return -1;
    }

    FILE *f = fopen(path, "rb");
    if (!f) {
        fprintf(stderr, "error: could not open '%s'\n", path);
        return -1;
    }

    long lines = 0;
    long bytes = 0;
    int c;

    while ((c = fgetc(f)) != EOF) {
        bytes++;
        if (c == '\n') {
            lines++;
        }
    }

    if (fseek(f, 0, SEEK_SET) != 0) {
        fclose(f);
        fprintf(stderr, "error: seek failed on '%s'\n", path);
        return -1;
    }

    /* Print a small preview of the first line, often an event header. */
    char first_line[256];
    if (fgets(first_line, (int)sizeof(first_line), f) != NULL) {
        size_t len = strlen(first_line);
        if (len > 0 && (first_line[len - 1] == '\n' || first_line[len - 1] == '\r')) {
            first_line[len - 1] = '\0';
        }
        printf("first_line: %s\n", first_line);
    }

    fclose(f);

    printf("bytes: %ld\n", bytes);
    printf("lines: %ld\n", lines);

    return 0;
}

static int summary_file(const char *path) {
    if (!path) {
        return -1;
    }

    FILE *f = fopen(path, "rb");
    if (!f) {
        fprintf(stderr, "error: could not open '%s'\n", path);
        return -1;
    }

    long total = 0;
    long boot = 0;
    long recover = 0;
    long init = 0;
    long metric = 0;
    long last_boot_count = -1;
    char last_mode[32];
    last_mode[0] = '\0';

    char line[256];
    while (fgets(line, (int)sizeof(line), f) != NULL) {
        total++;
        /* very simple substring checks; OOJOUR.LOG lines include event tags like "boot", "recover", "init" */
        if (strstr(line, "boot") != NULL) {
            boot++;
        }
        if (strstr(line, "recover") != NULL) {
            recover++;
        }
        if (strstr(line, "init") != NULL) {
            init++;
        }
        if (strstr(line, "consult_metric") != NULL) {
            metric++;
        }

        /* Very small parser for fragments like "boot_count=<n>" and "mode=<NAME>". */
        {
            const char *p = strstr(line, "boot_count=");
            if (p != NULL) {
                p += (int)strlen("boot_count=");
                char *endptr = NULL;
                long v = strtol(p, &endptr, 10);
                if (endptr != p) {
                    last_boot_count = v;
                }
            }
        }

        {
            const char *p = strstr(line, "mode=");
            if (p != NULL) {
                p += (int)strlen("mode=");
                size_t i = 0;
                while (*p != '\0' && *p != ' ' && *p != '\t' && *p != '\r' && *p != '\n' && i + 1 < sizeof(last_mode)) {
                    last_mode[i++] = *p++;
                }
                last_mode[i] = '\0';
            }
        }
    }

    fclose(f);

    printf("events_total: %ld\n", total);
    printf("events_boot: %ld\n", boot);
    printf("events_recover: %ld\n", recover);
    printf("events_init: %ld\n", init);
    printf("events_consult_metric: %ld\n", metric);
    if (last_boot_count >= 0) {
        printf("last_boot_count: %ld\n", last_boot_count);
    }
    if (last_mode[0] != '\0') {
        printf("last_mode: %s\n", last_mode);
    }

    return 0;
}

static int grep_file(const char *token, const char *path) {
    if (!token || !path) {
        return -1;
    }

    FILE *f = fopen(path, "rb");
    if (!f) {
        fprintf(stderr, "error: could not open '%s'\n", path);
        return -1;
    }

    size_t token_len = strlen(token);
    if (token_len == 0) {
        fclose(f);
        fprintf(stderr, "error: empty token\n");
        return -1;
    }

    char line[512];
    while (fgets(line, (int)sizeof(line), f) != NULL) {
        if (strstr(line, token) != NULL) {
            fputs(line, stdout);
        }
    }

    fclose(f);
    return 0;
}

static int sim_summary(void) {
    FILE *f = fopen("OOSIM.LOG", "rb");
    if (!f) {
        fprintf(stderr, "error: could not open '%s'\n", "OOSIM.LOG");
        return -1;
    }

    long safe_ticks = 0;
    long degraded_ticks = 0;
    long normal_ticks = 0;

    /* very small per-task counters for the built-in tasks */
    long baseline_ticks = 0;
    long urgent_ticks = 0;
    long longterm_ticks = 0;

    /* per-organism tick counters */
    long orgA_ticks = 0;
    long orgB_ticks = 0;
    long orgC_ticks = 0;

    char line[256];
    while (fgets(line, (int)sizeof(line), f) != NULL) {
        if (strstr(line, "mode=SAFE") != NULL) {
            safe_ticks++;
        } else if (strstr(line, "mode=DEGRADED") != NULL) {
            degraded_ticks++;
        } else if (strstr(line, "mode=NORMAL") != NULL) {
            normal_ticks++;
        }

        if (strstr(line, "task=baseline") != NULL) {
            baseline_ticks++;
        } else if (strstr(line, "task=urgent_fix") != NULL) {
            urgent_ticks++;
        } else if (strstr(line, "task=long_term") != NULL) {
            longterm_ticks++;
        }

        if (strstr(line, "org=A") != NULL) {
            orgA_ticks++;
        } else if (strstr(line, "org=B") != NULL) {
            orgB_ticks++;
        } else if (strstr(line, "org=C") != NULL) {
            orgC_ticks++;
        }
    }

    fclose(f);

    printf("OOSIM.LOG summary:\n");
    printf("  mode SAFE     ticks: %ld\n", safe_ticks);
    printf("  mode DEGRADED ticks: %ld\n", degraded_ticks);
    printf("  mode NORMAL   ticks: %ld\n", normal_ticks);
    printf("  task baseline   ticks: %ld\n", baseline_ticks);
    printf("  task urgent_fix ticks: %ld\n", urgent_ticks);
    printf("  task long_term  ticks: %ld\n", longterm_ticks);
    printf("  organism A ticks: %ld\n", orgA_ticks);
    printf("  organism B ticks: %ld\n", orgB_ticks);
    printf("  organism C ticks: %ld\n", orgC_ticks);

    return 0;
}

/* ============================================================
 * --train-summary <path>
 * Parse OO_TRAIN.JSONL produced by the firmware journal_train module.
 * Each line is a JSON object: {"instruction":...,"response":...,"meta":{...}}
 * We extract: sample count, quality distribution, pressure levels, phases.
 * ============================================================ */
static int train_summary(const char *path) {
    if (!path) return -1;

    FILE *f = fopen(path, "rb");
    if (!f) {
        fprintf(stderr, "error: could not open '%s'\n", path);
        return -1;
    }

    long total = 0;
    long quality_buckets[11]; /* 0-10 */
    long pressure_buckets[5]; /* 0=CALM 1=AWARE 2=STRESSED 3=CRITICAL 4=DYING */
    long phase_buckets[3];    /* 0=NORMAL 1=DEGRADED 2=SAFE */
    long diverged = 0;

    for (int i = 0; i <= 10; i++) quality_buckets[i] = 0;
    for (int i = 0; i < 5;  i++) pressure_buckets[i] = 0;
    for (int i = 0; i < 3;  i++) phase_buckets[i] = 0;

    char line[4096];
    while (fgets(line, (int)sizeof(line), f) != NULL) {
        if (line[0] != '{') continue; /* skip blank lines */
        total++;

        /* Extract quality */
        const char *qp = strstr(line, "\"quality\":");
        if (qp) {
            qp += strlen("\"quality\":");
            while (*qp == ' ') qp++;
            long q = strtol(qp, NULL, 10);
            if (q >= 0 && q <= 10) quality_buckets[q]++;
        }

        /* Extract pressure */
        const char *pp = strstr(line, "\"pressure\":");
        if (pp) {
            pp += strlen("\"pressure\":");
            while (*pp == ' ') pp++;
            long p = strtol(pp, NULL, 10);
            if (p >= 0 && p < 5) pressure_buckets[p]++;
        }

        /* Extract phase */
        const char *ph = strstr(line, "\"phase\":");
        if (ph) {
            ph += strlen("\"phase\":");
            while (*ph == ' ') ph++;
            long pv = strtol(ph, NULL, 10);
            if (pv >= 0 && pv < 3) phase_buckets[pv]++;
        }

        /* Extract diverged */
        const char *dp = strstr(line, "\"diverged\":1");
        if (dp) diverged++;
    }

    fclose(f);

    static const char *pressure_names[] = { "CALM", "AWARE", "STRESSED", "CRITICAL", "DYING" };
    static const char *phase_names[]    = { "NORMAL", "DEGRADED", "SAFE" };

    printf("OO_TRAIN.JSONL summary:\n");
    printf("  total_samples    : %ld\n", total);
    printf("  splitbrain_diverged: %ld (%.1f%%)\n",
           diverged, total > 0 ? (double)diverged * 100.0 / (double)total : 0.0);

    printf("\n  quality distribution (0-10):\n");
    for (int i = 10; i >= 0; i--) {
        if (quality_buckets[i] == 0) continue;
        printf("    [%2d] %ld (%.1f%%)\n", i, quality_buckets[i],
               total > 0 ? (double)quality_buckets[i] * 100.0 / (double)total : 0.0);
    }

    printf("\n  pressure at generation:\n");
    for (int i = 0; i < 5; i++) {
        if (pressure_buckets[i] == 0) continue;
        printf("    %-10s %ld (%.1f%%)\n", pressure_names[i], pressure_buckets[i],
               total > 0 ? (double)pressure_buckets[i] * 100.0 / (double)total : 0.0);
    }

    printf("\n  phase at generation:\n");
    for (int i = 0; i < 3; i++) {
        if (phase_buckets[i] == 0) continue;
        printf("    %-10s %ld (%.1f%%)\n", phase_names[i], phase_buckets[i],
               total > 0 ? (double)phase_buckets[i] * 100.0 / (double)total : 0.0);
    }

    return 0;
}

/* ============================================================
 * --kvc-meta <path>
 * Parse OO_KVC.META produced by kv_persist module.
 * Format: key=value per line.
 * ============================================================ */
static int kvc_meta(const char *path) {
    if (!path) return -1;

    FILE *f = fopen(path, "rb");
    if (!f) {
        fprintf(stderr, "error: could not open '%s'\n", path);
        return -1;
    }

    char line[256];
    printf("OO_KVC.META:\n");
    while (fgets(line, (int)sizeof(line), f) != NULL) {
        /* Strip trailing newline */
        size_t len = strlen(line);
        while (len > 0 && (line[len-1] == '\n' || line[len-1] == '\r')) {
            line[--len] = '\0';
        }
        if (len == 0) continue;
        printf("  %s\n", line);
    }

    fclose(f);
    return 0;
}

/* ============================================================
 * --dna-delta <path>
 * Parse OO_DNA.DELTA produced by hw_dna module.
 * Reports hardware drift events.
 * ============================================================ */
static int dna_delta(const char *path) {
    if (!path) return -1;

    FILE *f = fopen(path, "rb");
    if (!f) {
        fprintf(stderr, "error: could not open '%s'\n", path);
        return -1;
    }

    char line[256];
    int changed = 0;
    char ram_drift[64] = "";

    printf("OO_DNA.DELTA:\n");
    while (fgets(line, (int)sizeof(line), f) != NULL) {
        size_t len = strlen(line);
        while (len > 0 && (line[len-1] == '\n' || line[len-1] == '\r')) {
            line[--len] = '\0';
        }
        if (len == 0) continue;
        printf("  %s\n", line);

        if (strstr(line, "FINGERPRINT_CHANGED yes") != NULL) changed = 1;
        if (strstr(line, "RAM_DRIFT") != NULL) {
            strncpy(ram_drift, line, sizeof(ram_drift) - 1);
        }
    }
    fclose(f);

    printf("\n  Assessment:\n");
    if (changed) {
        printf("  *** ALERT: CPU fingerprint changed - possible hardware migration ***\n");
        printf("  *** D+ policy may DENY this hardware on next boot. ***\n");
    } else if (ram_drift[0] != '\0') {
        printf("  RAM drift detected (%s) - CPU identity stable.\n", ram_drift);
        printf("  D+ policy: ALLOW with AUDIT.\n");
    } else {
        printf("  No significant hardware drift.\n");
    }

    return 0;
}

/* ============================================================
 * --bench <log_a> [<log_b>]
 *
 * Micro-benchmark: compare two OOSIM.LOG files (engine A vs B).
 * If only one log is given, prints metrics for that log.
 *
 * Metrics extracted from OOSIM.LOG format:
 *   tick=N mode=M org=X task=T class=C energy=N deadline=N done=N
 *   splitbrain=rational|creative|both diverge=0|1 pressure=N
 *   tick=N mode=M org=X idle=1 pressure=N gc=1
 *   tick=N mode_change org=X old=M new=M pressure=P
 *
 * Scores (0-100):
 *   completion_efficiency (30 pts): non-idle ticks / total ticks
 *   experimental_success  (25 pts): experimental tasks done / total experimental
 *   stability             (20 pts): fraction of ticks in NORMAL mode
 *   low_pressure          (15 pts): 1 - (peak_pressure / 100)
 *   creativity            (10 pts): diverge_rate (splitbrain events), capped
 * ============================================================ */

typedef struct {
    char  label[64];
    long  total_ticks;
    long  normal_ticks;
    long  degraded_ticks;
    long  safe_ticks;
    long  idle_ticks;
    long  gc_ticks;
    long  diverge_ticks;        /* ticks with diverge=1 */
    long  mode_change_count;
    long  experimental_total;   /* task ticks with class=experimental */
    long  experimental_done;    /* task ticks with class=experimental done=1 */
    long  normal_task_total;
    long  normal_task_done;
    long  recovery_task_total;
    long  recovery_task_done;
    long  pressure_max;         /* highest pressure% seen */
    long  pressure_sum;         /* for average */
    long  pressure_samples;
} BenchMetrics;

static long parse_kv_long(const char *line, const char *key) {
    const char *p = strstr(line, key);
    if (!p) return -1;
    p += strlen(key);
    if (*p != '=') return -1;
    p++;
    char *end = NULL;
    long v = strtol(p, &end, 10);
    return (end != p) ? v : -1;
}

static int bench_parse(const char *path, BenchMetrics *m) {
    if (!path || !m) return -1;
    FILE *f = fopen(path, "rb");
    if (!f) {
        fprintf(stderr, "error: could not open '%s'\n", path);
        return -1;
    }

    char line[512];
    while (fgets(line, (int)sizeof(line), f) != NULL) {
        /* mode_change lines */
        if (strstr(line, "mode_change") != NULL) {
            m->mode_change_count++;
            continue;
        }

        /* idle ticks */
        if (strstr(line, "idle=1") != NULL) {
            m->idle_ticks++;
            m->total_ticks++;
            if (strstr(line, "gc=1") != NULL) m->gc_ticks++;

            long pval = parse_kv_long(line, "pressure");
            if (pval >= 0) {
                if (pval > m->pressure_max) m->pressure_max = pval;
                m->pressure_sum += pval;
                m->pressure_samples++;
            }
            /* mode for idle */
            if (strstr(line, "mode=NORMAL")   != NULL) m->normal_ticks++;
            else if (strstr(line, "mode=DEGRADED") != NULL) m->degraded_ticks++;
            else if (strstr(line, "mode=SAFE")     != NULL) m->safe_ticks++;
            continue;
        }

        /* task ticks: must have task= */
        if (strstr(line, "task=") == NULL) continue;

        m->total_ticks++;

        /* mode */
        if (strstr(line, "mode=NORMAL")   != NULL) m->normal_ticks++;
        else if (strstr(line, "mode=DEGRADED") != NULL) m->degraded_ticks++;
        else if (strstr(line, "mode=SAFE")     != NULL) m->safe_ticks++;

        /* diverge */
        if (strstr(line, "diverge=1") != NULL) m->diverge_ticks++;

        /* pressure */
        long pval = parse_kv_long(line, "pressure");
        if (pval >= 0) {
            if (pval > m->pressure_max) m->pressure_max = pval;
            m->pressure_sum += pval;
            m->pressure_samples++;
        }

        /* class + done */
        long done = parse_kv_long(line, "done");
        if (strstr(line, "class=experimental") != NULL) {
            m->experimental_total++;
            if (done == 1) m->experimental_done++;
        } else if (strstr(line, "class=recovery") != NULL) {
            m->recovery_task_total++;
            if (done == 1) m->recovery_task_done++;
        } else if (strstr(line, "class=normal") != NULL) {
            m->normal_task_total++;
            if (done == 1) m->normal_task_done++;
        }
    }

    fclose(f);
    return 0;
}

/* Score breakdown (0-100). */
static double bench_score(const BenchMetrics *m,
                           double *s_efficiency,
                           double *s_experimental,
                           double *s_stability,
                           double *s_pressure,
                           double *s_creativity) {
    double eff  = 0.0, exp_s = 0.0, stab = 0.0, pres = 0.0, crea = 0.0;

    if (m->total_ticks > 0) {
        long active = m->total_ticks - m->idle_ticks;
        eff  = (double)active / (double)m->total_ticks * 30.0;
        stab = (double)m->normal_ticks / (double)m->total_ticks * 20.0;
        crea = (double)m->diverge_ticks / (double)m->total_ticks * 200.0; /* *2 then capped */
        if (crea > 10.0) crea = 10.0;
    }

    if (m->experimental_total > 0) {
        exp_s = (double)m->experimental_done / (double)m->experimental_total * 25.0;
    } else {
        exp_s = 12.5; /* neutral if no experimental tasks */
    }

    if (m->pressure_max >= 0) {
        pres = (1.0 - (double)m->pressure_max / 100.0) * 15.0;
        if (pres < 0.0) pres = 0.0;
    } else {
        pres = 15.0;
    }

    if (s_efficiency)   *s_efficiency   = eff;
    if (s_experimental) *s_experimental = exp_s;
    if (s_stability)    *s_stability    = stab;
    if (s_pressure)     *s_pressure     = pres;
    if (s_creativity)   *s_creativity   = crea;

    return eff + exp_s + stab + pres + crea;
}

static void bench_print_metrics(const BenchMetrics *m) {
    long avg_p = (m->pressure_samples > 0) ? m->pressure_sum / m->pressure_samples : 0;
    long exp_rate = (m->experimental_total > 0)
        ? (m->experimental_done * 100 / m->experimental_total) : -1;

    printf("  total_ticks          : %ld\n",   m->total_ticks);
    printf("  active_ticks         : %ld  (idle=%ld gc=%ld)\n",
           m->total_ticks - m->idle_ticks, m->idle_ticks, m->gc_ticks);
    printf("  mode NORMAL          : %ld ticks\n", m->normal_ticks);
    printf("  mode DEGRADED        : %ld ticks\n", m->degraded_ticks);
    printf("  mode SAFE            : %ld ticks\n", m->safe_ticks);
    printf("  mode_change_count    : %ld\n",   m->mode_change_count);
    printf("  diverge_ticks        : %ld\n",   m->diverge_ticks);
    printf("  pressure_peak        : %ld%%\n", m->pressure_max);
    printf("  pressure_avg         : %ld%%\n", avg_p);
    printf("  experimental tasks   : %ld total  %ld done",
           m->experimental_total, m->experimental_done);
    if (exp_rate >= 0) printf("  (%ld%%)", exp_rate);
    printf("\n");
    printf("  recovery tasks       : %ld total  %ld done\n",
           m->recovery_task_total, m->recovery_task_done);
    printf("  normal tasks         : %ld total  %ld done\n",
           m->normal_task_total, m->normal_task_done);
}

static void bench_print_score(const BenchMetrics *m) {
    double eff, exp_s, stab, pres, crea;
    double total = bench_score(m, &eff, &exp_s, &stab, &pres, &crea);
    printf("  score breakdown:\n");
    printf("    completion_efficiency  : %5.1f / 30\n",  eff);
    printf("    experimental_success   : %5.1f / 25\n",  exp_s);
    printf("    stability              : %5.1f / 20\n",  stab);
    printf("    low_pressure           : %5.1f / 15\n",  pres);
    printf("    creativity (diverge)   : %5.1f / 10\n",  crea);
    printf("    ---------------------------------\n");
    printf("    TOTAL                  : %5.1f / 100\n", total);
}

static int bench_export_json(const char *out_path,
                              const char *label_a, double score_a,
                              double ea, double exp_a, double sa, double pa, double ca,
                              const BenchMetrics *ma,
                              const char *label_b, double score_b,
                              double eb, double exp_b, double sb, double pb, double cb,
                              const BenchMetrics *mb) {
    FILE *f = fopen(out_path, "w");
    if (!f) {
        fprintf(stderr, "error: cannot write JSON to %s\n", out_path);
        return -1;
    }

    /* Determine policy hint from winner's score */
    const char *winner = (score_a >= score_b) ? label_a : label_b;
    double winner_score = (score_a >= score_b) ? score_a : score_b;
    const char *policy_hint = "cautious";
    if (winner_score >= 70.0) policy_hint = "stable";
    else if (winner_score >= 50.0) policy_hint = "cautious";
    else policy_hint = "degraded";

    long active_a = ma->total_ticks - ma->idle_ticks;
    long active_b = (mb && mb->total_ticks > 0) ? mb->total_ticks - mb->idle_ticks : 0;

    fprintf(f, "{\n");
    fprintf(f, "  \"version\": 1,\n");
    fprintf(f, "  \"winner\": \"%s\",\n", winner);
    fprintf(f, "  \"policy_hint\": \"%s\",\n", policy_hint);
    fprintf(f, "  \"engines\": [\n");

    /* Engine A */
    fprintf(f, "    {\n");
    fprintf(f, "      \"label\": \"%s\",\n", label_a);
    fprintf(f, "      \"score\": %.1f,\n", score_a);
    fprintf(f, "      \"breakdown\": {\n");
    fprintf(f, "        \"completion_efficiency\": %.1f,\n", ea);
    fprintf(f, "        \"experimental_success\": %.1f,\n", exp_a);
    fprintf(f, "        \"stability\": %.1f,\n", sa);
    fprintf(f, "        \"low_pressure\": %.1f,\n", pa);
    fprintf(f, "        \"creativity\": %.1f\n", ca);
    fprintf(f, "      },\n");
    fprintf(f, "      \"metrics\": {\n");
    fprintf(f, "        \"total_ticks\": %ld,\n", ma->total_ticks);
    fprintf(f, "        \"active_ticks\": %ld,\n", active_a);
    fprintf(f, "        \"pressure_max\": %ld,\n", ma->pressure_max);
    fprintf(f, "        \"mode_changes\": %ld,\n", ma->mode_change_count);
    fprintf(f, "        \"experimental_done\": %ld\n", ma->experimental_done);
    fprintf(f, "      }\n");
    if (mb && mb->total_ticks > 0) {
        fprintf(f, "    },\n");
    } else {
        fprintf(f, "    }\n");
    }

    /* Engine B (optional) */
    if (mb && mb->total_ticks > 0) {
        fprintf(f, "    {\n");
        fprintf(f, "      \"label\": \"%s\",\n", label_b);
        fprintf(f, "      \"score\": %.1f,\n", score_b);
        fprintf(f, "      \"breakdown\": {\n");
        fprintf(f, "        \"completion_efficiency\": %.1f,\n", eb);
        fprintf(f, "        \"experimental_success\": %.1f,\n", exp_b);
        fprintf(f, "        \"stability\": %.1f,\n", sb);
        fprintf(f, "        \"low_pressure\": %.1f,\n", pb);
        fprintf(f, "        \"creativity\": %.1f\n", cb);
        fprintf(f, "      },\n");
        fprintf(f, "      \"metrics\": {\n");
        fprintf(f, "        \"total_ticks\": %ld,\n", mb->total_ticks);
        fprintf(f, "        \"active_ticks\": %ld,\n", active_b);
        fprintf(f, "        \"pressure_max\": %ld,\n", mb->pressure_max);
        fprintf(f, "        \"mode_changes\": %ld,\n", mb->mode_change_count);
        fprintf(f, "        \"experimental_done\": %ld\n", mb->experimental_done);
        fprintf(f, "      }\n");
        fprintf(f, "    }\n");
    }

    fprintf(f, "  ]\n");
    fprintf(f, "}\n");
    fclose(f);
    printf("  exported bench results: %s\n", out_path);
    return 0;
}

static int bench_compare(const char *path_a, const char *label_a,
                          const char *path_b, const char *label_b,
                          const char *json_out) {
    BenchMetrics ma, mb;
    memset(&ma, 0, sizeof(ma));
    memset(&mb, 0, sizeof(mb));
    ma.pressure_max = 0;
    mb.pressure_max = 0;

    strncpy(ma.label, label_a, sizeof(ma.label) - 1);
    strncpy(mb.label, label_b, sizeof(mb.label) - 1);

    if (bench_parse(path_a, &ma) != 0) return -1;

    int single = (path_b == NULL);
    if (!single && bench_parse(path_b, &mb) != 0) return -1;

    printf("\n");
    printf("+------------------------------------------------------+\n");
    printf("|            oo-lab: Engine Micro-Benchmark            |\n");
    printf("+------------------------------------------------------+\n\n");

    if (single) {
        printf("[ %s ] - %s\n\n", ma.label, path_a);
        bench_print_metrics(&ma);
        printf("\n");
        bench_print_score(&ma);
        return 0;
    }

    /* Two-engine comparison */
    double ea, exp_a, sa, pa, ca;
    double eb, exp_b, sb, pb, cb;
    double score_a = bench_score(&ma, &ea, &exp_a, &sa, &pa, &ca);
    double score_b = bench_score(&mb, &eb, &exp_b, &sb, &pb, &cb);

    printf("  %-28s  %-12s  %-12s  WINNER\n", "Metric", ma.label, mb.label);
    printf("  %s\n", "-----------------------------------------------------------------");

#define CMP_ROW(mname, va, vb, fmt, hi_wins) do { \
    int w = (hi_wins) ? ((va) >= (vb) ? 0 : 1) : ((va) <= (vb) ? 0 : 1); \
    printf("  %-28s  " fmt "        " fmt "        %s\n", \
           (mname), (va), (vb), w == 0 ? ma.label : mb.label); \
} while(0)

    long active_a = ma.total_ticks - ma.idle_ticks;
    long active_b = mb.total_ticks - mb.idle_ticks;
    CMP_ROW("total_ticks",         ma.total_ticks,   mb.total_ticks,   "%-12ld", 0);
    CMP_ROW("active_ticks",        active_a,          active_b,         "%-12ld", 1);
    CMP_ROW("idle_ticks",          ma.idle_ticks,    mb.idle_ticks,    "%-12ld", 0);
    CMP_ROW("NORMAL mode ticks",   ma.normal_ticks,  mb.normal_ticks,  "%-12ld", 1);
    CMP_ROW("mode_changes",        ma.mode_change_count, mb.mode_change_count, "%-12ld", 0);
    CMP_ROW("diverge_ticks",       ma.diverge_ticks, mb.diverge_ticks, "%-12ld", 1);
    CMP_ROW("pressure_peak (%)",   ma.pressure_max,  mb.pressure_max,  "%-12ld", 0);
    CMP_ROW("experimental_done",   ma.experimental_done, mb.experimental_done, "%-12ld", 1);

    printf("  %s\n", "-----------------------------------------------------------------");
    printf("  %-28s  %-12.1f  %-12.1f  %s\n", "SCORE / 100",
           score_a, score_b, score_a >= score_b ? ma.label : mb.label);

    printf("\n  Score breakdown:\n");
    printf("  %-28s  %-12s  %-12s\n", "", ma.label, mb.label);
    printf("  %-28s  %-12.1f  %-12.1f\n", "completion_efficiency /30", ea, eb);
    printf("  %-28s  %-12.1f  %-12.1f\n", "experimental_success  /25", exp_a, exp_b);
    printf("  %-28s  %-12.1f  %-12.1f\n", "stability             /20", sa, sb);
    printf("  %-28s  %-12.1f  %-12.1f\n", "low_pressure          /15", pa, pb);
    printf("  %-28s  %-12.1f  %-12.1f\n", "creativity (diverge)  /10", ca, cb);

    printf("\n  Verdict: ");
    if (score_a > score_b + 5.0) {
        printf("%s wins by %.1f pts - more efficient and stable.\n", ma.label, score_a - score_b);
    } else if (score_b > score_a + 5.0) {
        printf("%s wins by %.1f pts - more efficient and stable.\n", mb.label, score_b - score_a);
    } else {
        printf("DRAW (%.1f vs %.1f) - both engines within 5 pts.\n", score_a, score_b);
    }

    if (json_out) {
        bench_export_json(json_out,
            label_a, score_a, ea, exp_a, sa, pa, ca, &ma,
            label_b, score_b, eb, exp_b, sb, pb, cb, &mb);
    }

    return 0;
}


static int oo_profile(void) {
    FILE *f = fopen("OOJOUR.LOG", "rb");
    if (!f) {
        fprintf(stderr, "error: could not open '%s'\n", "OOJOUR.LOG");
        return -1;
    }

    long boots = 0;
    long recovers = 0;
    long inits = 0;
    long mode_safe = 0;
    long mode_degraded = 0;
    long mode_normal = 0;

    char line[256];
    while (fgets(line, (int)sizeof(line), f) != NULL) {
        if (strstr(line, "boot") != NULL) {
            boots++;
        }
        if (strstr(line, "recover") != NULL) {
            recovers++;
        }
        if (strstr(line, "init") != NULL) {
            inits++;
        }
        if (strstr(line, "mode=SAFE") != NULL) {
            mode_safe++;
        } else if (strstr(line, "mode=DEGRADED") != NULL) {
            mode_degraded++;
        } else if (strstr(line, "mode=NORMAL") != NULL) {
            mode_normal++;
        }
    }

    fclose(f);

    long total_mode = mode_safe + mode_degraded + mode_normal;
    printf("OOJOUR.LOG profile:\n");
    printf("  boots=%ld recovers=%ld inits=%ld\n", boots, recovers, inits);
    if (total_mode > 0) {
        double ps = (double)mode_safe * 100.0 / (double)total_mode;
        double pd = (double)mode_degraded * 100.0 / (double)total_mode;
        double pn = (double)mode_normal * 100.0 / (double)total_mode;
        printf("  mode SAFE     lines: %ld (%.1f%%)\n", mode_safe, ps);
        printf("  mode DEGRADED lines: %ld (%.1f%%)\n", mode_degraded, pd);
        printf("  mode NORMAL   lines: %ld (%.1f%%)\n", mode_normal, pn);
        printf("\n");
        printf("  short personality sketch:\n");
        if (ps > pn && ps > pd) {
            printf("    - organism tends to stay in SAFE; conservative / recovery-driven.\n");
        } else if (pn >= ps && pn >= pd) {
            printf("    - organism spends most of its time in NORMAL; comfortable exploring.\n");
        } else {
            printf("    - organism lives often in DEGRADED; under pressure but still operating.\n");
        }
    } else {
        printf("  no mode markers found in OOJOUR.LOG\n");
    }

    return 0;
}

int main(int argc, char **argv) {
    print_banner();

    const int max_lines = 40;

    if (argc >= 2 && (strcmp(argv[1], "--help") == 0 || strcmp(argv[1], "-h") == 0)) {
        usage(argv[0]);
        return EXIT_SUCCESS;
    }

    if (argc >= 3) {
        if (strcmp(argv[1], "--stats") == 0) {
            const char *path = argv[2];
            if (stats_file(path) != 0) {
                return EXIT_FAILURE;
            }
            return EXIT_SUCCESS;
        } else if (strcmp(argv[1], "--summary") == 0) {
            const char *path = argv[2];
            if (summary_file(path) != 0) {
                return EXIT_FAILURE;
            }
            return EXIT_SUCCESS;
        } else if (strcmp(argv[1], "--grep") == 0 && argc >= 4) {
            const char *token = argv[2];
            const char *path = argv[3];
            if (grep_file(token, path) != 0) {
                return EXIT_FAILURE;
            }
            return EXIT_SUCCESS;
        } else if (strcmp(argv[1], "--train-summary") == 0) {
            if (train_summary(argv[2]) != 0) {
                return EXIT_FAILURE;
            }
            return EXIT_SUCCESS;
        } else if (strcmp(argv[1], "--kvc-meta") == 0) {
            if (kvc_meta(argv[2]) != 0) {
                return EXIT_FAILURE;
            }
            return EXIT_SUCCESS;
        } else if (strcmp(argv[1], "--dna-delta") == 0) {
            if (dna_delta(argv[2]) != 0) {
                return EXIT_FAILURE;
            }
            return EXIT_SUCCESS;
        } else if (strcmp(argv[1], "--bench") == 0) {
            const char *log_a    = argv[2];
            const char *log_b    = NULL;
            const char *label_a  = "mamba";
            const char *label_b  = "llama2";
            const char *json_out = NULL;
            int i;
            for (i = 3; i < argc; i++) {
                if (strcmp(argv[i], "--label-a") == 0 && i + 1 < argc) {
                    label_a = argv[++i];
                } else if (strcmp(argv[i], "--label-b") == 0 && i + 1 < argc) {
                    label_b = argv[++i];
                } else if (strcmp(argv[i], "--export-json") == 0 && i + 1 < argc) {
                    json_out = argv[++i];
                } else if (argv[i][0] != '-' && log_b == NULL) {
                    log_b = argv[i];
                }
            }
            if (bench_compare(log_a, label_a, log_b, label_b, json_out) != 0) {
                return EXIT_FAILURE;
            }
            return EXIT_SUCCESS;
        }
    } else if (argc >= 2 && argv[1][0] == '-' && strcmp(argv[1], "--sim-summary") != 0 && strcmp(argv[1], "--profile") != 0) {
        /* Unknown/incomplete flag usage.
         * Keep stdout clean but explain via usage.
         */
        fprintf(stderr, "error: invalid arguments\n\n");
        usage(argv[0]);
        return EXIT_FAILURE;
    } else if (argc == 2 && strcmp(argv[1], "--sim-summary") == 0) {
        if (sim_summary() != 0) {
            return EXIT_FAILURE;
        }
        return EXIT_SUCCESS;
    } else if (argc == 2 && strcmp(argv[1], "--profile") == 0) {
        if (oo_profile() != 0) {
            return EXIT_FAILURE;
        }
        return EXIT_SUCCESS;
    }

    const char *path = NULL;
    if (argc >= 2) {
        path = argv[1];
    } else {
        /* Best-effort default for quick use on collected firmware artifacts. */
        path = "OOJOUR.LOG";
        fprintf(stderr, "info: no log path provided, defaulting to '%s'\n", path);
    }

    if (tail_file(path, max_lines) != 0) {
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}

