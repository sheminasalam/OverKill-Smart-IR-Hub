# Pin-map notes — D1 mini build

The physical build uses a **D1 mini ESP8266 development board**. The current uploaded [connection diagram](circuit-diagram.png) is drawn with ESP-12F GPIO names, so use the board-label mapping below when wiring the D1 mini.

| ESP8266 GPIO | Typical D1 mini label | Function |
|---|---|---|
| GPIO2 | D4 | Single status LED, active LOW |
| GPIO3 | RX | W25Q32 flash CS in the compile-time firmware constant; also UART0 RX |
| GPIO4 | D2 | IR transmitter output default |
| GPIO5 | D1 | IR receiver output; used to capture commands from the original remote |
| GPIO12 | D6 | W25Q32 MISO |
| GPIO13 | D7 | W25Q32 MOSI |
| GPIO14 | D5 | W25Q32 SCLK |
| GPIO16 | D0 | DS18B20 temperature data default |
| RST | RST | Physical reset button under the enclosure |
| None | — | There is no physical learning button. Start learning from the Web UI and use the IR receiver to capture the original remote's signal. |

## Flash CS and serial diagnostic mismatch

The current circuit diagram assigns W25Q32 CS to GPIO3, matching the compile-time constant `PIN_FLASH_CS = 3` in `firmware/OverKill-v1.ino`. However, the firmware's Stage Info diagnostic currently prints `GPIO15 -> W25Qxx CS`. That printed label is stale and should be corrected to GPIO3 before release.

GPIO3 is also UART0 RX / D1 mini RX. Sharing this line with flash CS can affect serial input, flashing or diagnostics depending on the board's USB-UART wiring and the timing of serial activity. Confirm on the actual D1 mini that the W25Q32 CS wire reaches GPIO3, verify boot and flash operations, and test the programming/debug workflow. Do not remap the pin or redraw the diagram unless the physical build and firmware are changed together.

## Enclosure provisions and controls

- **Top plate:** three openings for the waterproof DS18B20 temperature probe, the IR receiver used during learning, and the single status LED.
- **Side wall/base:** provision for the 4-pin USB-C female breakout used as the 5 V power input.
- **Underside:** pinhole/access for the physical reset button connected to D1 mini RST.
- **Learning:** no physical learn button; learning begins in the Web UI and uses the IR receiver.

The current [labeled enclosure image](Overkill_labeled.png) shows the openings and control locations. It is a visual reference; the actual printable model is [`3d_printing/Overkill.3mf`](../3d_printing/Overkill.3mf).

## Bare ESP-12F alternative

The author's build uses a D1 mini, which already includes 3.3 V regulation. A builder using a bare ESP-12F/module without a suitable regulator needs a separate regulated 3.3 V supply from the 5 V input, such as the [AMS1117-3.3 800 mA module](https://quartzcomponents.com/collections/all/products/ams1117-3-3-ldo-800ma-dc-5v-to-3-3v-step-down-power-supply-module). Never connect the 5 V USB-C power input directly to ESP-12F VCC/3V3.

This pin map is documentation, not proof of a validated assembly. Before public release, test flash JEDEC ID, read/write/erase, power-cycle persistence, normal boot and the intended serial programming/debug procedure.
