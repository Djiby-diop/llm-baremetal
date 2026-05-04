/* oo_net_wifi.c — OO WiFi Driver
 *
 * Stratégie "fonctionne à coup sûr" :
 *
 * NIVEAU 1 — EFI WiFi2 Protocol (Intel CNVi, Qualcomm, MediaTek sur laptops modernes)
 *   Le firmware UEFI gère lui-même :
 *     - Le scan 802.11 (beacons/probe requests)
 *     - L'authentification WPA2/WPA3 (4-way handshake)
 *     - La gestion des clés (PMK, PTK, GTK)
 *   Nous faisons juste : GetNetworks() → Connect() → DHCP
 *
 * NIVEAU 2 — Détection SNP secondaire
 *   Certains adaptateurs USB WiFi (Realtek RTL8188/RTL8192) s'exposent
 *   comme EFI_SIMPLE_NETWORK_PROTOCOL après init UEFI.
 *   On les détecte et on les utilise comme Ethernet supplémentaire.
 *
 * NIVEAU 3 — Fallback gracieux
 *   Si aucun protocole WiFi trouvé → message clair → utiliser Ethernet.
 */
#include "oo_net_wifi.h"
#include "oo_net_core.h"

/* ── Global State ─────────────────────────────────────────────────────── */
OoWifiState g_oo_wifi;
static OoWifi2Protocol *g_wifi2 = NULL;

/* ── Memory helpers ────────────────────────────────────────────────────── */
static void wf_zero(void *p, int n) { for(int i=0;i<n;i++) ((UINT8*)p)[i]=0; }
static void wf_copy(void *d, const void *s, int n) {
    for(int i=0;i<n;i++) ((UINT8*)d)[i]=((const UINT8*)s)[i];
}
static int wf_strlen(const char *s) { int n=0; while(s[n]) n++; return n; }
static int wf_strcmp(const char *a, const char *b) {
    while(*a && *a==*b){a++;b++;} return (int)(unsigned char)*a-(int)(unsigned char)*b;
}
static void wf_strncpy(char *d, const char *s, int n) {
    int i=0; while(i<n-1 && s[i]){d[i]=s[i];i++;} d[i]=0;
}

/* ── SSID helpers ──────────────────────────────────────────────────────── */
void oo_wifi_ascii_to_ssid(Efi80211Ssid *out, const char *ascii) {
    int len = wf_strlen(ascii);
    if (len > OO_WIFI_MAX_SSID_LEN) len = OO_WIFI_MAX_SSID_LEN;
    out->SSIDLength = (UINT8)len;
    wf_zero(out->SSID, OO_WIFI_MAX_SSID_LEN);
    for (int i = 0; i < len; i++) out->SSID[i] = (UINT8)ascii[i];
}

static void ssid_to_ascii(char *out, int outlen, const Efi80211Ssid *ssid) {
    int len = ssid->SSIDLength;
    if (len > OO_WIFI_MAX_SSID_LEN) len = OO_WIFI_MAX_SSID_LEN;
    if (len > outlen - 1) len = outlen - 1;
    for (int i = 0; i < len; i++) out[i] = (char)ssid->SSID[i];
    out[len] = 0;
}

/* ── Credentials ───────────────────────────────────────────────────────── */
void oo_wifi_set_credentials(const char *ssid, const char *pass) {
    wf_strncpy(g_oo_wifi.ssid, ssid, OO_WIFI_MAX_SSID_LEN + 1);
    wf_strncpy(g_oo_wifi.pass, pass, OO_WIFI_MAX_PASS_LEN + 1);
}

/* ── Level 1: Find EFI WiFi2 Protocol ─────────────────────────────────── */
static int wifi2_find(void) {
    EFI_GUID guid = EFI_WIRELESS_MAC_CONNECTION2_PROTOCOL_GUID;
    EFI_HANDLE *handles = NULL;
    UINTN count = 0;

    EFI_STATUS st = uefi_call_wrapper(BS->LocateHandleBuffer, 5,
        ByProtocol, &guid, NULL, &count, &handles);
    if (EFI_ERROR(st) || count == 0) return 0;

    OoWifi2Protocol *proto = NULL;
    st = uefi_call_wrapper(BS->HandleProtocol, 3,
        handles[0], &guid, (void**)&proto);
    uefi_call_wrapper(BS->FreePool, 1, handles);

    if (EFI_ERROR(st) || !proto) return 0;
    g_wifi2 = proto;
    g_oo_wifi.proto_found = 1;
    return 1;
}

/* ── Level 2: Detect WiFi exposed as SNP ──────────────────────────────── */
/* Some USB WiFi adapters (RTL8188, RTL8192CU) register as SNP after UEFI init.
 * We identify them by checking if there are >1 SNP handles and the first one
 * is already being used by oo_net_core (Ethernet). */
