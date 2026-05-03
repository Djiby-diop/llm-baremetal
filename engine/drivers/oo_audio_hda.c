#include "oo_audio_hda.h"

/*
 * OO Intel HDA Audio Driver — bare-metal implementation.
 * CORB/RIRB ring setup, codec verb submission, PCM stream launch.
 * No libc, no malloc — all state in the OoAudioHda struct.
 */

/* ── MMIO helpers ─────────────────────────────────────────────────── */

static inline uint8_t hda_read8(uint64_t base, uint32_t off) {
    return *(volatile uint8_t *)(uintptr_t)(base + off);
}

static inline uint16_t hda_read16(uint64_t base, uint32_t off) {
    return *(volatile uint16_t *)(uintptr_t)(base + off);
}

static inline uint32_t hda_read32(uint64_t base, uint32_t off) {
    return *(volatile uint32_t *)(uintptr_t)(base + off);
}

static inline void hda_write8(uint64_t base, uint32_t off, uint8_t v) {
    *(volatile uint8_t *)(uintptr_t)(base + off) = v;
}

static inline void hda_write16(uint64_t base, uint32_t off, uint16_t v) {
    *(volatile uint16_t *)(uintptr_t)(base + off) = v;
}

static inline void hda_write32(uint64_t base, uint32_t off, uint32_t v) {
    *(volatile uint32_t *)(uintptr_t)(base + off) = v;
}

/* ── Simple spin-delay (busy loop) ───────────────────────────────── */

static void hda_delay(uint32_t iters) {
    for (volatile uint32_t i = 0; i < iters; i++)
        __asm__ volatile ("pause");
}

/* ── CORB static storage (aligned to 128 bytes per spec) ─────────── */
static uint32_t s_corb[HDA_CORB_ENTRIES] __attribute__((aligned(128)));
static uint64_t s_rirb[HDA_RIRB_ENTRIES] __attribute__((aligned(128)));

/* ── Reset controller ────────────────────────────────────────────── */

static int hda_reset(uint64_t base) {
    /* Clear CRST bit to enter reset */
    hda_write32(base, HDA_REG_GCTL, 0);
    for (int i = 0; i < 10000; i++) {
        if ((hda_read32(base, HDA_REG_GCTL) & 1) == 0) break;
        hda_delay(100);
    }

    /* Set CRST to exit reset */
    hda_write32(base, HDA_REG_GCTL, 1);
    for (int i = 0; i < 10000; i++) {
        if (hda_read32(base, HDA_REG_GCTL) & 1) return 0; /* OK */
        hda_delay(100);
    }
    return -1; /* timeout */
}

/* ── CORB setup ──────────────────────────────────────────────────── */

static void hda_corb_init(OoAudioHda *a) {
    uint64_t base = a->mmio_base;
    a->corb    = s_corb;
    uint64_t phys = (uint64_t)(uintptr_t)s_corb;

    /* Stop DMA */
    hda_write8(base, HDA_REG_CORBCTL, 0);
    hda_delay(1000);

    /* Program base address */
    hda_write32(base, HDA_REG_CORBLBASE, (uint32_t)(phys & 0xFFFFFFFF));
    hda_write32(base, HDA_REG_CORBUBASE, (uint32_t)(phys >> 32));

    /* Set size to 256 entries (CORBSIZE[1:0] = 10b) */
    hda_write8(base, HDA_REG_CORBSIZE, 0x02);

    /* Reset write pointer */
    hda_write16(base, HDA_REG_CORBWP, 0);

    /* Reset read pointer: set CORBRPRST, wait, clear */
    hda_write16(base, HDA_REG_CORBRP, 0x8000);
    for (int i = 0; i < 1000; i++) {
        if (hda_read16(base, HDA_REG_CORBRP) & 0x8000) break;
        hda_delay(10);
    }
    hda_write16(base, HDA_REG_CORBRP, 0);

    a->corb_wp = 0;

    /* Start DMA */
    hda_write8(base, HDA_REG_CORBCTL, 0x02);
}

/* ── RIRB setup ──────────────────────────────────────────────────── */

static void hda_rirb_init(OoAudioHda *a) {
    uint64_t base = a->mmio_base;
    a->rirb    = s_rirb;
    uint64_t phys = (uint64_t)(uintptr_t)s_rirb;

    /* Stop DMA */
    hda_write8(base, HDA_REG_RIRBCTL, 0);
    hda_delay(1000);

    hda_write32(base, HDA_REG_RIRBLBASE, (uint32_t)(phys & 0xFFFFFFFF));
    hda_write32(base, HDA_REG_RIRBUBASE, (uint32_t)(phys >> 32));

    /* 256 entries */
    hda_write8(base, HDA_REG_RIRBSIZE, 0x02);

    /* Reset write pointer */
    hda_write16(base, HDA_REG_RIRBWP, 0x8000);

    /* Response interrupt count = 1 (interrupt after every response) */
    hda_write16(base, HDA_REG_RINTCNT, 1);

    a->rirb_rp = 0;

    /* Start DMA */
    hda_write8(base, HDA_REG_RIRBCTL, 0x02);
}

