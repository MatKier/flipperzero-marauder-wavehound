# Marauder serial protocol (the subset Wavehound uses)

Wavehound drives a WiFi dev board running **stock ESP32 Marauder v1.19.0** over
the Flipper USART (GPIO 13 TX / 14 RX <-> ESP32 UART, 115200 baud, 8N1).
Marauder's serial interface is its human-readable command line, not a machine
protocol: the app sends CLI commands terminated by `\n` and parses the free-text
lines the board prints back. Both sides ignore lines they do not understand, so
boot chatter is harmless.

This documents only the commands and replies Wavehound relies on. The command
builders live in [`flipper_app/wh_marauder.c`](../flipper_app/wh_marauder.c) and
the line parsers in the same file; the UART worker that dispatches lines by mode
is in [`flipper_app/wavehound.c`](../flipper_app/wavehound.c).

## Prompt quirk

After it finishes handling a command, Marauder prints a `"> "` prompt **with no
trailing newline**. It therefore prefixes the first streamed line of the next
response (e.g. `> -45 Ch: 6 aa:bb:cc:dd:ee:ff ESSID: Net`). Wavehound strips a
leading `"> "` from every received line before parsing.

## Flipper -> board (commands)

| Command | Purpose |
|---------|---------|
| `stopscan` | Stop any scan or hunt; return to idle. Sent on startup, when leaving a scan/hunt, and on app exit. |
| `channel` | Print the current channel. Used as a liveness poll while idle (any received line proves the link is up). |
| `channel -s <ch>` | Seed the radio onto channel `<ch>` (1–13) before a hunt, so it locks on quickly. It is **not** a hard park: Marauder's `scanall` and `foxhunt` manage the channel themselves (see below). |
| `clearlist -a` | Clear the board's AP **and** station lists, so the next scan's indices restart at 0. Sent before a fresh scan, never on a resume. |
| `scanall` | Start a combined AP + station scan (`WIFI_SCAN_AP_STA`). Streams AP and station lines until `stopscan`. It always sweeps every channel (the `ChanHop` setting does not apply to this mode), so a channel choice in the app can't narrow the scan — it only seeds a later hunt. |
| `add -a -b <mac> -ch <ch> [-e <ssid>]` | Add an access point by MAC to the board's list (channel defaults to 1 for auto). Used so a hand-typed or saved MAC can be hunted; the board replies `Added AP [<index>]...`. |
| `foxhunt -w <ap_index>` | Fox-hunt the AP at `<ap_index>` in the board's list. Streams RSSI samples. |
| `foxhunt -s <ap_index> <sta_index>` | Fox-hunt the station `<sta_index>` associated with AP `<ap_index>`. |
| `info` | Print board identity (firmware, hardware, partition, IDF, MACs). |
| `info -a <ap_index>` | Print one AP's detail (ESSID/BSSID/channel/RSSI/frames/stations/brand/security). The app sends this from the details screen to recover the AP's security, which `scanall` lines don't carry. |

**Fox-hunt targets are addressed by list index, not by MAC.** Wavehound builds
the list with `scanall`, assigns each AP an index in the order Marauder first
prints it, resolves each station's AP by MAC, and launches the hunt by those
indices. Because a resume (returning from a result's details) keeps the board's
lists intact (no `clearlist`), the stored indices still address the right
targets.

## Board -> Flipper (parsed replies)

| Line shape | When / meaning |
|------------|----------------|
| `<rssi> Ch: <ch> <bssid> ESSID: <essid> <bb> <bb>` | One access point from `scanall`, printed once when first heard. `<rssi>` dBm, `<ch>` 1–13, `<bssid>` the MAC, `<essid>` the SSID (the board prints the BSSID in place of the SSID for hidden networks), and two trailing beacon capability bytes Wavehound strips. The N-th distinct AP line is Marauder list index N-1. |
| `<n>: ap: <ap_mac> -> sta: <sta_mac>` or `<n>: sta: <sta_mac> -> ap: <ap_mac>` | One station from `scanall`, printed once when first associated with a known AP. `<n>` is Marauder's station-list size; the app does not read it and instead numbers stations by the order it sees them (same result, since each station is printed exactly once). The AP is matched by `<ap_mac>` to an AP already seen. |
| `<name> RSSI: <dBm>` | One fox-hunt sample, printed per matching frame. Wavehound accumulates these over 100 ms windows into the hunt meter/graph/geiger. Marauder forces the target's channel while it is fresh and hops after ~1.5 s of silence (so a true channel "pin" is not possible). |
| `Added AP [<index>]...` / `AP already exists at [<index>]:...` | Reply to `add -a`: the first on a fresh add, the second when that MAC was already in the list. Either way `<index>` is the AP-list slot the app then hunts with `foxhunt -w`. |
| `<label>: <value>` | Reply lines to `info` (e.g. `Firmware: Marauder`, `Version: v1.19.0`, `Hardware: ...`, `App partition: ...`, `ESP-IDF: ...`, `Station MAC: ...`, `AP MAC: ...`). Shown as-is on the Board info screen; there is no end marker, so the screen renders progressively. |
| `Security: <value>` | Part of the `info -a` reply. The app parses just this line and maps the value to a short token (`Open`/`WEP`/`WPA`/`WPA2`/`WPA3`/`WAPI`; `WPA2 Enterprise`, `WPA/WPA2 Mixed` etc. collapse to the base token, `Unknown` is left blank) for the AP details line. Marauder reports no 802.11 PHY/mode, so that is not shown. |

## Dropped Wavehound features

Marauder exposes no serial equivalent for a per-channel **spectrum sweep** or a
**noise-floor** reading, so those Wavehound features were removed. The hunt
screen still shows avg/peak/trend/frames-per-second, all derived on the Flipper
from the raw `RSSI:` stream.
