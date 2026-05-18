#include "oo_paging.h"
#include <string.h>

// Pointeur vers la table PML4 du noyau (le niveau le plus haut)
static uint64_t *g_kernel_pml4 = NULL;

void oo_paging_init(void) {
    // Dans un vrai OS, on lirait le CR3 actuel pour récupérer la table du noyau
    uint64_t cr3;
    __asm__ volatile("mov %%cr3, %0" : "=r"(cr3));
    
    // On masque les bits de flags pour n'avoir que l'adresse physique
    g_kernel_pml4 = (uint64_t *)(cr3 & ~0xFFFf);
}

void oo_map_page(uint64_t virtual_addr, uint64_t physical_addr, uint32_t flags) {
    // Ici, il faudrait parcourir les 4 niveaux de tables :
    // PML4 -> PDP -> PD -> PT
    // Et créer les entrées manquantes avec les 'flags' (User, Writable, Present).
    
    // C'est un algorithme complexe d'arbre à 4 niveaux.
}

void oo_switch_address_space(uint64_t pml4_physical_addr) {
    // L'instruction ultime pour changer d'univers mémoire : on charge CR3
    __asm__ volatile("mov %0, %%cr3" : : "r"(pml4_physical_addr) : "memory");
}

uint64_t oo_alloc_page(void) {
    // Ici, on appellerait un allocateur physique (bitmap ou stack de pages libres)
    // Pour l'instant, c'est un bouchon.
    return 0;
}
