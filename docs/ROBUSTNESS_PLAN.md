# Robustness plan: fragilities in the Marauder-based app

Findings from a read-only audit of the app (parsers, UART framing, index/state
handling, the add→foxhunt handshake, threading, lifetimes, error paths). The
audit found **no crash-class bugs** in the parsers or command builders. It did
find places where the app can silently hunt the wrong target or nothing at all.

The class of bug: the app drives Marauder's human-readable CLI, which has no
acknowledgements, and keeps its own counters (`scan_ap_count`, `mo_index`) that
must stay in lockstep with Marauder's internal lists. Anything that desyncs them
fails silently.

Already fixed before this audit (do not redo): hunting via `foxhunt -s` with
app-tracked station indices (now `add -a` → `foxhunt -w`), the per-hunt `channel`
poll, and the dead `nf` / `pin_channel` / `sta_index` fields.

Note: the auditor lost access to the cached Marauder v1.19.0 source partway
through, so items marked **unverified** need a check against the source before
acting on them.

## Outcome (v2.1)
Every item below was checked against the app and the Marauder v1.19.0 source and
addressed on the `robustness-fixes` branch; each carries its verdict. New
findings from that check: Marauder echoes each command as `#<command>` and
prints `Starting<name>. Stop with stopscan` on a real hunt start (both now used
as acks); `add -a` creates entries with security 0 = "Open" and RSSI -127, so
`info -a` is only asked for entries that already existed and an `RSSI: -127`
reply suppresses the security. A real-board shakedown is still due before
tagging v2.1.

## Status key
`[ ]` open · `[x]` done · `(rec)` recommended for the next change · `(later)` real but lower impact

## Recommended change set

### 1. `mo_index` can drift from Marauder's real AP index — HIGH (rec)
- **Where:** `wavehound.c:89` (`mo_index = scan_ap_count++`), `scenes/wh_scene_hunt.c:62-64` (`foxhunt -w <mo_index>`), `scenes/wh_scene_details.c:56-58` (`info -a <mo_index>`).
- **Assumption:** every AP line the app counts is an entry Marauder added at that index, in order, with no line lost or invented.
- **Ways it breaks:**
  - a. **Restart ghosts.** `scenes/wh_scene_target_list.c:133-148` zeroes `result_count` and `scan_ap_count` while `board_mode` is still `Scanning` from the previous scan, and only then sends `stopscan` + `clearlist -a`. Old-scan AP lines still in flight are stored with indices 0..k. Marauder then clears its list, so every real AP after that is offset by k and the ghosts are phantom rows.
  - b. **Printed but not added.** In Marauder's scanall callback the AP line and beacon bytes are printed first; `checkMem()` / `memoryGuardedAdd` run afterwards (about `WiFiScan.cpp:7586-7630`), so an AP can be printed and never added. The app still does `scan_ap_count++`. If that AP is re-printed later the app dedups by MAC and keeps the stale first `mo_index` (`wavehound.c:51-53,89`).
  - c. **Dropped UART bytes.** `wh_uart.c:34` calls `furi_stream_buffer_send(..., 0)` and ignores the result. The 1024-byte buffer holds about 90 ms at 115200 baud. The UI thread holds `results_mutex` while calling `submenu_add_item` up to 96 times (`target_list.c:120-128`), and `board_info` does the same (`scenes/wh_scene_board_info.c:37-44`), while the worker blocks on `FuriWaitForever`. Bytes drop; a dropped line is a missed AP, and two merged lines both fail to parse.
  - d. **Newline in an SSID.** SSIDs may contain 0x0A. One beacon becomes two lines, and a crafted SSID like `"\n-40 Ch: 6 aa:bb:cc:dd:ee:ff ESSID: x"` injects a fake AP row and shifts every later index. Any nearby transmitter can do this.
  - e. **Mode set too late.** `target_list.c:52-57` calls `wh_marauder_scan()` and then sets `board_mode = Scanning`, so early AP lines are dropped as `Idle`. Every other mode transition sets the mode first.
  - f. **Marauder clears its own list** (**unverified**): `WiFiScan.cpp:8250` and `8579` print "AP List Full - Clearing list to make room" in sniffer callbacks. Not confirmed to be on the scanall path. If it is, indices restart at 0 and the app keeps counting.
  - g. **Channel above 14.** `wh_marauder.c:88` rejects `ch > 14`, but Marauder would still index that AP. Unlikely on the S2 board.
