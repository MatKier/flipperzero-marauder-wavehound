# Wavehound

**WiFi fox hunting for the Flipper Zero WiFi dev board.**

Wavehound uses the Flipper and its ESP32-S2 WiFi dev board to locate a 2.4 GHz
WiFi transmitter by its signal strength. Pick an access point or other device
heard during a short listen, or type in a MAC by hand, then walk around: the
Flipper clicks faster, the meter fills and the board's RGB LED shifts from blue
to red as the signal gets stronger.

Typical uses:

- Find that forgotten access point, repeater or IoT plug hidden in a ceiling or closet.
- Track down a rogue AP on a network you administer.
- Locate your own lost phone, smart plug or camera by its MAC.
- See which 2.4 GHz channel is least congested before you configure your router.

Wavehound is **receive-only**. It reads frame headers to measure signal
strength and never transmits deauth or other attack frames. It isn't built on
Marauder because it only needs passive RSSI reading, not Marauder's
attack/injection toolkit, so it runs its own small receive-only firmware instead.

```
┌──────────── Flipper Zero ─────────────┐        ┌──── WiFi dev board (ESP32-S2) ─────┐
│  Wavehound.fap                        │  UART  │  Wavehound firmware                │
│   ├ Targets: AP search, device search,│ 115200 │   ├ timed listen, AP/STA filter    │
│   │  manual MAC entry                 │◄──────►│   │  → target list                 │
│   ├ Hunt view: dBm, meter, graph,     │  pins  │   ├ promiscuous RX → RSSI of target│
│   │  Geiger clicks (speaker)          │ 13/14  │   ├ channel sweep → activity       │
│   ├ Spectrum view: ch 1-13 bars       │        │   └ RGB LED heat indicator         │
│   └ 5V OTG on, expansion svc off      │        │                                    │
└───────────────────────────────────────┘        └────────────────────────────────────┘
```

## Features

| Mode | What you see |
|------|--------------|
| **Hunt access point** | Pick how long to listen: 5, 10 or 30 seconds, or 1, 2 or 5 minutes. Press **OK** during the listen to stop early and jump straight to the results. The board listens on every channel and lists only access points, strongest first, by SSID (hidden networks show their BSSID). Pick one to start hunting. |
| **Search devices** | First pick the channel: **All channels (hop)**, **Channel 1–13**, or **Channel of nearby AP…**, which runs an AP listen so you can pick a network and borrow its channel. Then the same timed listen (5 s to 5 min, OK to stop early), with access points left out. You get the other transmitters: phones, laptops, smart plugs, cameras. `*` marks a randomized (privacy) MAC that may change over time. Pick one to start hunting. If you fixed the channel, the hunt stays on it. |
| **Enter MAC** | Type a MAC on the hex keypad (pre-filled with your last target), then pick the channel the same way: **Auto (hop until heard)**, a channel number, or a nearby AP's channel. A chosen channel is pinned: the board stays there even while the target is quiet. Clients always use their AP's channel, so the AP option is the quick way to hunt a known client. |
| **Hunt screen** | The board parks on the target's channel and reports the signal strength of every frame that MAC sends (beacons, data, probes) every 100 ms. The screen shows a big dBm readout, a COLD/COOL/WARM/HOT! label, a meter with peak marker and a 12-second signal graph. Geiger clicks play through the speaker, faster as the signal gets stronger. `?ch` in the corner means the board is still searching channels. After 8 s of silence it hops again to re-find the target. |
| **Channel spectrum** | The board sweeps channels 1–13 (120 ms dwell each) in promiscuous mode. The Flipper draws one bar per channel with peak hold, a sweep cursor, the busiest channel, and the number of distinct transmitters heard per sweep. You can switch the metric between packets, max dBm and bytes. |

Controls:

- **Hunt:** OK = mute/unmute, Left = reset peak, Back = return to the list
- **Searching:** OK = stop now and show the results, Back = cancel
- **Target list:** the first entry, "~ Search again", reruns the same listen
- **Spectrum:** OK/Left/Right = change metric, Back = return to menu

## Layout

