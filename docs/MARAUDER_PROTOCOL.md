# Marauder serial protocol (the subset Wavehound uses)

Wavehound drives a WiFi dev board running **stock ESP32 Marauder v1.19.0** over
the Flipper USART (GPIO 13 TX / 14 RX <-> ESP32 UART, 115200 baud, 8N1).
Marauder's serial interface is its human-readable command line, not a machine
protocol: the app sends CLI commands terminated by `\n` and parses the free-text
lines the board prints back. Both sides ignore lines they do not understand, so
boot chatter is harmless.

This documents only the commands and replies Wavehound relies on. The command
builders live in [`wh_marauder.c`](../wh_marauder.c) and the line parsers in the
same file; the UART worker that dispatches lines by mode is in
[`wavehound.c`](../wavehound.c).

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
| `channel -s <ch>` | Park the radio on channel `<ch>` (1–13) before a hunt. On the screenless dev board this is a **hard park**: nothing moves the channel during a `foxhunt` (see below), so the hunt stays exactly here. |
| `clearlist -a` | Clear the board's AP **and** station lists, so the next scan's indices restart at 0. Sent before a fresh scan, never on a resume. |
| `scanall` | Start a combined AP + station scan (`WIFI_SCAN_AP_STA`). Streams AP and station lines until `stopscan`. It always sweeps every channel (the `ChanHop` setting does not apply to this mode), so a channel choice in the app can't narrow the scan — it only seeds a later hunt. The app's "clients of a nearby AP" filter is therefore done Flipper-side on the stream; the board is **not** scoped to that AP's channel, so it only listens there ~1/13 of the time and the client list fills slowly. Marauder's station→AP association is also loose: it logs a client for **any** data frame whose source or receiver is the AP's BSSID (no ToDS/direction check, multicast destinations not filtered), so the list may include devices that aren't truly associated. |
| `add -a -b <mac> -ch <ch> [-e <ssid>]` | Add an entry by MAC to the board's list (channel defaults to 1 for auto). Used so a station, a hand-typed MAC or a saved MAC can be hunted; the board replies `Added AP [<index>]...`. |
| `foxhunt -w <ap_index>` | Fox-hunt the list entry at `<ap_index>`, streaming RSSI samples. It matches the entry's MAC as transmitter, receiver **or** BSSID, so it tracks a station just as well as an AP. Wavehound hunts *everything* this way (`foxhunt -s` is not used — its station-index/association validation proved unreliable). |
| `info` | Print board identity (firmware, hardware, partition, IDF, MACs). |
| `info -a <ap_index>` | Print one AP's detail (ESSID/BSSID/channel/RSSI/frames/stations/brand/security). The app sends this from the details screen to recover the AP's security, which `scanall` lines don't carry. |

**Hunting is by MAC, via a list index.** `foxhunt` takes a list index, not a
MAC, so to hunt an AP Wavehound uses the index Marauder assigned it during
`scanall` (APs are numbered in the order their lines are first printed). To hunt
a station, a typed MAC or a saved MAC, Wavehound first `add -a`s the MAC (which
returns a fresh index) and then `foxhunt -w`s that — the same reliable path for
all non-AP targets. AP indices only need to stay valid between the scan and the
hunt, which they do because a resume (returning from a result's details) keeps
the board's lists intact (no `clearlist`).

## Board -> Flipper (parsed replies)

| Line shape | When / meaning |
|------------|----------------|
| `<rssi> Ch: <ch> <bssid> ESSID: <essid> <bb> <bb>` | One access point from `scanall`, printed once when first heard. `<rssi>` dBm, `<ch>` 1–13, `<bssid>` the MAC, `<essid>` the SSID (the board prints the BSSID in place of the SSID for hidden networks), and two trailing beacon capability bytes Wavehound strips. The N-th distinct AP line is Marauder list index N-1. |
| `<n>: ap: <ap_mac> -> sta: <sta_mac>` or `<n>: sta: <sta_mac> -> ap: <ap_mac>` | One station from `scanall`, printed once when first associated with a known AP. Wavehound reads `<sta_mac>` (the device) and `<ap_mac>` (matched to an AP already seen, to tag which AP it belongs to for the clients filter); the station carries no RSSI or channel, so it inherits its AP's channel. `<n>` is ignored. |
| `<name> RSSI: <dBm>` | One fox-hunt sample, printed per matching frame the board hears **on the parked channel** (it matches the target MAC as transmitter, receiver, or BSSID). Wavehound accumulates these over 100 ms windows into the hunt meter/graph/geiger. The sample carries no channel — on the dev board the radio never leaves the channel the hunt started on, so the app just shows that fixed channel. |
| `<n>` (a bare integer, 1–13) | Reply to a `channel` query — the board's current channel. Used only as an idle liveness heartbeat; the app does not parse it during a hunt. |
| `Added AP [<index>]...` / `AP already exists at [<index>]:...` | Reply to `add -a`: the first on a fresh add, the second when that MAC was already in the list. Either way `<index>` is the AP-list slot the app then hunts with `foxhunt -w`. |
| `<label>: <value>` | Reply lines to `info` (e.g. `Firmware: Marauder`, `Version: v1.19.0`, `Hardware: ...`, `App partition: ...`, `ESP-IDF: ...`, `Station MAC: ...`, `AP MAC: ...`). Shown as-is on the Board info screen; there is no end marker, so the screen renders progressively. |
| `Security: <value>` | Part of the `info -a` reply. The app parses just this line and maps the value to a short token (`Open`/`WEP`/`WPA`/`WPA2`/`WPA3`/`WAPI`; `WPA2 Enterprise`, `WPA/WPA2 Mixed` etc. collapse to the base token, `Unknown` is left blank) for the AP details line. Marauder reports no 802.11 PHY/mode, so that is not shown. |

## Dropped Wavehound features

Marauder exposes no serial equivalent for a per-channel **spectrum sweep** or a
**noise-floor** reading, so those Wavehound features were removed. The hunt
screen still shows avg/peak/trend/frames-per-second, all derived on the Flipper
from the raw `RSSI:` stream.

## Fox hunt is fixed-channel on the dev board

Marauder's fox-hunt **channel logic** — park on the target's channel, hop to
re-acquire after ~1.5 s of silence, follow the target as it moves — lives
entirely inside `WiFiScan::runFoxHunt()`, which is wrapped in `#ifdef HAS_SCREEN`.
The Flipper WiFi dev board build (`MARAUDER_FLIPPER`) does not define
`HAS_SCREEN`, so that function compiles to nothing. What still runs is the
promiscuous RX callback's `WIFI_SCAN_SIG_STREN` branch (not screen-gated), which
matches the target and prints `<name> RSSI: <dBm>`.

So on the dev board a fox hunt simply reports RSSI on whatever channel it
started on and never changes channel. Consequences:

- A hunt **stays parked** on the channel we set with `channel -s` — effectively
  a hard pin, which is good for a target that holds still on one channel
  (including quiet ones: you just get bursty readings between its frames).
- There is **no sweep-to-find**: an "Auto" hunt with no known channel sits on
  one channel and won't discover a target elsewhere.
- There is **no follow**: if the target changes channel — or moves to 5 GHz,
  which this 2.4 GHz-only radio can't see — it drops out until it is re-scanned
  and hunted again on its new channel.

(On screen-equipped Marauder boards `runFoxHunt` is compiled in, so they *do*
hop/follow during a hunt; this section describes the dev board only.)