- **Failure:** the user picks AP "Cafe" and the hunt follows a different AP, or `foxhunt -w` is out of range and Marauder silently does nothing. `info -a` can show another AP's security.
- **Fix:**
  - Stop trusting `mo_index`. Hunt every target, APs included, through the add handshake; Marauder's `already exists at [n]` reply returns the authoritative index by MAC.
  - Set `board_mode = Scanning` before sending `scanall`.
  - On restart, drop in-flight lines from the previous scan (a restart generation counter, or a sentinel command whose echo marks the boundary) before accepting results.
  - For `info -a`, use the index from the add handshake as well, or skip it when the index is unconfirmed.
- [x] **Resolved (v2.1).** Validated against Marauder v1.19.0 source. Confirmed 1a–1e and 1g; **1f refuted** (those "AP List Full" prints are the pinescan/multissid trackers, not `access_points`). Worse than reported: `RunAPScan` deletes and rebuilds `access_points` on **every** `scanall`, so every resume invalidated all stored indices, and `info -a` with a stale index can dereference a null station list on the board. Fix: `mo_index`/`ap_index`/`scan_ap_count` removed; every index comes from the `add -a` handshake (`wh_add_begin`); `WhBoardScanArming` drops lines until the `#scanall` echo (1a, 1e); RX buffer 4 KB (1c). 1d remains possible but is now a cosmetic phantom row.

### 2. The add→foxhunt handshake has no timeout, retry or failure parsing — HIGH (rec)
- **Where:** `scenes/wh_scene_hunt.c:13,18-21,57-75`, `wh_marauder.c:164-178`.
- **Assumption:** every `add -a` yields a parseable `Added AP [n]` / `already exists at [n]`, and one `foxhunt -w` always starts the hunt.
- **Failure scenarios:**
  - `Memory pressure: AP not added` (`CommandLine.cpp:2078,2101`) is never parsed, and neither is `Format must be XX:XX...` for a malformed MAC. The reply can also be dropped (see 1c). `s_awaiting_add` then stays true forever and the hunt view shows an empty meter with no error.
  - `foxhunt -w` is sent once and `s_awaiting_add` is cleared immediately (`wh_scene_hunt.c:19-20`). If that line is lost or Marauder rejects it, there is no retry.
  - Marauder only parses `foxhunt` inside `if (!wifi_scan_obj.scanning())` (`CommandLine.cpp:~759`). If the earlier `stopscan` was lost or is still tearing down, `foxhunt` is silently ignored. An out-of-range index (`CommandLine.cpp:~770`) is also silent.
  - With no UART (`app->uart == NULL`) the hunt scene never shows "USART busy"; only the target list does.
- **Fix:** run a small state machine on the 100 ms timer.
  - Send the add with a 1.5 s timeout and up to 3 retries.
  - Parse the `Memory pressure` and `Format must be` lines as failures.
  - Treat the first `RSSI:` line as confirmation that the hunt started. If none arrives within about 2 s of `foxhunt`, resend it.
  - Show "add failed" / "board not responding" on the hunt view.
  - Consider resending `stopscan` first and pausing briefly so `foxhunt` is not rejected as "already scanning".
- [x] **Resolved (v2.1).** Hunt scene runs add → foxhunt → channel as a timer-driven state machine with 1.5 s steps and 3 tries; failures parsed (`wh_marauder_parse_add_error`); hunt start confirmed by Marauder's `Starting…Fox Hunt. Stop with stopscan` line (better than "first RSSI line", which a quiet target never sends); a missed start re-sends `stopscan` + `foxhunt`; status shown on the hunt view, including "USART busy".

### 9. Stale `pending_ap_index` and unscoped `Added AP` parsing — MEDIUM (rec, with 2)
- **Where:** `wavehound.c:161-163`, `scenes/wh_scene_hunt.c:72`.
- **Failure:**
  - The parse runs in every mode with no generation token. If the user leaves a hunt and quickly starts another, the previous hunt's late `Added AP [5]` reply lands after `pending_ap_index = -1` and the new hunt follows index 5.
  - A hunted target's name or SSID containing `"Added AP ["` can also set it.
  - `pending_ap_index` is written by the worker and read by the timer thread with no lock. It is a 16-bit aligned value so it is not torn, only loosely ordered.