```
firmware/     ESP-IDF project for the ESP32-S2 on the WiFi dev board
  main/main.c     command parser
  main/radio.c    discover / hunt / spectrum state machine + promiscuous callback
  main/proto.c    UART line I/O
  main/led.c      RGB LED (LEDC PWM)
flipper_app/  Flipper external app (uFBT)
  wavehound.c             entry, board power/expansion handling, board line parser
  wh_uart.c               USART worker, line splitting, keep-alive ping
  scenes/                 one file per screen (start, listen time, searching,
                          target list, MAC input, hunt, spectrum, about)
  views/wh_hunt_view.c    hunt screen + Geiger click thread
  views/wh_progress_view.c search progress screen
  views/wh_spectrum_view.c spectrum screen
docs/PROTOCOL.md  the UART protocol
```

## 1. Flash the dev board firmware

Requirements: ESP-IDF 5.x or 6.x **or** PlatformIO (verified building with PlatformIO + ESP-IDF 6.1).

Put the board in download mode: hold **BOOT**, tap **RESET**, release
**BOOT**, and connect the board to your PC over its USB-C port.

With ESP-IDF:

```bash
cd firmware
idf.py set-target esp32s2
idf.py build
idf.py -p COM5 flash        # use your port
```

With PlatformIO:

```bash
cd firmware
pio run -t upload
```

Tap **RESET** afterwards to boot the new firmware. Pins, baud rate, LED and
max channel (11 for US-style regions) are configurable under `idf.py menuconfig`
→ *Wavehound*.

> The defaults assume the official board: UART0 on GPIO43/44 to Flipper
> pins 13/14, and an active-low RGB LED on GPIO6/5/4 (R/G/B). If your clone
> differs, change them in menuconfig.

## 2. Build and install the Flipper app

```bash
pip install ufbt
cd flipper_app
ufbt            # build -> dist/wavehound.fap
ufbt launch     # build, upload over USB and run
```

Or copy `dist/wavehound.fap` to `SD:/apps/GPIO/ESP/` with qFlipper. The app shows up
under **Apps → GPIO → ESP → Wavehound**. Built against official firmware SDK 1.5.1
(API 88.2).

## 3. Use it

1. Plug the dev board onto the Flipper and open Wavehound. The app turns on
   5 V on the GPIO header and takes over the USART from the expansion service.
2. The menu header changes to **"Wavehound - board OK"** once the board answers.
3. Pick a target: **Hunt access point**, **Search devices** or **Enter MAC**.
   Then start walking. Hold the Flipper away from your body and turn slowly.
   Your body blocks the signal, so the strongest reading points toward the
   source.

Hunting phones and other client devices works best while they are in use. An
idle phone may only transmit every few seconds, so the readout updates in
bursts. Phones also rotate randomized MACs (`*`), so a MAC you found yesterday
may not exist today.

## Protocol

The link is a plain-text, line-based protocol, so you can test the board from
any serial terminal. See [docs/PROTOCOL.md](docs/PROTOCOL.md). Example:

```
> HUNT,a4:2b:b0:11:22:33,6
< OK,HUNT
< H,6,9,-47,-48,-45,1
> DISCOVER,10,AP
< OK,DISCOVER
< DISCOVER_BEGIN,10,AP
< DP,1,4
< ...
< DEV,a4:2b:b0:11:22:33,6,-41,93,A,HomeNet
< DISCOVER_END,1,1
> DISCOVER,10,STA,6
< ...
< DEV,3a:4f:12:ab:cd:ef,6,-58,12,SR,
< DISCOVER_END,1,1
```

## Disclaimer

This is a hobby project, provided as-is and without any warranty. Flashing the
dev board and running this firmware is at your own risk; I don't guarantee it
won't damage, brick or otherwise break your hardware. You are responsible for
anything that happens to your Flipper, dev board or other equipment.

This code is AI-assisted: much of it was written with the help of AI tools.
Review it yourself before trusting it or flashing it to your hardware.

Only locate networks and devices you own or are explicitly authorized to locate,
and only on your own network. Wavehound is receive-only, and passive reception is
legal in most places, but radio and privacy rules vary by jurisdiction. Checking
what you are allowed to do, and getting permission first, is up to you.

## License

MIT. See [LICENSE](LICENSE).
