#ifndef OO_DRIVERS_AUDIO_HDA_H
#define OO_DRIVERS_AUDIO_HDA_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * OO Intel HDA Audio Driver (Bare-Metal, Freestanding)
 * PCI device 0x8086:0x2668 (Intel 82801FB/ICH6 HDA) and compatibles.
 * CORB/RIRB command ring, codec verb submission, PCM output stream.
 */

/* HDA MMIO register offsets */
#define HDA_REG_GCAP        0x00   /* Global Capabilities */
#define HDA_REG_GCTL        0x08   /* Global Control */
#define HDA_REG_WAKEEN      0x0C   /* Wake Enable */
#define HDA_REG_STATESTS    0x0E   /* State Change Status */
#define HDA_REG_CORBLBASE   0x40   /* CORB Lower Base Address */
#define HDA_REG_CORBUBASE   0x44   /* CORB Upper Base Address */
#define HDA_REG_CORBWP      0x48   /* CORB Write Pointer */
#define HDA_REG_CORBRP      0x4A   /* CORB Read Pointer */
#define HDA_REG_CORBCTL     0x4C   /* CORB Control */
#define HDA_REG_CORBSTS     0x4D   /* CORB Status */
#define HDA_REG_CORBSIZE    0x4E   /* CORB Size */
#define HDA_REG_RIRBLBASE   0x50   /* RIRB Lower Base Address */
#define HDA_REG_RIRBUBASE   0x54   /* RIRB Upper Base Address */
#define HDA_REG_RIRBWP      0x58   /* RIRB Write Pointer */
#define HDA_REG_RINTCNT     0x5A   /* Response Interrupt Count */
#define HDA_REG_RIRBCTL     0x5C   /* RIRB Control */
#define HDA_REG_RIRBSTS     0x5D   /* RIRB Status */
#define HDA_REG_RIRBSIZE    0x5E   /* RIRB Size */

/* Output stream descriptor base (stream 0 = offset 0x80) */
#define HDA_SD_BASE         0x80
#define HDA_SD_CTL          0x00   /* Stream Descriptor Control */
#define HDA_SD_STS          0x03   /* Stream Descriptor Status */
#define HDA_SD_LPIB         0x04   /* Link Position in Buffer */
#define HDA_SD_CBL          0x08   /* Cyclic Buffer Length */
#define HDA_SD_LVI          0x0C   /* Last Valid Index */
#define HDA_SD_FMT          0x12   /* Stream Format */
#define HDA_SD_BDPL         0x18   /* Buffer Descriptor List Pointer Lower */
#define HDA_SD_BDPU         0x1C   /* Buffer Descriptor List Pointer Upper */

/* CORB/RIRB sizes */
#define HDA_CORB_ENTRIES    256
#define HDA_RIRB_ENTRIES    256

/* BDL entry count */
#define HDA_BDL_ENTRIES     4
#define HDA_PCM_BUF_SIZE    4096   /* bytes per BDL entry */

/* Codec verb helpers */
#define HDA_VERB(cad, nid, verb, payload) \
    (((uint32_t)(cad) << 28) | ((uint32_t)(nid) << 20) | ((uint32_t)(verb) << 8) | (uint8_t)(payload))

/* Buffer Descriptor List entry */
typedef struct {
    uint64_t addr;     /* physical address of PCM buffer */
    uint32_t len;      /* length in bytes */
    uint32_t ioc;      /* interrupt-on-completion flag */
} __attribute__((packed)) OoHdaBdlEntry;

typedef struct {
    uint64_t mmio_base;        /* CORB/RIRB BAR */
    int      initialized;
    int      output_ready;
    uint32_t sample_rate;      /* 48000 default */
    uint8_t  channels;         /* 2 = stereo */
    uint32_t pci_bus_dev_fn;

    /* Internal state */
    uint32_t *corb;            /* CORB ring (static buffer) */
    uint64_t  *rirb;           /* RIRB ring (static buffer, 64-bit entries) */
    uint16_t  corb_wp;         /* CORB write pointer */
    uint16_t  rirb_rp;         /* RIRB read pointer (software) */
    uint8_t   codec_addr;      /* first codec address found */

    /* PCM output buffers (static) */
    OoHdaBdlEntry bdl[HDA_BDL_ENTRIES]; /* Buffer Descriptor List */
    int16_t   pcm_buf[HDA_BDL_ENTRIES][HDA_PCM_BUF_SIZE / sizeof(int16_t)];
} OoAudioHda;

void oo_audio_hda_init(OoAudioHda *a, uint32_t bus_dev_fn, uint64_t mmio_base);
int  oo_audio_hda_play_pcm(OoAudioHda *a, const int16_t *samples, uint32_t n_samples);
int  oo_audio_hda_beep(OoAudioHda *a, uint32_t freq_hz, uint32_t duration_ms);
void oo_audio_hda_print_status(const OoAudioHda *a, void (*print_fn)(const char *));

#ifdef __cplusplus
}
#endif

#endif /* OO_DRIVERS_AUDIO_HDA_H */
