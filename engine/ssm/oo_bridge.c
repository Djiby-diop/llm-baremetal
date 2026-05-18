#include "oo_bridge.h"
#include <string.h>

// Déclaration externe pour le journal (logging)
extern void _log_causal(uint32_t id, const char *action);

void oo_bridge_execute_morph(const char *domain) {
    if (strstr(domain, "ARCHITECT")) {
        // _log_causal(0, "MORPH_MODE: Architect activated");
        // 1. FAT32 adaptation: oo_fat32_mkdir("/blueprints");
        // 2. Load Math WASM
        // 3. Suspend trading: mercator_suspend();
    } else if (strstr(domain, "PREDATOR")) {
        // _log_causal(0, "MORPH_MODE: Predator/Trading activated");
        // 1. Activate Mercatorion
        // 2. Load low-latency network WASM
    }
}

void oo_bridge_handle_fat32(const char *args) {
    // args contient par exemple "CREATE /plans_maison"
    if (strncmp(args, "CREATE ", 7) == 0) {
        const char *path = args + 7;
        // _log_causal(0, "BRIDGE: Creating folder");
        // oo_fat32_mkdir(path);
    }
}

int oo_bridge_parse_thought(const char *thought_stream) {
    if (!thought_stream) return 0;

    // L'IA génère ses actions avec un tag spécial, par exemple :
    // "[SYS_CMD: FAT32_CREATE /blueprints]"
    const char *cmd_start = strstr(thought_stream, "[SYS_CMD: ");
    
    if (cmd_start) {
        const char *cmd_inner = cmd_start + 10; // Skip "[SYS_CMD: "
        const char *cmd_end = strchr(cmd_inner, ']');
        
        if (cmd_end) {
            char cmd_buf[128];
            int len = cmd_end - cmd_inner;
            if (len >= sizeof(cmd_buf)) len = sizeof(cmd_buf) - 1;
            
            strncpy(cmd_buf, cmd_inner, len);
            cmd_buf[len] = '\0';
            
            // Dispatch des commandes
            if (strncmp(cmd_buf, "MORPH_", 6) == 0) {
                oo_bridge_execute_morph(cmd_buf + 6);
            }
            else if (strncmp(cmd_buf, "FAT32_", 6) == 0) {
                oo_bridge_handle_fat32(cmd_buf + 6);
            }
            else if (strncmp(cmd_buf, "WASM_LOAD ", 10) == 0) {
                // _log_causal(0, "BRIDGE: Loading WASM module");
                // char *module_name = cmd_buf + 10;
                // oo_wasm_load_module_from_disk(module_name);
            }
            else if (strncmp(cmd_buf, "SLEEP", 5) == 0) {
                // _log_causal(0, "BRIDGE: Forcing Mnemion Dream State");
                // force_sleep_mode();
            }
        }
        return 1; // Commande système interceptée et masquée de l'écran
    }
    
    return 0; // Texte normal, on l'affiche
}

// Buffer statique pour accumuler les lettres générées par l'IA
static char g_thought_buffer[512];
static int g_thought_idx = 0;

void oo_bridge_feed_char(char c) {
    // Si fin de ligne (pensée complète) ou buffer plein
    if (c == '\n' || c == '\r' || g_thought_idx >= 510) {
        g_thought_buffer[g_thought_idx] = '\0';
        
        // On analyse la phrase complète
        int is_cmd = oo_bridge_parse_thought(g_thought_buffer);
        
        if (!is_cmd && g_thought_idx > 0) {
            // Ce n'est pas une commande système. On l'affiche sur l'écran (GOP)
            // Print(L"%s\n", g_thought_buffer);
        }
        
        g_thought_idx = 0; // On réinitialise pour la phrase suivante
    } else {
        g_thought_buffer[g_thought_idx++] = c; // On empile la lettre
    }
}
