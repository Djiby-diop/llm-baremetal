/* oo_bus_channels.h — OO Inter-module Channel Registry
 *
 * All channel IDs, payload type tags, and message flow contract.
 * Every module communicates ONLY through these channels.
 * No module may include another module's header directly.
 */
#pragma once
#include <stdint.h>

/* ── Channel ID convention ──────────────────────────────
 * 0x01xx = Cognition layer
 * 0x02xx = Memory layer
 * 0x03xx = Metabolism layer
 * 0x04xx = Social layer
 * 0x05xx = Morphology layer
 * 0x06xx = Immunity layer
 * 0x07xx = Coordination layer
 * 0x08xx = Diagnostics layer
 * 0x10xx = Hub / routing
 * 0x20xx = Security / policy
 */

/* Cognition */
#define OO_CH_CALIBRION    0x0101ULL  /* calibration request/response */
#define OO_CH_CONSCIENCE   0x0102ULL  /* thermal alert / precision downgrade */
#define OO_CH_DJIBION      0x0103ULL  /* coherence gate check */
#define OO_CH_DIOPION      0x0104ULL  /* chaos/mutation intent */

/* Memory */
#define OO_CH_MEMORION     0x0201ULL  /* manifest load/save */
#define OO_CH_NEURALFS     0x0202ULL  /* vector query / store */
#define OO_CH_SYNAPTION    0x0203ULL  /* memory layout priority */

/* Metabolism */
#define OO_CH_METABION     0x0301ULL  /* sampling param update */
#define OO_CH_DREAMION     0x0302ULL  /* background consolidation trigger */
#define OO_CH_CELLION      0x0303ULL  /* wasm stem-cell hot load */

/* Social */
#define OO_CH_COLLECTIVION 0x0401ULL  /* swarm KV broadcast */
#define OO_CH_PHEROMION    0x0402ULL  /* hot-path trace update */
#define OO_CH_SYMBION      0x0403ULL  /* hardware adaptation hint */
#define OO_CH_GHOST        0x0404ULL  /* covert inter-OO channel */

/* Morphology */
#define OO_CH_MORPHION     0x0501ULL  /* boot-time module load order */
#define OO_CH_EVOLVION     0x0502ULL  /* JIT stub generate/execute */

/* Immunity */
#define OO_CH_IMMUNION     0x0601ULL  /* threat pattern alert */

/* Coordination */
#define OO_CH_ORCHESTRION  0x0701ULL  /* workflow step dispatch */

/* Diagnostics */
#define OO_CH_DIAGNOSTION  0x0801ULL  /* kernel health report */
#define OO_CH_COMPATIBILION 0x0802ULL /* feature negotiation */

/* Hub */
#define OO_CH_SOMAMIND     0x1000ULL  /* SomaMind routing decisions */
#define OO_CH_SOMA_REFLEX  0x1001ULL  /* reflex path (fast) */
#define OO_CH_SOMA_INTERNAL 0x1002ULL /* internal inference path */

/* Security */
#define OO_CH_WARDEN       0x2000ULL  /* D+ verdict bus */
#define OO_CH_SENTINEL     0x2001ULL  /* sentinel violation alert */
#define OO_CH_DPLUS        0x2002ULL  /* D+ policy eval request */

/* ── Payload type tags ──────────────────────────────────
 * Carried in hermes_header_t.kind field
 */
#define OO_KIND_THERMAL_ALERT   0x01  /* float temp_celsius, uint8_t severity */
#define OO_KIND_PRECISION_DOWN  0x02  /* uint8_t from_bits, uint8_t to_bits */
#define OO_KIND_SAMPLING_UPDATE 0x03  /* float temperature, float top_p */
#define OO_KIND_THREAT_PATTERN  0x04  /* uint8_t pattern[32] (fingerprint) */
#define OO_KIND_JIT_STUB        0x05  /* uint8_t code[256], uint32_t crc32 */
#define OO_KIND_VECTOR_QUERY    0x06  /* float embedding[64], uint32_t top_k */
#define OO_KIND_DPLUS_VERDICT   0x07  /* uint8_t verdict (0=deny,1=allow,2=defer) */
#define OO_KIND_HANDOFF         0x08  /* OO_HANDOFF.TXT content hash */
#define OO_KIND_MANIFEST        0x09  /* manifest key+value pair */
