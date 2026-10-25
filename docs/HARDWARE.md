# Hardware, BOM, assembly and enclosure

This page is the step-by-step physical build guide. Start with the caution below and resolve the pin conflict against the actual assembled board before creating or powering a final build.

## Bill of materials (BOM)

This BOM consolidates parts named in the supplied diagram and firmware. Items marked “verify” are not confirmed by a tested PCB BOM, because a PCB manufacturing file and physical assembly record were not supplied.

| Qty. | Component | Use / selection notes |
|---:|---|---|
| 1 | ESP8266 ESP-12F module | Main controller. Use a carrier/adapter that exposes the required GPIOs and permits programming. Confirm the exact module's flash capacity and pinout. |
| 1 | [W25Q32 32-Mbit SPI flash module](https://quartzcomponents.com/products/w25q32-32mbit-capacity-flash-storage-module?variant=44978575376618) | Exact module used. 32 Mbit = 4 MiB. Supplier lists SPI, 2.7–3.6 V operation and 100,000 program/erase cycles. Power it from 3.3 V, not 5 V, and confirm the breakout pin labels. |
| 1 | KY-022 IR receiver module or compatible demodulating 940 nm receiver | Receives IR from original remotes. Use the labels on the actual module to identify OUT, VCC and GND; do not assume a universal physical pin order. |
| 12 | [L518EIR1BC1-S61 940-nm, 5-mm IR transmitter LED](https://quartzcomponents.com/products/l518eir1bc1-s61-940nm-5mm-infrared-ir-transmitter-led?variant=46154073506026) | Exact LED used. The seller identifies it as a 940-nm 5-mm IR emitter. Use 12 total in six branches of two series LEDs as drawn, but do not assume the 30 Ω resistor is validated until the exact part datasheet/current limits are checked. |
| 1 | [AO3400 N-channel MOSFET, SOT-23](https://quartzcomponents.com/products/ao3400-mosfet-30v-5-8a-a09t-sot-23-c-n-channel-smd-package?variant=44716853821674) | Exact part used. Supplier listing states 30 V drain-source voltage and up to 5.8 A drain current, but that headline current depends on package/PCB thermal conditions. The ESP8266 drives the gate at 3.3 V, so verify that the actual device is adequately enhanced at that gate voltage and check the SOT-23 thermal rise. |
| 6 | 30 Ω, 1/4 W resistors (as drawn) | One in series with each two-LED branch. **Treat this as the value in the supplied drawing, not an electrically validated value.** Calculate branch current and resistor dissipation for the actual LEDs and supply before assembly. |
| 1 | 100 Ω resistor | Gate series resistor shown between ESP GPIO4 and MOSFET gate. |
| 1 | 10 kΩ resistor | Gate-to-GND pulldown shown in the driver drawing; helps keep the MOSFET off during reset/boot. |
| 1 | 5 V regulated DC supply | Drawing recommends ≥2 A. Confirm capacity against the array's measured peak load and the ESP regulator input. |
| 1 | AMS1117-3.3 regulator or correctly rated 3.3 V regulator module | 5 V to 3.3 V supply for ESP8266/peripherals. Check regulator heat, dropout, current and transient response in the real circuit. |
| 1 | 10 µF capacitor | 5 V input decoupling shown in the diagram. |
| 1 | 10 µF capacitor | 3.3 V rail bulk decoupling shown in the diagram. |
| 1 | 100 nF capacitor | 3.3 V high-frequency decoupling shown in the diagram. Place close to the ESP/peripheral supply pins where practical. |
| 1 | [Waterproof DS18B20 temperature probe](https://quartzcomponents.com/products/ds18b20-water-proof-temperature-sensor?variant=31898050953351) | Exact sensor used. Three-wire 1-Wire digital probe; firmware default data pin is GPIO16. Add the 4.7 kΩ pull-up from DATA to 3.3 V shown in the circuit. Confirm the wire colours against the supplied probe/measurement before connecting. |
| 1 | 4.7 kΩ resistor | DS18B20 data pull-up to 3.3 V, if using the sensor as drawn. |
| 1 | Status LED and current-limiting resistor | Firmware default is one active-LOW status output on GPIO2. The circuit drawing depicts a common-anode RGB LED and three GPIOs, which do not match the present firmware pin map. Choose one implementation after verifying code and hardware. |
| 1 | Reset/programming arrangement | Use the reset and boot/program pins required by the ESP-12F carrier. The additional reset button shown in the diagram is not represented by a separate GPIO in the current firmware source. |
| As needed | PCB/perfboard, headers, connectors, wire, insulation, mounting hardware | Exact PCB, connector and fastener part numbers were not included in the supplied files. |
| 1 | 3D-printed enclosure | Use `Overkill.3mf` from the prepared release bundle. It is not yet committed to this GitHub branch; fit and screw dimensions require a test print. |

### Power and LED safety

1. The ESP8266 and its signal pins are **3.3 V logic**. Never feed 5 V into an ESP GPIO or the 3V3 pin.
2. Use the 5 V supply for the IR LED branches as shown, with the MOSFET switching the return path. Keep the LED supply and ESP logic at a common ground.
3. Do not drive the LED array directly from an ESP8266 GPIO.
4. Calculate each branch current from the actual supply's maximum voltage, both LEDs' forward-voltage range, the resistor tolerance and temperature. Check resistor power, LED pulse limits and MOSFET limits at the intended transmit duty cycle.
5. Test one LED branch first. Check current and temperature before populating or enabling all six branches.
6. Do not run the ESP8266 and all twelve IR LEDs through a small 3.3 V regulator. Use a correctly sized 5 V source and an appropriate 3.3 V regulator for the logic.
7. Disconnect power before continuity checks and before changing any SPI/GPIO wiring.

### Exact parts used in this build

These are the specific parts selected by the project author, rather than generic alternatives:

- [AO3400 30 V N-channel MOSFET, SOT-23](https://quartzcomponents.com/products/ao3400-mosfet-30v-5-8a-a09t-sot-23-c-n-channel-smd-package?variant=44716853821674)
- [L518EIR1BC1-S61 940 nm, 5 mm IR transmitter LED](https://quartzcomponents.com/products/l518eir1bc1-s61-940nm-5mm-infrared-ir-transmitter-led?variant=46154073506026) — 12 pieces for the six-branch array shown.
- [Waterproof DS18B20 temperature probe](https://quartzcomponents.com/products/ds18b20-water-proof-temperature-sensor?variant=31898050953351)
- [W25Q32 32-Mbit (4-MiB) SPI flash module](https://quartzcomponents.com/products/w25q32-32mbit-capacity-flash-storage-module?variant=44978575376618)

The supplier product pages are the procurement references. For electrical design, use the manufacturer datasheet for the exact item shipped (particularly the IR LED's continuous and pulsed current ratings and the MOSFET's on-resistance at a 3.3 V gate drive); a shop listing headline is not a substitute for those guaranteed limits.

## Confirm the pin map before assembly

The following are the current defaults in `firmware/OverKill-v1.ino`. They are not a substitute for verifying the flash-CS connection on the actual board.

| Function | Current source setting | Notes |
|---|---|---|
| W25Qxx MISO | GPIO12 | SPI bus |
| W25Qxx MOSI | GPIO13 | SPI bus |
| W25Qxx SCLK | GPIO14 | SPI bus |
| W25Qxx CS | **GPIO3** | Conflicts with the provided diagram, which labels CS as GPIO15. GPIO3 is also UART0 RX. Resolve before final wiring. |
| IR TX | GPIO4 default | Can be changed in the Web UI's Hub configuration; confirm the resulting pin allocation. |
| IR RX | GPIO5 default | Can be changed in the Web UI's Hub configuration; confirm the resulting pin allocation. |
| Status LED | GPIO2 default, active LOW | Firmware uses a single status indicator. |
| DS18B20 data | GPIO16 default | Optional; can be changed in Hub configuration. |
| Learn button | GPIO0 declared | Current source reports the physical learn button is disabled because GPIO0 is a boot-strap pin. Use the Web UI to learn commands. |
| UART0 TX/RX | GPIO1 / GPIO3 | Used for serial upload/debugging. GPIO3's use as flash CS in the current source requires special attention. |

**Do this now, before fabricating or powering the final assembly:**

1. With all power removed, identify the actual W25Qxx module's CS pad.
2. Use a continuity meter to determine which ESP-12F pad it reaches. Do not infer the connection from wire colour or from a rendered image.
3. Compare that physical connection against `PIN_FLASH_CS` in the firmware.
4. Decide the final implementation based on the real board and ESP8266 boot requirements. If a pin change is made, update the source, diagram and self-test documentation together.
5. Test boot, serial programming, flash JEDEC-ID detection, read/write/erase and power-cycle persistence.
6. Replace the original drawing only after those tests pass. The current review asset is a review aid, not a wiring schematic: [hardware/pin-map-review.svg](hardware/pin-map-review.svg).

The actual firmware constant is the code's behaviour; the fact that a drawing labels a different pin does not make the physical board correct.

## Assemble and inspect the electronics

Do not follow the original diagram blindly while the CS conflict is unresolved. Once the final CS decision is settled, use the following sequence to reduce debugging complexity.

1. **Prepare the ESP supply.** With the ESP disconnected, apply 5 V to the regulator input and check the output against the module's 3.3 V requirement. Check regulator and capacitor polarity.
2. **Verify grounds.** Confirm continuity among ESP GND, flash GND, IR receiver GND, MOSFET source/ground and the 5 V LED supply ground.
3. **Install external flash.** Connect VCC to 3.3 V, GND to GND, MISO to GPIO12, MOSI to GPIO13 and SCLK to GPIO14. Connect CS only after resolving the GPIO conflict above. Do not power a 3.3 V flash module from 5 V.
4. **Inspect for shorts.** Measure between 3.3 V and GND and between 5 V and GND before attaching the ESP. Investigate any near-short before power-on.
5. **Add the IR receiver.** Connect its supply and ground according to the board labels; use GPIO5 as the default OUT signal unless the firmware/Web UI configuration is changed.
6. **Build one IR LED branch.** Wire the resistor, two series LEDs and MOSFET load path using the polarity in the verified circuit. Add the 100 Ω gate resistor and 10 kΩ gate pulldown as drawn. Check branch current with a suitable meter/test setup.
7. **Add the remaining five branches** only after current and thermal checks. Make wiring and supply distribution thick enough for the current.
8. **Add the optional DS18B20.** Connect data to GPIO16 by default and a 4.7 kΩ pull-up from data to 3.3 V. Make sure this wiring matches the actual sensor package and pinout.
9. **Add status indication** only after deciding whether the design uses the firmware's single status LED or the drawn RGB LED. Do not wire the RGB pins as shown without a matching firmware change.
10. Inspect solder joints, polarity, insulation, strain relief and clearance in the enclosure. Do not connect the full-power IR array until the low-current logic/flash tests pass.

## Print and fit the enclosure

The project uses a supplied multi-object `Overkill.3mf` model. It is a slicer project/model, not proof of a tested print profile. The dimensions of the screws/inserts, material, layer height, infill, supports and print orientation have not been verified from a physical assembly.

1. Open `Overkill.3mf` in your slicer.
2. Inspect the object list and colour/material assignments. Confirm all objects are positioned and enabled as intended.
3. Review the preview for thin walls, unsupported features, holes and bridges. Orient parts to suit your printer and the visible surface finish.
4. Use a material appropriate for the expected ambient temperature and your printer; choose the slicer's normal settings for that filament rather than copying unverified settings from this repository.
5. Print a small fit-check or the parts with critical posts/holes first.
6. Test-fit the ESP carrier, IR array, connectors and cables before printing the complete set. Check that the IR LEDs and receiver have an unobstructed optical path.
7. Confirm screw lengths and clearances physically. Do not allow screws or conductive hardware to touch exposed circuit pads.
8. Record the validated material, slicer version/settings, hardware and any print orientation changes in the repository before calling the enclosure instructions final.


## IR branch current — verify before release

The drawing shows a 5 V supply, two IR LEDs in series and a 30 Ω resistor per branch. Estimate the branch current as:

`I_branch ≈ (V_supply − Vf_LED1 − Vf_LED2 − V_DS(on)) / R_branch`

Use the exact delivered LED datasheet and the AO3400 drain-source on-resistance at the measured 3.3 V gate drive to make the final calculation. As an illustration only, if the two LED forward drops total 2.8 V and the MOSFET drop is small, the drawn 30 Ω branch gives about 73 mA while a pulse is on: (5.0 − 2.8) / 30 = 0.073 A. That is a substantial pulse current and must be checked against the exact L518EIR1BC1-S61 datasheet; the supplier's short listing does not establish its safe continuous or repetitive pulse limit. Confirm peak pulse current, pulse width, repetition rate and duty-cycle limits before using the array. Do not assume it is safe because the resistor is marked 1/4 W. Check instantaneous resistor power `I²R`, average resistor heating, LED pulse current and transmit duty cycle. If the datasheet does not support the resulting load, increase branch resistance or redesign the array before publishing a final schematic.
