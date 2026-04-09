// oo_neuralfs2.h — NeuralFS v2: Persistent RAM Key-Value Store (Phase F)
//
// A fast, RAM-resident key-value filesystem stored in LLMK_ARENA_ZONE_C.
// Survives across REPL sessions (within one boot). Can be serialized to EFI
// FAT as OO_NFS2.BIN for cross-boot persistence.
//
// Design:
//   - Up to NFS2_MAX_RECORDS (64) records
//   - Each record: name[64] + data[384] + metadata
//   - Header: magic "NFS2" + version + record_count
//   - Zero-copy: data stored inline in record struct
//   - Find by name: linear scan (64 records × 64B names ≈ 4KB, fast enough)
//
// Freestanding C11 — no libc, no malloc.

#pragma once

// ============================================================
// Constants
// ============================================================

#define NFS2_MAGIC         0x324653464F4F0000ULL   // "OFS2\0\0\0\0"
#define NFS2_VERSION       1
#define NFS2_MAX_RECORDS   64
#define NFS2_NAME_MAX      64
#define NFS2_DATA_MAX      384

// Flags
#define NFS2_FLAG_USED     0x01U
#define NFS2_FLAG_READONLY 0x02U
#define NFS2_FLAG_BINARY   0x04U

// ============================================================
// Types
// ============================================================

typedef struct {
    char         name[NFS2_NAME_MAX];   // null-terminated key
    unsigned int data_len;              // bytes used in data[]
    unsigned int flags;                 // NFS2_FLAG_*
    unsigned int write_count;           // times this record was written
    unsigned int reserved;
    char         data[NFS2_DATA_MAX];   // value (text or binary)
} Nfs2Record;  // 464 bytes

typedef struct {
    unsigned long long magic;           // NFS2_MAGIC
    unsigned int       version;
    unsigned int       record_count;    // active records
    unsigned int       total_writes;
    unsigned int       reserved;
    Nfs2Record         records[NFS2_MAX_RECORDS];
} Nfs2Store;   // ~29 KB — fits comfortably in ZONE_C

// ============================================================
// Inline helpers
// ============================================================

static inline int _nfs2_namecmp(const char *a, const char *b) {
    while (*a && *b && *a == *b) { a++; b++; }
    return (int)(unsigned char)*a - (int)(unsigned char)*b;
}

static inline void _nfs2_namecpy(char *dst, const char *src, int cap) {
    int i = 0;
    while (i < cap - 1 && src[i]) { dst[i] = src[i]; i++; }
    dst[i] = '\0';
}

static inline int _nfs2_datalen(const char *s) {
    int n = 0;
    while (s[n]) n++;
    return n;
}

static inline void _nfs2_datacpy(char *dst, const char *src, int len) {
    for (int i = 0; i < len; i++) dst[i] = src[i];
}

// ============================================================
// API
// ============================================================

// Initialize store (zeroes all records, sets magic)
static inline void nfs2_init(Nfs2Store *s) {
    if (!s) return;
    // Zero all fields manually (freestanding)
    char *p = (char *)s;
    for (int i = 0; i < (int)sizeof(Nfs2Store); i++) p[i] = 0;
    s->magic   = NFS2_MAGIC;
    s->version = NFS2_VERSION;
}

// Find record by name (returns index or -1)
static inline int nfs2_find(const Nfs2Store *s, const char *name) {
    if (!s || !name) return -1;
    for (int i = 0; i < NFS2_MAX_RECORDS; i++) {
        if ((s->records[i].flags & NFS2_FLAG_USED) &&
             _nfs2_namecmp(s->records[i].name, name) == 0)
            return i;
    }
    return -1;
}

// Find a free slot (returns index or -1 if full)
static inline int nfs2_free_slot(const Nfs2Store *s) {
    for (int i = 0; i < NFS2_MAX_RECORDS; i++) {
        if (!(s->records[i].flags & NFS2_FLAG_USED)) return i;
    }
    return -1;
}

