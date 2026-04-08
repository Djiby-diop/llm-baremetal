/*
 * oo-crypto — SHA-256 + ChaCha20 + CSPRNG + DNA signing
 * Freestanding: no libc, no OS, no external deps
 */
#include "oo_crypto.h"

/* ── SHA-256 constants ────────────────────────────────────────────── */
static const uint32_t K[64] = {
    0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,
    0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,
    0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,
    0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,
    0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,
    0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,
    0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,
    0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,
    0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,
    0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,
    0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,
    0xd192e819,0xd6990624,0xf40e3585,0x106aa070,
    0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,
    0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,
    0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,
    0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2
};

#define ROTR32(v,n) (((v)>>(n))|((v)<<(32-(n))))
#define CH(x,y,z)  (((x)&(y))^(~(x)&(z)))
#define MAJ(x,y,z) (((x)&(y))^((x)&(z))^((y)&(z)))
#define S0(x) (ROTR32(x,2)^ROTR32(x,13)^ROTR32(x,22))
#define S1(x) (ROTR32(x,6)^ROTR32(x,11)^ROTR32(x,25))
#define s0(x) (ROTR32(x,7)^ROTR32(x,18)^((x)>>3))
#define s1(x) (ROTR32(x,17)^ROTR32(x,19)^((x)>>10))

static void sha256_compress(uint32_t s[8], const uint8_t block[64]) {
    uint32_t w[64], a,b,c,d,e,f,g,h,t1,t2;
    for (int i=0;i<16;i++)
        w[i]=(uint32_t)block[i*4]<<24|(uint32_t)block[i*4+1]<<16|
             (uint32_t)block[i*4+2]<<8|(uint32_t)block[i*4+3];
    for (int i=16;i<64;i++)
        w[i]=s1(w[i-2])+w[i-7]+s0(w[i-15])+w[i-16];
    a=s[0];b=s[1];c=s[2];d=s[3];e=s[4];f=s[5];g=s[6];h=s[7];
    for (int i=0;i<64;i++) {
        t1=h+S1(e)+CH(e,f,g)+K[i]+w[i];
        t2=S0(a)+MAJ(a,b,c);
        h=g;g=f;f=e;e=d+t1;d=c;c=b;b=a;a=t1+t2;
    }
    s[0]+=a;s[1]+=b;s[2]+=c;s[3]+=d;
    s[4]+=e;s[5]+=f;s[6]+=g;s[7]+=h;
}

void oo_sha256_init(OoCryptoSha256Ctx *ctx) {
    ctx->state[0]=0x6a09e667;ctx->state[1]=0xbb67ae85;
    ctx->state[2]=0x3c6ef372;ctx->state[3]=0xa54ff53a;
    ctx->state[4]=0x510e527f;ctx->state[5]=0x9b05688c;
    ctx->state[6]=0x1f83d9ab;ctx->state[7]=0x5be0cd19;
    ctx->count=0;ctx->buf_len=0;
}

void oo_sha256_update(OoCryptoSha256Ctx *ctx, const uint8_t *data, int len) {
    for (int i=0;i<len;i++) {
        ctx->buf[ctx->buf_len++]=data[i];
        if (ctx->buf_len==64) {
            sha256_compress(ctx->state,ctx->buf);
            ctx->count+=512;ctx->buf_len=0;
        }
    }
}

void oo_sha256_final(OoCryptoSha256Ctx *ctx, uint8_t digest[32]) {
    ctx->count+=(uint64_t)ctx->buf_len*8;
    ctx->buf[ctx->buf_len++]=0x80;
    if (ctx->buf_len>56) {
        while (ctx->buf_len<64) ctx->buf[ctx->buf_len++]=0;
        sha256_compress(ctx->state,ctx->buf);
        ctx->buf_len=0;
    }
    while (ctx->buf_len<56) ctx->buf[ctx->buf_len++]=0;
    for (int i=0;i<8;i++)
        ctx->buf[56+i]=(uint8_t)(ctx->count>>(56-8*i));
    sha256_compress(ctx->state,ctx->buf);
    for (int i=0;i<8;i++) {
        digest[i*4  ]=(uint8_t)(ctx->state[i]>>24);
        digest[i*4+1]=(uint8_t)(ctx->state[i]>>16);
        digest[i*4+2]=(uint8_t)(ctx->state[i]>>8);
        digest[i*4+3]=(uint8_t)(ctx->state[i]);
    }
}

void oo_sha256(const uint8_t *data, int len, uint8_t digest[32]) {
    OoCryptoSha256Ctx ctx;
    oo_sha256_init(&ctx);
    oo_sha256_update(&ctx, data, len);
    oo_sha256_final(&ctx, digest);
}

/* ── ChaCha20 ───────────────────────────────────────────────────────── */
#define ROTL32(v,n) (((v)<<(n))|((v)>>(32-(n))))
#define QR(a,b,c,d) \
    a+=b;d^=a;d=ROTL32(d,16); \
    c+=d;b^=c;b=ROTL32(b,12); \
    a+=b;d^=a;d=ROTL32(d,8);  \
    c+=d;b^=c;b=ROTL32(b,7);

