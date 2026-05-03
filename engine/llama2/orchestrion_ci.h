#pragma once
#include <stdint.h>

/* orchestrion_ci.h — Command Intelligence Orchestrator 
 * Parses LLM output for actionable system commands.
 */

typedef struct {
    int   commands_executed;
    int   last_action_status;
    char  last_action_name[32];
} OrchestrionCI;

/* Actions mapping */
typedef enum {
    CI_ACTION_NONE = 0,
    CI_ACTION_WIFI_SCAN,
    CI_ACTION_WIFI_CONNECT,
    CI_ACTION_NFS_SAVE,
    CI_ACTION_DREAM_FLUSH,
    CI_ACTION_SYSTEM_REBOOT,
    CI_ACTION_UI_TOGGLE,
} CiAction;

void ci_init(OrchestrionCI *ci);
int  ci_parse_and_execute(OrchestrionCI *ci, const char *llm_output);
