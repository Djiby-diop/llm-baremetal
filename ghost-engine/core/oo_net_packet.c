/*
 * OO-NET packet codec — freestanding C, no stdlib.
 *
 * float32 packing uses union aliasing (safe with -fno-strict-aliasing,
 * which gnu-efi build already implies via -ffreestanding).
 */

#include "oo_net_packet.h"

/* ---------- helpers -------------------------------------------- */

static uint8_t oo_xor_checksum(const uint8_t *buf, uint32_t len) {
    uint8_t c = 0;
    for (uint32_t i = 0; i < len; i++) c ^= buf[i];
    return c;
}

static void oo_memcpy(uint8_t *dst, const uint8_t *src, uint32_t n) {
    for (uint32_t i = 0; i < n; i++) dst[i] = src[i];
}

static void oo_memset(uint8_t *dst, uint8_t v, uint32_t n) {
    for (uint32_t i = 0; i < n; i++) dst[i] = v;
}

static uint32_t oo_strlen(const char *s) {
    uint32_t n = 0;
    while (s[n]) n++;
    return n;
}

static inline uint32_t f32_to_u32(float f) {
    union { float f; uint32_t u; } x; x.f = f; return x.u;
}
static inline float u32_to_f32(uint32_t u) {
    union { float f; uint32_t u; } x; x.u = u; return x.f;
}

/* ---------- core API ------------------------------------------- */

void oo_net_pkt_build(OoNetPacket *pkt,
                      OoNetPktType type,
                      uint8_t src_id, uint8_t dst_id,
                      uint8_t seq,
                      const uint8_t *payload, uint32_t payload_len,
                      uint32_t extra) {
    if (!pkt) return;
    oo_memset((uint8_t *)pkt, 0, OO_NET_PKT_SIZE);
    pkt->magic[0] = OO_NET_MAGIC0;
    pkt->magic[1] = OO_NET_MAGIC1;
    pkt->type     = (uint8_t)type;
    pkt->src_id   = src_id;
    pkt->dst_id   = dst_id;
    pkt->seq      = seq;
    pkt->extra    = extra;
    pkt->reserved = 0;
    if (payload && payload_len > 0) {
        uint32_t n = payload_len < 20 ? payload_len : 20;
        oo_memcpy(pkt->payload, payload, n);
    }
    /* compute checksum over bytes [0..29] */
    uint8_t tmp[OO_NET_PKT_SIZE];
    oo_net_pkt_to_bytes(pkt, tmp);
    pkt->checksum = oo_xor_checksum(tmp, 30);
    /* re-serialize with checksum set */
    tmp[30] = pkt->checksum;
}

int oo_net_pkt_valid(const OoNetPacket *pkt) {
    if (!pkt) return 0;
    if (pkt->magic[0] != OO_NET_MAGIC0) return 0;
    if (pkt->magic[1] != OO_NET_MAGIC1) return 0;
    uint8_t tmp[OO_NET_PKT_SIZE];
    oo_net_pkt_to_bytes(pkt, tmp);
    uint8_t expected = oo_xor_checksum(tmp, 30);
    return (pkt->checksum == expected) ? 1 : 0;
}

void oo_net_pkt_set_text(OoNetPacket *pkt, const char *text) {
    if (!pkt || !text) return;
    uint32_t n = oo_strlen(text);
    if (n > 20) n = 20;
    oo_memset(pkt->payload, 0, 20);
    oo_memcpy(pkt->payload, (const uint8_t *)text, n);
    pkt->type = (uint8_t)OO_PKT_TEXT;
    /* recompute checksum */
    uint8_t tmp[OO_NET_PKT_SIZE];
    oo_net_pkt_to_bytes(pkt, tmp);
    pkt->checksum = oo_xor_checksum(tmp, 30);
}

void oo_net_pkt_get_text(const OoNetPacket *pkt, char *out, uint32_t cap) {
    if (!pkt || !out || cap == 0) return;
    uint32_t n = cap - 1 < 20 ? cap - 1 : 20;
    for (uint32_t i = 0; i < n; i++) out[i] = (char)pkt->payload[i];
    out[n] = 0;
}

