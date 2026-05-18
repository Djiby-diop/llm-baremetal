#pragma once
#include <stdint.h>

/**
 * OO Family Council (Multi-Agent System)
 * 
 * Defines the archetypes and interactions of the distinct AIs 
 * living within the Operating Organism.
 */

// Les Archétypes de la Famille
typedef enum {
    AI_ROLE_EMPATHIC,  // Le Front-End : Gère la relation avec l'humain
    AI_ROLE_CREATOR,   // Le Génie Codeur : Conçoit le code, le WASM, l'architecture
    AI_ROLE_WORKER,    // Le Bosseur : Exécute les tâches lourdes (Disque, Réseau)
    AI_ROLE_ARBITER    // L'Arbitre : Valide la sécurité, et tranche les conflits
} AiRole;

// Un membre de la Famille
typedef struct {
    int         scheduler_task_id; // Son "corps" dans l'Ordonnanceur
    AiRole      role;              // Son rôle
    char        name[32];          // Son prénom (ex: "Diop-Creator")
    void*       lora_adapter;      // Son "Âme" (ses compétences spécifiques)
    uint32_t    energy_level;      // Niveau d'énergie
} AiFamilyMember;

// Initialiser le Conseil de Famille au démarrage
void oo_family_init(void);

// L'Humain (User) ne parle qu'à l'Empathique
void oo_family_user_input(const char *user_text);

// La "Table Familiale" : Une IA envoie une idée à une autre IA
void oo_family_internal_chat(AiRole from, AiRole to, const char *message);

// L'Arbitre tranche un conflit
int oo_family_arbitrate(const char *proposal);