// Write (create or update) a text record
// Returns 0 on success, -1 if store full, -2 if readonly, -3 data too long
static inline int nfs2_write(Nfs2Store *s, const char *name, const char *data) {
    if (!s || !name || !data) return -1;
    int idx = nfs2_find(s, name);
    if (idx < 0) {
        idx = nfs2_free_slot(s);
        if (idx < 0) return -1;  // full
        _nfs2_namecpy(s->records[idx].name, name, NFS2_NAME_MAX);
        s->records[idx].flags = NFS2_FLAG_USED;
        s->record_count++;
    } else if (s->records[idx].flags & NFS2_FLAG_READONLY) {
        return -2;
    }
    int dlen = _nfs2_datalen(data);
    if (dlen >= NFS2_DATA_MAX) dlen = NFS2_DATA_MAX - 1;
    _nfs2_datacpy(s->records[idx].data, data, dlen);
    s->records[idx].data[dlen] = '\0';
    s->records[idx].data_len   = (unsigned int)dlen;
    s->records[idx].write_count++;
    s->total_writes++;
    return 0;
}

// Read a record by name — returns pointer to data or NULL
static inline const char *nfs2_read(const Nfs2Store *s, const char *name) {
    int idx = nfs2_find(s, name);
    if (idx < 0) return (const char *)0;
    return s->records[idx].data;
}

// Delete a record by name
// Returns 0 on success, -1 not found, -2 readonly
static inline int nfs2_delete(Nfs2Store *s, const char *name) {
    int idx = nfs2_find(s, name);
    if (idx < 0) return -1;
    if (s->records[idx].flags & NFS2_FLAG_READONLY) return -2;
    // Zero the slot
    char *p = (char *)&s->records[idx];
    for (int i = 0; i < (int)sizeof(Nfs2Record); i++) p[i] = 0;
    s->record_count = (s->record_count > 0) ? s->record_count - 1 : 0;
    return 0;
}

// Append to an existing record (or create). Returns 0 or error code.
static inline int nfs2_append(Nfs2Store *s, const char *name, const char *more) {
    if (!s || !name || !more) return -1;
    int idx = nfs2_find(s, name);
    if (idx < 0) return nfs2_write(s, name, more);
    if (s->records[idx].flags & NFS2_FLAG_READONLY) return -2;
    unsigned int cur = s->records[idx].data_len;
    int mlen = _nfs2_datalen(more);
    if ((int)cur + mlen >= NFS2_DATA_MAX) mlen = NFS2_DATA_MAX - 1 - (int)cur;
    if (mlen <= 0) return -3;
    _nfs2_datacpy(s->records[idx].data + cur, more, mlen);
    s->records[idx].data_len += (unsigned int)mlen;
    s->records[idx].data[s->records[idx].data_len] = '\0';
    s->records[idx].write_count++;
    s->total_writes++;
    return 0;
}

// Serialize store to a flat byte buffer (for EFI FAT write)
// Returns bytes written (sizeof Nfs2Store = ~29KB)
static inline unsigned int nfs2_serialize(const Nfs2Store *s, char *buf, unsigned int cap) {
    unsigned int sz = (unsigned int)sizeof(Nfs2Store);
    if (sz > cap) sz = cap;
    const char *src = (const char *)s;
    for (unsigned int i = 0; i < sz; i++) buf[i] = src[i];
    return sz;
}

// Deserialize from byte buffer
static inline int nfs2_deserialize(Nfs2Store *s, const char *buf, unsigned int len) {
    if (!s || !buf || len < sizeof(Nfs2Store)) return -1;
    const Nfs2Store *src = (const Nfs2Store *)buf;
    if (src->magic != NFS2_MAGIC) return -2;
    char *dst = (char *)s;
    for (unsigned int i = 0; i < sizeof(Nfs2Store); i++) dst[i] = buf[i];
    return 0;
}