static void chacha20_block(const uint32_t in[16], uint8_t out[64]) {
    uint32_t x[16];
    for (int i=0;i<16;i++) x[i]=in[i];
    for (int i=0;i<10;i++) {
        QR(x[0],x[4],x[8], x[12]) QR(x[1],x[5],x[9], x[13])
        QR(x[2],x[6],x[10],x[14]) QR(x[3],x[7],x[11],x[15])
        QR(x[0],x[5],x[10],x[15]) QR(x[1],x[6],x[11],x[12])
        QR(x[2],x[7],x[8], x[13]) QR(x[3],x[4],x[9], x[14])
    }
    for (int i=0;i<16;i++) {
        uint32_t v=x[i]+in[i];
        out[i*4  ]=(uint8_t)v;
        out[i*4+1]=(uint8_t)(v>>8);
        out[i*4+2]=(uint8_t)(v>>16);
        out[i*4+3]=(uint8_t)(v>>24);
    }
}

static uint32_t le32(const uint8_t *b) {
    return (uint32_t)b[0]|(uint32_t)b[1]<<8|(uint32_t)b[2]<<16|(uint32_t)b[3]<<24;
}

void oo_chacha20_init(OoCryptoChaCha20Ctx *ctx, const uint8_t key[32],
                      const uint8_t nonce[12], uint32_t counter) {
    ctx->state[0]=0x61707865; ctx->state[1]=0x3320646e;
    ctx->state[2]=0x79622d32; ctx->state[3]=0x6b206574;
    for (int i=0;i<8;i++) ctx->state[4+i]=le32(key+i*4);
    ctx->state[12]=counter;
    ctx->state[13]=le32(nonce);
    ctx->state[14]=le32(nonce+4);
    ctx->state[15]=le32(nonce+8);
    ctx->pos=64; /* force refill on first use */
}

void oo_chacha20_xor(OoCryptoChaCha20Ctx *ctx,
                     const uint8_t *in, uint8_t *out, int len) {
    for (int i=0;i<len;i++) {
        if (ctx->pos==64) {
            chacha20_block(ctx->state, ctx->keystream);
            ctx->state[12]++;
            ctx->pos=0;
        }
        out[i]=in[i]^ctx->keystream[ctx->pos++];
    }
}

/* ── FNV-1a 64-bit ──────────────────────────────────────────────────── */
uint64_t oo_fnv1a_64(const uint8_t *data, int len) {
    uint64_t h=0xcbf29ce484222325ULL;
    for (int i=0;i<len;i++) h=(h^data[i])*0x00000100000001b3ULL;
    return h;
}

/* ── CSPRNG ─────────────────────────────────────────────────────────── */
int oo_csprng_init(OoCryptoCsprng *ctx) {
    uint8_t seed[32];
    /* Try RDRAND x86 instruction */
    int ok=0;
    for (int attempt=0;attempt<10&&!ok;attempt++) {
        uint64_t r=0;
        __asm__ volatile("rdrand %0; setc %1" : "=r"(r), "=r"(ok));
        if (ok) {
            for (int i=0;i<8;i++) seed[i*4  ]=(uint8_t)(r>>i*8);
        }
    }
    if (!ok) {
        /* fallback: TSC-based seed (not cryptographic, better than nothing) */
        uint64_t tsc;
        __asm__ volatile("rdtsc" : "=A"(tsc));
        oo_sha256((const uint8_t*)&tsc, 8, seed);
    }
    uint8_t nonce[12]={0};
    oo_chacha20_init(&ctx->chacha, seed, nonce, 0);
    ctx->initialized=1;
    return ok;
}

void oo_csprng_bytes(OoCryptoCsprng *ctx, uint8_t *out, int len) {
    /* XOR zeroes with keystream = pure keystream */
    for (int i=0;i<len;i++) out[i]=0;
    oo_chacha20_xor(&ctx->chacha, out, out, len);
}

uint32_t oo_csprng_u32(OoCryptoCsprng *ctx) {
    uint8_t b[4]; oo_csprng_bytes(ctx,b,4);
    return le32(b);
}
uint64_t oo_csprng_u64(OoCryptoCsprng *ctx) {
    return ((uint64_t)oo_csprng_u32(ctx)<<32)|oo_csprng_u32(ctx);
}

/* ── DNA signing ────────────────────────────────────────────────────── */
void oo_dna_sign(const uint8_t *hw_fp, int hw_fp_len,
                 const uint8_t *model_hash, int model_hash_len,
                 uint8_t dna_out[32]) {
    OoCryptoSha256Ctx ctx;
    oo_sha256_init(&ctx);
    oo_sha256_update(&ctx, hw_fp, hw_fp_len);
    oo_sha256_update(&ctx, model_hash, model_hash_len);
    oo_sha256_final(&ctx, dna_out);
}

uint32_t oo_dna_to_u32(const uint8_t dna[32]) {
    return (uint32_t)dna[0]<<24|(uint32_t)dna[4]<<16|
           (uint32_t)dna[8]<<8|(uint32_t)dna[12];
}
