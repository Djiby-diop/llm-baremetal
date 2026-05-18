/* oo-modules/module_registry.h — Master Module ID Registry
 *
 * Assigns a stable 32-bit ID to each of the 21 OO biological engines.
 * IDs are organized by subsystem category (same ranges as OO bus channels).
 *
 * Category ranges:
 *   0x01-0x04  sensing     (perceive environment)
 *   0x05-0x09  adaptation  (change behavior)
 *   0x0A-0x0E  memory      (remember + consolidate)
 *   0x0F-0x10  identity    (who the OO is / policy)
 *   0x11-0x12  social      (inter-OO communication)
 *   0x13-0x14  meta        (system orchestration)
 *
 * Source locations in the original repo (read-only reference):
 *   <name>-engine/core/<name>.h  (header)
 *   <name>-engine/core/<name>.c  (implementation)
 *
 * New worktree locations (after migration):
 *   oo-modules/<category>/<name>/module_<name>.h
 *
 * Hermes bus channel base for module messages:
 *   OO_CH_MODULE_BASE + OO_MOD_<NAME>  (e.g. 0x0400 + 0x01 = 0x0401)
 */

#ifndef OO_MODULES_MODULE_REGISTRY_H
#define OO_MODULES_MODULE_REGISTRY_H

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================
 * Module IDs
 * ============================================================ */

/* sensing — perceive the environment */
#define OO_MOD_CONSCIENCE     0x01u   /* thermal homeostasis, precision downgrade   */
#define OO_MOD_COMPATIBILION  0x02u   /* platform detection, CPU/memory caps        */
#define OO_MOD_DIAGNOSTION    0x03u   /* observability, failure explanations        */
#define OO_MOD_METABION       0x04u   /* metabolism: tokens/s, flops/W, cache rate  */

/* adaptation — change behavior based on environment */
#define OO_MOD_CALIBRION      0x05u   /* adaptive sampling (temp/top_k/top_p)       */
#define OO_MOD_DIOPION        0x06u   /* chaos/burst exploration, mutation          */
#define OO_MOD_MORPHION       0x07u   /* morphological boot, hardware skeleton      */
#define OO_MOD_SYMBION        0x08u   /* hardware symbiosis, driver tuning          */
#define OO_MOD_EVOLVION       0x09u   /* self-evolving kernel, LLM codegen          */

/* memory — remember and consolidate */
#define OO_MOD_MEMORION       0x0Au   /* persistent manifests, cross-reboot memory  */
#define OO_MOD_SYNAPTION      0x0Bu   /* synaptic memory layout, heat tiers         */
#define OO_MOD_PHEROMION      0x0Cu   /* pheromone trails, hot-path prefetch        */
#define OO_MOD_DREAMION       0x0Du   /* dream state, KV dedup + consolidation      */
#define OO_MOD_NEURALFS       0x0Eu   /* semantic filesystem, embedding search      */

/* identity — who the OO is, what it's allowed to do */
#define OO_MOD_DJIBION        0x0Fu   /* meta-coherence, bio-code, policy verdicts  */
#define OO_MOD_IMMUNION       0x10u   /* immune memory, threat patterns, antibodies */

/* social — inter-OO communication */
#define OO_MOD_COLLECTIVION   0x11u   /* collective consciousness, swarm decisions  */
#define OO_MOD_GHOST          0x12u   /* inter-OO comms (LED/PC speaker channels)   */

/* meta — system-level orchestration and extensibility */
#define OO_MOD_ORCHESTRION    0x13u   /* workflow runner, pipeline sequencer        */
#define OO_MOD_CELLION        0x14u   /* Wasm stem cells, hot-load config deltas    */
#define OO_MOD_DESKTOP_DISPLAY 0x15u  /* bus-native HUD / DIOP / swarm bridge       */

#define OO_MOD_COUNT          21u

/* Hermes bus channel for a given module ID */
#define OO_MOD_CHANNEL(mod_id)  (0x0400u + (mod_id))

/* ============================================================
 * Category tags
 * ============================================================ */
#define OO_MOD_CAT_SENSING    0x01u
#define OO_MOD_CAT_ADAPTATION 0x02u
#define OO_MOD_CAT_MEMORY     0x03u
#define OO_MOD_CAT_IDENTITY   0x04u
#define OO_MOD_CAT_SOCIAL     0x05u
#define OO_MOD_CAT_META       0x06u

/* ============================================================
 * Module state flags (used in OoModuleDescriptor.flags)
 * ============================================================ */
#define OO_MOD_FLAG_ENABLED   (1u << 0)  /* module is active                */
#define OO_MOD_FLAG_BUS_READY (1u << 1)  /* registered on Hermes bus        */
#define OO_MOD_FLAG_TICKING   (1u << 2)  /* called every inference step     */
#define OO_MOD_FLAG_NO_HEAP   (1u << 3)  /* confirmed: no dynamic allocation */

#ifdef __cplusplus
}
#endif

#endif /* OO_MODULES_MODULE_REGISTRY_H */
