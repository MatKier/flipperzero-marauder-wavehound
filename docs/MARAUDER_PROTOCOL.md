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

## Prompt and echo

After it finishes handling a command, Marauder prints a `"> "` prompt **with no
trailing newline**. It therefore prefixes the first line of the next response
(e.g. `> #scanall`). Wavehound strips a leading `"> "` from every received line
before parsing.

Marauder also **echoes every command** it reads as `#<command>` on its own line
before handling it (`CommandLine::runCommand`). The CLI has no other
acknowledgements, so Wavehound uses the echo as a sequence marker:

- `#scanall` marks where a new scan's output starts. Lines before it are from the
  previous scan (still printing until the board read our `stopscan`) and are
  dropped.
- `#add -a -b <mac> -ch <ch>` (exactly the command sent) marks where our `add`'s
  reply starts. An `add` reply only counts after its echo, so a late reply to an
  earlier add can't be taken for ours.

## Flipper -> board (commands)

| Command | Purpose |
|---------|---------|
| `stopscan` | Stop any scan or hunt; return to idle. Sent on startup, when leaving a scan/hunt, on app exit, and before re-sending a `foxhunt` that didn't start. |
| `channel` | Print the current channel. Used as a liveness poll while the link is quiet (any received line proves the link is up), and once after a hunt starts on **Auto** to learn which channel it parked on. |
| `channel -s <ch>` | Set the radio to channel `<ch>` (1–13). Sent **after** a hunt has started (see below); on the screenless dev board nothing moves the channel during a `foxhunt`, so this is a hard park. Accepted while a scan/hunt runs (it is outside Marauder's "not scanning" guard). |
| `clearlist -a` | Clear the board's AP and station lists. Sent before a **fresh** scan so stations Marauder already knows are printed again; not sent on a resume. (The AP list is rebuilt by `scanall` regardless.) |
| `scanall` | Start a combined AP + station scan (`WIFI_SCAN_AP_STA`). Streams AP and station lines until `stopscan`. **Every `scanall` discards and rebuilds Marauder's AP list** (`RunAPScan` does `delete access_points`), so AP indices from an earlier scan never survive. It always sweeps every channel (the `ChanHop` setting does not apply to this mode), so a channel choice in the app can't narrow the scan. The app's "clients of a nearby AP" filter is therefore done Flipper-side on the stream; the board is **not** scoped to that AP's channel, so it only listens there ~1/13 of the time and the client list fills slowly. Marauder's station→AP association is also loose: it logs a client for **any** data frame whose source or receiver is the AP's BSSID (no ToDS/direction check, multicast destinations not filtered), so the list may include devices that aren't truly associated. |
| `add -a -b <mac> -ch <ch>` | Look up a MAC in the board's AP list, adding it if absent (channel 1 for Auto). This is how Wavehound gets **every** list index it uses — for a scanned AP, a station, a typed or a saved MAC — because Marauder dedups by MAC and reports the existing index. No `-e <name>` is sent: the entry's name stays the MAC, so no SSID or user text reaches Marauder's quote-aware tokenizer. Retried (idempotently) up to 3 times if no reply arrives within 1.5 s. |
| `foxhunt -w <index>` | Fox-hunt the list entry at `<index>`, streaming RSSI samples. It matches the entry's MAC as transmitter, receiver **or** BSSID, so it tracks a station just as well as an AP (`foxhunt -s` is not used — its station-index/association validation proved unreliable). Marauder **silently ignores** it while a scan is still running or when the index is out of range, so the app waits for the start line (below) and otherwise sends `stopscan` + `foxhunt` again, up to 3 times. Starting the hunt tunes the radio to the **list entry's** channel (`setFoxHuntTarget` → `RunRawScan` → `changeChannel`), which is why `channel -s` is sent after it. |
| `info` | Print board identity (firmware, hardware, partition, IDF, MACs). |
| `info -a <index>` | Print one AP's detail (ESSID/BSSID/channel/RSSI/frames/stations/brand/security). The details screen sends it to show an AP's security, which `scanall` lines don't carry, using the index from an `add` that answered **already exists** (a scanned entry). Marauder does not range-check this index (an out-of-range one dereferences a null station list), which is one more reason indices are never cached. |

**Hunting is by MAC, via a fresh index.** The hunt is a short handshake driven
from the hunt screen's 100 ms timer: `add -a` → wait for the echo then
`Added AP [n]` / `already exists at [n]` (or a failure line) → `foxhunt -w n` →
wait for `Starting…Fox Hunt` → `channel -s <ch>` (or `channel` for Auto). The
hunt screen shows *starting...* until then, and a short reason if a step fails
(*no board reply*, *MAC rejected*, *low board memory*, *start failed*).

## Board -> Flipper (parsed replies)

| Line shape | When / meaning |
|------------|----------------|
| `#<command>` | Echo of a command the board read (see above). Matched exactly for `#scanall` and our `#add …`. |
| `<rssi> Ch: <ch> <bssid> ESSID: <essid> <BB> <BB>` | One access point from `scanall`, printed once when first heard in this scan. `<rssi>` dBm, `<ch>` 1–14, `<bssid>` the MAC (lowercase), `<essid>` the raw SSID (the board prints the BSSID in place of the SSID for hidden networks). The two trailing **uppercase** hex beacon bytes are printed only when Marauder has memory to keep the entry; Wavehound strips up to two trailing uppercase hex-pair words (a heuristic: an SSID ending in e.g. ` 12` loses that word when the bytes are missing). Control characters in the SSID are shown as `?`. The SSID is display-only. Note that an AP can be printed and then **not** added (low memory), so print order is not list order — another reason indices come from `add`. A crafted SSID containing a newline can make a fake AP line; with MAC-keyed rows this can only add a cosmetic phantom row. |
| `<n>: ap: <ap_mac> -> sta: <sta_mac>` or `<n>: sta: <sta_mac> -> ap: <ap_mac>` | One station from `scanall`, printed once when first seen with a known AP. Wavehound keeps `<sta_mac>` (the device) and `<ap_mac>` (its AP, used by the clients filter — so the filter works even before the AP's own row arrives). The station carries no RSSI or channel; it inherits its AP's channel, now or when the AP row arrives. `<n>` is ignored. Station lines are skipped in an AP-only search. |
| `Added AP [<index>][CH:<ch>] <name> (selected)` / `AP already exists at [<index>]: <essid>` | Reply to `add -a`, matched at the line start and only after our echo. The first is a fresh entry (no scan data: Marauder gives it RSSI -127 and default "Open" security); the second an existing one. Either way `<index>` is the slot to `foxhunt -w` / `info -a`. |
| `Format must be XX:XX:XX:XX:XX:XX` / `BSSID required…` / `Memory pressure: AP not added` | `add -a` failures: a malformed MAC, or the board too short of memory to add the entry. The hunt stops with *MAC rejected* / *low board memory*. |
| `Starting<name>. Stop with stopscan` | Printed by a `foxhunt` that really started (e.g. `StartingFox Hunt. Stop with stopscan` — Marauder prints no space after "Starting"). Matched as a line starting with `Starting` and containing `Fox Hunt. Stop with`. RSSI samples are only counted after it. |
| `<name> RSSI: <dBm>` | One fox-hunt sample, printed per matching frame the board hears **on the parked channel**. `<name>` is the entry's ESSID (any text) or, for an entry Wavehound added, its MAC. Parsed from the **last** ` RSSI: `; the value must be an integer running to the end of the line and the name must not be blank (which rejects `info -a`'s `    RSSI: -50` line). Wavehound accumulates samples over 100 ms windows into the hunt meter/graph/geiger. |
| `<n>` (a bare integer, 1–14) | Reply to `channel` / `channel -s` (the latter prints it twice) — the board's current channel. While idle it is just a liveness heartbeat; once a hunt has started it is shown as the hunt's channel. |
| `<label>: <value>` | Reply lines to `info` (e.g. `Firmware: Marauder`, `Version: v1.19.0`, `Hardware: ...`, `App partition: ...`, `ESP-IDF: ...`, `Station MAC: ...`, `AP MAC: ...`). Shown as-is on the Board info screen; there is no end marker, so the screen renders progressively (capped at 2 KB). |
| `Security: <value>` | Part of the `info -a` reply, matched at the line start. The app maps the value to a short token (`Open`/`WEP`/`WPA`/`WPA2`/`WPA3`/`WAPI`; `WPA2 Enterprise`, `WPA/WPA2 Mixed` etc. collapse to the base token, `Unknown` is left blank) for the AP details line. Ignored when the same reply's RSSI line was `    RSSI: -127`, i.e. the entry was created by an earlier `add` and its "Open" is just Marauder's default. Marauder reports no 802.11 PHY/mode, so that is not shown. |

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

Starting the hunt tunes once, to the list entry's channel; after that nothing
changes it. Consequences:

- A hunt **stays parked** on the channel we set with `channel -s` right after it
  starts — effectively a hard pin, which is good for a target that holds still
  on one channel (including quiet ones: you just get bursty readings between its
  frames).
- There is **no sweep-to-find**: an "Auto" hunt sits on the entry's channel —
  the one Marauder recorded for that MAC during a scan, or channel 1 for a MAC
  it hadn't seen — and won't discover a target elsewhere. The app asks the board
  for that channel and shows it.
- There is **no follow**: if the target changes channel — or moves to 5 GHz,
  which this 2.4 GHz-only radio can't see — it drops out until it is re-scanned
  and hunted again on its new channel.

(On screen-equipped Marauder boards `runFoxHunt` is compiled in, so they *do*
hop/follow during a hunt; this section describes the dev board only.)