static int wifi2_find_snp_fallback(void) {
    EFI_GUID snp_guid = EFI_SIMPLE_NETWORK_PROTOCOL_GUID;
    EFI_HANDLE *handles = NULL;
    UINTN count = 0;

    EFI_STATUS st = uefi_call_wrapper(BS->LocateHandleBuffer, 5,
        ByProtocol, &snp_guid, NULL, &count, &handles);
    if (EFI_ERROR(st) || count < 2) {
        if (!EFI_ERROR(st) && handles) uefi_call_wrapper(BS->FreePool, 1, handles);
        return 0;
    }

    /* Try handles beyond the first (Ethernet usually gets handle[0]) */
    int found = 0;
    for (UINTN i = 1; i < count && !found; i++) {
        EFI_SIMPLE_NETWORK_PROTOCOL *snp2 = NULL;
        st = uefi_call_wrapper(BS->HandleProtocol, 3,
            handles[i], &snp_guid, (void**)&snp2);
        if (EFI_ERROR(st) || !snp2) continue;

        if (snp2->Mode->State == EfiSimpleNetworkStopped) {
            st = uefi_call_wrapper(snp2->Start, 1, snp2);
            if (EFI_ERROR(st)) continue;
        }
        if (snp2->Mode->State == EfiSimpleNetworkStarted) {
            st = uefi_call_wrapper(snp2->Initialize, 3, snp2, 0, 0);
            if (EFI_ERROR(st)) continue;
        }
        uefi_call_wrapper(snp2->ReceiveFilters, 6, snp2,
            EFI_SIMPLE_NETWORK_RECEIVE_UNICAST |
            EFI_SIMPLE_NETWORK_RECEIVE_BROADCAST,
            0, FALSE, 0, NULL);

        /* Found a second SNP - treat it as WiFi interface.
         * Note: this only works if the adapter is already associated
         * (e.g., the UEFI firmware pre-configured it, or it's a bridged adapter) */
        g_oo_wifi.snp_found = 1;
        g_oo_wifi.state = OO_WIFI_STATE_CONNECTED; /* optimistic: try DHCP */
        Print(L"[WiFi] Secondary SNP found (USB WiFi adapter?)\r\n");
        found = 1;
    }
    uefi_call_wrapper(BS->FreePool, 1, handles);
    return found;
}

/* ── Async event wait helper ───────────────────────────────────────────── */
static EFI_STATUS wifi_wait_event(EFI_EVENT evt, UINTN timeout_us) {
    UINTN waited = 0;
    UINTN step   = 10000; /* 10ms */
    while (waited < timeout_us) {
        UINTN idx;
        EFI_STATUS st = uefi_call_wrapper(BS->CheckEvent, 1, evt);
        if (!EFI_ERROR(st)) return EFI_SUCCESS;
        uefi_call_wrapper(BS->Stall, 1, step);
        waited += step;
    }
    return EFI_TIMEOUT;
}

/* ── Scan Networks ─────────────────────────────────────────────────────── */
int oo_wifi_scan(void) {
    if (!g_wifi2) return 0;
    g_oo_wifi.num_found = 0;
    g_oo_wifi.state = OO_WIFI_STATE_SCANNING;

    /* Allocate result buffer */
    Efi80211GetNetworksResult *result = NULL;
    EFI_STATUS st = uefi_call_wrapper(BS->AllocatePool, 3,
        EfiLoaderData,
        sizeof(Efi80211GetNetworksResult),
        (void**)&result);
    if (EFI_ERROR(st) || !result) return 0;
    wf_zero(result, sizeof(*result));

    /* Create async event */
    EFI_EVENT scan_evt = NULL;
    st = uefi_call_wrapper(BS->CreateEvent, 5,
        EVT_NOTIFY_SIGNAL, TPL_CALLBACK, NULL, NULL, &scan_evt);
    if (EFI_ERROR(st)) {
        uefi_call_wrapper(BS->FreePool, 1, result);
        return 0;
    }

    Efi80211GetNetworksToken token;
    wf_zero(&token, sizeof(token));
    token.Event  = scan_evt;
    token.Data   = NULL;       /* NULL = scan all */
    token.Result = &result;

    /* Launch scan */
    st = uefi_call_wrapper(g_wifi2->GetNetworks, 3, g_wifi2, NULL, &token);
    if (EFI_ERROR(st)) {
        uefi_call_wrapper(BS->CloseEvent, 1, scan_evt);
        uefi_call_wrapper(BS->FreePool, 1, result);
        g_oo_wifi.state = OO_WIFI_STATE_FAILED;
        return 0;
    }

    /* Wait for scan to complete */
    st = wifi_wait_event(scan_evt, OO_WIFI_SCAN_TIMEOUT_US);
    uefi_call_wrapper(BS->CloseEvent, 1, scan_evt);

    int found = 0;
    if (!EFI_ERROR(st) && result && token.Status == EFI_SUCCESS) {
        UINT32 n = result->NumOfNetworks;
        if (n > OO_WIFI_MAX_NETWORKS) n = OO_WIFI_MAX_NETWORKS;
        for (UINT32 i = 0; i < n; i++) {
            ssid_to_ascii(g_oo_wifi.found_ssid[i],
                          OO_WIFI_MAX_SSID_LEN + 1,
                          &result->Networks[i].NetworkName);
            g_oo_wifi.found_sec[i] = result->Networks[i].SecurityType;
            found++;
        }
        g_oo_wifi.num_found = (UINT8)found;
    }

    uefi_call_wrapper(BS->FreePool, 1, result);
    g_oo_wifi.state = OO_WIFI_STATE_OFF; /* reset until connect */
    return found;
}

