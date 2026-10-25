# Hardware, bill of materials and assembly

This document separates items visible in the supplied connection diagram from items verified in the firmware. Confirm component ratings and wiring against the modules actually purchased.

## Bill of materials

| Item | Quantity | Evidence / notes |
|---|---:|---|
| ESP8266 ESP-12F module or development carrier | 1 | Firmware target. Exact carrier/adapter PCB is unspecified. |
| [W25Q32 32-Mbit SPI NOR flash module](https://quartzcomponents.com/products/w25q32-32mbit-capacity-flash-storage-module?variant=44978575376618) | 1 | Exact module used. Supplier lists 32 Mbit (4 MiB), SPI, 2.7–3.6 V operation and 100,000 program/erase cycles. Power from 3.3 V, not 5 V; verify module header labels. |
| KY-022 / demodulating IR receiver module | 1 | Receiver shown in the diagram; firmware default RX is GPIO5. Confirm the board's OUT/VCC/GND labels. A purchase link for this module was not supplied. |
| [L518EIR1BC1-S61 940-nm 5-mm IR transmitter LED](https://quartzcomponents.com/products/l518eir1bc1-s61-940nm-5mm-infrared-ir-transmitter-led?variant=46154073506026) | 12 shown | Exact LED used. Drawing proposes six branches with two LEDs in series per branch. Confirm the delivered part's full datasheet, especially continuous current, peak pulse current, pulse width and duty-cycle limits. |
| [AO3400 N-channel MOSFET, SOT-23](https://quartzcomponents.com/products/ao3400-mosfet-30v-5-8a-a09t-sot-23-c-n-channel-smd-package?variant=44716853821674) | 1 | Exact MOSFET used. Supplier lists 30 V VDS, 5.8 A maximum drain current and typical RDS(on) values at VGS = 4.5 V and 10 V. Those figures do not establish the on-resistance at the ESP8266's 3.3 V gate drive; verify the datasheet and actual pulse-load heating. |
| IR LED branch resistors | 6 shown | Drawing labels 30 ohm, 1/4 W per branch; value is not electrically validated here. |
| MOSFET gate resistor / pulldown | 1 each | Drawing labels 100 ohm series gate resistor and 10 kOhm gate-to-ground pulldown. |
| Regulated 5 V supply | 1 | Drawing recommends at least 2 A. Size for actual peak current and thermal load. |
| 3.3 V regulator, AMS1117-3.3 or equivalent | 1 | Check regulator dropout, transient response and heat at actual load. |
| Decoupling capacitors | as required | Drawing shows 10 uF on 5 V and 10 uF + 100 nF on 3.3 V. |
| [Waterproof DS18B20 temperature probe](https://quartzcomponents.com/products/ds18b20-water-proof-temperature-sensor?variant=31898050953351) | 1 | Exact sensor used. One-Wire digital temperature probe; firmware default is GPIO16. Drawing shows a 4.7 kΩ DATA pull-up to 3.3 V. Confirm the actual cable colours/pinout before connecting. |
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


## Exact component links supplied by the project author

- AO3400 MOSFET: https://quartzcomponents.com/products/ao3400-mosfet-30v-5-8a-a09t-sot-23-c-n-channel-smd-package?variant=44716853821674
- L518EIR1BC1-S61 IR LED: https://quartzcomponents.com/products/l518eir1bc1-s61-940nm-5mm-infrared-ir-transmitter-led?variant=46154073506026
- Waterproof DS18B20 probe: https://quartzcomponents.com/products/ds18b20-water-proof-temperature-sensor?variant=31898050953351
- W25Q32 32-Mbit flash module: https://quartzcomponents.com/products/w25q32-32mbit-capacity-flash-storage-module?variant=44978575376618

## IR branch current — verify before release

The drawing shows a 5 V supply, two IR LEDs in series and a 30 Ω resistor per branch. Estimate the branch current as:

`I_branch ≈ (V_supply − Vf_LED1 − Vf_LED2 − V_DS(on)) / R_branch`

Use the exact delivered LED datasheet and the AO3400 drain-source on-resistance at the measured 3.3 V gate drive to make the final calculation. As an illustration only, if the two LED forward drops total 2.8 V and the MOSFET drop is small, the drawn 30 Ω branch gives about 73 mA while a pulse is on: (5.0 − 2.8) / 30 = 0.073 A. That is above a typical 20 mA continuous rating for a 5 mm IR emitter, so it must be justified by the exact part's peak pulse current, pulse width, repetition and duty-cycle limits. Do not assume it is safe because the resistor is marked 1/4 W. Check instantaneous resistor power `I²R`, average resistor heating, LED pulse current and transmit duty cycle. If the datasheet does not support the resulting load, increase branch resistance or redesign the array before publishing a final schematic.
