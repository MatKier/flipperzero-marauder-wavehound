#include "wh_marauder.h"

#include "wavehound_i.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---------- commands ---------- */

void wh_marauder_stop(WavehoundApp* app) {
    wh_send(app, "stopscan");
}

void wh_marauder_set_channel(WavehoundApp* app, uint8_t channel) {
    char cmd[24];
    snprintf(cmd, sizeof(cmd), "channel -s %u", channel);
    wh_send(app, cmd);
}

void wh_marauder_clear(WavehoundApp* app) {
    wh_send(app, "clearlist -a");
}

void wh_marauder_scan(WavehoundApp* app) {
    wh_send(app, "scanall");
}

void wh_marauder_info(WavehoundApp* app) {
    wh_send(app, "info");
}

void wh_marauder_info_ap(WavehoundApp* app, int16_t ap_index) {
    char cmd[24];
    snprintf(cmd, sizeof(cmd), "info -a %d", ap_index);
    wh_send(app, cmd);
}

void wh_marauder_build_add_ap(char* out, size_t out_size, const char* mac, uint8_t channel) {
    /* Marauder needs a channel; 0 (auto) is added on ch 1. */
    snprintf(out, out_size, "add -a -b %s -ch %u", mac, channel ? channel : 1);
}

void wh_marauder_foxhunt_ap(WavehoundApp* app, int16_t ap_index) {
    char cmd[24];
    snprintf(cmd, sizeof(cmd), "foxhunt -w %d", ap_index);
    wh_send(app, cmd);
}

void wh_marauder_query_channel(WavehoundApp* app) {
    wh_send(app, "channel");
}

/* ---------- parse helpers ---------- */