- **Fix:** only parse the reply while an add is pending (`s_awaiting_add`), and match the MAC (or name) in the reply to the one we sent. Guard the field with `results_mutex`.
- [x] **Resolved (v2.1).** MAC matching isn't possible (`already exists at [n]: <essid>` carries no MAC); instead replies are only parsed after Marauder's echo of our exact `add` command, so an earlier add's late reply is ignored. Replies are matched at the line start, `-e <name>` is no longer sent, and the handshake state lives under `results_mutex`.

### 6. `parse_foxhunt` takes the first `" RSSI: "` and accepts any line containing it — MEDIUM (rec)
- **Where:** `wh_marauder.c:131`.
- **Failure:** the hunt line is `"<name> RSSI: <n>"`, and `<name>` is the target's ESSID or a label sent via `add -e`. A name containing `" RSSI: "` gives a wrong value. `info -a` prints `"    RSSI: -50"` (`WiFiScan.cpp:4865`), so a late `info -a` reply arriving after Details → Hunt is counted as a fake hunt sample.
- **Fix:** use the last occurrence and require that the integer runs to the end of the line.
- [x] **Resolved (v2.1).** The suggested fix alone still accepted `"    RSSI: -50"`; the parser also requires a non-blank name, and samples only count after the hunt-start ack.

## Smaller fixes worth bundling

### 3. Hunt timer is a static; use-after-free if the app exits from the hunt scene — MEDIUM (rec, latent)
- **Where:** `scenes/wh_scene_hunt.c:9,99-107`, `wavehound.c:269-280,315-325`.
- **Assumption:** the hunt scene's `on_exit` always runs before teardown. `scene_manager_free` does not run the active scene's `on_exit` (see the comment in `wh_app_free`).
- **Failure:** if `view_dispatcher_stop` fires while the hunt scene is active, `s_feed_timer` keeps running after `wh_uart_free` and `free(app)`. The next tick calls `wh_marauder_foxhunt_ap(app, ...)` or `wh_hunt_feed_flush(app)` on freed memory. No `view_dispatcher_stop` call was found on the hunt path, so this is latent.
- **Fix:** store the timer in `app` and stop and free it in `wh_app_free` before `wh_uart_free` (or expose `wh_scene_hunt_shutdown()`).
- [x] **Resolved (v2.1).** Timer moved to `app->hunt_timer`; `wavehound_app()` stops and frees it before `wh_uart_free`.

### 8. No "no board" / silent-board feedback — MEDIUM (rec)
- **Where:** `wavehound.c:205` (`board_alive`), used only in `scenes/wh_scene_board_info.c:25`. `target_list.c:68-82` only checks `app->uart`.
- **Failure:** with the board unplugged or hung, the scan header animates "Scan [#  ] 0 networks" forever and a hunt shows an idle meter. Board Info decides "No board" once, at scene entry.
- **Fix:** show "No board" in the target list and the hunt screen when `!board_alive` after about 3 s. Add a "no frames for N s" hint in the hunt view.
- [x] **Resolved (v2.1).** Target list header "No board!" after 3 s silent; hunt view status "no board"; Board info re-checks on tick.

### 10. `board_info` grows without bound in Info mode — MEDIUM (rec)
- **Where:** `wavehound.c:136-143`.
- **Failure:** every line containing `':'` is appended while the Board Info scene is open, including keep-alive `channel` replies if they contain a colon, boot garbage and any asynchronous chatter. There is no cap, so memory grows for as long as the user stays on the screen.
- **Fix:** cap the string (for example 2 KB), or stop collecting after a quiet period.
- [x] **Resolved (v2.1).** Capped at 2 KB (`WH_BOARD_INFO_MAX`). Note: the keep-alive `channel` reply is a bare integer without a colon, so it never fed this.

