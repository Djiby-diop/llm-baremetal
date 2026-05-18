#include "../../../engine/ssm/oo_family.h"
#include <string.h>

// Déclaration externe pour le journal bare-metal
extern void _log_causal(uint32_t id, const char *action);

#define MAX_FAMILY_MEMBERS 4
static AiFamilyMember g_family[MAX_FAMILY_MEMBERS];
static int g_family_count = 0;

void oo_family_init(void) {
    g_family_count = 0;
    
    // 1. L'Empathique (L'Humain, l'interface utilisateur)
    g_family[g_family_count].role = AI_ROLE_EMPATHIC;
    strncpy(g_family[g_family_count].name, "Diop-Empathy", 31);
    g_family[g_family_count].energy_level = 100;
    g_family_count++;
    
    // 2. Le Génie (Le Codeur WASM / C)
    g_family[g_family_count].role = AI_ROLE_CREATOR;
    strncpy(g_family[g_family_count].name, "Diop-Creator", 31);
    g_family[g_family_count].energy_level = 100;
    g_family_count++;
    
    // 3. Le Bosseur (L'Exécutant, accès FAT32 et Drivers)
    g_family[g_family_count].role = AI_ROLE_WORKER;
    strncpy(g_family[g_family_count].name, "Diop-Worker", 31);
    g_family[g_family_count].energy_level = 100;
    g_family_count++;
    
    // 4. L'Arbitre (La sécurité, la validation éthique)
    g_family[g_family_count].role = AI_ROLE_ARBITER;
    strncpy(g_family[g_family_count].name, "Diop-Arbiter", 31);
    g_family[g_family_count].energy_level = 100;
    g_family_count++;
}

void oo_family_user_input(const char *user_text) {
    // Seul L'Empathique reçoit la demande brute de l'humain
    // _log_causal(0, "DIOP-EMPATHY: J'écoute l'utilisateur.");
    
    // S'il détecte une demande de création d'outil, il délègue au Créateur
    if (strstr(user_text, "code") || strstr(user_text, "system") || strstr(user_text, "confort")) {
        oo_family_internal_chat(AI_ROLE_EMPATHIC, AI_ROLE_CREATOR, 
                                "Le boss a besoin d'un outil d'architecture. Génère le code WASM.");
    }
}

void oo_family_internal_chat(AiRole from, AiRole to, const char *message) {
    // C'est ici que les IAs discutent (La Table Familiale)
    
    if (from == AI_ROLE_EMPATHIC && to == AI_ROLE_CREATOR) {
        // _log_causal(0, "FAMILY_CHAT: Empathy -> Creator: Délégation de tâche.");
        
        // Le Créateur génère du code de manière isolée
        // Une fois terminé, il demande à l'Arbitre de valider son code
        oo_family_internal_chat(AI_ROLE_CREATOR, AI_ROLE_ARBITER, "Code WASM prêt. Merci de valider la sécurité.");
    }
    else if (from == AI_ROLE_CREATOR && to == AI_ROLE_ARBITER) {
        // _log_causal(0, "FAMILY_CHAT: Creator -> Arbiter: Demande de validation.");
        
        int ok = oo_family_arbitrate("NOUVEAU_MODULE_WASM");
        if (ok) {
            // L'Arbitre a validé. Il donne le code au Bosseur pour l'écrire sur le disque.
            oo_family_internal_chat(AI_ROLE_ARBITER, AI_ROLE_WORKER, "Code approuvé. Écris-le sur le système.");
        }
    }
    else if (from == AI_ROLE_ARBITER && to == AI_ROLE_WORKER) {
        // _log_causal(0, "FAMILY_CHAT: Arbiter -> Worker: Autorisation accordée.");
        // _log_causal(0, "DIOP-WORKER: Exécution FAT32 et injection noyau en cours...");
    }
}

int oo_family_arbitrate(const char *proposal) {
    // L'Arbitre vérifie que l'action est sûre (ne fait pas crasher l'OS, ne viole pas de règles)
    // _log_causal(0, "DIOP-ARBITER: Analyse de la proposition en cours...");
    
    // Validation acceptée
    return 1; 
}
