#include "oo_wasm.h"
#include "../../overclock-engine/core/overclock.h" // For MSR/logging if needed
#include <string.h>

#define WASM_MAGIC 0x6d736100 // "\0asm" in little-endian

void oo_wasm_init(void) {
    // Initialize the interpreter state
}

static uint32_t read_leb128(const uint8_t **ptr) {
    uint32_t result = 0;
    uint32_t shift = 0;
    while (1) {
        uint8_t byte = **ptr;
        (*ptr)++;
        result |= (byte & 0x7f) << shift;
        if ((byte & 0x80) == 0) break;
        shift += 7;
    }
    return result;
}

int oo_wasm_load_module(WasmModule *mod, void *buffer, uint32_t size) {
    if (!mod || !buffer || size < 8) return -1;

    uint32_t *magic = (uint32_t*)buffer;
    if (*magic != WASM_MAGIC) {
        return -2; // Not a valid WASM file
    }

    mod->bytecode = (uint8_t*)buffer;
    mod->size = size;
    mod->initialized = 1;

    return 0;
}

int oo_wasm_execute(WasmModule *mod, const char *func_name) {
    if (!mod || !mod->initialized) return -1;

    const uint8_t *ptr = mod->bytecode + 8; // Skip Magic and Version
    const uint8_t *end = mod->bytecode + mod->size;

    // Loop through WASM sections
    while (ptr < end) {
        uint8_t section_id = *ptr++;
        uint32_t section_size = read_leb128(&ptr);
        const uint8_t *section_end = ptr + section_size;
        
        if (section_id == 7) { // Export Section
            uint32_t count = read_leb128(&ptr);
            mod->export_count = count < 16 ? count : 16;
            
            for (uint32_t i = 0; i < mod->export_count; i++) {
                uint32_t name_len = read_leb128(&ptr);
                uint32_t copy_len = name_len < 31 ? name_len : 31;
                
                memcpy(mod->exports[i].name, ptr, copy_len);
                mod->exports[i].name[copy_len] = '\0';
                ptr += name_len;
                
                uint8_t kind = *ptr++; // 0x00 = Function
                uint32_t index = read_leb128(&ptr);
                mod->exports[i].index = index;
            }
            ptr = section_end; // Sécurité : on se recalale à la fin de la section
        } else if (section_id == 10) { // Code Section
            uint32_t count = read_leb128(&ptr);
            // On peut stocker ou afficher le nombre de corps de fonctions
            if (count > 0) {
                uint32_t func_size = read_leb128(&ptr);
                // ptr pointe maintenant sur le code de la première fonction
            }
            ptr = section_end; // On passe à la suite pour l'instant
        } else {
            ptr = section_end; // Skip other sections
        }
    }
    
    return 0; // Success stub
}

void oo_wasm_unload_module(WasmModule *mod) {
    if (!mod) return;
    mod->bytecode = 0;
    mod->size = 0;
    mod->initialized = 0;
}
