#include "hermes_json.h"

#include <string.h>

static int hermes_is_space(char c) {
    return (c == ' ' || c == '\t' || c == '\r' || c == '\n');
}

static const char* hermes_skip_ws(const char* p, const char* end) {
    while (p < end && hermes_is_space(*p)) p++;
    return p;
}

static hermes_status_t hermes_append_char(char** dst, size_t* left, char c) {
    if (*left < 1) return HERMES_ERR_BAD_LENGTH;
    **dst = c;
    (*dst)++;
    (*left)--;
    return HERMES_OK;
}

static hermes_status_t hermes_append_str(char** dst, size_t* left, const char* s) {
    size_t n = strlen(s);
    if (*left < n) return HERMES_ERR_BAD_LENGTH;
    memcpy(*dst, s, n);
    (*dst) += n;
    (*left) -= n;
    return HERMES_OK;
}

static hermes_status_t hermes_append_u64(char** dst, size_t* left, uint64_t v) {
    char tmp[32];
    size_t i = 0;
    if (v == 0) {
        tmp[i++] = '0';
    } else {
        while (v && i < sizeof(tmp)) {
            tmp[i++] = (char)('0' + (v % 10));
            v /= 10;
        }
    }
    if (*left < i) return HERMES_ERR_BAD_LENGTH;
    while (i--) {
        **dst = tmp[i];
        (*dst)++;
        (*left)--;
    }
    return HERMES_OK;
}

static const char* hermes_kind_to_str(hermes_kind_t k) {
    switch (k) {
        case HERMES_KIND_COMMAND: return "COMMAND";
        case HERMES_KIND_EVENT: return "EVENT";
        case HERMES_KIND_RESPONSE: return "RESPONSE";
        default: return "UNKNOWN";
    }
}

static hermes_status_t hermes_append_json_string(char** dst, size_t* left, const char* s, size_t n) {
    hermes_status_t st = hermes_append_char(dst, left, '"');
    if (st != HERMES_OK) return st;
    for (size_t i = 0; i < n; i++) {
        char c = s[i];
        switch (c) {
            case '"':
                st = hermes_append_str(dst, left, "\\\"");
                break;
            case '\\':
                st = hermes_append_str(dst, left, "\\\\");
                break;
            case '\n':
                st = hermes_append_str(dst, left, "\\n");
                break;
            case '\r':
                st = hermes_append_str(dst, left, "\\r");
                break;
            case '\t':
                st = hermes_append_str(dst, left, "\\t");
                break;
            default:
                st = hermes_append_char(dst, left, c);
                break;
        }
        if (st != HERMES_OK) return st;
    }
    return hermes_append_char(dst, left, '"');
}

