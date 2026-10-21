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

/** Build `add -a -b <mac> -ch <ch>` into `out` (not sent). `channel` 0 is sent
 * as 1. No `-e`: the entry's name stays the MAC, so nothing user- or
 * air-controlled reaches Marauder's tokenizer. The caller sends it via
 * wh_add_begin(), which needs the exact text to match Marauder's echo. */
void wh_marauder_build_add_ap(char* out, size_t out_size, const char* mac, uint8_t channel);

/** `foxhunt -w <ap_index>`: fox-hunt the list entry at `<ap_index>`. */
void wh_marauder_foxhunt_ap(WavehoundApp* app, int16_t ap_index);

/** Bare `channel`: ask for the board's current channel (reply: a bare int). */
void wh_marauder_query_channel(WavehoundApp* app);

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

/** `"<name> RSSI: <dBm>"` (fox-hunt sample) -> true, writing *rssi. Uses the
 * last " RSSI: ", needs a non-blank name before it and an integer running to
 * the end of the line (so `info -a`'s "    RSSI: -50" is rejected). */
bool wh_marauder_parse_foxhunt(const char* line, int8_t* rssi);

/** `Added AP [<index>][CH:..]..` or `AP already exists at [<index>]: ..` ->
 * true, writing *index and whether the entry already existed. */
bool wh_marauder_parse_added_ap(const char* line, int16_t* index, bool* existed);

/** Why an `add -a` was refused. */
typedef enum {
    WhAddErrNone,
    WhAddErrFormat, /* "Format must be XX:..", "BSSID required.." */
    WhAddErrMemory, /* "Memory pressure: AP not added" */
} WhAddErr;

/** An `add -a` failure reply -> its kind, else WhAddErrNone. */
WhAddErr wh_marauder_parse_add_error(const char* line);

/** `Starting<name>. Stop with stopscan` printed when a `foxhunt` actually
 * started (index in range, board not busy) -> true. */
bool wh_marauder_parse_hunt_started(const char* line);

/** Marauder echoes each command as `#<command>`; true if `line` is the echo of
 * exactly `cmd`. */
bool wh_marauder_is_echo(const char* line, const char* cmd);

/** A bare integer line 1..14 (reply to `channel` / `channel -s`) -> true. */
bool wh_marauder_parse_channel(const char* line, uint8_t* channel);

/** `info -a` "Security: <value>" line -> true, writing a short token into `out`
 * ("Open"/"WEP"/"WPA"/"WPA2"/"WPA3"/"WAPI"). Marauder's richer values (e.g.
 * "WPA2 Enterprise", "WPA/WPA2 Mixed") map to the base token; "Unknown" and
 * unrecognized values return false (leaving the field empty). */
bool wh_marauder_parse_security(const char* line, char* out, size_t out_size);