void oo_net_pkt_set_dna(OoNetPacket *pkt, uint32_t dna_hash,
                        float delta_temp, float delta_topp) {
    if (!pkt) return;
    oo_memset(pkt->payload, 0, 20);
    /* payload[0..3] = delta_temp as uint32 IEEE754 */
    uint32_t t = f32_to_u32(delta_temp);
    pkt->payload[0] = (uint8_t)( t        & 0xFF);
    pkt->payload[1] = (uint8_t)((t >>  8) & 0xFF);
    pkt->payload[2] = (uint8_t)((t >> 16) & 0xFF);
    pkt->payload[3] = (uint8_t)((t >> 24) & 0xFF);
    /* payload[4..7] = delta_topp */
    uint32_t p = f32_to_u32(delta_topp);
    pkt->payload[4] = (uint8_t)( p        & 0xFF);
    pkt->payload[5] = (uint8_t)((p >>  8) & 0xFF);
    pkt->payload[6] = (uint8_t)((p >> 16) & 0xFF);
    pkt->payload[7] = (uint8_t)((p >> 24) & 0xFF);
    pkt->extra = dna_hash;
    pkt->type  = (uint8_t)OO_PKT_DNA_SYNC;
    uint8_t tmp[OO_NET_PKT_SIZE];
    oo_net_pkt_to_bytes(pkt, tmp);
    pkt->checksum = oo_xor_checksum(tmp, 30);
}

void oo_net_pkt_get_dna(const OoNetPacket *pkt, uint32_t *dna_hash,
                        float *delta_temp, float *delta_topp) {
    if (!pkt) return;
    if (dna_hash)   *dna_hash   = pkt->extra;
    if (delta_temp) {
        uint32_t t = (uint32_t)pkt->payload[0]
                   | ((uint32_t)pkt->payload[1] << 8)
                   | ((uint32_t)pkt->payload[2] << 16)
                   | ((uint32_t)pkt->payload[3] << 24);
        *delta_temp = u32_to_f32(t);
    }
    if (delta_topp) {
        uint32_t p = (uint32_t)pkt->payload[4]
                   | ((uint32_t)pkt->payload[5] << 8)
                   | ((uint32_t)pkt->payload[6] << 16)
                   | ((uint32_t)pkt->payload[7] << 24);
        *delta_topp = u32_to_f32(p);
    }
}

void oo_net_pkt_to_bytes(const OoNetPacket *pkt, uint8_t out[OO_NET_PKT_SIZE]) {
    if (!pkt || !out) return;
    out[0]  = pkt->magic[0];
    out[1]  = pkt->magic[1];
    out[2]  = pkt->type;
    out[3]  = pkt->src_id;
    out[4]  = pkt->dst_id;
    out[5]  = pkt->seq;
    oo_memcpy(out + 6, pkt->payload, 20);
    out[26] = (uint8_t)( pkt->extra        & 0xFF);
    out[27] = (uint8_t)((pkt->extra >>  8) & 0xFF);
    out[28] = (uint8_t)((pkt->extra >> 16) & 0xFF);
    out[29] = (uint8_t)((pkt->extra >> 24) & 0xFF);
    out[30] = pkt->checksum;
    out[31] = pkt->reserved;
}

int oo_net_pkt_from_bytes(OoNetPacket *pkt, const uint8_t buf[OO_NET_PKT_SIZE]) {
    if (!pkt || !buf) return 0;
    pkt->magic[0] = buf[0];
    pkt->magic[1] = buf[1];
    pkt->type     = buf[2];
    pkt->src_id   = buf[3];
    pkt->dst_id   = buf[4];
    pkt->seq      = buf[5];
    oo_memcpy(pkt->payload, buf + 6, 20);
    pkt->extra = (uint32_t)buf[26]
               | ((uint32_t)buf[27] <<  8)
               | ((uint32_t)buf[28] << 16)
               | ((uint32_t)buf[29] << 24);
    pkt->checksum = buf[30];
    pkt->reserved = buf[31];
    return oo_net_pkt_valid(pkt);
}

const char *oo_net_pkt_type_name(OoNetPktType t) {
    switch (t) {
        case OO_PKT_HELLO:    return "HELLO";
        case OO_PKT_TOKEN:    return "TOKEN";
        case OO_PKT_DNA_SYNC: return "DNA_SYNC";
        case OO_PKT_ACK:      return "ACK";
        case OO_PKT_PING:     return "PING";
        case OO_PKT_TEXT:     return "TEXT";
        default:              return "?";
    }
}
