#include "wh_marauder.h"

#include "wavehound_i.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---------- commands ---------- */

void wh_marauder_stop(WavehoundApp* app) {
    wh_send(app, "stopscan");
}

void wh_marauder_poll(WavehoundApp* app) {
    wh_send(app, "channel");
}

void wh_marauder_set_hop(WavehoundApp* app, bool on) {
    wh_send(app, on ? "settings -s ChanHop enable" : "settings -s ChanHop disable");
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

void wh_marauder_add_ap(WavehoundApp* app, const char* mac, uint8_t channel, const char* ssid) {
    char cmd[80];
    /* Marauder needs a channel; 0 (auto) is added on ch 1 and hunted with hop. */
    int n = snprintf(cmd, sizeof(cmd), "add -a -b %s -ch %u", mac, channel ? channel : 1);
    if(ssid && ssid[0]) snprintf(cmd + n, sizeof(cmd) - n, " -e %s", ssid);
    wh_send(app, cmd);
}

void wh_marauder_foxhunt_ap(WavehoundApp* app, int16_t ap_index) {
    char cmd[24];
    snprintf(cmd, sizeof(cmd), "foxhunt -w %d", ap_index);
    wh_send(app, cmd);
}

void wh_marauder_foxhunt_sta(WavehoundApp* app, int16_t ap_index, int16_t sta_index) {
    char cmd[32];
    snprintf(cmd, sizeof(cmd), "foxhunt -s %d %d", ap_index, sta_index);
    wh_send(app, cmd);
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

/* True if token (up to a space/end) is exactly two uppercase/lowercase hex
 * digits -- the shape of Marauder's two trailing beacon bytes. */
static bool is_hex2(const char* p) {
    return hex_nibble(p[0]) >= 0 && hex_nibble(p[1]) >= 0 && (p[2] == ' ' || p[2] == '\0');
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

    /* Strip the (up to two) trailing beacon bytes and surrounding spaces. */
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
    const char* r = strstr(line, " RSSI: ");
    if(!r) return false;
    r += 7;
    if(*r != '-' && (*r < '0' || *r > '9')) return false;
    *rssi = (int8_t)atoi(r);
    return true;
}

bool wh_marauder_parse_added_ap(const char* line, int16_t* index) {
    /* "Added AP [n]..." on a fresh add, or "AP already exists at [n]:..." when
     * the MAC was added before -- both give the index to hunt by. */
    const char* p = strstr(line, "Added AP [");
    const char* fmt = "Added AP [%d]";
    if(!p) {
        p = strstr(line, "already exists at [");
        fmt = "already exists at [%d]";
    }
    if(!p) return false;
    int idx = -1;
    if(sscanf(p, fmt, &idx) != 1 || idx < 0) return false;
    *index = (int16_t)idx;
    return true;
}
