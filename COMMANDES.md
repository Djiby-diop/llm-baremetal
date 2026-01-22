# REPL Commands (llm-baremetal)

This file is a “complete” cheat-sheet of the commands available in the UEFI REPL.

## Keyboard shortcuts

- **Enter**: submit the line (or the full prompt)
- **Backspace**: delete one character
- **Up / Down arrows**: command history (single-line only)
- **Tab**: auto-complete `/...` commands (press repeatedly to cycle matches, single-line only)

## Multi-line input

- End a line with `\` to continue on the next line.
- Type `;;` on a line by itself to **submit** the multi-line block.
- If you want a literal trailing backslash, end the line with `\\`.

## Sampling / generation

- `/temp <val>`: temperature (0.0=greedy, 1.0=creative)
- `/min_p <val>`: min_p (0.0–1.0, 0=off)
- `/top_p <val>`: nucleus sampling (0.0–1.0)
- `/top_k <int>`: top-k (0=off)
- `/norepeat <n>`: no-repeat ngram (0=off)
- `/repeat <val>`: repeat penalty (1.0=none)
- `/max_tokens <n>`: max generated tokens (1–256)
- `/seed <n>`: RNG seed
- `/stats <0|1>`: print generation stats
- `/stop_you <0|1>`: stop on the `\nYou:` pattern
- `/stop_nl <0|1>`: stop on double newline

## Info / debug

- `/version`: version + build + features
- `/ctx`: show model + sampling + budgets
- `/model`: loaded model info
- `/cpu`: SIMD status
- `/attn [auto|sse2|avx2]`: force the attention SIMD path
- `/zones`: dump allocator zones + sentinel
- `/budget [p] [d]`: budgets in cycles (prefill, decode)
- `/test_failsafe [prefill|decode|both] [cycles]`: one-shot strict_budget trip
- `/commands [filter]`: list commands (filter is case-insensitive substring; if it starts with `/` it's a prefix)
  - examples: `/commands dump` (matches `/save_dump`), `/commands /oo_`
- `/help [filter]`: help (same filtering rules)
  - examples: `/help save`, `/help /oo_`

## Logs / dumps

- `/log [n]`: print the last n log entries
- `/save_log [n]`: write the last n log entries to `llmk-log.txt`
- `/save_dump`: write ctx+zones+sentinel+log to `llmk-dump.txt`

## GOP / rendering

- `/gop`: GOP framebuffer info
- `/render <dsl>`: render simple shapes via DSL
- `/save_img [f]`: save GOP framebuffer as PPM (default `llmk-img.ppm`)
- `/draw <text>`: ask the model for DSL then execute `/render` (GOP required)

DSL quick ref:
- `clear R G B; rect X Y W H R G B; pixel X Y R G B`

## LLM-OO (organism-oriented)

- `/oo_new <goal>`: create an entity (long-lived intention)
- `/oo_list`: list entities
- `/oo_show <id>`: show an entity (goal/status/digest/notes tail)
- `/oo_kill <id>`: delete an entity
- `/oo_note <id> <text>`: append a note

Agenda:
- `/oo_plan <id> [prio] <action(s)>`: add actions (separator `;`, prio like `+2`)
- `/oo_agenda <id>`: show agenda
- `/oo_next <id>`: pick next action (marks “doing”)
- `/oo_done <id> <k>`: mark action #k done
- `/oo_prio <id> <k> <p>`: set priority for action #k
- `/oo_edit <id> <k> <text>`: edit action #k text

Execution:
- `/oo_step <id>`: advance one entity by one step
- `/oo_run [n]`: run n cooperative steps
- `/oo_digest <id>`: update digest + compress notes

Persistence:
- `/oo_save [f]`: save (default `oo-state.bin`)
- `/oo_load [f]`: load (default `oo-state.bin`)
- Note: a `*.bak` backup is created best-effort before overwrite.

Think/auto:
- `/oo_think <id> <prompt>`: ask the model, store the answer in notes
- `/oo_auto <id> [n] [prompt]`: n cycles think->store->step (stop: `q` or Esc between cycles)
- `/oo_auto_stop`: stop auto mode

## Autorun

- `/autorun_stop`: stop the current autorun
- `/autorun [--print] [--shutdown|--no-shutdown] [f]`
  - `--print`: print runnable lines from the script without executing
  - `--shutdown`: UEFI shutdown when the script completes
  - `--no-shutdown`: do not shutdown when the script completes
  - `f`: file name (default: `autorun_file` from `repl.cfg`, else `llmk-autorun.txt`)

### Autorun config (repl.cfg)

- `autorun_autostart=1` to start autorun at boot (disabled by default)
- `autorun_file=llmk-autorun.txt`
- `autorun_shutdown_when_done=0`

## Reset / context

- `/reset`: reset budgets/log + untrip sentinel
- `/clear`: clear KV cache (reset conversation context)

## DjibMark

- `/djibmarks`: DjibMark trace
- `/djibperf`: performance analysis by phase
