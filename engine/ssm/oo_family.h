#ifndef OO_FAMILY_H
#define OO_FAMILY_H

#include <stdint.h>

// Les Archétypes de la Famille (Noyau Bare-Metal)
typedef enum {
    AI_ROLE_EMPATHIC,  // Interface utilisateur, analyse des émotions
    AI_ROLE_CREATOR,   // Génie Codeur (WASM, C)
    AI_ROLE_WORKER,    // Bosseur (Accès I/O, FAT32)
    AI_ROLE_ARBITER    // Juge (Validation de sécurité)
} AiRole;

typedef struct {
    int         scheduler_task_id;
    AiRole      role;
    char        name[32];
    void*       lora_adapter;
    uint32_t    energy_level;
} AiFamilyMember;

// Initialise le conseil au boot de l'OS
void oo_family_init(void);

// Point d'entrée des pensées utilisateur
void oo_family_user_input(const char *user_text);

// La table de négociation interne
void oo_family_internal_chat(AiRole from, AiRole to, const char *message);

// Le système d'arbitrage (Ethique et Stabilité)
int oo_family_arbitrate(const char *proposal);

#endif
