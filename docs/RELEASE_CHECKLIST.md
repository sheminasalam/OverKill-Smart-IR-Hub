# Release checklist

## Hardware blockers

- [ ] Correct the firmware Stage Info diagnostic from `GPIO15 -> W25Qxx CS` to GPIO3, matching `PIN_FLASH_CS = 3` and the current connection diagram.
- [ ] Verify the physical W25Q32 CS wire is on D1 mini GPIO3/RX and test boot, flash read/write/erase, persistence, and serial-programming/debug behaviour with this shared pin.
- [ ] Confirm the physical assembly matches the uploaded enclosure image: three top openings (DS18B20, IR receiver, single status LED), side USB-C power breakout, underside reset button, and no physical learning button.
- [ ] Verify IR LED pulse/average current and resistor/MOSFET/LED thermal ratings from exact part datasheets.
- [ ] Validate external flash ID, erase, write/read-back and persistence after power-cycle.

## Firmware

- [ ] Clean build with ESP8266 Arduino core; record core/board settings and PubSubClient version.
- [ ] Confirm IRremoteESP8266 2.9.0.
- [ ] Test first boot/self-test, Wi-Fi recovery, local UI, OTA, MQTT reconnect/discovery/commands, DS18B20, automations and power-cycle persistence.
- [ ] Attach a compiled firmware binary only after reproducible build and hardware testing.

## Home Assistant / Alexa

- [ ] Publish the integration in a dedicated HACS-compatible repository with exactly one integration in custom_components/overkill_smart_hub/.
- [ ] Run current HACS action, hassfest and tests on clean Home Assistant.
- [ ] Test local, cloud, both, multiple hubs, bridge assignment, certificate failure and reconnect cases.
- [ ] Test AWS IoT and Lambda using own endpoint, least-privilege IoT policy and no development secrets.
- [ ] Test Alexa discovery, state, TV direct channel, relative channel, AC/fan commands and isolated household assignments.

## Mechanical / repository

- [ ] Test-print and assemble all 3MF objects; document screw/insert sizes, material and slicer settings.
- [ ] Add physical assembly photos and a wiring-verified final schematic.
- [ ] Choose and add a license covering firmware, integration, Lambda, documentation and 3D files.
- [ ] Verify links and release notes, create a version tag and GitHub Release.

## Hygiene

- [x] Exclude Python __pycache__ and .pyc from the clean source bundle.
- [x] Keep certificate files and passwords out of Git.
