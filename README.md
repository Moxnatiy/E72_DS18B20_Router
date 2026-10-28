# E72_DS18B20_Router

Zigbee 3.0 router firmware for the **Ebyte E72-2G4M20S1E** (TI CC2652P, +20 dBm PA) USB stick
(egony rev 1.5 style board, CH340 USB-serial), with up to **16 DS18B20** temperature sensors on a
single 1-Wire bus, a debug log on the USB serial port, and a Zigbee2MQTT external converter.

It is built on TI Z-Stack (SimpleLink CC13xx/CC26xx SDK) `zr_genericapp` with the
[Koenkk router changes](https://github.com/Koenkk/Z-Stack-firmware) (bigger tables, parent
announce, remote TX power), adapted to the E72 module.

## Features

- Zigbee router, joins any channel 11–26, TX power up to **+20 dBm** (high-PA path).
- **DS18B20** on one 1-Wire bus, up to 16 sensors, auto-discovered at boot (ROM search),
  one Zigbee endpoint per sensor (Temperature Measurement `0x0402`, endpoints 10–25),
  ordered by ROM code so the mapping is stable.
- Sensors forced to **12-bit** resolution (0.0625 °C) at boot (RAM only, no EEPROM wear),
  CRC-checked reads with one retry.
- Polling every **10 s**; a report is sent to the coordinator on a change of **0.1 °C**
  or at least every **60 s**.
- **Debug log** on UART0 (the CH340 port, 115200 8N1): boot, network state, join info,
  temperatures, reports, plus an `alive` line every 10 s.
- **BOOT button** (DIO15): short press = network steering (join, or open permit-join when
  already joined), hold 5 s = leave network and factory reset.
- Automatic steering retry every 60 s while not on a network.
- The ROM serial bootloader stays enabled (backdoor on DIO15, active low), so the stick can
  always be re-flashed over USB.

## Hardware

| Signal | DIO | Notes |
|---|---|---|
| UART0 RX | DIO12 | CH340 TX; also used by the ROM bootloader |
| UART0 TX | DIO13 | CH340 RX; debug log |
| BOOT / Flash button | DIO15 | bootloader backdoor at reset; join/reset at runtime |
| LED1 (green) | DIO8 | network status / report sent |
| LED2 | DIO7 | sensor read activity |
| 1-Wire DATA | DIO22 | DS18B20 bus, **4.7 kΩ pull-up to 3.3 V** |
| RF switch 2.4 GHz | DIO6 | E72 antenna switch |
| RF switch high-PA | DIO5 | E72 antenna switch |

DS18B20 must be powered from 3.3 V (VDD pin); parasite power is not supported.
The internal pull-up on DIO22 is only a fallback so an empty bus reads high — use the
external 4.7 kΩ resistor.

### LEDs

| LED1 (green, DIO8) | Meaning |
|---|---|
| fast blink | searching / joining |
| slow blink (1 s) | not on a network, waiting for the next retry |
| off, short flash | joined; one flash per temperature report sent |

LED2 (DIO7) flashes on every sensor read cycle. If LED2 stays on, the debug UART failed to open.

## Flashing

Download `e72_router_ds18b20.hex` from the [latest release](../../releases/latest).

