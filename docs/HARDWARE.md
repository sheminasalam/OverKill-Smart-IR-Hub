# Hardware, BOM, assembly and enclosure

This page documents the actual build: a D1 mini development board, W25Q32 flash, USB-C 4-pin power breakout, IR receiver and transmitter, waterproof DS18B20 probe, one status LED, an underside reset button and a PETG enclosure. The original ESP-12F circuit drawing must be adapted for the D1 mini board labels.

**Current connection diagram:** [Open the committed circuit diagram (PNG)](../hardware/circuit-diagram.png). It shows flash CS on GPIO3, matching the compile-time `PIN_FLASH_CS = 3` constant. The intended pin mapping is therefore clear; the Stage Info serial diagnostic is stale and prints `GPIO15 -> W25Qxx CS`. GPIO3 is labelled RX on a D1 mini and is shared with UART0 RX, so the physical build still needs tests for flash read/write/erase, boot and serial programming before it can be called hardware-validated. The diagram uses ESP-12F GPIO names; apply the D1 mini label mapping in [PIN-MAP.md](../hardware/PIN-MAP.md).

## Bill of materials (BOM)

This table reflects the components used in the author's build. The main controller in the built device is a **D1 mini ESP8266 development board**. The original drawing uses bare ESP-12F module pin labels, so a bare-module build needs the additional regulator noted below.

