# OverKill Smart IR Hub

OverKill is a DIY universal infrared (IR) hub built around an ESP8266. It is designed to give existing IR-controlled appliances—such as TVs, air conditioners and fans—smart control without requiring you to buy an expensive branded IR hub.

The project combines a compact, PETG-printed enclosure with a browser-based setup interface, external flash storage for learned commands, MQTT, Home Assistant integration, an optional temperature probe, and an Alexa Smart Home cloud bridge.

## What you can build

- **Learn and replay IR commands:** use the receiver to capture commands from an appliance's original remote, then replay them from the hub.
- **Manage devices locally:** use the hub's Web UI to organize remotes and commands, configure Wi-Fi and MQTT, and manage supported rules and schedules.
- **Integrate with Home Assistant:** use MQTT discovery and the companion custom integration to connect the hub with your home-automation setup.
- **Add temperature-aware behaviour:** the enclosure and firmware support the waterproof DS18B20 temperature probe.
- **Add voice control:** the companion AWS Lambda and Alexa Smart Home setup can expose selected devices to Alexa.
- **Build and repair it yourself:** the enclosure model, component links, firmware and setup instructions are provided so you can understand the hardware rather than depend on a closed commercial hub.

## What to expect

This is a DIY build, not a plug-and-play retail product. You should expect to source the components, assemble and test the electronics, print the enclosure in PETG, flash the firmware and configure the local MQTT/Home Assistant path. Alexa voice control adds separate Amazon/AWS account and cloud setup, and requires an internet connection. The hub's local Web UI and local MQTT/Home Assistant path are intended for local control; Alexa's cloud voice path is not fully local.

The author's physical build uses a **D1 mini ESP8266 development board**, which already includes 3.3 V regulation. The alternative bare ESP-12F build needs a suitable external 5 V-to-3.3 V regulator. Before constructing a board from the older schematic, read the pin-map warning below: the external flash chip-select assignment still needs to be reconciled between the firmware and the original diagram.

## Indicative component cost

As a rough reference, six major component line items total about **US$3.34**, converted from the supplier's Indian retail prices using **US$1 = ₹94.76** (reference rate dated 25 October 2026). This is a partial materials subtotal—not the cost of a complete working hub.

