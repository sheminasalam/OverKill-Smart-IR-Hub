# Hardware, bill of materials and assembly

This document separates items visible in the supplied connection diagram from items verified in the firmware. Confirm component ratings and wiring against the modules actually purchased.

## Bill of materials

| Item | Quantity | Evidence / notes |
|---|---:|---|
| ESP8266 ESP-12F module or development carrier | 1 | Firmware target. Exact carrier/adapter PCB is unspecified. |
| W25Q32 SPI NOR flash module (4 MiB) or supported W25Qxx part | 1 | Drawing labels W25Q32. Firmware includes capacity IDs for W25Q32 and W25Q64; choose and test the exact chip. |
| KY-022 / demodulating IR receiver module | 1 | Drawing labels KY-022; firmware default RX is GPIO5. Confirm board pin order. |
| 940 nm IR LEDs, L-518EIR1BC1-S61 or equivalent | 12 shown | Drawing proposes six parallel branches with two LEDs in series per branch. Verify the actual LED datasheet. |
| AO3400 N-channel MOSFET or equivalent | 1 | Low-side array switch in drawing; confirm gate drive, pulse current and thermal margin. |
| IR LED branch resistors | 6 shown | Drawing labels 30 ohm, 1/4 W per branch; value is not electrically validated here. |
| MOSFET gate resistor / pulldown | 1 each | Drawing labels 100 ohm series gate resistor and 10 kOhm gate-to-ground pulldown. |
| Regulated 5 V supply | 1 | Drawing recommends at least 2 A. Size for actual peak current and thermal load. |
| 3.3 V regulator, AMS1117-3.3 or equivalent | 1 | Check regulator dropout, transient response and heat at actual load. |
| Decoupling capacitors | as required | Drawing shows 10 uF on 5 V and 10 uF + 100 nF on 3.3 V. |
| DS18B20 sensor | 1 optional | Firmware default GPIO16; drawing shows 4.7 kOhm data pull-up to 3.3 V. |
| Single status LED + resistor | 1 | Firmware default is a single active-LOW status output on GPIO2. The drawn RGB LED does not match the present firmware map. |
| Reset button | optional / unresolved | Drawn in supplied diagram, but firmware has no dedicated reset-button GPIO. |
| Wiring, headers, PCB/perfboard and enclosure fasteners | as required | PCB layout and fastener dimensions were not supplied. |

## Firmware-derived pin map

| Signal | Default GPIO | Status |
|---|---:|---|
| W25Qxx MISO | 12 | Source constant |
| W25Qxx MOSI | 13 | Source constant |
| W25Qxx SCLK | 14 | Source constant |
| W25Qxx CS | **3** | Critical conflict with UART0 RX; the supplied drawing labels GPIO15 instead. Resolve before wiring/release. |
| IR TX | 4 | Default; persistent configuration can change it |
| IR RX | 5 | Default; persistent configuration can change it |
| Status LED | 2 | Default; active LOW |
| DS18B20 data | 16 | Default |
| Physical learn button | 0 | Declared, but firmware says the physical button was removed/disabled due to boot-strap behavior |
| UART0 TX/RX | 1 / 3 | Serial debugging/input; GPIO3 conflicts with current flash-CS constant |

The PIN_FLASH_CS = 3 constant controls actual chip select via storageCS(). The original diagram assigns GPIO15, but the firmware constant is GPIO3. Correct and physically verify the code and wiring together. Do not guess which reference is intended.

## IR array safety

The drawing shows six branches, each with two IR LEDs in series and a 30 ohm / 1/4 W resistor, powered from 5 V and switched by one MOSFET. This is a proposal, not a validated design. Calculate branch current from the exact LED forward voltage, supply maximum and resistor tolerance. Verify peak/average resistor dissipation, LED pulsed current and MOSFET limits. Test a single branch before installing all six. Do not drive IR LEDs directly from ESP8266 GPIO. Use a common ground between logic and the external LED supply.

## Assembly checklist

1. Resolve the flash-CS contradiction in firmware before constructing the final board.
2. Check the W25Qxx module pinout; board layouts vary.
3. Assemble and test 3.3 V logic supply and flash first. Verify JEDEC ID, sector erase, write/read-back, and data persistence after power cycling.
4. Add the IR receiver and verify its 3.3 V supply, output and GPIO.
5. Validate one IR LED branch at the bench before populating the array; confirm the MOSFET remains off during boot.
6. Add DS18B20 with pull-up if used; verify readings and calibration.
7. Flash firmware over serial, inspect the hardware self-test and serial logs, then configure Wi-Fi and MQTT.
8. Learn a low-risk test command and verify transmission/storage before using the complete LED array.
9. Back up configuration/learned-data database before firmware changes.
10. Open the 3MF in a slicer and test-fit the parts before final assembly. Actual screw/inserts, material, supports and tested printer settings were not provided.

## 3D printing

Use the provided Overkill.3mf from the release bundle. It contains multiple mesh objects/material colors, but the model does not provide verified slicer settings, fastener sizes or assembly instructions. Test-print fit-critical areas before a full print.

## Supplied connection diagram

The original drawing is retained as reference only. It is not a verified final schematic because flash CS, RGB status LED and reset-button details conflict with the firmware source.


## Firmware and Lambda update note

The current `alexa_lambda/index.mjs` now requires `IOT_ENDPOINT` as an environment variable. Set it to your own AWS IoT data endpoint when deploying the Lambda; do not rely on a hard-coded default endpoint.