| Qty. | Component | Use / selection notes |
|---:|---|---|
| 1 | **D1 mini ESP8266 development board** | **Used in this build.** It includes USB/programming circuitry and an onboard 3.3 V regulator. Follow the D1 mini board-label mapping in the pin section below; do not directly apply the ESP-12F module pin labels to D-pin names. |
| 1 | [W25Q32 32-Mbit SPI flash module](https://quartzcomponents.com/products/w25q32-32mbit-capacity-flash-storage-module?variant=44978575376618) | Exact module used. 32 Mbit = 4 MiB. Power it from 3.3 V, not 5 V. Confirm the breakout's printed pin labels. |
| 1 | KY-022 IR receiver module / compatible demodulating IR receiver | Receives signals from the original remote and is also the sensor used during the Web UI's IR-learning process. There is **no separate physical learning button**. Identify OUT, VCC and GND from the actual module labels. |
| 12 | [L518EIR1BC1-S61 940-nm, 5-mm IR transmitter LED](https://quartzcomponents.com/products/l518eir1bc1-s61-940nm-5mm-infrared-ir-transmitter-led?variant=46154073506026) | Exact LED used. The proposed array has six branches with two series LEDs per branch. Validate the exact LED's current, pulse width and duty-cycle limits before using the depicted resistor values. |
| 1 | [AO3400 N-channel MOSFET, SOT-23](https://quartzcomponents.com/products/ao3400-mosfet-30v-5-8a-a09t-sot-23-c-n-channel-smd-package?variant=44716853821674) | Exact part used as the low-side IR array switch. The supplier's headline drain-current rating does not guarantee that the SOT-23 package can dissipate the required heat; the gate is driven by 3.3 V logic. |
| 6 | 30 Ω, 1/4 W resistors (as shown in the original circuit drawing) | One per two-LED branch in the drawing. This is the depicted value, not a confirmed safe value; calculate current and pulse/average dissipation for the delivered LEDs. |
| 1 | 100 Ω resistor | MOSFET gate series resistor shown in the drawing. |
| 1 | 10 kΩ resistor | MOSFET gate-to-ground pulldown shown in the drawing. |
| 1 | [USB-C 4-pin female breakout PCB](https://quartzcomponents.com/collections/all/products/usb-3-1-female-socket-type-c-connector-4-pin-breakout-pcb-board) | Exact power-input connector used; the enclosure has a dedicated side provision for this PCB. The four pins are VBUS (+5 V), GND, D+ and D−. Use VBUS/GND for power; D+/D− are not needed for power-only operation. This is not the board used for programming the D1 mini. |
| 1 | Regulated 5 V USB power supply and suitable cable | Enters through the USB-C breakout. The exact supply-current requirement depends on the tested LED array. **Quartz's listing/review for this 4-pin breakout reports USB-A-to-USB-C cable compatibility; use a USB-A source with an A-to-C cable unless you add/verify the required USB-C CC pull-down arrangement.** A bare 4-pin board does not expose CC1/CC2. |
| 1 (only for a bare ESP-12F build) | [AMS1117-3.3, 800 mA LDO module](https://quartzcomponents.com/collections/all/products/ams1117-3-3-ldo-800ma-dc-5v-to-3-3v-step-down-power-supply-module) | **Not needed for this D1 mini build** because the D1 mini already regulates 5 V to 3.3 V. If building with a bare ESP-12F without a suitable onboard regulator, feed 5 V to IN, ground to GND, and use OUT as the regulated 3.3 V rail. The 800 mA listing is a headline rating; usable current depends on heat and module layout. Never feed 5 V to ESP-12F VCC/3V3. |
| 1 | 10 µF capacitor | 5 V input decoupling shown in the circuit. |
| 1 | 10 µF capacitor | 3.3 V rail bulk decoupling shown in the circuit. |
| 1 | 100 nF capacitor | 3.3 V high-frequency decoupling; place close to the ESP/peripheral supply pins where practical. |
| 1 | [Waterproof DS18B20 temperature probe](https://quartzcomponents.com/products/ds18b20-water-proof-temperature-sensor?variant=31898050953351) | Exact sensor used. The top plate has an opening for the probe. Firmware default data pin is GPIO16; confirm the cable colours/pinout before connecting. |
| 1 | 4.7 kΩ resistor | Pulls the DS18B20 data line up to 3.3 V. |
| 1 | Single status LED and current-limiting resistor | One status LED only, connected to GPIO2 (D1 mini D4), active LOW. This matches the current firmware default. No RGB LED is fitted. |
| 1 | Momentary reset button | Installed underneath the device and connected to the D1 mini's dedicated RST input (not a general GPIO). It is a **reset button**, not an IR-learning button. There is no physical learning button. |
| 1 | 3D-printed enclosure | [Original `Overkill.3mf`](../3d_printing/Overkill.3mf) is committed in this repository. Print in **PETG**. The top-plate openings are for the DS18B20 probe, IR receiver used for learning, and status LED; the side provision accommodates the USB-C power breakout PCB; the reset button is underneath. |
| As needed | Headers, wire, insulation, mounting hardware | Test-fit the USB-C breakout, D1 mini, sensor probe, receiver and LED before final soldering. Actual screw/inserts and fit tolerances still require a test print. |

### Power and LED safety

1. The ESP8266 and its signal pins are **3.3 V logic**. Never feed 5 V into an ESP GPIO or the 3V3 pin.
2. Use the 5 V supply for the IR LED branches as shown, with the MOSFET switching the return path. Keep the LED supply and ESP logic at a common ground.
3. Do not drive the LED array directly from an ESP8266 GPIO.
4. Calculate each branch current from the actual supply's maximum voltage, both LEDs' forward-voltage range, the resistor tolerance and temperature. Check resistor power, LED pulse limits and MOSFET limits at the intended transmit duty cycle.
5. Test one LED branch first. Check current and temperature before populating or enabling all six branches.
6. Power the IR LED array directly from the regulated 5 V rail through its MOSFET/resistor branches. In the author's D1 mini build, the D1 mini's onboard regulator supplies 3.3 V logic. If using a bare ESP-12F without a regulator, use the AMS1117 module (or another adequately rated regulator) from 5 V to 3.3 V; never use this 3.3 V rail to power the IR array.
7. Disconnect power before continuity checks and before changing any SPI/GPIO wiring.

### Exact parts used in this build

These are the specific parts selected by the project author, rather than generic alternatives:

- [AO3400 30 V N-channel MOSFET, SOT-23](https://quartzcomponents.com/products/ao3400-mosfet-30v-5-8a-a09t-sot-23-c-n-channel-smd-package?variant=44716853821674)
- [L518EIR1BC1-S61 940 nm, 5 mm IR transmitter LED](https://quartzcomponents.com/products/l518eir1bc1-s61-940nm-5mm-infrared-ir-transmitter-led?variant=46154073506026) — 12 pieces for the six-branch array shown.
- [Waterproof DS18B20 temperature probe](https://quartzcomponents.com/products/ds18b20-water-proof-temperature-sensor?variant=31898050953351)
- [W25Q32 32-Mbit (4-MiB) SPI flash module](https://quartzcomponents.com/products/w25q32-32mbit-capacity-flash-storage-module?variant=44978575376618)

The supplier product pages are the procurement references. For electrical design, use the manufacturer datasheet for the exact item shipped (particularly the IR LED's continuous and pulsed current ratings and the MOSFET's on-resistance at a 3.3 V gate drive); a shop listing headline is not a substitute for those guaranteed limits.

## Confirm the pin map before assembly

### D1 mini board aliases

The firmware uses raw ESP8266 GPIO numbers. On a typical D1 mini these correspond to board labels as follows: GPIO2 = D4, GPIO3 = RX, GPIO4 = D2, GPIO5 = D1, GPIO12 = D6, GPIO13 = D7, GPIO14 = D5 and GPIO16 = D0. Check the silkscreen on your particular board before wiring.

The following are the current defaults in `firmware/OverKill-v1.ino`. They are not a substitute for verifying the flash-CS connection on the actual board.

| Function | Current source setting | Notes |
|---|---|---|
| W25Qxx MISO | GPIO12 | SPI bus |
| W25Qxx MOSI | GPIO13 | SPI bus |
| W25Qxx SCLK | GPIO14 | SPI bus |
| W25Qxx CS | **GPIO3** | Matches the current connection image and compile-time constant. The Stage Info diagnostic now prints GPIO3 for flash CS. GPIO3 is also UART0 RX; verify the board's serial/programming behaviour. |
| IR TX | GPIO4 default | Can be changed in the Web UI's Hub configuration; confirm the resulting pin allocation. |
| IR RX | GPIO5 default | Can be changed in the Web UI's Hub configuration; confirm the resulting pin allocation. |
| Status LED | GPIO2 default, active LOW | Firmware uses a single status indicator. |
| DS18B20 data | GPIO16 default | Optional; can be changed in Hub configuration. |
| Physical learning button / GPIO | **None** | No physical learning button or learn-button GPIO is used. Start learning from the Web UI and use the IR receiver. GPIO0 remains an ESP8266 boot-strap pin, not a user control. |
| UART0 TX/RX | GPIO1 / GPIO3 | Used for serial upload/debugging. GPIO3's use as flash CS in the current source requires special attention. |

**Do this now, before fabricating or powering the final assembly:**

1. With all power removed, identify the actual W25Qxx module's CS pad.
2. Use a continuity meter to determine which ESP-12F pad it reaches. Do not infer the connection from wire colour or from a rendered image.
3. Compare that physical connection against `PIN_FLASH_CS` in the firmware.
4. Confirm the W25Q32 CS wire on the actual D1 mini goes to GPIO3/RX as the current image and compile-time constant specify. If the physical board is wired differently, stop and reconcile the wiring, source and diagram together before powering the flash.
5. Test boot, serial programming, flash JEDEC-ID detection, read/write/erase and power-cycle persistence.
6. Use the uploaded [connection diagram](../hardware/circuit-diagram.png) as the current reference, but do not treat an image alone as proof of the physical wiring.

The actual firmware constant is the code's behaviour; the fact that a drawing labels a different pin does not make the physical board correct.

## Assemble and inspect the electronics

The code constant, Stage Info diagnostic and connection image now all specify GPIO3 for flash CS. GPIO3 is also UART0 RX on the D1 mini, so the physical build still needs testing for flash read/write/erase, power-cycle persistence, boot stability and serial programming.

1. **Prepare the 5 V input.** Solder/connect the 4-pin USB-C breakout in its enclosure provision. Wire VBUS to the 5 V input and GND to common ground; leave D+/D− unconnected for power-only operation. The breakout has no CC1/CC2 pins, so use a USB-A source with a USB-A-to-USB-C cable unless you add and validate the proper USB-C CC pull-down arrangement. For the D1 mini build, feed 5 V to its 5V input pin and let its onboard regulator produce 3.3 V. For a bare ESP-12F build, connect 5 V to the AMS1117 module input and use its 3.3 V output for the ESP/peripherals.
2. **Verify grounds.** Confirm continuity among ESP GND, flash GND, IR receiver GND, MOSFET source/ground and the 5 V LED supply ground.
3. **Install external flash.** Connect VCC to 3.3 V, GND to GND, MISO to GPIO12, MOSI to GPIO13 and SCLK to GPIO14. Connect CS to GPIO3 as shown in the current diagram and compile-time constant, but verify the actual D1 mini wiring and remember GPIO3 is also UART0 RX. The Stage Info diagnostic has been corrected to print GPIO3. Do not power a 3.3 V flash module from 5 V.
4. **Inspect for shorts.** Measure between 3.3 V and GND and between 5 V and GND before attaching the ESP. Investigate any near-short before power-on.
5. **Add the IR receiver.** Connect its supply and ground according to the board labels; use GPIO5 as the default OUT signal unless the firmware/Web UI configuration is changed.
6. **Build one IR LED branch.** Wire the resistor, two series LEDs and MOSFET load path using the polarity in the verified circuit. Add the 100 Ω gate resistor and 10 kΩ gate pulldown as drawn. Check branch current with a suitable meter/test setup.
7. **Add the remaining five branches** only after current and thermal checks. Make wiring and supply distribution thick enough for the current.
8. **Install the DS18B20 probe.** Fit the waterproof probe into its top-plate opening, connect data to GPIO16 by default and add the 4.7 kΩ pull-up from data to 3.3 V. Check the probe cable pinout/colours rather than relying on assumed wire colours.
9. **Install the single status LED** in its top-plate opening. Firmware defaults to GPIO2 (D1 mini D4), active LOW, through a suitable current-limiting resistor. Do not fit an RGB LED; the current BOM calls for one status LED only.
10. Install the IR receiver in its top-plate opening and orient it so it can receive the original remote. Learning is started from the Web UI; the receiver captures the IR signal. There is no physical learning button. Install the reset button underneath and connect it to the D1 mini RST input. Inspect solder joints, polarity, insulation, strain relief and clearance in the enclosure. Do not connect the full-power IR array until the low-current logic/flash tests pass.

## Print and fit the enclosure

![Enclosure views with labeled openings and controls](../hardware/Overkill_labeled.png)

The image labels the enclosure openings, USB-C power provision and underside reset pinhole.

The project uses the committed multi-object [`Overkill.3mf`](../3d_printing/Overkill.3mf). **Print the enclosure in PETG.** The model includes provisions for the USB-C breakout board and the top-plate openings for the DS18B20 probe, IR receiver and status LED. The reset button is accessed underneath the device. Use the filament manufacturer's recommended PETG profile as the starting point; exact temperatures, fan settings, speed, infill and orientation have not been validated for this model.

1. Open [`3d_printing/Overkill.3mf`](../3d_printing/Overkill.3mf) in your slicer.
2. Inspect the object list and colour/material assignments. Confirm all objects are positioned and enabled as intended.
3. Review the preview for thin walls, unsupported features, holes and bridges. Orient parts to suit your printer and the visible surface finish.
4. Select PETG filament for the enclosure. Start with the filament manufacturer's PETG profile for your printer/nozzle, verify bed adhesion and layer bonding, and adjust only after inspecting the preview and a small test print. Exact temperatures, fan settings, speed and infill have not been validated for this model.
5. Print a small fit-check or the parts with critical posts/holes first.
6. Test-fit the ESP carrier, IR array, connectors and cables before printing the complete set. Check that the IR LEDs and receiver have an unobstructed optical path.
7. Confirm reset-button access from the underside, top-plate sensor/receiver/LED clearances, screw lengths and board clearance physically. Do not allow screws or conductive hardware to touch exposed circuit pads.
8. Record the validated material, slicer version/settings, hardware and any print orientation changes in the repository before calling the enclosure instructions final.


## IR branch current — verify before release

The drawing shows a 5 V supply, two IR LEDs in series and a 30 Ω resistor per branch. Estimate the branch current as:

`I_branch ≈ (V_supply − Vf_LED1 − Vf_LED2 − V_DS(on)) / R_branch`

Use the exact delivered LED datasheet and the AO3400 drain-source on-resistance at the measured 3.3 V gate drive to make the final calculation. As an illustration only, if the two LED forward drops total 2.8 V and the MOSFET drop is small, the drawn 30 Ω branch gives about 73 mA while a pulse is on: (5.0 − 2.8) / 30 = 0.073 A. That is a substantial pulse current and must be checked against the exact L518EIR1BC1-S61 datasheet; the supplier's short listing does not establish its safe continuous or repetitive pulse limit. Confirm peak pulse current, pulse width, repetition rate and duty-cycle limits before using the array. Do not assume it is safe because the resistor is marked 1/4 W. Check instantaneous resistor power `I²R`, average resistor heating, LED pulse current and transmit duty cycle. If the datasheet does not support the resulting load, increase branch resistance or redesign the array before publishing a final schematic.
