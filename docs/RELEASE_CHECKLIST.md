# Release checklist

## Hardware blockers

- [ ] Resolve PIN_FLASH_CS = GPIO3 vs Stage Info and diagram GPIO15. Validate on physical hardware and update firmware plus schematic together.
- [ ] Decide if the RGB LED and separate reset button are present in the final board; the current firmware map does not implement the depicted RGB channel pins.
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
