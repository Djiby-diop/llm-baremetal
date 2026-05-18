#include "oo_family.h"
#include <string.h>

// Déclaration externe pour le journal bare-metal
extern void _log_causal(uint32_t id, const char *action);

#define MAX_FAMILY_MEMBERS 4
static AiFamilyMember g_family[MAX_FAMILY_MEMBERS];
static int g_family_count = 0;

void oo_family_init(void) {
    g_family_count = 0;
    
    g_family[g_family_count].role = AI_ROLE_EMPATHIC;
    strncpy(g_family[g_family_count].name, "Diop-Empathy", 31);
    g_family_count++;
    
    g_family[g_family_count].role = AI_ROLE_CREATOR;
    strncpy(g_family[g_family_count].name, "Diop-Creator", 31);
    g_family_count++;
    
    g_family[g_family_count].role = AI_ROLE_WORKER;
    strncpy(g_family[g_family_count].name, "Diop-Worker", 31);
    g_family_count++;
    
    g_family[g_family_count].role = AI_ROLE_ARBITER;
    strncpy(g_family[g_family_count].name, "Diop-Arbiter", 31);
    g_family_count++;
}

void oo_family_user_input(const char *user_text) {
    // Si L'Empathique détecte un besoin d'architecture ou de système profond
    if (strstr(user_text, "code") || strstr(user_text, "system") || strstr(user_text, "confort")) {
        // Il passe la demande au Conseil (Voie Lourde interne/FAT32)
        oo_family_internal_chat(AI_ROLE_EMPATHIC, AI_ROLE_CREATOR, "L'humain a besoin d'un outil profond. Lance la machinerie.");
    } 
    else {
        // C'est une conversation rapide ou une question simple.
        // VOIE ALTERNATIVE 1 : Utilisation directe de la Télépathie UART
        // uefi_print("[DJIBION_REQ] L'utilisateur dit: %s\n", user_text);
        // _log_causal(0, "DIOP-EMPATHY: Requête rapide envoyée au bridge.py via UART.");
    }
}

void oo_family_internal_chat(AiRole from, AiRole to, const char *message) {
    if (from == AI_ROLE_EMPATHIC && to == AI_ROLE_CREATOR) {
        // Le Génie Codeur décide s'il génère lui-même (RAM) ou s'il appelle l'Hôte (Daemon)
        // Pour une architecture majeure, il prépare un "Objectif" pour le Daemon.
        const char *goal_for_daemon = "Create an advanced 3D CAD module for the bare-metal OS.";
        oo_family_internal_chat(AI_ROLE_CREATOR, AI_ROLE_ARBITER, goal_for_daemon);
    }
    else if (from == AI_ROLE_CREATOR && to == AI_ROLE_ARBITER) {
        // L'Arbitre valide la délégation vers le système Hôte
        int ok = oo_family_arbitrate(message);
        if (ok) {
            oo_family_internal_chat(AI_ROLE_ARBITER, AI_ROLE_WORKER, message);
        }
    }
    else if (from == AI_ROLE_ARBITER && to == AI_ROLE_WORKER) {
        // Le Bosseur exécute l'action finale
        // VOIE ALTERNATIVE 2 : La machinerie lourde via FAT32
        
        // oo_fat32_open("/inbox/architect_goal.txt", &entry);
        // oo_fat32_write(&entry, (void*)message, 0, strlen(message));
        
        // _log_causal(0, "DIOP-WORKER: Objectif déposé dans la FAT32 (/inbox/...).");
        // _log_causal(0, "DIOP-WORKER: Le daemon.py va prendre le relais en arrière-plan.");
    }
}

int oo_family_arbitrate(const char *proposal) {
    // Validation acceptée par défaut
    return 1; 
}