### 12. Minor races and edge cases — LOW-MEDIUM (rec for the cheap ones)
- **UART worker stack** (`wh_uart.c:97`): 2048 bytes, while the worker holds `line[160]` and `buf[64]` and calls `snprintf` / `furi_string_cat_printf` inside the callback. Tight; raise to 3-4 KB.
- **Stale RSSI:** `hunt_last` is not reset in `hunt_on_enter` (`wh_scene_hunt.c:51-55`), so the previous hunt's last RSSI shows until the first frame.
- **Hunt restart on save:** Save-from-hunt calls `next_scene`, which runs the hunt `on_exit` (`stopscan`) and re-runs the whole handshake on return. It works only because `already exists` dedups. The hunt is interrupted for the whole name entry.
- **Queued pick after restart:** `sync_rows` row ids are `result index + 1`, so a pick event queued before Search-again resolves to a different result afterwards.
- **Unverified, resume path:** `target_list.c:152-166` assumes `stopscan` + `scanall` does not clear Marauder's lists. If it does, every stored `mo_index` and `ap_index` is stale.
- **Unverified, "Auto" channel:** `foxhunt -w` passes the entry's channel to `setFoxHuntTarget` (`CommandLine.cpp:776`). For a channel-0 added entry that is channel 1, while the UI reports an unlocked/hopping state. Not checked whether the channel is applied.
- **Added-AP clutter:** each `add -a` leaves a fake AP in Marauder's list (`selected = true`). After a resume without `clearlist`, new APs get real indices one higher than `scan_ap_count` per added entry. Latent: the clients filter is computed consistently inside the app, and AP search is not reachable after a non-AP hunt without a fresh clear.
- **Saved names with spaces:** `-e <name>` with spaces becomes extra tokens because `add -e` takes one argument. Harmless today since the first `-b` and `-ch` win.
- [x] **Resolved (v2.1)** where worth it. Worker stack 3 KB; `hunt_last` reset. **Resume (unverified) confirmed** — fixed by item 1. **Auto channel (unverified) confirmed**, and broader: starting a hunt tunes to the list entry's channel, overriding an earlier `channel -s`, and an "already exists" entry ignores our `-ch`; the app now sends `channel -s` after the start ack (or `channel` for Auto) and shows the channel the board reports. Added-AP clutter and names with spaces are moot (no index arithmetic, no `-e`). Not changed: save-from-hunt still re-runs the handshake on return (now acked and retried); queued pick after restart (needs two presses within one dispatch).

## Real, but lower impact (later)

### 4. Results table shared by APs and stations, with silent overflow — MEDIUM (later)
- **Where:** `wavehound.c:49-59`, `wavehound_i.h:20` (`WH_MAX_RESULTS 96`).
- **Failure:** a busy scan of mostly stations fills the 96 slots. In AP-only mode later APs are dropped (`wh_results_slot` returns NULL) and the list shows a normal count with no "full" indicator.
- **Fix:** reserve slots per kind (for example 48 for APs) or show a "list full" marker.
- [x] **Resolved (v2.1).** AP-only searches skip station lines; the header shows "(full)" at `WH_MAX_RESULTS`.

### 5. Clients filter is evaluated once per row and never re-evaluated — MEDIUM (later)
- **Where:** `target_list.c:122-126` with `wavehound.c:103`.
- **Failure:** `s_shown` advances past a non-matching row permanently. A station whose `ap_index` is -1 at first sight stays hidden even after the AP row arrives and `ap_index` resolves (AP line dropped, late, or table full). The `want` index is also recomputed by an O(n) loop per row (performance nit only).
- **Fix:** keep a per-result "shown" flag and re-scan unshown rows each tick.
- [x] **Resolved (v2.1).** Stations keep their AP's MAC from the station line itself (filter no longer depends on the AP row), and an AP row backfills the channel of stations seen before it.

### 7. Beacon-byte stripping eats SSID tokens when Marauder omits the bytes — MEDIUM (later)
- **Where:** `wh_marauder.c:101-111`.
- **Assumption:** two trailing hex tokens are always present. They are printed only inside the `checkMem` branch (`WiFiScan.cpp:~7609`), so under memory pressure the line ends with the SSID.
- **Failure:** SSID `"Room 12"` or `"Net AB"` loses its last token and becomes `"Room"`. The wrong name is then shown in labels and sent via `add -e`. It coincides with the not-added case in 1b.
- **Fix:** accept it as a heuristic and treat the SSID as display-only. Identity is by MAC everywhere, so no logic depends on it.
- [x] **Resolved (v2.1)** as a heuristic: only uppercase hex pairs are stripped (Marauder prints `%02X`), the SSID is display-only and no longer sent to the board.

