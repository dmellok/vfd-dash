# VFD Template — Pico W + GP1287BI

Minimal known-working starter for the EEI **EPC-INBN0BV1287UD** carrier (GP1287BI 256×50 VFD) driven from a **Raspberry Pi Pico W**. Flashing this template should print "Hello, world!" on the display with no other setup required.

## Wiring

| Carrier (CN7) | Pico W | Notes                                      |
| ------------- | ------ | ------------------------------------------ |
| FILMENT_EN    | GP2    | Active-HIGH (10kΩ internal pull-down)      |
| CLOCK         | GP9    | Not a hardware SCK pin — software SPI only |
| CHIPSELECT    | GP8    | Active-LOW                                 |
| DATA          | GP7    |                                            |
| RESET         | GP6    | Active-LOW                                 |
| GND           | GND    | **Must** be bonded for signals to work     |

**Power the carrier separately**: USB-C or DC005 at 4.5–20 V, ≥ 2 A. The VFD can draw ~1.5 A at full brightness — far more than the Pico can source. Optionally feed the Pico's `VBUS` from the carrier's `+5V OUT` (CN6 pin 8) so one wall-wart powers both.

Warning: the carrier's on-board boost converter generates VHG = 60 V and VHP = 90 V to drive the VFD anode. Don't poke exposed pins while powered.

## Why this isn't a one-line u8g2 constructor

Three hardware/library gotchas combined:

1. **GP1287BI is not in upstream u8g2.** The custom fork in `lib/U8g2` adds a `GP1287AI` driver; the AI and BI share a command set, so the AI driver works for the BI.
2. **LSB-first 8-bit SPI.** The datasheet specifies "DATA: SPI data input, LSB First." u8g2's stock *software* SPI byte drivers are hardcoded MSB-first — only the *hardware* SPI path respects LSB-first via a repurposed `i2c_bus_clock_100kHz == 254` flag. So we supply our own tiny LSB-first byte callback.
3. **GP9 is not a valid RP2040 SCK pin.** Hardware SPI is off the table without rewiring, which is why software SPI is used throughout.

The custom byte callback (~15 lines) plus a direct call to `u8g2_Setup_futaba_vfd_gp1287ai_256x50_f` handles all three.

## Getting started

1. Open this folder in VS Code / PlatformIO as its own project.
2. First build will take a minute while PlatformIO fetches the Pico toolchain.
3. Upload — put the Pico in BOOTSEL mode the first time (hold the button while plugging in USB).
4. Serial monitor at 115200 baud if you want to see boot messages.

## Dimming

`setContrast(value)` where value is 0–255. The u8g2 driver internally scales this to the chip's native 10-bit (0–1023) brightness. Default in this template is `128`; raise toward 255 for brighter, drop toward 30 for night use.

## Common symptoms

| Symptom                                   | Likely cause                                                                            |
| ----------------------------------------- | --------------------------------------------------------------------------------------- |
| Totally blank, **no filament glow at all**| Carrier not powered, or GND not bonded to Pico                                          |
| Filament glows but screen stays dark      | Signal issue — check all 5 wires, verify bit order hasn't been changed to MSB           |
| Garbled / scrambled pixels                | Usually bit order wrong; this template is correct (LSB-first)                           |
| Display super faint                       | Normal — VFD filament is faint. Look in a darkened room to see it                       |

## Library

`lib/U8g2/` is the **custom fork from the carrier vendor**. Do not replace it with the PlatformIO registry version of u8g2 — the upstream release does not contain the GP1287 driver.