1. Enter the bootloader: unplug the stick, hold **BOOT**, plug it in, release after ~2 s.
2. Flash with [cc2538-bsl](https://github.com/JelmerT/cc2538-bsl):

   ```bash
   cc2538-bsl -p /dev/cu.usbserial-XXXX -b 115200 -e -w -v e72_router_ds18b20.hex
   ```

   Run it straight away on a freshly entered bootloader; if another tool has already sent the
   sync sequence, re-enter the bootloader first.
3. Replug the stick without holding BOOT.

Mass erase (`-e`) also clears the stored network, so the router has to join again
(enable permit-join, press BOOT briefly).

Before flashing a self-built image, check that it keeps the bootloader reachable:

```bash
python3 tools/check_ccfg.py e72_router_ds18b20.hex
# RESULT: SAFE to flash (bootloader stays reachable)
```

## Debug log

```bash
python3 tools/uart_log.py /dev/cu.usbserial-XXXX 600
```

The logger reopens the port automatically after the stick is replugged. Example:

```
[   5273] state -> ROUTER
[   5275] joined: PAN 0x1a54 ch 15 addr 0xbee3 parent 0x0000
[  10088] alive: ROUTER
[  11994] ep 10: 18.18 C
[  12000] ep 10: report sent
```

## Zigbee2MQTT

Copy [`z2m/e72_ds18b20_router.mjs`](z2m/e72_ds18b20_router.mjs) to
`<z2m data>/external_converters/` and restart Zigbee2MQTT (tested with Z2M 2.13).

Exposes:

- `temperature_t1` … `temperature_t16` (°C, 0.01 resolution) — endpoints 10–25
- `transmit_power` (dBm, stored in NV on the stick)

Reports are pushed by the firmware, so the converter does not configure reporting.
New sensors are discovered at boot only: replug the stick after adding one. Endpoint numbers
follow the ROM code order, so adding a sensor with a lower ROM code shifts the others.

### TX power

Default is **20 dBm** (`TXPOWER` in `preinclude.h`), written to NV on first boot.
Available levels: −20 … +5 dBm (normal PA) and +14 … +20 dBm (high PA).
Within the EU, 2.4 GHz is limited to 20 dBm EIRP — reduce the power if the antenna gain is
above ~0 dBi.

## Building

Requirements:

- [SimpleLink CC13xx/CC26xx SDK](https://www.ti.com/tool/SIMPLELINK-LOWPOWER-SDK) 8.33.00.16
- [Code Composer Studio](https://www.ti.com/tool/CCSTUDIO) 20.1 (TI Arm Clang 4.0.2, SysConfig 1.23)

The project in `firmware/` is the SDK example
`examples/rtos/CC1352P_2_LAUNCHXL/zstack/zr_genericapp/tirtos7/ticlang` with the modified files
copied in (the rest is still linked from the SDK through `COM_TI_SIMPLELINK_CC13XX_CC26XX_SDK_INSTALL_DIR`).

Headless build on macOS (make sure CCS can discover the SDK, e.g. install it under `~/ti`):

```bash
CLI="/Applications/ti/ccs2010/ccs/ccs-server.app/Contents/MacOS/ccs-server-cli.sh"
WS="$PWD/workspace"
"$CLI" -workspace "$WS" -application projectImport -ccs.location "$PWD/firmware/zr_genericapp_CC1352P_2_LAUNCHXL_tirtos7_ticlang" -ccs.copyIntoWorkspace
"$CLI" -workspace "$WS" -application projectBuild -ccs.projects zr_genericapp_CC1352P_2_LAUNCHXL_tirtos7_ticlang -ccs.configuration default
```

Output: `workspace/zr_genericapp_CC1352P_2_LAUNCHXL_tirtos7_ticlang/default/zr_genericapp_CC1352P_2_LAUNCHXL_tirtos7_ticlang.hex`.

### What is changed against the SDK example

| File | Change |
|---|---|
| `zr_genericapp.syscfg` | E72 RF switch (DIO6/DIO5, no LaunchPad SKY13317), 20 dBm table, DC/DC off, XOSC cap delta `0xFA`, bootloader backdoor DIO15, NV 3 pages, LEDs DIO8/DIO7, 1-Wire DIO22, no external SPI flash |
| `Application/e72_rf.c` | antenna switch callback for the E72 (2.4 GHz only) |
| `Application/ds18b20.*` | bit-banged 1-Wire master, ROM search, 12-bit config, CRC8 |
| `Application/e72_temp.*` | sensor endpoints, polling, app-driven reports |
| `Application/e72_led.*` | LED behaviour |
| `Application/dbg_log.*` | debug log over UART2 |
| `Application/zcl_genericapp*.c` | Koenkk router changes, BOOT button, steering retry, heartbeat, model ID `E72.DS18B20.Router` |
| `Common/zcl/zcl.c`, `Stack/sys/zglobals.c`, `Stack/zdo/zd_app.c`, `cc13x2_cc26x2_tirtos7_ticlang.cmd`, `Stack/Config/*` | Koenkk router changes (TX power attribute, parent announce, NV layout, MAC/NWK tables) |

`tools/localize.py` turns SDK-linked files of a CCS project into local copies, which is how the
files above were taken out of the SDK.

## License

Own code (`e72_*`, `ds18b20.*`, `dbg_log.*`, `tools/`, `z2m/`) is MIT licensed, see [LICENSE](LICENSE).
Files derived from the TI SDK keep their original Texas Instruments BSD-3-Clause headers.
Router changes are based on [Koenkk/Z-Stack-firmware](https://github.com/Koenkk/Z-Stack-firmware).