hermes_status_t hermes_json_encode_msg(
    const hermes_msg_t* msg,
    char* out,
    size_t out_cap,
    size_t* out_len
) {
    if (!msg || !out || !out_len) return HERMES_ERR_INVALID_ARG;
    hermes_status_t st = hermes_validate_header(&msg->header);
    if (st != HERMES_OK) return st;

    char* p = out;
    size_t left = out_cap;

    st = hermes_append_char(&p, &left, '{');
    if (st != HERMES_OK) return st;

    // version
    st = hermes_append_str(&p, &left, "\"v\":{\"maj\":");
    if (st != HERMES_OK) return st;
    st = hermes_append_u64(&p, &left, msg->header.version_major);
    if (st != HERMES_OK) return st;
    st = hermes_append_str(&p, &left, ",\"min\":");
    if (st != HERMES_OK) return st;
    st = hermes_append_u64(&p, &left, msg->header.version_minor);
    if (st != HERMES_OK) return st;
    st = hermes_append_str(&p, &left, "},");
    if (st != HERMES_OK) return st;

    // kind
    st = hermes_append_str(&p, &left, "\"kind\":");
    if (st != HERMES_OK) return st;
    const char* ks = hermes_kind_to_str((hermes_kind_t)msg->header.kind);
    st = hermes_append_json_string(&p, &left, ks, strlen(ks));
    if (st != HERMES_OK) return st;
    st = hermes_append_char(&p, &left, ',');
    if (st != HERMES_OK) return st;

    // numeric fields
    st = hermes_append_str(&p, &left, "\"flags\":");
    if (st != HERMES_OK) return st;
    st = hermes_append_u64(&p, &left, msg->header.flags);
    if (st != HERMES_OK) return st;
    st = hermes_append_str(&p, &left, ",\"payload_len\":");
    if (st != HERMES_OK) return st;
    st = hermes_append_u64(&p, &left, msg->header.payload_len);
    if (st != HERMES_OK) return st;
    st = hermes_append_str(&p, &left, ",\"correlation_id\":");
    if (st != HERMES_OK) return st;
    st = hermes_append_u64(&p, &left, msg->header.correlation_id);
    if (st != HERMES_OK) return st;
    st = hermes_append_str(&p, &left, ",\"source\":");
    if (st != HERMES_OK) return st;
    st = hermes_append_u64(&p, &left, msg->header.source);
    if (st != HERMES_OK) return st;
    st = hermes_append_str(&p, &left, ",\"dest\":");
    if (st != HERMES_OK) return st;
    st = hermes_append_u64(&p, &left, msg->header.dest);
    if (st != HERMES_OK) return st;

    // payload as string (optional)
    if (msg->payload && msg->header.payload_len) {
        const char* ps = (const char*)msg->payload;
        st = hermes_append_str(&p, &left, ",\"payload\":");
        if (st != HERMES_OK) return st;
        st = hermes_append_json_string(&p, &left, ps, msg->header.payload_len);
        if (st != HERMES_OK) return st;
    }

    st = hermes_append_char(&p, &left, '}');
    if (st != HERMES_OK) return st;
    st = hermes_append_char(&p, &left, '\0');
    if (st != HERMES_OK) return st;

    *out_len = (size_t)(p - out - 1);
    return HERMES_OK;
}

static const char* hermes_find_key(const char* json, const char* end, const char* key) {
    // Search for "key" token, but only when it appears as a JSON object key
    // (i.e., a string immediately after "{" or "," and before a ":").
    if (!json || !end || !key) return NULL;

    size_t klen = strlen(key);
    if (klen == 0) return NULL;

    int in_string = 0;
    int escaped = 0;
    int object_depth = 0;
    char last_structural = 0;

    const char* p = json;
    while (p < end) {
        char c = *p;

        if (in_string) {
            if (escaped) {
                // Current character is escaped; consume it without special meaning.
                escaped = 0;
            } else if (c == '\\') {
                // Start of an escape sequence.
                escaped = 1;
            } else if (c == '"') {
                // End of string literal.
                in_string = 0;
            }
            p++;
            continue;
        }

        switch (c) {
        case '"':
            // Potential start of a key name: only valid directly inside an object
            // and after "{" or "," (i.e. where a key is allowed by JSON grammar).
            if (object_depth > 0 && (last_structural == '{' || last_structural == ',')) {
                const char* key_start = p + 1;
                const char* key_end = key_start + klen;
                if (key_end < end && memcmp(key_start, key, klen) == 0 && *key_end == '"') {
                    // Found the string "key". Now ensure it's followed (after optional
                    // whitespace) by a colon, as required for an object member.
                    const char* q = key_end + 1;
                    while (q < end && hermes_is_space(*q)) {
                        q++;
                    }
                    if (q < end && *q == ':') {
                        // To preserve existing behaviour, return a pointer just after the
                        // closing quote of the key. Callers then skip whitespace and
                        // expect to see the ':' character.
                        return key_end + 1;
                    }
                }
            }
            in_string = 1;
            p++;
            break;

        case '{':
            object_depth++;
            last_structural = '{';
            p++;
            break;

        case '}':
            if (object_depth > 0) {
                object_depth--;
            }
            last_structural = '}';
            p++;
            break;

        case ',':
            last_structural = ',';
            p++;
            break;

        case '[':
        case ']':
        case ':':
            last_structural = c;
            p++;
            break;

        default:
            if (!hermes_is_space(c)) {
                // Any other non-whitespace character outside of strings is not a
                // structural delimiter we care about for key detection, but we
                // record that we're no longer immediately after "{" or ",".
                last_structural = c;
            }
            p++;
            break;
        }
    }
    return NULL;
}

