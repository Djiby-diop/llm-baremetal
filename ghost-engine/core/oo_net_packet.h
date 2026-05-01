#pragma once

/*
 * OO-NET: Distributed OO communication protocol.
 *
 * Trame fixe 32 octets — freestanding, pas de stdlib.
 * Canal physique : LED clavier (Ghost), PC Speaker, ou futur UDP.
 *
 * Format paquet :
 *   [0..1]  magic    0x00, 0x4F ('O') — identifiant OO-NET
 *   [2]     type     OO_PKT_*
 *   [3]     src_id   identifiant noeud émetteur (0..255)
 *   [4]     dst_id   identifiant noeud destinataire (0xFF = broadcast)
 *   [5]     seq      numéro de séquence (uint8, modulo 256)
 *   [6..25] payload  20 octets de données
 *   [26..29] extra   champ libre (DNA hash, token count, etc.)
 *   [30]    checksum somme XOR des octets [0..29]
 *   [31]    reserved 0x00
 */

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define OO_NET_MAGIC0   0x00u
#define OO_NET_MAGIC1   0x4Fu   /* 'O' */
#define OO_NET_PKT_SIZE 32

typedef enum {
    OO_PKT_HELLO     = 0x01,  /* présence + node_id */
    OO_PKT_TOKEN     = 0x02,  /* 4 token IDs packed (uint16 x2 in payload[0..3]) */
    OO_PKT_DNA_SYNC  = 0x03,  /* DNA hash + 4 float32 params delta */
    OO_PKT_ACK       = 0x04,  /* acknowledge seq N */
    OO_PKT_PING      = 0x05,  /* keepalive */
    OO_PKT_TEXT      = 0x06,  /* 20 bytes ASCII text fragment */
} OoNetPktType;

typedef struct __attribute__((packed)) {
    uint8_t  magic[2];        /* [0..1]  0x00, 0x4F */
    uint8_t  type;            /* [2]     OoNetPktType */
    uint8_t  src_id;          /* [3]     sender node id */
    uint8_t  dst_id;          /* [4]     dest node id, 0xFF=broadcast */
    uint8_t  seq;             /* [5]     sequence number mod 256 */
    uint8_t  payload[20];     /* [6..25] type-specific data */
    uint32_t extra;           /* [26..29] DNA hash or misc */
    uint8_t  checksum;        /* [30]    XOR of bytes [0..29] */
    uint8_t  reserved;        /* [31]    0x00 */
} OoNetPacket;

/* Build a valid OO-NET packet (fills magic + checksum). */
void oo_net_pkt_build(OoNetPacket *pkt,
                      OoNetPktType type,
                      uint8_t src_id, uint8_t dst_id,
                      uint8_t seq,
                      const uint8_t *payload, uint32_t payload_len,
                      uint32_t extra);

/* Verify magic + checksum. Returns 1 if valid, 0 if corrupt. */
int oo_net_pkt_valid(const OoNetPacket *pkt);

/* Helpers to pack/unpack text payload (null-terminated, max 20 chars). */
void oo_net_pkt_set_text(OoNetPacket *pkt, const char *text);
void oo_net_pkt_get_text(const OoNetPacket *pkt, char *out, uint32_t cap);

/* Pack/unpack a DNA hash + 2 float32 delta fields into payload. */
void oo_net_pkt_set_dna(OoNetPacket *pkt, uint32_t dna_hash,
                        float delta_temp, float delta_topp);
void oo_net_pkt_get_dna(const OoNetPacket *pkt, uint32_t *dna_hash,
                        float *delta_temp, float *delta_topp);

/* Serialize/deserialize packet to/from raw 32-byte buffer. */
void oo_net_pkt_to_bytes(const OoNetPacket *pkt, uint8_t out[OO_NET_PKT_SIZE]);
int  oo_net_pkt_from_bytes(OoNetPacket *pkt, const uint8_t buf[OO_NET_PKT_SIZE]);

const char *oo_net_pkt_type_name(OoNetPktType t);

#ifdef __cplusplus
}
#endif
