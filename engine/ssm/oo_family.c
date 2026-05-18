#include "oo_family.h"
#include <string.h>

// On utilise le bridge et les pilotes FAT32 existants
#include "oo_bridge.h"

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
    
    // _log_causal(0, "OO_FAMILY: Conseil de Famille initialise (4 Membres).");
}

void oo_family_user_input(const char *user_text) {
    if (!user_text) return;
    
    // Si L'Empathique détecte un besoin lourd de système
    if (strstr(user_text, "code") || strstr(user_text, "system") || strstr(user_text, "confort")) {
        oo_family_internal_chat(AI_ROLE_EMPATHIC, AI_ROLE_CREATOR, "L'humain a besoin d'un outil profond. Lance la machinerie.");
    } 
}

void oo_family_internal_chat(AiRole from, AiRole to, const char *message) {
    if (from == AI_ROLE_EMPATHIC && to == AI_ROLE_CREATOR) {
        // Le Génie Codeur génère du code
        const char *goal_for_daemon = "Create an advanced 3D CAD module for the bare-metal OS.";
        oo_family_internal_chat(AI_ROLE_CREATOR, AI_ROLE_ARBITER, goal_for_daemon);
    }
    else if (from == AI_ROLE_CREATOR && to == AI_ROLE_ARBITER) {
        // L'Arbitre valide
        int ok = oo_family_arbitrate(message);
        if (ok) {
            oo_family_internal_chat(AI_ROLE_ARBITER, AI_ROLE_WORKER, message);
        }
    }
    else if (from == AI_ROLE_ARBITER && to == AI_ROLE_WORKER) {
        // Le Bosseur demande au Bridge d'envoyer la commande à QEMU/Python
        // oo_bridge_execute_morph("ARCHITECTURE_MORPH");
    }
}

int oo_family_arbitrate(const char *proposal) {
    return 1; // Validation toujours OK pour le moment
}
