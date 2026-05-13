/* efi_main.c — UEFI Entry Point (Public Prototype)
 *
 * This is the top-level EFI application entry. The system boots here,
 * initializes hardware, loads the zone allocator, then enters the
 * OO inference REPL.
 *
 * Compiled with: gcc -ffreestanding -fno-stack-protector -fpic
 *                    -fshort-wchar -mno-red-zone -I/usr/include/efi
 */
#include <efi.h>
#include <efilib.h>
#include "../core/llmk_zones.h"
#include "../warden/oo_sentinel.h"
#include "../policy/oo_dplus.h"
#include "../engine/llm/oo_llm.h"

static OoSentinel  g_sentinel;
static OoLlmModel  g_model;

EFI_STATUS EFIAPI efi_main(EFI_HANDLE ImageHandle,
                            EFI_SYSTEM_TABLE *SystemTable)
{
    InitializeLib(ImageHandle, SystemTable);

    Print(L"\r\n=== Operating Organism v0.1 ===\r\n");
    Print(L"Bare-metal LLM inference engine\r\n\r\n");

    /* Phase 1: Initialize memory zones from UEFI memory map */
    UINTN mmap_size = 0, map_key, desc_size;
    UINT32 desc_ver;
    EFI_MEMORY_DESCRIPTOR *mmap = NULL;
    uefi_call_wrapper(BS->GetMemoryMap, 5, &mmap_size, mmap,
                      &map_key, &desc_size, &desc_ver);
    mmap = AllocatePool(mmap_size + 2 * desc_size);
    uefi_call_wrapper(BS->GetMemoryMap, 5, &mmap_size, mmap,
                      &map_key, &desc_size, &desc_ver);
    llmk_zones_init(mmap, mmap_size, desc_size);
    llmk_zones_print_stats();

    /* Phase 2: Initialize warden sentinel */
    oo_sentinel_init(&g_sentinel);
    Print(L"[warden] Sentinel armed\r\n");

    /* Phase 3: Load D+ policy */
    dplus_init("\\OO\\OOPOLICY.BIN");
    Print(L"[policy] D+ engine ready\r\n");

    /* Phase 4: Load LLM model */
    Print(L"[engine] Loading model from \\OO\\model.gguf ...\r\n");
    if (oo_llm_load(&g_model, "\\OO\\model.gguf") != 0) {
        Print(L"[engine] Model load failed — entering safe REPL\r\n");
    } else {
        Print(L"[engine] Model ready: %d layers, d=%d\r\n",
              (int)g_model.cfg.n_layers, (int)g_model.cfg.d_model);
    }

    /* Phase 5: REPL loop */
    Print(L"\r\nOO> ");
    CHAR16 input[512];
    while (1) {
        /* Read input (simplified — real impl handles raw keyboard IRQ) */
        Input(NULL, input, sizeof(input) / sizeof(CHAR16));
        Print(L"\r\n");

        if (input[0] == 0) continue;

        /* Convert CHAR16 → char (ASCII subset) */
        char prompt[256];
        for (int i = 0; i < 255 && input[i]; i++)
            prompt[i] = (char)(input[i] & 0x7F);
        prompt[255] = 0;

        /* D+ gate: check if inference is allowed */
        DPlusContext ctx = {
            .action = ACTION_INFERENCE_OUTPUT,
            .confidence = 1.0f,
        };
        DPlusDecision d = dplus_evaluate(&ctx);
        if (d.verdict == DPLUS_DENY) {
            Print(L"[policy] Action denied: %a\r\n", d.reason);
        } else if (g_model.loaded) {
            /* Run inference */
            OoLlmSampleParams p = { .temperature=0.8f, .top_p=0.95f,
                                    .max_new_tokens=256 };
            oo_llm_generate(&g_model, prompt, &p,
                            NULL /* token callback */, NULL);
        }

        Print(L"\r\nOO> ");
    }

    return EFI_SUCCESS;
}
