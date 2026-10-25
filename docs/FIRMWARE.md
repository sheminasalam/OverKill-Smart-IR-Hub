# Firmware installation and initial hub configuration

This guide covers preparing the Arduino toolchain, compiling/flashing the ESP8266, first boot, Wi-Fi and MQTT configuration, appliance learning, backup and OTA.

## Prepare the firmware build environment

### Required software

- Arduino IDE 2.x or a compatible Arduino build environment.
- ESP8266 Arduino core.
- **IRremoteESP8266 version 2.9.0 exactly.** The sketch intentionally fails compilation if a different version is selected.
- PubSubClient library for MQTT.
- The firmware's companion headers: `webui_html.h`, `webui_backend.h` and `captive_dns.h`.

The other includes in the sketch are provided by the Arduino ESP8266 core. Install the ESP8266 core using its [official Boards Manager instructions](https://arduino-esp8266.readthedocs.io/en/latest/installing.html). Install IRremoteESP8266 2.9.0 from Library Manager or from its official release/archive, then select that exact version. Install PubSubClient from Library Manager.

### Create the sketch folder

1. Open this repository's `firmware/` folder.
2. On your development computer, create a sketch folder named `OverKill-v1`.
3. Put these four files together in that folder:
   - `OverKill-v1.ino`
   - `webui_html.h`
   - `webui_backend.h`
   - `captive_dns.h`
4. Open `OverKill-v1.ino` in Arduino IDE.
5. For the author's actual D1 mini board, select the matching D1 mini profile in Arduino IDE (commonly **LOLIN(WEMOS) D1 R2 & mini**) and make sure flash size/flash layout settings match the specific board. If building with a bare ESP-12F instead, select a compatible Generic ESP8266 Module profile and match its internal flash settings. Do not choose a generic ESP-12F profile for the author's D1 mini build.
6. Select the correct serial port and compile.
7. If compilation says the IRremoteESP8266 version macro is missing or wrong, re-check the installed library and ensure Arduino is not picking up a duplicate library copy.
8. Record the ESP8266 core version, board selection, flash setting and PubSubClient version used for the release.

No release binary is supplied in this repository at present because a reproducible build and hardware test still need to be completed. Do not distribute a local binary as a verified release until the pin-map conflict is resolved and the tests below pass.

## First flash and first boot

1. For the author's D1 mini, program through the board's USB programming port; the enclosure's 4-pin USB-C breakout is the separate 5 V product power input, not the programming interface. If programming a bare ESP-12F, use a suitable 3.3 V UART adapter and the module's documented boot/reset sequence.
2. Put the ESP8266 into its serial programming mode using the carrier's documented boot/reset procedure.
3. Upload the compiled sketch.
4. Open Serial Monitor at the baud rate defined by the sketch and reset the hub.
5. Review the startup and **HARDWARE FOUNDATION SELF-TEST** output. The source reports the IR TX idle state, W25Qxx CS idle state and configured GPIOs.
6. Confirm the W25Qxx is detected, its JEDEC ID/capacity are plausible, and storage initializes. If storage detection fails, stop and inspect CS, SPI wiring, voltage and the chip/module part number.
7. Before learning anything, test a minimal IR transmission and receiver capture. The built hardware has **no physical learning button**: start learning from the Web UI and use the top-plate IR receiver to capture the original remote's signal.
8. If serial output says storage is in RAM-only mode, do not expect learned devices to survive reset. Fix flash detection first.

The source code currently uses GPIO3 as flash CS while the supplied diagram says GPIO15 (D8 on a typical D1 mini). GPIO3 is also D1 mini RX, which can complicate serial programming/debugging if it is tied to the flash. Resolve the physical CS wiring and align code plus schematic before interpreting failures as a bad flash chip.

## Configure Wi-Fi and open the Web UI

### First-time recovery access point

When the firmware has no saved Wi-Fi SSID, it waits approximately 10 seconds, then starts an open recovery access point named:

`Overkill-XXXXXX`

`XXXXXX` is the hub's six-digit hexadecimal ESP chip ID. The Serial Monitor prints the actual SSID and recovery IP (normally `192.168.4.1`). The supplied code also implements a captive DNS responder.

1. Power the hub and wait for the recovery AP to appear.
2. On your phone or computer, connect to `Overkill-XXXXXX`. It is an open recovery AP, so use it only during local first-time configuration.
3. Open `http://192.168.4.1/` in a browser. If the device shows a captive sign-in page, use that. If the AP does not appear, check the serial log and confirm that Wi-Fi credentials are not already saved.
4. In the Web UI select **Configuration → Configure WiFi**.
5. Press **Scan**, wait for scan results, and select your home 2.4 GHz Wi-Fi network. ESP8266 supports 2.4 GHz Wi-Fi, not 5 GHz-only networks.
6. Enter the Wi-Fi password. Set a hostname if desired. Leave static-IP fields disabled unless you have selected a valid address, gateway, subnet and DNS for your LAN.
7. Press **Save**. Wait for the hub to join your router.
8. Find the hub's IP address in your router's DHCP/client list or from the serial log. Open `http://<hub-IP>/` from a device on the same LAN.
9. Give the hub a stable DHCP reservation on your router if you want its Web UI address to remain predictable.

The recovery AP is temporary; firmware can stop it after a client connects and the grace period expires. If you miss it, use the hub's LAN IP after Wi-Fi connects. Keep the Web UI on a trusted network.

### Naming the hub

Open **Configuration → Configure Hub** and set a memorable hub name. The browser UI includes pin configuration for IR TX, IR RX, status LED and temperature sensor. If you change any of these pins, record the mapping and inspect the physical wiring first. This UI does not solve the hard-coded external flash CS conflict.

## Configure the hub's MQTT connection

Home Assistant discovery, hub discovery in the custom integration and the cloud Alexa catalogue all depend on correct MQTT configuration.

1. Make sure an MQTT broker is available. For local Home Assistant use, install/configure Home Assistant's supported MQTT integration and broker (for example, Mosquitto) first.
2. In the hub Web UI, select **Configuration → Configure MQTT**.
3. Tick **Enable MQTT**.
4. Enter the broker hostname/IP reachable from the ESP8266. Do not use `localhost` unless the broker is actually running on the ESP (it is not); from an ESP, use the broker's LAN address or resolvable host name.
5. Enter the broker port. Firmware defaults to `1883`; it uses the provided username/password if the broker requires them.
6. Keep the configured client ID unique for this hub. The firmware creates a default based on the chip ID if a value has not been customised.
7. Keep or set the base topic. A fresh configuration uses an `irhub/<chip-id>` form. Do not reuse the same base topic for another hub on the same broker.
8. Leave **Enable Home Assistant Auto Discovery** enabled for a normal Home Assistant setup.
9. Save the settings. Check the Web UI information/status view and the serial log for broker connection status.
10. In Home Assistant, confirm the built-in MQTT integration is connected and that discovered button/sensor entities appear after hub discovery sync completes.

Keep the broker password out of screenshots and public issue reports. The firmware's standard MQTT connection supports the fields displayed in its Web UI; confirm network and broker security separately.

### Local MQTT versus remote/cloud MQTT

- **Local MQTT:** the hub publishes to a broker already accessible on the home LAN. The OverKill integration listens through Home Assistant's MQTT integration.
- **Cloud MQTT:** the hub at a remote site and the Home Assistant OverKill integration connect to the same separately configured reachable MQTT broker. The provided integration's cloud broker code is plain MQTT (not TLS) and defaults to port 1883. Do not expose it directly to the public Internet without an appropriate secure network design, firewall, access controls and credentials.
- **Both:** the integration is configured for local and cloud transport. Select the correct source/transport when confirming the hub.

AWS IoT is a separate TLS command/catalogue bridge and is not automatically a replacement for the hub's normal MQTT broker.

## Learn appliance remotes and test locally

Do this for each appliance, one at a time. Have the original remote and appliance in front of you.

1. Open the hub Web UI and choose **Device Dashboard**.
2. Select **Add New Remote**. Enter a clear device name, such as `Living Room TV`, `National TV`, `Bedroom AC` or `Ceiling Fan`, and choose the appropriate category where prompted.
3. Open the new device, select **Add New Command**, and use the learning action shown in the UI.
4. Point the original remote at the hub's IR receiver from a short distance. Press the intended physical key when the UI enters the learning window. Avoid sunlight or bright IR sources directly on the receiver while learning.
5. Name the command so it is understandable and can be referenced consistently (for example, `Power`, `Volume Up`, `Channel Up`, `1`, `2`, `Temp Up`, `Swing On`). The firmware normalizes button names for Alexa command matching, but clear names make troubleshooting easier.
6. Press Save, then use the UI's **Press** action to retransmit the newly learned command.
7. Check that the physical appliance responds. If it does not, re-learn it with the receiver closer to the source, then check IR TX pin assignment, LED polarity, MOSFET operation and emitter direction.
8. Add the rest of the commands needed for the device. Learn numeric keys if direct channel selection will use numeric-key presses.
9. For TVs, use the channel UI to add a channel name and its integer channel number when the UI supports that mapping. Test numeric commands directly on the TV first.
10. Back up configuration and learned data using **Configuration → Backup & Restore** once the hub has been configured and tested.

The firmware's Web UI provides storage information including capacity, used/free sectors and database state. If learned data disappears after reboot, check external flash initialization and confirm that you are not operating in RAM-only mode.

### Local Hue and cloud Alexa exposure are different

The firmware UI states that **local emulated-Hue exposure is configured per command**, while **Alexa cloud exposure is configured per device**. These are separate settings. Enable only what you intend to expose, and give every appliance an unambiguous name.

For cloud Alexa, ensure each intended device is marked for cloud exposure in its device settings and allow the MQTT Alexa catalogue synchronization to complete. The hub publishes device IDs in a form like `4A03_1`: the four-hex-digit prefix is the hub tag and the suffix identifies the stored device. Do not manually rename these IDs; the Lambda uses the tag to choose the directive topic and the firmware uses the suffix to resolve the learned device.

## Backup, recovery and OTA

- Use **Configuration → Backup & Restore → Download Backup** to keep a copy of Wi-Fi, MQTT, hub configuration and related settings. The UI distinguishes these settings from learned IR commands/rules stored in external flash.
- Use the UI's database tools only when you understand that reset/wipe operations may delete learned data. Download backups before destructive operations.
- The firmware Web UI uses the OTA update server on port 8080 and path `/update`. The interface's Firmware Upgrade page is the intended route. Use a reliable power supply and upload a binary built for the exact board/firmware version.
- Do not perform OTA over an untrusted network. A failed upload or wrong firmware image may require serial re-flashing.
- After an upgrade, verify flash detection, Wi-Fi, MQTT, learned commands and Alexa discovery again.


## Board/core version and flash setting

The committed source enforces IRremoteESP8266 2.9.0. The project hardware used for this build is a D1 mini. The source does **not** identify a tested ESP8266 Arduino core version, the internal flash chip fitted to the specific D1 mini, or the exact Arduino Tools settings used for the author's final build. These cannot be safely inferred from source code alone. Before publishing a compiled binary, record the actual Boards Manager core version, selected board profile, detected internal flash size, flash layout/OTA setting, CPU frequency, upload speed and PubSubClient version used for a successful build. Select flash settings to match the internal flash on the module—not the external W25Q32 database chip.