/* ── Send a codec verb and return the response ───────────────────── */

static uint64_t hda_send_verb(OoAudioHda *a, uint32_t verb) {
    uint64_t base = a->mmio_base;

    /* Advance CORB write pointer (wraps at 256) */
    a->corb_wp = (a->corb_wp + 1) & 0xFF;
    a->corb[a->corb_wp] = verb;

    /* Tell hardware about new entry */
    hda_write16(base, HDA_REG_CORBWP, a->corb_wp);

    /* Poll RIRB write pointer for a new response */
    for (int i = 0; i < 50000; i++) {
        uint16_t wp = hda_read16(base, HDA_REG_RIRBWP) & 0xFF;
        if (wp != a->rirb_rp) {
            a->rirb_rp = (a->rirb_rp + 1) & 0xFF;
            return a->rirb[a->rirb_rp];
        }
        hda_delay(10);
    }
    return (uint64_t)-1; /* timeout */
}

/* ── Discover first codec ────────────────────────────────────────── */

static void hda_find_codec(OoAudioHda *a) {
    uint16_t statests = hda_read16(a->mmio_base, HDA_REG_STATESTS);
    a->codec_addr = 0;
    for (uint8_t i = 0; i < 15; i++) {
        if (statests & (1u << i)) {
            a->codec_addr = i;
            break;
        }
    }
}

/* ── Configure output stream 0 for 48 kHz 16-bit stereo PCM ──────── */

static void hda_stream_setup(OoAudioHda *a) {
    uint64_t base  = a->mmio_base;
    uint32_t sd    = HDA_SD_BASE; /* stream descriptor 0 */

    /* Stop stream */
    hda_write8(base, sd + HDA_SD_CTL, 0x00);
    hda_delay(5000);

    /* Reset stream: set SRST, wait, clear */
    hda_write8(base, sd + HDA_SD_CTL, 0x01);
    for (int i = 0; i < 1000; i++) {
        if (hda_read8(base, sd + HDA_SD_CTL) & 0x01) break;
        hda_delay(10);
    }
    hda_write8(base, sd + HDA_SD_CTL, 0x00);
    for (int i = 0; i < 1000; i++) {
        if (!(hda_read8(base, sd + HDA_SD_CTL) & 0x01)) break;
        hda_delay(10);
    }

    /* Build BDL */
    for (int i = 0; i < HDA_BDL_ENTRIES; i++) {
        a->bdl[i].addr = (uint64_t)(uintptr_t)a->pcm_buf[i];
        a->bdl[i].len  = HDA_PCM_BUF_SIZE;
        a->bdl[i].ioc  = 1;
    }

    uint64_t bdl_phys = (uint64_t)(uintptr_t)a->bdl;
    hda_write32(base, sd + HDA_SD_BDPL, (uint32_t)(bdl_phys & 0xFFFFFFFF));
    hda_write32(base, sd + HDA_SD_BDPU, (uint32_t)(bdl_phys >> 32));
    hda_write32(base, sd + HDA_SD_CBL, HDA_BDL_ENTRIES * HDA_PCM_BUF_SIZE);
    hda_write16(base, sd + HDA_SD_LVI, HDA_BDL_ENTRIES - 1);

    /*
     * Stream format: 48 kHz, 16-bit, stereo.
     * Bits[14] = 0 (PCM), [13:11] = 000 (48 kHz base), [10:8] = 000 (*1),
     * [7:4] = 0001 (16-bit), [3:0] = 0001 (2 channels).
     */
    hda_write16(base, sd + HDA_SD_FMT, 0x0011);

    /* Tag = 1 in upper byte of CTL */
    hda_write8(base, sd + HDA_SD_CTL + 2, 0x10);

    a->output_ready = 1;
}

/* ── Public API ──────────────────────────────────────────────────── */

void oo_audio_hda_init(OoAudioHda *a, uint32_t bus_dev_fn, uint64_t mmio_base) {
    /* Zero-initialise struct */
    for (uint8_t *p = (uint8_t *)a, *e = p + sizeof(*a); p < e; p++) *p = 0;

    a->mmio_base     = mmio_base;
    a->pci_bus_dev_fn = bus_dev_fn;
    a->sample_rate   = 48000;
    a->channels      = 2;

    if (hda_reset(mmio_base) != 0) return; /* controller stuck */

    /* Small delay after reset for codecs to enumerate */
    hda_delay(100000);

    hda_corb_init(a);
    hda_rirb_init(a);
    hda_find_codec(a);

    /* Power-up codec (verb: Set Power State D0 on AFG node 1) */
    hda_send_verb(a, HDA_VERB(a->codec_addr, 1, 0x705, 0x00));
    hda_delay(50000);

    hda_stream_setup(a);
    a->initialized = 1;
}