static hermes_status_t hermes_parse_u64_after_colon(const char* p, const char* end, uint64_t* out) {
    if (!p || !out) return HERMES_ERR_INVALID_ARG;
    p = hermes_skip_ws(p, end);
    if (p >= end || *p != ':') return HERMES_ERR_INVALID_ARG;
    p++;
    p = hermes_skip_ws(p, end);
    if (p >= end) return HERMES_ERR_INVALID_ARG;

    uint64_t v = 0;
    int any = 0;
    while (p < end && *p >= '0' && *p <= '9') {
        any = 1;
        v = (v * 10u) + (uint64_t)(*p - '0');
        p++;
    }
    if (!any) return HERMES_ERR_INVALID_ARG;
    *out = v;
    return HERMES_OK;
}

static hermes_status_t hermes_parse_string_after_colon(const char* p, const char* end, char* out, size_t out_cap, size_t* out_len) {
    if (!p || !out_len) return HERMES_ERR_INVALID_ARG;
    p = hermes_skip_ws(p, end);
    if (p >= end || *p != ':') return HERMES_ERR_INVALID_ARG;
    p++;
    p = hermes_skip_ws(p, end);
    if (p >= end || *p != '"') return HERMES_ERR_INVALID_ARG;
    p++;

    size_t n = 0;
    while (p < end) {
        char c = *p++;
        if (c == '"') break;
        if (c == '\\') {
            if (p >= end) return HERMES_ERR_INVALID_ARG;
            char esc = *p++;
            switch (esc) {
                case '"': c = '"'; break;
                case '\\': c = '\\'; break;
                case 'n': c = '\n'; break;
                case 'r': c = '\r'; break;
                case 't': c = '\t'; break;
                default: return HERMES_ERR_INVALID_ARG;
            }
        }
        if (out) {
            if (n + 1 >= out_cap) return HERMES_ERR_BAD_LENGTH;
            out[n] = c;
        }
        n++;
    }

    if (out) out[n] = '\0';
    *out_len = n;
    return HERMES_OK;
}

static hermes_kind_t hermes_kind_from_str(const char* s, size_t n) {
    if (n == 7 && memcmp(s, "COMMAND", 7) == 0) return HERMES_KIND_COMMAND;
    if (n == 5 && memcmp(s, "EVENT", 5) == 0) return HERMES_KIND_EVENT;
    if (n == 8 && memcmp(s, "RESPONSE", 8) == 0) return HERMES_KIND_RESPONSE;
    return (hermes_kind_t)0;
}

