# VFD Desk HUD

![VFD desk dashboard showing the overview page](https://dmello.io/content/images/size/w2000/2026/05/IMG_4594.jpeg)

A desk dashboard built around a 256×50 GP1287BI VFD panel driven by a Raspberry Pi Pico W. It cycles through eleven pages of live info — clock, weather, now-playing with visualizers, OctoPrint print progress, Claude API usage, cat-scale telemetry, and a Portal/Aperture easter egg — pulled from MQTT and a handful of REST endpoints.

Full writeup with photos: **[Building a VFD Desk HUD](https://dmello.io/building-a-vfd-desk-hud-with-a-pi-pico-w/)**.

## Features

- **11 pages** (see gallery below), each navigable by rotary knob, MQTT, or serial
- ~20 fps render loop with independent network/MQTT refresh
- **OTA firmware updates** with a terminal-style `> FLASHING` splash rendered directly from the OTA callbacks
- **Wireless screenshot endpoint** at `http://vfd-dashboard.local/screenshot.bmp` — every image in this README was grabbed that way
- **Home Assistant integration** — `display on/off` from a presence sensor and `lux N` from an ambient-light sensor (mapped through a built-in brightness curve)
- **Two-way MQTT state**: device publishes its current state (page, brightness, display on/off, Claude data, uptime, IP, …) as retained JSON on `vfd/state`, plus an `online` / `offline` availability topic via MQTT LWT
- **LittleFS-cached state** for cats, Claude usage, and last-print timestamp so reboots show known values instantly — no "waiting for…" beat
- Configurable fonts, clock animations, music visualizers, and a "dash glitch" toggle for fun

## Pages

Screenshots are pulled live from the device over the HTTP endpoint and recoloured for the web. Pixels and layout are 1:1 with the real 256×50 panel (upscaled 4×).

|  |  |
| :--- | :--- |
| ![](docs/screenshots/0-overview.png)<br>**0 · Overview** — clock, song, weather, viz / Claude bars (knob-press to toggle) | ![](docs/screenshots/1-time.png)<br>**1 · Time** — full-screen clock with face animations |
| ![](docs/screenshots/2-weather.png)<br>**2 · Weather** — Open-Meteo grid + sunrise / sunset | ![](docs/screenshots/3-now-playing.png)<br>**3 · Now Playing** — Spotify metadata + audio visualizer |
| ![](docs/screenshots/4-matrix.png)<br>**4 · Matrix** — Katakana rain; knob doubles as brightness | ![](docs/screenshots/5-cats.png)<br>**5 · Cats** — Ada & Tux scale telemetry |
| ![](docs/screenshots/6-tamagotchi.png)<br>**6 · Tamagotchi** — procedural cat sim | ![](docs/screenshots/7-claude.png)<br>**7 · Claude** — API budget bars + 4-cell stats strip |
| ![](docs/screenshots/8-portal.png)<br>**8 · Portal** — Aperture splash + typewriter log | ![](docs/screenshots/9-prusa.png)<br>**9 · Prusa** — OctoPrint job, filament, temps |
| ![](docs/screenshots/10-watch.png)<br>**10 · Watch** — clock + compact Claude readout |  |

## Hardware

EEI **EPC-INBN0BV1287UD** carrier (GP1287BI 256×50 VFD) driven from a **Raspberry Pi Pico W** via software SPI.

### Wiring

| Carrier (CN7) | Pico W | Notes                                      |
| ------------- | ------ | ------------------------------------------ |
| FILMENT_EN    | GP2    | Active-HIGH (10kΩ internal pull-down)      |
| CLOCK         | GP9    | Not a hardware SCK pin — software SPI only |
| CHIPSELECT    | GP8    | Active-LOW                                 |
| DATA          | GP7    |                                            |
| RESET         | GP6    | Active-LOW                                 |
| GND           | GND    | **Must** be bonded for signals to work     |

Power the carrier separately via USB-C or DC005 at 4.5–20 V, ≥ 2 A — the VFD can draw ~1.5 A at full brightness. Optionally feed the Pico's `VBUS` from the carrier's `+5V OUT` (CN6 pin 8) so one supply powers both.

> **Warning:** the carrier's on-board boost converter generates VHG = 60 V and VHP = 90 V to drive the VFD anode. Don't poke exposed pins while powered.

## Software

PlatformIO + arduino-pico (Earle Philhower core), u8g2 for rendering, ArduinoJson for parsing, PubSubClient for MQTT.

### Why a custom u8g2 fork

Three gotchas combined:

1. **GP1287BI is not in upstream u8g2.** The vendor fork in `lib/U8g2/` adds a GP1287AI driver; AI and BI share a command set, so the AI driver works for the BI.
2. **LSB-first 8-bit SPI.** u8g2's stock *software* SPI byte drivers are hardcoded MSB-first — only the *hardware* SPI path respects LSB-first. A ~15-line custom byte callback handles it.
3. **GP9 is not a valid RP2040 SCK pin**, so hardware SPI is off the table without rewiring.

### Build & flash

USB (first time, or recovery):

```sh
pio run -e pico_w -t upload
```

Over the air, once the device is on the network and running firmware with `ArduinoOTA`:

```sh
pio run -e pico_w_ota -t upload
```

The OTA password lives in `src/net.cpp` (`OTA_PASSWORD`) and `platformio.ini` (`--auth=...`); change both to suit.

### Screenshots

The firmware serves a 1-bit BMP of the current framebuffer at `http://vfd-dashboard.local/screenshot.bmp`. Save it directly in a browser, or grab a series from the CLI:

```sh
curl -o page.bmp http://vfd-dashboard.local/screenshot.bmp
sips -s format png page.bmp --out page.png        # macOS built-in
# or: magick page.bmp -filter point -resize 400% page@4x.png
```

## MQTT interface

The device subscribes to `vfd/input` for plain-text commands. A few highlights:

| Command                       | Effect                                                  |
| ----------------------------- | ------------------------------------------------------- |
| `next` / `prev`               | Cycle page                                              |
| `page N`                      | Jump directly to page N (0..10)                         |
| `bright N`                    | Brightness 0..255 (persisted to flash)                  |
| `lux N`                       | Raw lux from an ambient-light sensor (auto-brightness)  |
| `display on` / `off` / blank  | Power the screen (presence sensor integration)          |
| `12h` / `24h`                 | Clock format                                            |
| `fnext` / `cnext` / `vnext`   | Cycle font / clock face / music visualizer              |
| `cat <name>`                  | Trigger an action on the tamagotchi page                |
| `glitch on/off/toggle`        | Dash-glitch overlay                                     |

Publishes:

- **`vfd/state`** — retained JSON snapshot of current state. Republished on any state change and at most every 30 s. Fields include `display`, `brightness`, `page` + `page_name`, `font`, `viz`, `clock_face`, `use_12h`, `ip`, `uptime_s`, and `reason` (what triggered the publish).
- **`vfd/availability`** — `online` on connect, `offline` via MQTT LWT when the broker loses contact.

Other topics the device listens on: `straybot/playing` (now-playing JSON), `claude/usage` (Claude API budget JSON), `keyboard/knob` (rotary knob events — `up` / `down` / `press` / `held`).

### Knob actions

`up` / `down` cycle pages everywhere. `press` and `held` are page-specific:

| Page          | `press` (short)               | `held` (long)        |
| ------------- | ----------------------------- | -------------------- |
| 0 Overview    | Cycle right panel (viz ↔ Claude) | Toggle 12h / 24h     |
| 1 Time        | Next clock face               | —                    |
| 3 Now Playing | Next music visualizer         | —                    |
| 4 Matrix      | Enter brightness mode (up/down adjusts brightness for 15s) | — |

![Watch page — time on the left, Claude usage on the right](https://dmello.io/content/images/2026/05/IMG_4614.jpeg)

## Credits

- Custom u8g2 fork from the carrier vendor (EEI).
- Full project writeup, more photos, and the Mac menu-bar app that pushes Claude usage to MQTT: **<https://dmello.io/building-a-vfd-desk-hud-with-a-pi-pico-w/>**