int oo_audio_hda_play_pcm(OoAudioHda *a, const int16_t *samples, uint32_t n_samples) {
    if (!a->initialized || !a->output_ready) return -1;

    uint64_t base = a->mmio_base;
    uint32_t sd   = HDA_SD_BASE;

    /* Fill BDL buffers round-robin */
    uint32_t remaining = n_samples;
    uint32_t offset    = 0;
    int      buf_idx   = 0;

    while (remaining > 0 && buf_idx < HDA_BDL_ENTRIES) {
        uint32_t slots  = HDA_PCM_BUF_SIZE / sizeof(int16_t);
        uint32_t copy   = remaining < slots ? remaining : slots;

        int16_t *dst = a->pcm_buf[buf_idx];
        for (uint32_t i = 0; i < copy; i++)
            dst[i] = samples[offset + i];
        /* Pad remainder with silence */
        for (uint32_t i = copy; i < slots; i++)
            dst[i] = 0;

        offset    += copy;
        remaining -= copy;
        buf_idx++;
    }

    /* Start stream (set RUN bit) */
    hda_write8(base, sd + HDA_SD_CTL, 0x02);

    /* Poll until at least one BDL cycle completes (check status BCIS) */
    for (int i = 0; i < 200000; i++) {
        if (hda_read8(base, sd + HDA_SD_STS) & 0x04) break;
        hda_delay(100);
    }

    /* Stop stream */
    hda_write8(base, sd + HDA_SD_CTL, 0x00);

    return 0;
}

int oo_audio_hda_beep(OoAudioHda *a, uint32_t freq_hz, uint32_t duration_ms) {
    if (!a->initialized) return -1;

    /* Generate a simple sine-approximation square wave in the first BDL buffer */
    if (freq_hz == 0) freq_hz = 440;

    uint32_t samples_total = (a->sample_rate * duration_ms) / 1000;
    uint32_t period        = a->sample_rate / freq_hz; /* samples per cycle */
    if (period == 0) period = 1;

    /* Fill all BDL PCM buffers with the tone */
    for (int b = 0; b < HDA_BDL_ENTRIES; b++) {
        uint32_t slots = HDA_PCM_BUF_SIZE / sizeof(int16_t);
        for (uint32_t i = 0; i < slots; i++) {
            uint32_t phase = i % period;
            a->pcm_buf[b][i] = (phase < period / 2) ? 8192 : -8192;
        }
    }

    /* Dispatch to play_pcm using a dummy pointer — buffers already filled */
    uint64_t base = a->mmio_base;
    uint32_t sd   = HDA_SD_BASE;

    /* Update CBL for actual duration */
    uint32_t total_bytes = samples_total * sizeof(int16_t) * a->channels;
    if (total_bytes == 0 || total_bytes > (uint32_t)(HDA_BDL_ENTRIES * HDA_PCM_BUF_SIZE))
        total_bytes = HDA_BDL_ENTRIES * HDA_PCM_BUF_SIZE;
    hda_write32(base, sd + HDA_SD_CBL, total_bytes);

    hda_write8(base, sd + HDA_SD_CTL, 0x02); /* RUN */
    for (int i = 0; i < 500000; i++) {
        if (hda_read8(base, sd + HDA_SD_STS) & 0x04) break;
        hda_delay(100);
    }
    hda_write8(base, sd + HDA_SD_CTL, 0x00);
    /* Restore CBL */
    hda_write32(base, sd + HDA_SD_CBL, HDA_BDL_ENTRIES * HDA_PCM_BUF_SIZE);

    return 0;
}

/* Minimal itoa for status printing (decimal, unsigned) */
static void hda_u32_to_str(uint32_t v, char *buf, int size) {
    char tmp[12];
    int  pos = 0;
    if (v == 0) { tmp[pos++] = '0'; }
    else { while (v && pos < 11) { tmp[pos++] = '0' + (v % 10); v /= 10; } }
    int out = 0;
    for (int i = pos - 1; i >= 0 && out < size - 1; i--)
        buf[out++] = tmp[i];
    buf[out] = '\0';
}

void oo_audio_hda_print_status(const OoAudioHda *a, void (*print_fn)(const char *)) {
    if (!print_fn) return;
    print_fn("[HDA] Intel High Definition Audio\n");
    print_fn("  initialized : "); print_fn(a->initialized  ? "yes" : "no");  print_fn("\n");
    print_fn("  output_ready: "); print_fn(a->output_ready ? "yes" : "no");  print_fn("\n");

    char buf[16];
    hda_u32_to_str(a->sample_rate, buf, sizeof(buf));
    print_fn("  sample_rate : "); print_fn(buf); print_fn(" Hz\n");

    hda_u32_to_str(a->channels, buf, sizeof(buf));
    print_fn("  channels    : "); print_fn(buf); print_fn("\n");

    hda_u32_to_str(a->codec_addr, buf, sizeof(buf));
    print_fn("  codec_addr  : "); print_fn(buf); print_fn("\n");
}