hermes_status_t hermes_json_decode_msg(
    const char* json,
    size_t json_len,
    hermes_msg_t* out_msg,
    char* payload_buf,
    size_t payload_buf_cap
) {
    if (!json || !out_msg) return HERMES_ERR_INVALID_ARG;
    const char* end = json + json_len;

    memset(out_msg, 0, sizeof(*out_msg));

    // v.maj and v.min
    const char* vpos = hermes_find_key(json, end, "v");
    if (!vpos) return HERMES_ERR_INVALID_ARG;
    vpos = hermes_skip_ws(vpos, end);
    if (vpos >= end || *vpos != ':') return HERMES_ERR_INVALID_ARG;
    vpos++;
    vpos = hermes_skip_ws(vpos, end);
    if (vpos >= end || *vpos != '{') return HERMES_ERR_INVALID_ARG;

    uint64_t maj = 0, min = 0;
    const char* majpos = hermes_find_key(vpos, end, "maj");
    const char* minpos = hermes_find_key(vpos, end, "min");
    if (!majpos || !minpos) return HERMES_ERR_INVALID_ARG;
    if (hermes_parse_u64_after_colon(majpos, end, &maj) != HERMES_OK) return HERMES_ERR_INVALID_ARG;
    if (hermes_parse_u64_after_colon(minpos, end, &min) != HERMES_OK) return HERMES_ERR_INVALID_ARG;
    out_msg->header.version_major = (uint16_t)maj;
    out_msg->header.version_minor = (uint16_t)min;

    // kind
    const char* kpos = hermes_find_key(json, end, "kind");
    if (!kpos) return HERMES_ERR_INVALID_ARG;
    size_t klen = 0;
    char ktmp[16];
    if (hermes_parse_string_after_colon(kpos, end, ktmp, sizeof(ktmp), &klen) != HERMES_OK) return HERMES_ERR_INVALID_ARG;
    hermes_kind_t kind = hermes_kind_from_str(ktmp, klen);
    if ((int)kind == 0) return HERMES_ERR_INVALID_ARG;
    out_msg->header.kind = (uint16_t)kind;

    // numeric fields
    uint64_t u = 0;
    const char* fpos = hermes_find_key(json, end, "flags");
    if (!fpos) return HERMES_ERR_INVALID_ARG;
    if (hermes_parse_u64_after_colon(fpos, end, &u) != HERMES_OK) return HERMES_ERR_INVALID_ARG;
    out_msg->header.flags = (uint16_t)u;

    const char* plpos = hermes_find_key(json, end, "payload_len");
    if (!plpos) return HERMES_ERR_INVALID_ARG;
    if (hermes_parse_u64_after_colon(plpos, end, &u) != HERMES_OK) return HERMES_ERR_INVALID_ARG;
    uint32_t declared_payload_len = (uint32_t)u;
    out_msg->header.payload_len = declared_payload_len;

    const char* cpos = hermes_find_key(json, end, "correlation_id");
    if (!cpos) return HERMES_ERR_INVALID_ARG;
    if (hermes_parse_u64_after_colon(cpos, end, &u) != HERMES_OK) return HERMES_ERR_INVALID_ARG;
    out_msg->header.correlation_id = (uint64_t)u;

    const char* spos = hermes_find_key(json, end, "source");
    if (!spos) return HERMES_ERR_INVALID_ARG;
    if (hermes_parse_u64_after_colon(spos, end, &u) != HERMES_OK) return HERMES_ERR_INVALID_ARG;
    out_msg->header.source = (uint64_t)u;

    const char* dpos = hermes_find_key(json, end, "dest");
    if (!dpos) return HERMES_ERR_INVALID_ARG;
    if (hermes_parse_u64_after_colon(dpos, end, &u) != HERMES_OK) return HERMES_ERR_INVALID_ARG;
    out_msg->header.dest = (uint64_t)u;

    // payload (optional)
    const char* ppos = hermes_find_key(json, end, "payload");
    if (ppos) {
        size_t n = 0;
        if (!payload_buf || payload_buf_cap == 0) return HERMES_ERR_BAD_LENGTH;
        hermes_status_t st = hermes_parse_string_after_colon(ppos, end, payload_buf, payload_buf_cap, &n);
        if (st != HERMES_OK) return st;
        if (declared_payload_len != (uint32_t)n) return HERMES_ERR_INVALID_ARG;
        out_msg->payload = payload_buf;
        out_msg->header.payload_len = (uint32_t)n;
    } else {
        if (declared_payload_len != 0) return HERMES_ERR_INVALID_ARG;
        out_msg->payload = NULL;
        out_msg->header.payload_len = 0;
    }

    return hermes_validate_header(&out_msg->header);
}
