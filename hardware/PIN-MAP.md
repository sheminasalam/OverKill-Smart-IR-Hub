# Pin-map review — actual build notes plus unresolved flash-CS issue

Updated vector diagram: [OverKill ESP-12F Connection Diagram](OverKill-ESP12F-Connection-Diagram.svg). It is the bare ESP-12F version, not a drop-in wiring diagram for the author's D1 mini board.

The project author's assembled version uses a **D1 mini ESP8266 development board**, not a bare ESP-12F module. The original connection diagram was drawn with ESP-12F GPIO numbers. The D1 mini silkscreen uses Dx aliases, so use the mapping below when wiring the actual board.

| ESP8266 GPIO used by firmware | Typical D1 mini silkscreen | Function / note |
|---|---|---|
| GPIO2 | D4 | Single status LED output, active LOW |
| GPIO3 | RX | UART0 RX; **current firmware also assigns this as W25Qxx CS**, which conflicts with serial RX |
| GPIO4 | D2 | IR transmitter default |
| GPIO5 | D1 | IR receiver default; used to capture remote signals during Web UI learning |
| GPIO12 | D6 | W25Qxx MISO |
| GPIO13 | D7 | W25Qxx MOSI |
| GPIO14 | D5 | W25Qxx SCLK |
| GPIO16 | D0 | DS18B20 temperature data default |
| RST | RST | Physical reset button is on the underside of the enclosure and resets the D1 mini |
| None | — | There is **no physical learning button**. Learning is started from the Web UI using the IR receiver. Do not add a learn button to GPIO0. |

![Corrected enclosure views with labeled openings and controls](hardware/OverKill-ESP12F-Connection-Diagram.svg)

## Conflict still requiring a physical check

The firmware's compile-time `PIN_FLASH_CS` constant is GPIO3 (D1 mini RX), but the original connection diagram labels the flash CS as GPIO15 (D8 on a typical D1 mini). These are not interchangeable assumptions. Determine which pad on the actual board goes to the W25Q32 module's CS pin, then align the source and final circuit. Do not treat the old drawing or firmware status message alone as proof of the physical wiring.

When CS is connected to GPIO3/RX, it can interfere with UART receive/programming. GPIO15/D8 is a boot-strap pin and must remain at the required logic level during boot. Test the physical choice for reliable boot, serial programming, flash JEDEC ID, erase, write/read-back and persistence after power cycling.

## Actual enclosure provisions

- **Top plate:** three openings/provisions for the waterproof DS18B20 temperature probe, IR receiver (the receiver used when learning commands from an original remote), and the single status LED.
- **Side:** mounting/provision for the 4-pin USB-C female breakout PCB used as the 5 V power input.
- **Underside:** access to the reset button connected to D1 mini RST.
- **No learning-button opening:** learning is performed from the Web UI; the IR receiver detects the original remote's signal.

The single status LED and the physical controls described above supersede the RGB LED and separate learn button shown in the older connection diagram. The drawing also needs to be redrawn to show the USB-C breakout provision and the underside reset button.

## Alternative bare ESP-12F build

If another builder uses a bare ESP-12F instead of the D1 mini, the module needs a suitable regulated 3.3 V supply. The author recommends the [AMS1117-3.3 800 mA module](https://quartzcomponents.com/collections/all/products/ams1117-3-3-ldo-800ma-dc-5v-to-3-3v-step-down-power-supply-module) for the 5 V to 3.3 V conversion in that variant. This regulator is not required for the author's D1 mini build because the D1 mini already has onboard regulation. Never connect the 5 V USB-C input directly to ESP-12F VCC/3V3.

This file is a pin-map review, not a validated final schematic. The IR LED branch current and flash CS pin must both be physically verified before marking the whole circuit final.
