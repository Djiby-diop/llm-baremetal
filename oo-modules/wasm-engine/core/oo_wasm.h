#pragma once

#include <stdint.h>

/**
 * WASM Engine — The Anti-DLL Sovereign System
 * 
 * Allows the Organism to load and execute sandboxed modules
 * without depending on OS-specific dynamic libraries (.dll/.so).
 */

typedef struct {
    char     name[32];
    uint32_t index;
} WasmExport;

typedef struct {
    uint8_t   *bytecode;
    uint32_t  size;
    int       initialized;
    WasmExport exports[16]; // Max 16 exports for now
    uint32_t   export_count;
} WasmModule;

/**
 * Initializes the WASM runtime environment.
 */
void oo_wasm_init(void);

/**
 * Loads a WASM module from a memory buffer.
 * Returns 0 on success.
 */
int oo_wasm_load_module(WasmModule *mod, void *buffer, uint32_t size);

/**
 * Executes a function inside the WASM module.
 */
int oo_wasm_execute(WasmModule *mod, const char *func_name);

/**
 * Unloads the module and frees resources.
 */
void oo_wasm_unload_module(WasmModule *mod);