static int hex_nibble(char c) {
    if(c >= '0' && c <= '9') return c - '0';
    if(c >= 'a' && c <= 'f') return c - 'a' + 10;
    if(c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

/* Parse "aa:bb:cc:dd:ee:ff" (exactly 17 chars) into 6 bytes. */
static bool parse_mac6(const char* s, uint8_t mac[6]) {
    for(int i = 0; i < 6; i++) {
        int hi = hex_nibble(s[i * 3]);
        int lo = hi < 0 ? -1 : hex_nibble(s[i * 3 + 1]);
        if(lo < 0) return false;
        if(i < 5 && s[i * 3 + 2] != ':') return false;
        mac[i] = (uint8_t)(hi << 4 | lo);
    }
    return true;
}

static bool is_upper_hex(char c) {
    return (c >= '0' && c <= '9') || (c >= 'A' && c <= 'F');
}

/* True if token (up to a space/end) is exactly two uppercase hex digits -- the
 * shape of Marauder's two trailing beacon bytes (printed "%02X"). Requiring
 * uppercase keeps SSID words like "ab" intact. */
static bool is_hex2(const char* p) {
    return is_upper_hex(p[0]) && is_upper_hex(p[1]) && (p[2] == ' ' || p[2] == '\0');
}

/* ---------- parsers ---------- */

bool wh_marauder_parse_ap_line(char* line, WhMarauderAp* out) {
    /* "<rssi> Ch: <ch> <bssid> ESSID: <essid...> <bb> <bb>" */
    int rssi = 0;
    unsigned ch = 0;
    char mac[18];
    if(sscanf(line, "%d Ch: %u %17s", &rssi, &ch, mac) != 3) return false;
    if(ch == 0 || ch > 14) return false;
    if(!parse_mac6(mac, out->mac)) return false;

    out->rssi = (int8_t)rssi;
    out->channel = (uint8_t)ch;
    strlcpy(out->mac_str, mac, sizeof(out->mac_str));
    out->ssid = "";

    char* e = strstr(line, "ESSID:");
    if(!e) return true; /* no ESSID field: treat as hidden */
    e += 6;
    while(*e == ' ') e++;

    /* Strip the (up to two) trailing beacon bytes and surrounding spaces.
     * Marauder omits the bytes when it is short of memory, so an SSID ending in
     * an uppercase hex-pair word ("Room 12") can lose that word -- a heuristic,
     * but the SSID is display-only (identity is the MAC everywhere). */
    size_t len = strlen(e);
    for(int stripped = 0; stripped < 2 && len >= 2; stripped++) {
        while(len > 0 && e[len - 1] == ' ') e[--len] = '\0';
        /* last space-separated token */
        size_t tok = len;
        while(tok > 0 && e[tok - 1] != ' ') tok--;
        if(!is_hex2(e + tok)) break;
        e[tok] = '\0';
        len = tok;
    }
    while(len > 0 && e[len - 1] == ' ') e[--len] = '\0';

    /* The SSID is raw beacon bytes: neutralize control characters (ESC would
     * otherwise reach widget text as a formatting escape). */
    for(char* c = e; *c; c++) {
        if((unsigned char)*c < 0x20 || *c == 0x7f) *c = '?';
    }

    /* Hidden networks print the BSSID in place of the SSID. */
    if(e[0] && strcmp(e, out->mac_str) != 0) out->ssid = e;
    return true;
}

bool wh_marauder_parse_sta_line(char* line, WhMarauderSta* out) {
    /* "<n>: ap: <mac> -> sta: <mac>"  or  "<n>: sta: <mac> -> ap: <mac>" */
    char* ap = strstr(line, "ap: ");
    char* sta = strstr(line, "sta: ");
    if(!ap || !sta) return false;
    if(!parse_mac6(ap + 4, out->ap_mac)) return false;
    if(!parse_mac6(sta + 5, out->sta_mac)) return false;
    strlcpy(out->sta_mac_str, sta + 5, sizeof(out->sta_mac_str));
    return true;
}

bool wh_marauder_parse_foxhunt(const char* line, int8_t* rssi) {
    /* The name is the target's ESSID (any text), so take the last " RSSI: ". */
    const char* r = NULL;
    for(const char* p = strstr(line, " RSSI: "); p; p = strstr(p + 1, " RSSI: ")) r = p;
    if(!r) return false;

    /* A blank name is `info -a`'s "    RSSI: <n>" line, not a hunt sample. */
    const char* n = line;
    while(n < r && *n == ' ') n++;
    if(n == r) return false;

    const char* v = r + 7;
    const char* d = (*v == '-') ? v + 1 : v;
    if(*d < '0' || *d > '9') return false;
    while(*d >= '0' && *d <= '9') d++;
    if(*d != '\0') return false;
    *rssi = (int8_t)atoi(v);
    return true;
}

bool wh_marauder_parse_security(const char* line, char* out, size_t out_size) {
    /* Anchored at the line start: the ESSID line before it is free text. */
    if(strncmp(line, "Security: ", 10) != 0) return false;
    const char* p = line + 10; /* past "Security: " */
    /* Map Marauder's values to the short token the details screen shows. Order
     * matters: longer/more specific prefixes first ("WPA3"/"WPA2"/"WPA/WPA2"
     * before plain "WPA"). "Unknown" and anything else leave the field empty. */
    const char* tok = NULL;
    if(strncmp(p, "Open", 4) == 0)
        tok = "Open";
    else if(strncmp(p, "WEP", 3) == 0)
        tok = "WEP";
    else if(strncmp(p, "WPA3", 4) == 0)
        tok = "WPA3";
    else if(strncmp(p, "WPA2", 4) == 0 || strncmp(p, "WPA/WPA2", 8) == 0)
        tok = "WPA2";
    else if(strncmp(p, "WPA", 3) == 0)
        tok = "WPA";
    else if(strncmp(p, "WAPI", 4) == 0)
        tok = "WAPI";
    if(!tok) return false;
    strlcpy(out, tok, out_size);
    return true;
}

bool wh_marauder_parse_added_ap(const char* line, int16_t* index, bool* existed) {
    /* "Added AP [n]..." on a fresh add, or "AP already exists at [n]:..." when
     * the MAC is already in the list (from the scan or an earlier add) -- both
     * give the index to hunt by. */
    const char* fmt = NULL;
    if(strncmp(line, "Added AP [", 10) == 0) {
        fmt = "Added AP [%d]";
        *existed = false;
    } else if(strncmp(line, "AP already exists at [", 22) == 0) {
        fmt = "AP already exists at [%d]";
        *existed = true;
    }
    if(!fmt) return false;
    int idx = -1;
    if(sscanf(line, fmt, &idx) != 1 || idx < 0 || idx > INT16_MAX) return false;
    *index = (int16_t)idx;
    return true;
}

WhAddErr wh_marauder_parse_add_error(const char* line) {
    if(strncmp(line, "Format must be", 14) == 0) return WhAddErrFormat;
    if(strncmp(line, "BSSID required", 14) == 0) return WhAddErrFormat;
    if(strncmp(line, "Memory pressure: AP not added", 29) == 0) return WhAddErrMemory;
    return WhAddErrNone;
}

bool wh_marauder_parse_hunt_started(const char* line) {
    /* "StartingFox Hunt. Stop with stopscan" (Marauder prints no space after
     * "Starting"); only printed when the hunt really started. */
    return strncmp(line, "Starting", 8) == 0 && strstr(line, "Fox Hunt. Stop with") != NULL;
}

bool wh_marauder_is_echo(const char* line, const char* cmd) {
    return line[0] == '#' && strcmp(line + 1, cmd) == 0;
}

bool wh_marauder_parse_channel(const char* line, uint8_t* channel) {
    unsigned ch = 0;
    const char* p = line;
    if(*p < '0' || *p > '9') return false;
    while(*p >= '0' && *p <= '9') {
        ch = ch * 10 + (unsigned)(*p - '0');
        if(ch > 14) return false;
        p++;
    }
    if(*p != '\0' || ch == 0) return false;
    *channel = (uint8_t)ch;
    return true;
}
