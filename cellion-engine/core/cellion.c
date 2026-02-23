#include "cellion.h"

static int cellion_ascii_streq_n(const char *a, const uint8_t *b, size_t bn) {
    if (!a || !b) return 0;
    size_t i = 0;
    for (; a[i] && i < bn; i++) {
        if ((uint8_t)a[i] != b[i]) return 0;
    }
    return (a[i] == 0 && i == bn);
}

static int cellion_read_u32_leb(const uint8_t *p, size_t len, size_t *io_off, uint32_t *out) {
    if (!p || !io_off || !out) return 0;
    size_t off = *io_off;
    uint32_t result = 0;
    uint32_t shift = 0;

    for (int i = 0; i < 5; i++) {
        if (off >= len) return 0;
        uint8_t byte = p[off++];
        result |= (uint32_t)(byte & 0x7Fu) << shift;
        if ((byte & 0x80u) == 0) {
            *io_off = off;
            *out = result;
            return 1;
        }
        shift += 7;
    }
    return 0;
}

void cellion_init(CellionEngine *e) {
    if (!e) return;
    e->last_error = 0;
}

int cellion_wasm_find_custom_section(
    CellionEngine *e,
    const uint8_t *wasm,
    size_t wasm_len,
    const char *custom_name_ascii,
    const uint8_t **out_data,
    size_t *out_len
) {
    if (out_data) *out_data = NULL;
    if (out_len) *out_len = 0;
    if (!e || !wasm || wasm_len < 8 || !custom_name_ascii || !out_data || !out_len) {
        if (e) e->last_error = CELLION_ERR_INVALID;
        return CELLION_ERR_INVALID;
    }

    // Magic: 00 61 73 6d, Version: 01 00 00 00
    if (!(wasm[0] == 0x00 && wasm[1] == 0x61 && wasm[2] == 0x73 && wasm[3] == 0x6d)) {
        e->last_error = CELLION_ERR_INVALID;
        return CELLION_ERR_INVALID;
    }
    if (!(wasm[4] == 0x01 && wasm[5] == 0x00 && wasm[6] == 0x00 && wasm[7] == 0x00)) {
        e->last_error = CELLION_ERR_INVALID;
        return CELLION_ERR_INVALID;
    }

    size_t off = 8;
    while (off < wasm_len) {
        // Section id (varuint7, but encoded as a byte here in practice)
        uint8_t id = wasm[off++];
        uint32_t payload_len_u32 = 0;
        if (!cellion_read_u32_leb(wasm, wasm_len, &off, &payload_len_u32)) {
            e->last_error = CELLION_ERR_TRUNCATED;
            return CELLION_ERR_TRUNCATED;
        }
        size_t payload_len = (size_t)payload_len_u32;
        if (payload_len > wasm_len - off) {
            e->last_error = CELLION_ERR_TRUNCATED;
            return CELLION_ERR_TRUNCATED;
        }

        if (id == 0) {
            size_t sec_off = off;
            uint32_t name_len_u32 = 0;
            if (!cellion_read_u32_leb(wasm, wasm_len, &sec_off, &name_len_u32)) {
                e->last_error = CELLION_ERR_TRUNCATED;
                return CELLION_ERR_TRUNCATED;
            }
            size_t name_len = (size_t)name_len_u32;
            if (name_len > wasm_len - sec_off) {
                e->last_error = CELLION_ERR_TRUNCATED;
                return CELLION_ERR_TRUNCATED;
            }

            // Ensure name is within this section's payload.
            if (sec_off + name_len > off + payload_len) {
                e->last_error = CELLION_ERR_TRUNCATED;
                return CELLION_ERR_TRUNCATED;
            }

            if (cellion_ascii_streq_n(custom_name_ascii, wasm + sec_off, name_len)) {
                const uint8_t *data = wasm + (sec_off + name_len);
                size_t data_len = (off + payload_len) - (sec_off + name_len);
                *out_data = data;
                *out_len = data_len;
                e->last_error = CELLION_OK;
                return CELLION_OK;
            }
        }

        off += payload_len;
    }

    e->last_error = CELLION_ERR_NOT_FOUND;
    return CELLION_ERR_NOT_FOUND;
}