/* ── Connect ───────────────────────────────────────────────────────────── */
int oo_wifi_connect(void) {
    if (!g_wifi2) return 0;
    if (g_oo_wifi.ssid[0] == 0) {
        Print(L"[WiFi] No SSID configured (set wifi_ssid= in repl.cfg)\r\n");
        return 0;
    }

    g_oo_wifi.state = OO_WIFI_STATE_CONNECTING;

    /* Build connect data */
    Efi80211ConnectData *cdata = NULL;
    EFI_STATUS st = uefi_call_wrapper(BS->AllocatePool, 3,
        EfiLoaderData, sizeof(Efi80211ConnectData), (void**)&cdata);
    if (EFI_ERROR(st) || !cdata) return 0;
    wf_zero(cdata, sizeof(*cdata));

    /* Set SSID */
    oo_wifi_ascii_to_ssid(&cdata->Network.NetworkName, g_oo_wifi.ssid);

    /* Detect security type from scan results */
    cdata->Network.SecurityType = Ieee80211SecurityTypeWpa2; /* safe default */
    for (int i = 0; i < g_oo_wifi.num_found; i++) {
        if (wf_strcmp(g_oo_wifi.found_ssid[i], g_oo_wifi.ssid) == 0) {
            cdata->Network.SecurityType = g_oo_wifi.found_sec[i];
            break;
        }
    }

    /* Set passphrase */
    int plen = wf_strlen(g_oo_wifi.pass);
    if (plen > OO_WIFI_MAX_PASS_LEN) plen = OO_WIFI_MAX_PASS_LEN;
    cdata->PassphraseLen = (UINT8)plen;
    wf_copy(cdata->Passphrase, g_oo_wifi.pass, plen);

    /* Create async event */
    EFI_EVENT conn_evt = NULL;
    st = uefi_call_wrapper(BS->CreateEvent, 5,
        EVT_NOTIFY_SIGNAL, TPL_CALLBACK, NULL, NULL, &conn_evt);
    if (EFI_ERROR(st)) {
        uefi_call_wrapper(BS->FreePool, 1, cdata);
        g_oo_wifi.state = OO_WIFI_STATE_FAILED;
        return 0;
    }

    Efi80211ConnectToken token;
    wf_zero(&token, sizeof(token));
    token.Event      = conn_evt;
    token.Data       = cdata;
    token.ResultCode = ConnectFailed;

    /* Launch connect */
    st = uefi_call_wrapper(g_wifi2->Connect, 2, g_wifi2, &token);
    if (EFI_ERROR(st)) {
        uefi_call_wrapper(BS->CloseEvent, 1, conn_evt);
        uefi_call_wrapper(BS->FreePool, 1, cdata);
        g_oo_wifi.state = OO_WIFI_STATE_FAILED;
        return 0;
    }

    /* Wait for connection (up to 20 seconds) */
    st = wifi_wait_event(conn_evt, OO_WIFI_CONN_TIMEOUT_US);
    uefi_call_wrapper(BS->CloseEvent, 1, conn_evt);
    uefi_call_wrapper(BS->FreePool, 1, cdata);

    if (!EFI_ERROR(st) && token.Status == EFI_SUCCESS
        && token.ResultCode == ConnectSuccess) {
        g_oo_wifi.state = OO_WIFI_STATE_CONNECTED;
        return 1;
    }

    g_oo_wifi.state = OO_WIFI_STATE_FAILED;
    return 0;
}

