# Firmware build notes

## Source files

Keep these files together in one Arduino sketch directory:

- OverKill-v1.ino — firmware and serial command processing.
- webui_html.h — embedded HTML/CSS/JavaScript user interface in program memory.
- webui_backend.h — HTTP API, Wi-Fi/configuration persistence, UI and OTA helpers.
- captive_dns.h — minimal captive-portal DNS responder for recovery AP.

The submitted sketch labels itself Overkill v2.0 and enforces IRremoteESP8266 2.9.0 at compile time. It uses ESP8266 core libraries (Wi-Fi, UDP, SPI, EEPROM, web server, HTTP OTA) plus PubSubClient. Record the ESP8266 core and PubSubClient versions after a clean build; the uploaded files did not include a reproducible build log.

The repository layout stores these files under firmware/; Arduino IDE may expect the primary .ino to live in a same-named sketch folder. Copy the four files into a local OverKill-v1/ folder before compiling, or add a verified build workflow.

## Serial and important behavior

- Serial debugging is enabled in the supplied source.
- GPIO0 is treated as an ESP8266 boot-strap pin; source output says the physical learn button was removed/disabled.
- The compile-time PIN_FLASH_CS constant is GPIO3, while the supplied connection diagram labels flash CS as GPIO15. This is a release blocker; see ../hardware/PIN-MAP.md.
- Web UI features include Wi-Fi/MQTT configuration, IR learning/relearning, command and device management, flash status, backup/restore, OTA, and automations.
- Current source defines 10 automation rules and 10 schedules, with up to 6 triggers and 8 steps per rule; 4 jobs can run in the runtime queue.
- Web UI OTA uses port 8080 at /update. Use only on a trusted network, with reliable power.

## Build verification

A clean compile and binary were not provided with these files. Before attaching a binary release, record the toolchain and verify boot/self-test, external flash erase/write/read-back, IR learn/transmit, OTA update, MQTT reconnect and power-cycle persistence.
