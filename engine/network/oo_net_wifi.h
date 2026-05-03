/* oo_net_wifi.h — OO WiFi Driver (EFI Wireless MAC Connection Protocol v2)
 *
 * Fonctionne sur : Intel CNVi (AX200/AX201/9260/8265), certains Realtek/MediaTek
 * Condition      : UEFI firmware doit avoir le WiFi stack intégré (standard sur
 *                  la plupart des laptops modernes depuis 2018+)
 *
 * Activation dans repl.cfg:
 *   wifi_ssid=MonReseau
 *   wifi_pass=MotDePasse
 *   oo_net=1
 */
#ifndef OO_NET_WIFI_H
#define OO_NET_WIFI_H

#ifdef UEFI_BUILD
#include <efi.h>
#include <efilib.h>
#else
#include "efi_compat.h"
#endif

/* ── UEFI WiFi2 Protocol GUID ─────────────────────────────────────────────
 * EFI_WIRELESS_MAC_CONNECTION2_PROTOCOL
 * UEFI Spec 2.6+, section 10.11.2 */
#define EFI_WIRELESS_MAC_CONNECTION2_PROTOCOL_GUID \
    { 0x6AA20AA0, 0x0A0A, 0x46CC, \
      { 0xA8, 0x98, 0xB3, 0xE5, 0x6E, 0xCA, 0x6B, 0x88 } }

/* ── 802.11 Data Structures (UEFI Spec 2.9 §10.11) ────────────────────── */

#define OO_WIFI_MAX_SSID_LEN     32
#define OO_WIFI_MAX_NETWORKS     16
#define OO_WIFI_MAX_PASS_LEN     64
#define OO_WIFI_SCAN_TIMEOUT_US  8000000UL   /* 8 seconds */
#define OO_WIFI_CONN_TIMEOUT_US  20000000UL  /* 20 seconds */

typedef struct {
    UINT8  SSIDLength;
    UINT8  SSID[OO_WIFI_MAX_SSID_LEN];
} Efi80211Ssid;

/* Security types */
typedef enum {
    Ieee80211SecurityTypeAny   = 0,
    Ieee80211SecurityTypeWpa   = 2,
    Ieee80211SecurityTypeWpa2  = 3,
    Ieee80211SecurityTypeWpa3  = 4,
    Ieee80211SecurityTypeOpen  = 1,
} Ieee80211SecurityType;

typedef struct {
    Efi80211Ssid       NetworkName;
    Ieee80211SecurityType SecurityType;
} Efi80211Network;

/* GetNetworks result */
typedef struct {
    UINT32          NumOfNetworks;
    Efi80211Network Networks[OO_WIFI_MAX_NETWORKS]; /* simplified fixed array */
} Efi80211GetNetworksResult;

/* GetNetworks token (async) */
typedef struct {
    EFI_EVENT                  Event;
    EFI_STATUS                 Status;
    void                      *Data;   /* EFI_80211_GET_NETWORKS_DATA* (NULL = scan all) */
    Efi80211GetNetworksResult **Result;
} Efi80211GetNetworksToken;

/* Connect result codes */
typedef enum {
    ConnectSuccess              = 0,
    ConnectRefused              = 1,
    ConnectFailed               = 2,
} Efi80211ConnectResultCode;

/* Connect data (credentials) */
typedef struct {
    Efi80211Network   Network;
    UINT8             Passphrase[OO_WIFI_MAX_PASS_LEN];
    UINT8             PassphraseLen;
} Efi80211ConnectData;

/* Connect token (async) */
typedef struct {
    EFI_EVENT               Event;
    EFI_STATUS              Status;
    Efi80211ConnectData    *Data;
    Efi80211ConnectResultCode ResultCode;
} Efi80211ConnectToken;

/* Disconnect token */
typedef struct {
    EFI_EVENT   Event;
    EFI_STATUS  Status;
    void       *Data;
} Efi80211DisconnectToken;

/* ── Protocol interface ────────────────────────────────────────────────── */
typedef struct _OoWifi2Protocol OoWifi2Protocol;

typedef EFI_STATUS (EFIAPI *OoWifi2GetNetworks)(
    IN  OoWifi2Protocol          *This,
    IN  void                     *Data,   /* NULL = scan all */
    IN  OUT Efi80211GetNetworksToken *Token);

typedef EFI_STATUS (EFIAPI *OoWifi2Connect)(
    IN  OoWifi2Protocol          *This,
    IN  Efi80211ConnectToken     *Token);

typedef EFI_STATUS (EFIAPI *OoWifi2Disconnect)(
    IN  OoWifi2Protocol          *This,
    IN  Efi80211DisconnectToken  *Token);

struct _OoWifi2Protocol {
    OoWifi2GetNetworks   GetNetworks;
    OoWifi2Connect       Connect;
    OoWifi2Disconnect    Disconnect;
};

/* ── WiFi State ────────────────────────────────────────────────────────── */
#define OO_WIFI_STATE_OFF         0
#define OO_WIFI_STATE_SCANNING    1
#define OO_WIFI_STATE_CONNECTING  2
#define OO_WIFI_STATE_CONNECTED   3
#define OO_WIFI_STATE_FAILED      4

typedef struct {
    int     state;          /* OO_WIFI_STATE_* */
    int     proto_found;    /* EFI WiFi2 protocol located */
    int     snp_found;      /* WiFi interface exposed as SNP */
    char    ssid[OO_WIFI_MAX_SSID_LEN + 1];
    char    pass[OO_WIFI_MAX_PASS_LEN + 1];
    UINT8   bssid[6];
    INT32   rssi;           /* signal strength (dBm, negative) */
    UINT8   channel;

    /* Found networks during last scan */
    UINT8   num_found;
    char    found_ssid[OO_WIFI_MAX_NETWORKS][OO_WIFI_MAX_SSID_LEN + 1];
    Ieee80211SecurityType found_sec[OO_WIFI_MAX_NETWORKS];
} OoWifiState;

extern OoWifiState g_oo_wifi;

/* ── API ───────────────────────────────────────────────────────────────── */

/* Configure target SSID + passphrase (from repl.cfg) */
void oo_wifi_set_credentials(const char *ssid, const char *pass);

/* Main init: find EFI WiFi2 protocol and attempt connection.
 * Returns 1 if connected and ready to use via oo_net_udp_send/recv.
 * Returns 0 on failure (no protocol, wrong SSID/pass, timeout). */
int  oo_wifi_init_best_effort(void);

/* Scan networks (fill g_oo_wifi.found_*). Returns count found. */
int  oo_wifi_scan(void);

/* Connect to configured SSID. Returns 1 on success. */
int  oo_wifi_connect(void);

/* Disconnect */
void oo_wifi_disconnect(void);

/* Print WiFi status + scanned networks */
void oo_wifi_print_status(void);

/* Internal: copy ASCII to EFI SSID struct */
void oo_wifi_ascii_to_ssid(Efi80211Ssid *out, const char *ascii);

#endif /* OO_NET_WIFI_H */