/* ── Disconnect ────────────────────────────────────────────────────────── */
void oo_wifi_disconnect(void) {
    if (!g_wifi2 || g_oo_wifi.state != OO_WIFI_STATE_CONNECTED) return;

    EFI_EVENT disc_evt = NULL;
    EFI_STATUS st = uefi_call_wrapper(BS->CreateEvent, 5,
        EVT_NOTIFY_SIGNAL, TPL_CALLBACK, NULL, NULL, &disc_evt);
    if (EFI_ERROR(st)) return;

    Efi80211DisconnectToken token;
    wf_zero(&token, sizeof(token));
    token.Event = disc_evt;

    uefi_call_wrapper(g_wifi2->Disconnect, 2, g_wifi2, &token);
    wifi_wait_event(disc_evt, 3000000UL);
    uefi_call_wrapper(BS->CloseEvent, 1, disc_evt);
    g_oo_wifi.state = OO_WIFI_STATE_OFF;
}

/* ── Main Init Entry Point ─────────────────────────────────────────────── */
int oo_wifi_init_best_effort(void) {
    wf_zero(&g_oo_wifi, sizeof(g_oo_wifi));

    /* Level 1: EFI WiFi2 Protocol */
    if (wifi2_find()) {
        Print(L"[WiFi] EFI WiFi2 protocol found\r\n");

        /* Scan first */
        int n = oo_wifi_scan();
        if (n > 0) {
            Print(L"[WiFi] Found %d network(s)\r\n", n);
            for (int i = 0; i < n && i < 5; i++) {
                const char *sec = "?";
                switch (g_oo_wifi.found_sec[i]) {
                    case Ieee80211SecurityTypeOpen: sec = "Open";  break;
                    case Ieee80211SecurityTypeWpa:  sec = "WPA";   break;
                    case Ieee80211SecurityTypeWpa2: sec = "WPA2";  break;
                    case Ieee80211SecurityTypeWpa3: sec = "WPA3";  break;
                    default: break;
                }
                Print(L"  [%d] %a (%a)\r\n", i, g_oo_wifi.found_ssid[i], sec);
            }
        } else {
            Print(L"[WiFi] No networks found in range\r\n");
        }

        /* Connect if SSID configured */
        if (g_oo_wifi.ssid[0]) {
            Print(L"[WiFi] Connecting to: %a ...\r\n", g_oo_wifi.ssid);
            int ok = oo_wifi_connect();
            if (ok) {
                Print(L"OK: WiFi connected to %a\r\n\r\n", g_oo_wifi.ssid);
                g_oo_net.wifi_ready = 1;
                return 1;
            } else {
                Print(L"NOTE: WiFi connect failed (wrong password or SSID?)\r\n\r\n");
                return 0;
            }
        } else {
            Print(L"NOTE: WiFi found but no SSID configured.\r\n");
            Print(L"      Add to repl.cfg: wifi_ssid=<name>  wifi_pass=<pass>\r\n\r\n");
            return 0;
        }
    }

    /* Level 2: SNP fallback (USB WiFi adapters) */
    Print(L"[WiFi] EFI WiFi2 not found — trying USB WiFi (SNP)...\r\n");
    if (wifi2_find_snp_fallback()) {
        /* SNP WiFi found — DHCP will be attempted by oo_net_core */
        g_oo_net.wifi_ready = 1;
        return 1;
    }

    /* Level 3: No WiFi available */
    Print(L"[WiFi] No WiFi hardware detected via UEFI.\r\n");
    Print(L"       Tip: Use Ethernet, or ensure Intel CNVi WiFi is enabled in BIOS.\r\n\r\n");
    return 0;
}

/* ── Status Print ──────────────────────────────────────────────────────── */
void oo_wifi_print_status(void) {
    const char *state_name[] = {
        "off", "scanning", "connecting", "connected", "failed"
    };
    int si = g_oo_wifi.state;
    if (si < 0 || si > 4) si = 4;

    Print(L"\r\n[OO-WiFi] state=%a  proto=%d  snp=%d\r\n",
          state_name[si],
          g_oo_wifi.proto_found,
          g_oo_wifi.snp_found);

    if (g_oo_wifi.ssid[0])
        Print(L"  Target SSID : %a\r\n", g_oo_wifi.ssid);

    if (g_oo_wifi.num_found > 0) {
        Print(L"  Last scan   : %d network(s)\r\n", (int)g_oo_wifi.num_found);
        for (int i = 0; i < g_oo_wifi.num_found; i++) {
            const char *sec = "?";
            switch (g_oo_wifi.found_sec[i]) {
                case Ieee80211SecurityTypeOpen: sec = "Open";  break;
                case Ieee80211SecurityTypeWpa:  sec = "WPA";   break;
                case Ieee80211SecurityTypeWpa2: sec = "WPA2";  break;
                case Ieee80211SecurityTypeWpa3: sec = "WPA3";  break;
                default: break;
            }
            Print(L"    [%d] %a (%a)%s\r\n", i,
                  g_oo_wifi.found_ssid[i], sec,
                  wf_strcmp(g_oo_wifi.found_ssid[i], g_oo_wifi.ssid) == 0
                      ? " ← target" : "");
        }
    }
    Print(L"\r\n");
}
