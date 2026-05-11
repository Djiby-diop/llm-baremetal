#pragma once
/* oo_organ_bus.h — Wires all biological organs to the united_bus IPC.
 *
 * Organ ID mapping (matches ORGAN_TYPE_* from kernel-baremetal):
 *   0x00 = CORTEX  (LLM/Cortex — Wq/Wk/Wv forward)
 *   0x01 = IMMUNE  (bot-baremetal — threat detection)
 *   0x02 = SENSORY (sense-baremetal — keyboard/touch/vision)
 *   0x03 = VITAL   (kernel-baremetal — scheduler state)
 *   0x04 = REFLEX  (reflex-baremetal — sub-ms reactions)
 *   0x05 = EVOLUTION (evolution-baremetal)
 *   0xFF = BROADCAST
 */

#include <efi.h>
#include <efilib.h>

/* Organ IDs */
#define OO_BUS_ORGAN_CORTEX    0x00
#define OO_BUS_ORGAN_IMMUNE    0x01
#define OO_BUS_ORGAN_SENSORY   0x02
#define OO_BUS_ORGAN_VITAL     0x03
#define OO_BUS_ORGAN_REFLEX    0x04
#define OO_BUS_ORGAN_EVOLUTION 0x05
#define OO_BUS_BROADCAST       0xFF

/* Globule types (mirrors united_bus.h) */
#define BUS_GLOBULE_RED    1  /* data */
#define BUS_GLOBULE_WHITE  2  /* immunity */
#define BUS_GLOBULE_YELLOW 3  /* energy/scheduling */

/* Public API */
void oo_organ_bus_init(void);
void oo_organ_bus_tick(void);  /* Call periodically from REPL/scheduler */

/* Emit helpers */
void oo_bus_emit_keyboard(UINT8 ch);          /* SENSORY → CORTEX (RED) */
void oo_bus_emit_scheduler_state(UINT8 state);/* VITAL → BROADCAST (YELLOW) */
void oo_bus_emit_threat(UINT8 threat_level);  /* IMMUNE → BROADCAST (WHITE) */
void oo_bus_emit_reflex(UINT8 irq);           /* VITAL → REFLEX (YELLOW) */

/* Poll helpers — called by REPL */
int  oo_bus_poll_cortex(void);    /* returns key from keyboard queue or -1 */
