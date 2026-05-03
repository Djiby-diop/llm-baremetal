#include "orchestrion_ci.h"
#include <efi.h>
#include <efilib.h>

/* Simple substring search since we don't have strstr in baremetal by default */
static const char* my_strstr(const char *haystack, const char *needle) {
    if (!haystack || !needle) return NULL;
    for (int i = 0; haystack[i]; i++) {
        int j = 0;
        while (needle[j] && haystack[i+j] == needle[j]) j++;
        if (!needle[j]) return &haystack[i];
    }
    return NULL;
}

static int my_strcmp(const char *s1, const char *s2) {
    while (*s1 && (*s1 == *s2)) { s1++; s2++; }
    return *(unsigned char*)s1 - *(unsigned char*)s2;
}

void ci_init(OrchestrionCI *ci) {
    if (!ci) return;
    ci->commands_executed = 0;
    ci->last_action_status = 0;
    ci->last_action_name[0] = 0;
}

int ci_parse_and_execute(OrchestrionCI *ci, const char *llm_output) {
    if (!ci || !llm_output) return 0;

    CiAction action = CI_ACTION_NONE;
    const char *tag = my_strstr(llm_output, "[[ACTION:");
    
    if (tag) {
        /* Parse action from tag [[ACTION: name]] */
        if (my_strstr(tag, "wifi_scan")) action = CI_ACTION_WIFI_SCAN;
        else if (my_strstr(tag, "nfs_save")) action = CI_ACTION_NFS_SAVE;
        else if (my_strstr(tag, "dream_flush")) action = CI_ACTION_DREAM_FLUSH;
        else if (my_strstr(tag, "reboot")) action = CI_ACTION_SYSTEM_REBOOT;
        else if (my_strstr(tag, "ui_toggle")) action = CI_ACTION_UI_TOGGLE;
    } else {
        /* Literary parsing fallback — handles natural language intents */
        if (my_strstr(llm_output, "scan wifi") || 
            my_strstr(llm_output, "cherche les réseaux") ||
            my_strstr(llm_output, "chercher les réseaux") ||
            my_strstr(llm_output, "active le wifi") ||
            my_strstr(llm_output, "activat le wifi")) action = CI_ACTION_WIFI_SCAN;
            
        else if (my_strstr(llm_output, "sauvegarde ma mémoire") ||
                 my_strstr(llm_output, "enregistre mon état") ||
                 my_strstr(llm_output, "sauver l'état")) action = CI_ACTION_NFS_SAVE;
                 
        else if (my_strstr(llm_output, "enregistre tes rêves") ||
                 my_strstr(llm_output, "consolide la mémoire") ||
                 my_strstr(llm_output, "pense à ce que tu as appris")) action = CI_ACTION_DREAM_FLUSH;
                 
        else if (my_strstr(llm_output, "redémarre le système") ||
                 my_strstr(llm_output, "reboot")) action = CI_ACTION_SYSTEM_REBOOT;
    }

    if (action == CI_ACTION_NONE) return 0;

    /* Execution bridge to existing REPL commands or drivers */
    Print(L"\r\n[Orchestrion-CI] Executing autonomous action: ");
    
    switch (action) {
        case CI_ACTION_WIFI_SCAN:
            Print(L"WIFI_SCAN\r\n");
            extern int oo_wifi_scan(void);
            oo_wifi_scan();
            break;
        case CI_ACTION_NFS_SAVE:
            Print(L"NFS_SAVE\r\n");
            /* Hook to /nfs_save logic */
            extern void llmk_oo_trigger_nfs_save(void);
            llmk_oo_trigger_nfs_save();
            break;
        case CI_ACTION_DREAM_FLUSH:
            Print(L"DREAM_FLUSH\r\n");
            extern int soma_dreamion_flush_to_disk(void *root_dir);
            extern void* g_root;
            soma_dreamion_flush_to_disk(g_root);
            break;
        case CI_ACTION_SYSTEM_REBOOT:
            Print(L"SYSTEM_REBOOT\r\n");
            RT->ResetSystem(EfiResetCold, EFI_SUCCESS, 0, NULL);
            break;
        case CI_ACTION_UI_TOGGLE:
            Print(L"UI_TOGGLE\r\n");
            extern int g_tui_enabled;
            g_tui_enabled = !g_tui_enabled;
            break;
        default: break;
    }

    ci->commands_executed++;
    return 1;
}
