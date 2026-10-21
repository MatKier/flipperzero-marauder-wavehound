#pragma once

/*
 * ESP32 Marauder (v1.19.0) serial protocol: command builders and line parsers.
 *
 * Marauder's UART is a human-readable CLI, not a machine protocol. This module
 * owns the exact subset Wavehound drives (fox hunt, scan, info, channel) so the
 * UART worker in wavehound.c stays readable. See docs/MARAUDER_PROTOCOL.md.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Opaque app pointer; wh_marauder_* command helpers send through wh_send(). */
typedef struct WavehoundApp WavehoundApp;

/* ---- commands (board <- app) ---- */

/** Stop whatever the board is doing (scan / hunt) and return to idle. */
void wh_marauder_stop(WavehoundApp* app);

/** `channel -s <ch>`: park the radio on one channel (1..14). On the screenless
 * dev board this holds for the whole hunt (nothing moves it). */
void wh_marauder_set_channel(WavehoundApp* app, uint8_t channel);

/** Clear the board's AP + station lists so the next scan's indices start at 0. */
void wh_marauder_clear(WavehoundApp* app);

/** Start a combined AP + station scan (`scanall`). */
void wh_marauder_scan(WavehoundApp* app);

/** `info`: board identity (firmware, hardware, partition, IDF, MACs). */
void wh_marauder_info(WavehoundApp* app);

/** `info -a <ap_index>`: detail for one AP in the list (incl. its security). */
void wh_marauder_info_ap(WavehoundApp* app, int16_t ap_index);

/** `add -a -b <mac> -ch <ch> [-e <ssid>]`: add an AP by MAC (for typed-MAC
 * hunts). `channel` 0 is sent as 1. `ssid` may be NULL/empty. */
void wh_marauder_add_ap(WavehoundApp* app, const char* mac, uint8_t channel, const char* ssid);

/** `foxhunt -w <ap_index>`: fox-hunt the list entry at `<ap_index>`. */
void wh_marauder_foxhunt_ap(WavehoundApp* app, int16_t ap_index);

/* ---- parsers (board -> app). All take a mutable line (may be tokenized). ---- */

/** Parsed `scanall` access-point line. `ssid` points into `line` (or is ""). */
typedef struct {
    uint8_t mac[6];
    char mac_str[18];
    uint8_t channel;
    int8_t rssi;
    const char* ssid; /* "" when hidden (board printed the BSSID instead) */
} WhMarauderAp;

/** Parsed `scanall` station line. */
typedef struct {
    uint8_t sta_mac[6];
    char sta_mac_str[18];
    uint8_t ap_mac[6]; /* the AP it is associated with */
} WhMarauderSta;

/** `<rssi> Ch: <ch> <bssid> ESSID: <essid...>` -> true on a match. */
bool wh_marauder_parse_ap_line(char* line, WhMarauderAp* out);

/** `<n>: ap: <mac> -> sta: <mac>` or `<n>: sta: <mac> -> ap: <mac>`. */
bool wh_marauder_parse_sta_line(char* line, WhMarauderSta* out);

/** `"<name> RSSI: <dBm>"` (fox-hunt sample) -> true, writing *rssi. */
bool wh_marauder_parse_foxhunt(const char* line, int8_t* rssi);

/** `Added AP [<index>][CH:..]..` -> true, writing *index. */
bool wh_marauder_parse_added_ap(const char* line, int16_t* index);

/** `info -a` "Security: <value>" line -> true, writing a short token into `out`
 * ("Open"/"WEP"/"WPA"/"WPA2"/"WPA3"/"WAPI"). Marauder's richer values (e.g.
 * "WPA2 Enterprise", "WPA/WPA2 Mixed") map to the base token; "Unknown" and
 * unrecognized values return false (leaving the field empty). */
bool wh_marauder_parse_security(const char* line, char* out, size_t out_size);