| Major component | Approx. price in USD |
|---|---:|
| [D1 mini ESP8266 board](https://quartzcomponents.com/products/wemos-d1-mini-esp8266-nodemcu-mini-d1-module) | $1.52 |
| [W25Q32 4 MiB flash module](https://quartzcomponents.com/collections/all/products/w25q32-32mbit-capacity-flash-storage-module) | $0.60 |
| [Waterproof DS18B20 temperature probe](https://quartzcomponents.com/collections/all/products/ds18b20-water-proof-temperature-sensor) | $0.36 |
| [12 × 940 nm IR LEDs](https://quartzcomponents.com/products/l518eir1bc1-s61-940nm-5mm-infrared-ir-transmitter-led) | $0.57 |
| [AO3400 MOSFET](https://quartzcomponents.com/products/ao3400-mosfet-30v-5-8a-a09t-sot-23-c-n-channel-smd-package) | $0.06 |
| [USB-C 4-pin power breakout](https://quartzcomponents.com/collections/all/products/usb-3-1-female-socket-type-c-connector-4-pin-breakout-pcb-board) | $0.24 |
| **Subtotal for these six line items** | **$3.34** |

*The prices are converted from [Quartz Components' Indian listings](https://quartzcomponents.com/) using a [USD/INR reference rate of ₹94.76 per US dollar](https://exchangeratetool.com/historical/usd-inr) for 25 October 2026. This is an illustration for international readers, not a worldwide quote.* It excludes the IR receiver, status LED, resistors, capacitors, wiring, 5 V supply and cable, PETG filament and print waste, tools, shipping, taxes, and any optional regulator. Local prices and import costs will vary; the linked IR LED listing was shown as sold out when checked.

The sections below walk through the hardware, firmware, Home Assistant integration, AWS IoT certificates and Alexa setup in detail.

> [!CAUTION]
> **Use the corrected connection diagram below.** It shows W25Q32 flash CS on GPIO3 to match the firmware constants; GPIO3 is also UART0 RX, so the diagram notes the serial-console limitation and cautions against remapping the pin. The enclosure image identifies the three top openings, USB-C 5 V input provision, and underside reset pinhole. The hardware has one status LED and no physical learning button; learning is started from the Web UI using the IR receiver. See the [pin-map notes](hardware/PIN-MAP.md).

**Connection diagram:** [Open the connection diagram (PNG)](hardware/circuit-diagram.png). It shows the ESP-12F pin names used by the firmware. A bare ESP-12F needs a suitable 5 V-to-3.3 V regulator; the author's D1 mini build already has onboard regulation. GPIO3 is also UART0 RX, so read the serial/flash-CS note in the diagram and [pin-map notes](hardware/PIN-MAP.md).

![OverKill enclosure with labeled openings and controls](hardware/Overkill_labeled.png)

**Enclosure printing:** print the enclosure in **PETG**. Follow [Hardware, BOM, assembly and enclosure](docs/HARDWARE.md) for preparation and test-fitting. The labeled PNG above is a visual reference, **not** the printable 3MF file.

## Build and setup guides

1. [**Hardware, BOM, assembly and 3D printing**](docs/HARDWARE.md) — parts with exact supplier links, power/driver safety, pin conflict and enclosure fit.
2. [**Firmware and first boot**](docs/FIRMWARE.md) — Arduino environment, flashing, recovery AP, Web UI, MQTT and remote learning.
3. [**Home Assistant integration and HACS plan**](docs/CUSTOM_INTEGRATION.md) — installation, transports, hub discovery and planned dedicated HACS repository.
4. [**AWS IoT, Lambda, account linking and Alexa**](docs/ALEXA_AWS.md) — includes a three-step copy/paste guide for the Alexa Smart Home skill, Lambda trigger/endpoint and Login with Amazon account linking, plus certificates and troubleshooting.
5. [**3D printing / PETG guide**](3d_printing/README.md) — enclosure model status, PETG material and test-fit steps.
6. [**Release checklist**](docs/RELEASE_CHECKLIST.md) — hardware, firmware, cloud and packaging gates.

## Architecture

```text
Alexa Smart Home skill
  -> Login with Amazon account linking
  -> AWS Lambda (alexa_lambda/index.mjs)
  -> AWS IoT Core (retained catalogue and directives)
  -> Home Assistant OverKill integration / mutual-TLS AWS bridge
  -> local or optional remote MQTT transport
  -> ESP8266 hub -> IR LED driver -> appliance
```

The supplied Lambda uses the retained device catalogue at `alexa/config/devices` by default and publishes commands to `alexa/{hub}/directive`. It requires the `IOT_ENDPOINT` environment variable. The Home Assistant integration's AWS bridge uses a separate X.509 certificate/key pair and connects over MQTT/TLS.

## Repository status

The firmware sources, updated Lambda, documentation, corrected connection diagram and enclosure feature views are committed. The original 3MF is at [`3d_printing/Overkill.3mf`](3d_printing/Overkill.3mf). The connection diagram and labeled enclosure image are visual references; they do not replace the 3MF printable model. GPIO3 is shared by flash CS and UART0 RX in the current firmware, as documented in the diagram. The companion Home Assistant integration is supplied separately for now and should be published in its own repository before HACS submission. There is no project license yet; `LICENSE_PENDING.md` is a reminder, not a license.

The repository's source-check workflow validates Python syntax and JSON files. It does not compile the ESP8266 firmware or prove AWS/Alexa end-to-end behavior. The first release remains blocked on the verified flash-CS connection, matching schematic and successful hardware tests.

## Current Lambda source

[Open `alexa_lambda/index.mjs`](alexa_lambda/index.mjs). The repository version does not use `ALEXA_EVENT_CLIENT_ID` / `ALEXA_EVENT_CLIENT_SECRET`; it does not implement event-gateway token exchange or token refresh. Keep **Send Alexa Events** disabled unless the Lambda is extended and tested.

## References

- [ESP8266 Arduino Core documentation](https://arduino-esp8266.readthedocs.io/en/latest/)
- [AWS IoT device and certificate setup](https://docs.aws.amazon.com/iot/latest/developerguide/create-iot-resources.html)
- [Alexa Smart Home account linking](https://developer.amazon.com/docs/alexaplus/account-linking/account-linking-for-sh-and-other.html)
- [HACS integration publication](https://www.hacs.dev/docs/publish/integration/)

Project: [sheminasalam/OverKill-Smart-IR-Hub](https://github.com/sheminasalam/OverKill-Smart-IR-Hub).
