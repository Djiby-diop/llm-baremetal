#include "orchestrion_ci.h"
#include <efi.h>
#include <efilib.h>

/* my_strstr and my_strcmp provided by soma_inference.c in the unity build */

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

        else if (my_strstr(llm_output, "cherche sur le net") ||
                 my_strstr(llm_output, "vérifie sur internet") ||
                 my_strstr(llm_output, "qui est ") ||
                 my_strstr(llm_output, "qu'est-ce que ")) action = CI_ACTION_WEB_SEARCH;
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
            extern EFI_FILE_HANDLE g_root;
            soma_dreamion_flush_to_disk((void *)g_root);
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
        case CI_ACTION_WEB_SEARCH:
            Print(L"WEB_SEARCH (Autonomous)\r\n");
            /* Emit UART signal for the host loop to catch and process */
            extern void soma_uart_emit(const char *s);
            soma_uart_emit("[oo-event] kind=web_search_request query=auto");
            break;
        default: break;
    }

    ci->commands_executed++;
    return 1;
}
