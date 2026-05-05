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

/* Stream Descriptor base (first SD = offset 0x80; each SD = 0x20 bytes)
 * ISD (input)  : 0x80 + n*0x20   for n in [0, ISS)
 * OSD (output) : 0x80 + ISS*0x20 for n in [0, OSS)
 * ISS/OSS extracted from GCAP at init time. */
#define HDA_SD_BASE         0x80
#define HDA_SD_STRIDE       0x20   /* bytes per stream descriptor */
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

/* GCAP register (offset 0x00, 16-bit) field masks */
#define HDA_GCAP_ISS_SHIFT  8    /* input stream count bits [11:8] */
#define HDA_GCAP_ISS_MASK   0xF
#define HDA_GCAP_OSS_SHIFT  12   /* output stream count bits [15:12] */
#define HDA_GCAP_OSS_MASK   0xF

/* Codec NID constants (QEMU HDA duplex layout) */
#define HDA_NID_AFG         1    /* Audio Function Group */
#define HDA_NID_DAC         3    /* Output converter (QEMU) */
#define HDA_NID_ADC         6    /* Input converter / ADC (QEMU) */
#define HDA_NID_PIN_OUT     4    /* Headphone out pin (QEMU) */
#define HDA_NID_PIN_MIC     7    /* Mic in pin (QEMU) */

/* Codec verb codes (12-bit verb field) */
#define HDA_VERB_SET_POWER_STATE    0x705
#define HDA_VERB_SET_CONV_CHANNEL   0x706  /* [7:4]=stream, [3:0]=channel */
#define HDA_VERB_SET_PIN_WIDGET_CTL 0x707
#define HDA_VERB_SET_CONV_FORMAT    0x200  /* + hi_byte of format in lower 8 bits of verb */
#define HDA_VERB_GET_CONV_FORMAT    0xA00

/* Stream format constants */
#define HDA_FMT_48KHZ_16BIT_STEREO  0x0011  /* 48kHz 16-bit 2ch */
#define HDA_FMT_48KHZ_16BIT_MONO    0x0010  /* 48kHz 16-bit 1ch (capture) */

/* Pin Widget Control flags */
#define HDA_PIN_CTL_IN      0x20   /* Input enable */
#define HDA_PIN_CTL_OUT     0x40   /* Output enable */
#define HDA_PIN_CTL_HP      0x80   /* Headphone drive */
#define HDA_PIN_CTL_VREF80  0x04   /* VRef 80% (microphone bias) */

/* Stream tag embedded in SD_CTL bits [23:20] */
#define HDA_SD_CTL_STREAM_TAG_SHIFT  20

/* Capture BDL and buffer sizes (16kHz-equivalent after 3:1 decimation) */
#define HDA_CAP_BDL_ENTRIES   4
#define HDA_CAP_BUF_SIZE      4096  /* bytes per BDL entry */

/* After decimation 48kHz→16kHz, output samples per BDL fill:
 * 4096 bytes / 2 bytes/sample = 2048 samples at 48kHz
 * 2048 / 3 = 682 samples at 16kHz */
#define HDA_CAP_PCM16_PER_BUF  682

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
    int      capture_ready;    /* 1 if capture stream is configured */
    uint32_t sample_rate;      /* 48000 default */
    uint8_t  channels;         /* 2 = stereo */
    uint32_t pci_bus_dev_fn;

    /* GCAP-derived stream descriptor offsets */
    uint32_t iss;              /* input stream count */
    uint32_t oss;              /* output stream count */
    uint32_t isd_base;         /* ISD 0 register offset (= 0x80) */
    uint32_t osd_base;         /* OSD 0 register offset (= 0x80 + ISS*0x20) */

    /* Internal state */
    uint32_t *corb;            /* CORB ring (static buffer) */
    uint64_t  *rirb;           /* RIRB ring (static buffer, 64-bit entries) */
    uint16_t  corb_wp;         /* CORB write pointer */
    uint16_t  rirb_rp;         /* RIRB read pointer (software) */
    uint8_t   codec_addr;      /* first codec address found */

    /* PCM output buffers (static) */
    OoHdaBdlEntry bdl[HDA_BDL_ENTRIES];
    int16_t   pcm_buf[HDA_BDL_ENTRIES][HDA_PCM_BUF_SIZE / sizeof(int16_t)];

    /* PCM capture buffers (static, 48kHz mono 16-bit) */
    OoHdaBdlEntry cap_bdl[HDA_CAP_BDL_ENTRIES];
    int16_t   cap_buf[HDA_CAP_BDL_ENTRIES][HDA_CAP_BUF_SIZE / sizeof(int16_t)];
    int       cap_rd_buf;      /* BDL buffer index last read by software */
} OoAudioHda;

void oo_audio_hda_init(OoAudioHda *a, uint32_t bus_dev_fn, uint64_t mmio_base);
int  oo_audio_hda_play_pcm(OoAudioHda *a, const int16_t *samples, uint32_t n_samples);
int  oo_audio_hda_beep(OoAudioHda *a, uint32_t freq_hz, uint32_t duration_ms);

/* Microphone capture API:
 *   capture_start: configure ADC codec path and start ISD DMA
 *   read_samples:  copy pending 16kHz 16-bit mono samples into buf
 *                  returns number of samples written (0 if none ready) */
int  oo_audio_hda_capture_start(OoAudioHda *a);
int  oo_audio_hda_read_samples(OoAudioHda *a, int16_t *buf, int buf_cap);

void oo_audio_hda_print_status(const OoAudioHda *a, void (*print_fn)(const char *));

#ifdef __cplusplus
}
#endif

#endif /* OO_DRIVERS_AUDIO_HDA_H */