### 11. Saved-MAC and settings file handling — MEDIUM-LOW (later)
- **Where:** `wh_store.c:50,71`, `scenes/wh_scene_saved_entry.c` (`wh_parse_mac` return ignored), `scenes/wh_scene_name_input.c:36`, `wh_settings.c:104`.
- **Failures:**
  - Load checks `mac[0]` is non-empty but never validates it as a MAC. A hand-edited bad MAC goes into `add -a -b` and Marauder answers `Format must be...`, which hangs the handshake (see 2). The ignored `wh_parse_mac` result also leaves `mac_bytes` stale, so vendor and `randomized` come from the previous target.
  - `wh_store_write` opens with `FSOM_CREATE_ALWAYS`. SD full or power loss mid-write loses the whole file. Settings have the same pattern.
  - `wh_store_add` returns false when the list is full or the input is empty, and the caller ignores it, so a save silently does nothing.
- **Fix:** validate the MAC on load, show a "saved list full" / "save failed" message, and write atomically (write `.tmp`, then rename).
- [x] **Resolved (v2.1).** MACs validated on load; `wh_parse_mac` results checked; saved list and settings written to `.tmp` then renamed; a failed save shows "list full" / "couldn't write".

## Cosmetic / nits
Resolved in v2.1: control characters in SSIDs show as `?`; the vendor line says
"no oui.db" vs "Unknown" and "multicast" instead of the em dash. Unchanged by
design: trimmed SSID spaces, first-seen RSSI in row labels (rows are never
rebuilt so the highlight stays put), silent settings failures (the write is now
atomic), `ITEM_FROM_AP == WhEventMacEntered` (different scenes).
- Leading and trailing SSID spaces are stripped, and an all-space SSID is shown as hidden (`wh_marauder.c:99,112`).
- `oui` reports "Unknown" for both a missing DB and an unmatched OUI, and uses a UTF-8 em dash that the Flipper font may not render (`wh_oui.c:64`).
- Row labels freeze the first-seen RSSI, since existing rows are never updated.
- Non-printable bytes, including ESC, from garbage or an SSID reach Widget text. Only the Board Info scroll element interprets `\e#`.
- Settings load and save failures are silent. `ITEM_FROM_AP` (100) numerically equals `WhEventMacEntered`; they are in different scenes, so it is harmless.

## Verified OK (no change needed)
- **MAC case:** Marauder prints AP and station MACs lowercase (`getMAC` uses `%02x`, `WiFiScan.cpp:7374`) and the app compares lowercase to lowercase (`wavehound.c:65-68`). Uppercase `%02X` appears elsewhere in Marauder (for example `WiFiScan.cpp:6197,6349`), so normalize before comparing against `info` or other output.
- **Buffer bounds:** `mac_str[18]`, `parse_mac6` and the `cmd[24/80]` buffers are safe. The longest `add` command is about 70 bytes (34 + `" -e "` + 32 SSID chars), which fits in 80, and `snprintf` truncation is safe anyway.
- **Line framing:** over-long lines are truncated to 159 bytes, not split (`wh_uart.c:63-65`). A legitimate AP line is at most about 100 characters. `\r` is dropped and empty lines are skipped.
- **Prompt strip:** the `"> "` loop (`wavehound.c:116`) is bounds-safe; it only mis-strips a line that really starts with `"> "`.
- **Hidden-AP detection:** Marauder prints the BSSID in place of an empty SSID (`WiFiScan.cpp:~7581`), and the case-identical `strcmp` against `mac_str` is correct.
- **Timer, speaker, vibro, backlight:** timer create and free are paired in `on_enter` / `on_exit` and the timer is stopped before `stopscan` is sent, so no `foxhunt` can follow it. Speaker and vibro release on the hunt view's worker exit (`views/wh_hunt_view.c:262-266`). Backlight enforce is released in both `on_exit` and `wh_app_free`.
- **UART shutdown order:** RX stop, then stop flag and join, then deinit, so the callback cannot touch a freed app.
- **Keep-alive and `tx_mutex`:** sends are atomic per command line, so the worker's `channel` poll cannot corrupt another command (its reply can still interleave).
- **`oui.db` missing:** falls through to "Unknown", no crash.

## Suggested order
1. #1 + #9 + #2 together (the add handshake becomes the single hunt path, with a state machine and scoped replies).
2. #6 (one-function parser fix).
3. #3, #8, #10 and the cheap items from #12.
4. Cut v2.1 (changelog, `commit_sha`, tag) once a real-board shakedown passes.
5. Later, as needed: #4, #5, #7, #11.

Verify the **unverified** items against the Marauder v1.19.0 source before relying on them.
