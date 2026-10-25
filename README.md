# OverKill Smart IR Hub

An ESP8266 ESP-12F-based universal infrared hub with learn/replay, external SPI flash storage, a local web UI, MQTT integration, Home Assistant support, an Alexa Smart Home Lambda path, and a 3D-printable enclosure.

> **Draft release warning:** source and diagram review found a critical flash chip-select conflict. The supplied firmware sets `PIN_FLASH_CS = GPIO3` (UART0 RX), while its own Stage Info output and supplied diagram say GPIO15. The diagram's RGB LED and reset button also do not match the firmware's current pin constants. See [`hardware/PIN-MAP.md`](hardware/PIN-MAP.md) and [`hardware/pin-map-review.png`](hardware/pin-map-review.png). Do not wire a build from the original diagram until this has been resolved and verified on the physical hardware.

## Repository structure

- `firmware/` — Arduino sketch and companion web UI/captive-DNS headers.
- `hardware/` — supplied diagram and pin-map review.
- `3d_printing/` — provided enclosure 3MF.
- `home_assistant/custom_components/overkill_smart_hub/` — integration source.
- `alexa_lambda/` — AWS Lambda Smart Home handler.
- `docs/` — hardware, firmware, AWS/Alexa, integration and release notes.

## Firmware

The supplied sketch labels itself **Overkill v2.0** and enforces **IRremoteESP8266 2.9.0** at compile time. Keep `OverKill-v1.ino`, `webui_html.h`, `webui_backend.h`, and `captive_dns.h` together in one Arduino sketch directory. A clean reproducible build and firmware binary have not yet been verified for this draft; see [`docs/FIRMWARE.md`](docs/FIRMWARE.md).

Features visible in the supplied source include IR learning/replay, W25Qxx external SPI flash storage, Wi-Fi recovery, browser UI, MQTT, Home Assistant MQTT discovery, local emulated Hue exposure, DS18B20 temperature readings, backup/restore, OTA upload and automation rules/schedules.

## Hardware and enclosure

Start with [`docs/HARDWARE.md`](docs/HARDWARE.md) and [`hardware/PIN-MAP.md`](hardware/PIN-MAP.md). The original drawing is preserved at [`hardware/original-connection-diagram.png`](hardware/original-connection-diagram.png), but it is not yet a verified final schematic. The 3MF enclosure is in `3d_printing/Overkill.3mf`; print parameters, fastener dimensions and fit checks still need physical validation.

## Home Assistant

Copy `home_assistant/custom_components/overkill_smart_hub/` into `/config/custom_components/overkill_smart_hub/`, restart Home Assistant, and add **OverKill Smart Hub** through Settings → Devices & services. The config flow supports local MQTT, cloud MQTT, or both, and can manage multiple AWS IoT bridge entries with explicit hub-to-bridge assignment. Local MQTT uses Home Assistant's MQTT integration. Cloud MQTT is plain MQTT in the submitted implementation; do not expose it to untrusted networks without appropriate protection.

More detail: [`docs/CUSTOM_INTEGRATION.md`](docs/CUSTOM_INTEGRATION.md).

## Alexa and AWS IoT

The supplied Lambda reads a retained catalogue from `alexa/config/devices` by default, publishes commands to `alexa/{hub}/directive`, and reads state topics from `alexa/state/{id}`. Configure the AWS IoT endpoint, Lambda environment, permissions, Alexa Smart Home skill, and Home Assistant bridge using your own AWS account. The endpoint literal in the supplied handler must be verified/replaced as appropriate. Never commit private keys, certificates with private material, AWS credentials, MQTT passwords or secret backups.

See [`docs/ALEXA_AWS.md`](docs/ALEXA_AWS.md).

## Release limitations

- Flash CS GPIO3/GPIO15 mismatch is a release blocker and needs hardware verification.
- The submitted RGB/reset circuit does not match the current firmware's pin constants.
- The 12-LED array and 30 Ω branch resistors need current/thermal validation against the actual LED datasheet.
- Home Assistant/HACS validation and end-to-end AWS/Alexa testing have not yet been performed as part of this repository preparation.
- No license was included with the source; choose and add an explicit license before announcing a public release.

Track required work in [`docs/RELEASE_CHECKLIST.md`](docs/RELEASE_CHECKLIST.md).

Created by [@sheminasalam](https://github.com/sheminasalam).
