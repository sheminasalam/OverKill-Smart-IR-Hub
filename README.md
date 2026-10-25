# OverKill Smart IR Hub

An ESP8266 ESP-12F-based universal infrared hub with IR learning/replay, external SPI flash storage, a local web UI, MQTT integration, Home Assistant support, an Alexa Smart Home Lambda path, and a 3D-printable enclosure.

> **Draft release warning:** source and diagram review found a critical flash chip-select conflict. The supplied firmware sets `PIN_FLASH_CS = GPIO3` (UART0 RX), while its own Stage Info output and supplied diagram say GPIO15. The diagram's RGB LED and reset button also do not match the firmware's current pin constants. See [`hardware/PIN-MAP.md`](hardware/PIN-MAP.md) and the [visual pin-map review](hardware/pin-map-review.svg). Do not wire a build from the original diagram until the mismatch is resolved and verified on the physical hardware.

## Current repository contents

This first GitHub pass contains the firmware text sources, Alexa Lambda handler, README and release documentation. The enclosure 3MF, original/review diagram images and Home Assistant integration source are included in the prepared release bundle but have **not yet been committed to this GitHub branch**. Keep the integration as a separate repository for the planned HACS release; see [`docs/CUSTOM_INTEGRATION.md`](docs/CUSTOM_INTEGRATION.md).

- `firmware/` — Arduino sketch and companion web UI/captive-DNS headers.
- `hardware/PIN-MAP.md` — pin conflict review and what must be verified.
- `alexa_lambda/` — AWS Lambda Smart Home handler.
- `docs/` — hardware, firmware, AWS/Alexa, integration and release notes.

## Firmware

The supplied sketch labels itself **Overkill v2.0** and enforces **IRremoteESP8266 2.9.0** at compile time. Keep `OverKill-v1.ino`, `webui_html.h`, `webui_backend.h`, and `captive_dns.h` together in one Arduino sketch directory. A clean reproducible build and firmware binary have not yet been verified; see [`docs/FIRMWARE.md`](docs/FIRMWARE.md).

Features visible in the supplied source include IR learning/replay, W25Qxx external SPI flash storage, Wi-Fi recovery, browser UI, MQTT, Home Assistant MQTT discovery, local emulated Hue exposure, DS18B20 temperature readings, backup/restore, OTA upload and automation rules/schedules.

## Hardware and enclosure

See [`docs/HARDWARE.md`](docs/HARDWARE.md), [`hardware/PIN-MAP.md`](hardware/PIN-MAP.md) and the [visual pin-map review](hardware/pin-map-review.svg). The diagram supplied by the author is not yet a verified final schematic. The external flash CS assignment, status LED and reset/learn buttons need to be reconciled with the actual board and firmware before presenting the circuit as final. The LED array's current and thermal limits must also be calculated from the exact component datasheets.

## Home Assistant

The companion integration source was provided for review but is not yet committed here. It supports local MQTT, cloud MQTT or both and independent AWS IoT bridge entries with explicit hub-to-bridge assignment. See [`docs/CUSTOM_INTEGRATION.md`](docs/CUSTOM_INTEGRATION.md). Cloud MQTT is plain MQTT in the submitted implementation; do not expose it to untrusted networks without appropriate protection.

## Alexa and AWS IoT

The supplied Lambda reads a retained catalogue from `alexa/config/devices` by default, publishes commands to `alexa/{hub}/directive`, and reads state topics from `alexa/state/{id}`. Configure the AWS IoT endpoint, Lambda environment, permissions, Alexa Smart Home skill, and Home Assistant bridge using your own AWS account. The endpoint literal in the supplied handler must be verified/replaced as appropriate. Never commit private keys, AWS credentials, MQTT passwords or secret backups.

See [`docs/ALEXA_AWS.md`](docs/ALEXA_AWS.md).

## Release limitations

- Flash CS GPIO3/GPIO15 mismatch is a release blocker and needs hardware verification.
- The submitted RGB/reset circuit does not match the current firmware's pin constants.
- The 12-LED array and 30-ohm branch resistors need current/thermal validation against the actual LED datasheet.
- Home Assistant/HACS validation and end-to-end AWS/Alexa testing have not yet been performed as part of this repository preparation.
- No license was included with the source; choose and add an explicit license before announcing a public release.

Track required work in [`docs/RELEASE_CHECKLIST.md`](docs/RELEASE_CHECKLIST.md).

Created by [@sheminasalam](https://github.com/sheminasalam).
