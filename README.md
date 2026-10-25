# OverKill Smart IR Hub

A self-built universal IR hub using an ESP8266 ESP-12F, external W25Qxx SPI flash, an IR receiver, a MOSFET-driven IR LED array, a browser-based configuration interface, MQTT, Home Assistant and an Alexa Smart Home Lambda bridge.

This README is intended to take a builder from parts procurement through first boot, appliance learning, Home Assistant integration, AWS IoT certificates and Alexa setup. Work through the sections in order; do not skip the hardware verification gates.

> [!CAUTION]
> **Do not build the pictured circuit as a verified final design yet.** The current firmware source sets `PIN_FLASH_CS = GPIO3`, while the supplied circuit diagram labels flash CS as GPIO15. GPIO3 is UART0 RX; GPIO15 is an ESP8266 boot-strap pin. Do not simply change one to the other by guesswork. Inspect the actual board/continuity and verify the firmware and circuit together before soldering or powering the final assembly. The diagram's RGB LED and button details also do not match the current firmware pin definitions. See [Pin-map review](hardware/PIN-MAP.md) and [visual pin-map review](hardware/pin-map-review.svg).

## 1. What the project does

- Learns and replays IR remote-control signals.
- Stores learned devices and commands in an external W25Qxx SPI flash database.
- Provides a mobile-friendly local Web UI for configuration, learning, device management, backup/restore, automation and OTA firmware upload.
- Publishes Home Assistant MQTT discovery entities.
- Can expose selected IR devices locally through emulated Hue and selected devices to the cloud Alexa catalogue.
- Supports an optional DS18B20 temperature sensor.
- Uses the companion Home Assistant custom integration to discover hubs over local MQTT, cloud MQTT, or both; route messages; and bridge the retained Alexa catalogue and Alexa directives to/from AWS IoT.
- Uses an AWS Lambda Smart Home handler to convert Alexa directives into the MQTT command format understood by the hub.

### Architecture

```text
Alexa app / voice request
        |
        v
Alexa Smart Home skill
        |
        v
AWS Lambda (alexa_lambda/index.mjs)
        |  AWS IoT Data Plane API (HTTPS)
        v
AWS IoT Core
        ^  MQTT over mutual TLS, port 8883
        |
Home Assistant: OverKill Smart Hub integration / AWS bridge
        |
        +---- local MQTT through Home Assistant's MQTT integration
        |
        +---- optional cloud MQTT broker for remote hub(s)
        |
        v
ESP8266 hub -> MOSFET driver -> IR LEDs -> appliance

IR receiver -> ESP8266 -> W25Qxx external flash
DS18B20 (optional) -> ESP8266
```

The AWS bridge is separate from the hub's ordinary MQTT broker. The supplied cloud-broker runtime in Home Assistant uses plain MQTT; AWS IoT uses TLS with an X.509 client certificate. Do not confuse the two paths.

## 2. Before starting

Prepare the following:

- A computer with Arduino IDE and access to this GitHub repository.
- A USB-to-serial adapter suitable for the ESP-12F carrier, or the correct programming adapter.
- A regulated 5 V supply for the design; the supplied drawing recommends at least 2 A.
- A multimeter and the ability to verify continuity and voltage before powering the circuit.
- Home Assistant with the MQTT integration available if using local MQTT.
- An AWS account with access to AWS IoT Core, IAM and Lambda.
- An Alexa Developer Console account and an Alexa device for testing.
- A safe low-current test arrangement for the IR driver. Do not start by powering all twelve IR LEDs.

## 3. Bill of materials (BOM)

This BOM consolidates parts named in the supplied diagram and firmware. Items marked “verify” are not confirmed by a tested PCB BOM, because a PCB manufacturing file and physical assembly record were not supplied.

| Qty. | Component | Use / selection notes |
|---:|---|---|
| 1 | ESP8266 ESP-12F module | Main controller. Use a carrier/adapter that exposes the required GPIOs and permits programming. Confirm the exact module's flash capacity and pinout. |
| 1 | W25Q32 SPI flash module (4 MiB) | External database shown in the diagram. Firmware detects W25Qxx family devices; use the exact part you intend to support and test it. The flash module pin labels vary by breakout. |
| 1 | KY-022 IR receiver module or compatible demodulating 940 nm receiver | Receives IR from original remotes. Use the labels on the actual module to identify OUT, VCC and GND; do not assume a universal physical pin order. |
| 12 | 940 nm IR LEDs | Diagram names L-518EIR1BC1-S61 and shows 6 branches with 2 LEDs in series in each branch. Verify the part's forward voltage, continuous/pulsed current and duty-cycle ratings from its datasheet. |
| 1 | AO3400 N-channel MOSFET, or a verified equivalent | Low-side switch for the IR LED array. Verify its gate drive, current, pulse and thermal specifications for your load. |
| 6 | 30 Ω, 1/4 W resistors (as drawn) | One in series with each two-LED branch. **Treat this as the value in the supplied drawing, not an electrically validated value.** Calculate branch current and resistor dissipation for the actual LEDs and supply before assembly. |
| 1 | 100 Ω resistor | Gate series resistor shown between ESP GPIO4 and MOSFET gate. |
| 1 | 10 kΩ resistor | Gate-to-GND pulldown shown in the driver drawing; helps keep the MOSFET off during reset/boot. |
| 1 | 5 V regulated DC supply | Drawing recommends ≥2 A. Confirm capacity against the array's measured peak load and the ESP regulator input. |
| 1 | AMS1117-3.3 regulator or correctly rated 3.3 V regulator module | 5 V to 3.3 V supply for ESP8266/peripherals. Check regulator heat, dropout, current and transient response in the real circuit. |
| 1 | 10 µF capacitor | 5 V input decoupling shown in the diagram. |
| 1 | 10 µF capacitor | 3.3 V rail bulk decoupling shown in the diagram. |
| 1 | 100 nF capacitor | 3.3 V high-frequency decoupling shown in the diagram. Place close to the ESP/peripheral supply pins where practical. |
| 1 (optional) | DS18B20 temperature sensor | Optional temperature input; firmware default is GPIO16. |
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

## 4. Confirm the pin map before assembly

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

## 5. Assemble and inspect the electronics

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

## 6. Print and fit the enclosure

The project uses a supplied multi-object `Overkill.3mf` model. It is a slicer project/model, not proof of a tested print profile. The dimensions of the screws/inserts, material, layer height, infill, supports and print orientation have not been verified from a physical assembly.

1. Open `Overkill.3mf` in your slicer.
2. Inspect the object list and colour/material assignments. Confirm all objects are positioned and enabled as intended.
3. Review the preview for thin walls, unsupported features, holes and bridges. Orient parts to suit your printer and the visible surface finish.
4. Use a material appropriate for the expected ambient temperature and your printer; choose the slicer's normal settings for that filament rather than copying unverified settings from this repository.
5. Print a small fit-check or the parts with critical posts/holes first.
6. Test-fit the ESP carrier, IR array, connectors and cables before printing the complete set. Check that the IR LEDs and receiver have an unobstructed optical path.
7. Confirm screw lengths and clearances physically. Do not allow screws or conductive hardware to touch exposed circuit pads.
8. Record the validated material, slicer version/settings, hardware and any print orientation changes in the repository before calling the enclosure instructions final.

## 7. Prepare the firmware build environment

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
5. Select the ESP8266 board profile that matches your actual ESP-12F carrier. If using a bare ESP-12F module/adapter, a Generic ESP8266 Module profile may be appropriate, but its flash size and other board settings must match the module/carrier you actually have. Do not assume settings from another ESP board.
6. Select the correct serial port and compile.
7. If compilation says the IRremoteESP8266 version macro is missing or wrong, re-check the installed library and ensure Arduino is not picking up a duplicate library copy.
8. Record the ESP8266 core version, board selection, flash setting and PubSubClient version used for the release.

No release binary is supplied in this repository at present because a reproducible build and hardware test still need to be completed. Do not distribute a local binary as a verified release until the pin-map conflict is resolved and the tests below pass.

## 8. First flash and first boot

1. Connect the ESP-12F programming adapter. Check the adapter's voltage levels; use 3.3 V UART logic.
2. Put the ESP8266 into its serial programming mode using the carrier's documented boot/reset procedure.
3. Upload the compiled sketch.
4. Open Serial Monitor at the baud rate defined by the sketch and reset the hub.
5. Review the startup and **HARDWARE FOUNDATION SELF-TEST** output. The source reports the IR TX idle state, W25Qxx CS idle state and configured GPIOs.
6. Confirm the W25Qxx is detected, its JEDEC ID/capacity are plausible, and storage initializes. If storage detection fails, stop and inspect CS, SPI wiring, voltage and the chip/module part number.
7. Before learning anything, test a minimal IR transmission and receiver capture. The physical GPIO0 learn button is disabled in the current source; learning is initiated through the Web UI.
8. If serial output says storage is in RAM-only mode, do not expect learned devices to survive reset. Fix flash detection first.

The source code currently uses GPIO3 as flash CS while the drawing says GPIO15. Serial upload/debugging also uses GPIO3 as UART RX, which can complicate diagnosis if that pin is tied to the flash. Resolve this before interpreting failures as a bad flash chip.

## 9. Configure Wi-Fi and open the Web UI

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

## 10. Configure the hub's MQTT connection

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

## 11. Learn appliance remotes and test locally

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

## 12. Install the Home Assistant custom integration

The supplied integration ZIP is for manual installation and is not yet the final HACS distribution. It contains a folder named `overkill_smart_hub`. Do not copy `__pycache__` or `.pyc` files into the release.

### Manual installation

1. Make a backup of Home Assistant.
2. From the provided integration ZIP, copy the clean `overkill_smart_hub` directory to:

   `/config/custom_components/overkill_smart_hub/`

   The resulting directory must contain `manifest.json`, `__init__.py`, `config_flow.py`, `coordinator.py`, `hub.py`, `aws_bridge.py`, `cloud_broker.py`, the entity/platform files, translations and the other integration resources directly inside the domain directory.
3. If using **local MQTT**, set up Home Assistant's MQTT integration first. It must connect to the same broker the hub publishes to.
4. Restart Home Assistant.
5. Open **Settings → Devices & services → Add integration** and search for **OverKill Smart Hub**.
6. In the submitted version, the first-time configuration flow opens the AWS bridge setup first and requests an AWS endpoint plus Root CA, device certificate and private key. Complete that bridge step before proceeding to MQTT transport selection. If local-only usage without any AWS bridge is required, this flow should be adjusted before release; the supplied version does not provide a skip button on its initial entry path.
7. Choose the MQTT transport mode:
   - **Local**: use Home Assistant's existing MQTT integration; there is no separate broker hostname form for this mode.
   - **Cloud**: enter the cloud MQTT broker host, port and optional client credentials.
   - **Both**: configure both paths as requested. For cloud modes, use a broker the remote ESP8266 can actually reach.
8. Finish the setup flow and wait for MQTT hub discovery. Confirm the discovered hub in the UI and give it a meaningful name. Explicitly select the intended AWS bridge for that hub; choose **None (no Alexa bridge)** for a hub that should remain local-only.
9. Watch **Settings → System → Logs** for the OverKill integration. Confirm the hub is online and configuration/device sync completes.
10. Confirm the expected OverKill hub device and its button/sensor entities in Home Assistant.
11. If the hub is publishing to a cloud broker, verify host, port, credentials and exact base topic. A successful connection to the wrong broker will not produce discovery on the intended Home Assistant instance.

### What the integration forwards

The firmware publishes Home Assistant MQTT discovery under the configured base topic and Alexa configuration messages under its system topic namespace, including a per-hub four-hex-digit tag. The integration listens for retained discovery/catalogue sync, creates hub/entity config entries and, for hubs assigned to an AWS bridge, merges the cloud-exposed devices into a retained catalogue at `alexa/config/devices`.

The integration subscribes to the configured AWS directive filter, normally `alexa/+/directive`, and routes messages by the four-hex-digit tag in the topic. Keep the hub's `<TAG>_<device-id>` identity pattern intact and assign each hub to the correct bridge.

A Home Assistant service named `overkill_smart_hub.rediscover_hubs` requests rediscovery if you need the integration to reconcile retained discovery after a broker/hub change.

## 13. Create the AWS IoT Core bridge certificate

This section describes the AWS IoT client used by the Home Assistant integration. It is distinct from the IAM role used by Lambda.

### Choose the AWS account and region

1. Sign in to the AWS account that will own this Alexa household's device catalogue.
2. Open **AWS IoT Core** and select the region used for this deployment. For the current Alexa Smart Home setup for the **English (India)** locale, use **Europe (Ireland), `eu-west-1`**, which is the currently documented recommended Lambda region for the EU/IN Alexa endpoint. Follow Amazon's current locale/region mapping if you change the skill locale: [Amazon Alexa Lambda regions](https://developer.amazon.com/en-US/docs/alexa/custom-skills/host-a-custom-skill-as-an-aws-lambda-function.html).
3. In AWS IoT Core, open the device data endpoint/settings view and copy the account's **IoT data endpoint hostname**. Do not copy a different account's endpoint.

### Create a certificate and Root CA file

1. In AWS IoT Core, open **Security → Policies** and create a policy for the bridge. The policy needs to allow the bridge's MQTT connection, subscribe/receive access to the Alexa directive topics, and publish retained messages to its device-catalogue topic.
2. Open **All devices → Things** (console names may change), create a Thing for the Home Assistant bridge, or create an equivalent certificate identity.
3. Create a new X.509 certificate and activate it.
4. Download/save the certificate and its private key when AWS offers the download. **Save the private key securely at this point; AWS does not provide the original private key for download later.**
5. Download the Amazon Root CA certificate that matches the AWS IoT endpoint's certificate chain. The integration's file picker expects a PEM file; the suggested filename in its code is `AmazonRootCA1.pem`.
6. Attach the certificate to the policy you created. Confirm the certificate is active.
7. Store the three PEM files somewhere private on the Home Assistant administrator's computer:
   - Root CA certificate (contains a certificate PEM block)
   - AWS IoT client/device certificate (contains a certificate PEM block)
   - Matching AWS IoT client private key (contains a private-key PEM block)
8. Do not paste any private key into GitHub, an issue, a chat log or an unredacted screenshot.

Follow [AWS's official resource-creation guide](https://docs.aws.amazon.com/iot/latest/developerguide/create-iot-resources.html). The policy must match the actual region, account ID, bridge client ID and topic pattern; avoid giving the bridge unrestricted access to all AWS IoT topics.

### Policy permissions to plan for

For the **Home Assistant bridge certificate**, the integration needs:

- `iot:Connect` for the bridge client ID you enter in the integration.
- `iot:Subscribe` for the directive topic filter `alexa/+/directive`.
- `iot:Receive` for matching directive topics.
- `iot:Publish` and `iot:RetainPublish` for retained catalogue topic `alexa/config/devices`.

AWS policy ARNs for topic filters and topic names are different resource types. AWS policy wildcards use IAM-style `*` and `?`; MQTT's `+` and `#` are topic-filter syntax and are not wildcards in policy resource ARNs. Restrict resources to the required topics and client ID. See [AWS publish/subscribe policy examples](https://docs.aws.amazon.com/iot/latest/developerguide/pub-sub-policy.html) and [retained-message policy examples](https://docs.aws.amazon.com/iot/latest/developerguide/retained-message-policy-examples.html). Test the policy with the bridge logs rather than adopting a broad `iot:*` rule.


#### Example policy template for the Home Assistant bridge certificate

Replace `REGION`, `ACCOUNT_ID` and `BRIDGE_CLIENT_ID`. Set the bridge client ID explicitly in the OverKill setup form so it matches this policy. Review the policy for your account before attaching it to a certificate.

```json
{
  "Version": "2012-10-17",
  "Statement": [
    {
      "Effect": "Allow",
      "Action": "iot:Connect",
      "Resource": "arn:aws:iot:REGION:ACCOUNT_ID:client/BRIDGE_CLIENT_ID"
    },
    {
      "Effect": "Allow",
      "Action": "iot:Subscribe",
      "Resource": "arn:aws:iot:REGION:ACCOUNT_ID:topicfilter/alexa/????/directive"
    },
    {
      "Effect": "Allow",
      "Action": "iot:Receive",
      "Resource": "arn:aws:iot:REGION:ACCOUNT_ID:topic/alexa/????/directive"
    },
    {
      "Effect": "Allow",
      "Action": ["iot:Publish", "iot:RetainPublish"],
      "Resource": "arn:aws:iot:REGION:ACCOUNT_ID:topic/alexa/config/devices"
    }
  ]
}
```

The `????` wildcard pattern targets the four-character hub tag used in the directive topic. If the deployment uses custom topics or client IDs, change the ARNs to match.

## 14. Configure the AWS bridge in Home Assistant

1. Open **Settings → Devices & services → OverKill Smart Hub** and start the integration's bridge setup/additional bridge flow as appropriate.
2. Enter the AWS IoT data endpoint hostname copied in the previous section. The submitted form expects the hostname, not the `https://` prefix.
3. Enter a unique AWS bridge client ID. Use the same value in the certificate policy's allowed client resource; leaving it blank makes the integration generate a default ID.
4. Upload the Root CA PEM, device certificate PEM and matching private-key PEM in their respective file selectors.
5. Submit the setup and check Home Assistant logs. The bridge should log the endpoint, port 8883, client ID, a successful connection and an accepted SUBACK for its directive subscription.
6. Assign each intended hub to the correct bridge during hub confirmation. **Do not automatically assign remote hubs to a bridge just because it is the only one.** This integration supports multiple separate bridge entries and expects the hub-to-bridge mapping to be explicit.
7. If using separate Alexa households, use separate AWS IoT endpoints/accounts and separate certificate identities. Do not point multiple households at one shared catalogue unless shared access is deliberate.
8. Confirm the bridge publishes a retained JSON array to `alexa/config/devices`. It should contain the cloud-exposed device IDs, names and types.

### Cloud MQTT for remote hubs

If a remote hub uses the cloud transport option, it must be able to connect to the broker configured under the integration's MQTT transport. The AWS bridge is not that broker. Because the supplied cloud-broker implementation does not set up TLS, do not directly expose port 1883 to the Internet as a public service. Use a secured private network/VPN or a broker design with suitable encryption, firewall restrictions and per-client ACLs. If your design cannot safely provide this, use a different supported secure transport before deployment.

## 15. Deploy the updated Alexa Lambda

The Lambda source in this repository is the updated file provided by the project owner. The current version **requires** the `IOT_ENDPOINT` environment variable; it no longer silently falls back to a fixed endpoint embedded in the source.

### Lambda code and runtime

1. Make a deployment folder on your computer and copy `alexa_lambda/index.mjs` into it.
2. Use an AWS-supported Node.js Lambda runtime available in the console. The source is an ES module because the entry file is `.mjs`.
3. Install and package the AWS SDK v3 IoT Data Plane dependency with the function code. For example, from the deployment folder run:

   ```bash
   npm init -y
   npm install @aws-sdk/client-iot-data-plane
   zip -r function.zip index.mjs package.json node_modules
   ```

   Use a deployment package compatible with the Lambda runtime/architecture you select. If you build on Windows, use a packaging method that creates a valid ZIP containing the JS file, package metadata and installed dependency directory.
4. In AWS Lambda, create a function in the required region (`eu-west-1` for English India as described above), choose the supported Node.js runtime and upload the package or use the supported source editor/deployment method.
5. Set the handler to `index.handler` for the `index.mjs` module exporting `handler`.
6. Add the environment variables described below, then save/deploy.

### Required and optional environment variables

| Variable | Required? | Value |
|---|---|---|
| `IOT_ENDPOINT` | **Required** | Your AWS IoT data endpoint hostname, or the same hostname prefixed with `https://`. It must belong to the same AWS IoT account/region you're using. |
| `IOT_REGION` | Optional | Defaults to `eu-west-1`; set it explicitly to the region corresponding to the endpoint. |
| `CONFIG_TOPIC` | Optional | Defaults to `alexa/config/devices`; retained device catalogue topic. |
| `STATE_TOPIC` | Optional | Defaults to `alexa/state/{id}`; retained state topic template. Keep `{id}` literal. |
| `CMD_TOPIC` | Optional | Defaults to `alexa/{hub}/directive`; command topic template. Keep `{hub}` literal. |
| `STATUS_TOPIC` | Optional | Defaults to `alexa/status/{hub}`; hub status topic template. Keep `{hub}` literal. |
| `WRITE_STATE` | Optional | Defaults to enabled. Set exactly to `false` if the hub/integration should be the only state writer. |

The endpoint must be provided by your own AWS account. Never copy an endpoint from someone else's project and assume it is yours.

### Lambda execution-role permissions

The Lambda calls the AWS IoT Data Plane HTTPS API to read the retained catalogue/state/status and publish commands/state. Its execution role therefore needs the appropriate IAM permissions, at minimum:

- `iot:GetRetainedMessage` for the configured catalogue topic, state topics and hub status topics.
- `iot:Publish` for the command topic and state topics if `WRITE_STATE` is enabled.

Use resource ARNs scoped to the actual topics and region/account. Do not use administrator access for the Lambda. The X.509 IoT policy used by the Home Assistant MQTT bridge is a different policy from this Lambda execution role.

### Test Lambda discovery before Alexa app setup

1. Use the Lambda console test tool with an Alexa Smart Home **Discover** event.
2. Confirm logs show the retained catalogue loaded and a count of valid devices.
3. If the catalogue is empty, first verify that the Home Assistant bridge is connected and publishing a retained JSON array on `alexa/config/devices`.
4. Confirm the returned endpoints have unique IDs, human-readable friendly names and the expected categories.
5. Test a harmless power/volume directive only after verifying the endpoint maps to the correct appliance. Never debug with a command that might accidentally switch the wrong device.

## 16. Create and link the Alexa Smart Home skill

1. Open the [Alexa Developer Console](https://developer.amazon.com/alexa/console/ask) and create a **Smart Home** skill.
2. Choose the intended locale. For English (India), use the documented Europe (Ireland) Lambda endpoint region, `eu-west-1`; region selection depends on the Alexa account locale, not just your physical location.
3. Configure the Smart Home skill's Lambda endpoint with the ARN of the deployed function in the correct region.
4. Save the skill configuration and ensure the Lambda resource policy allows invocation by the Alexa Smart Home service as instructed by the console.
5. Use the console's test page with the skill enabled for testing. Complete any account-linking steps only if your chosen skill configuration requires them; the supplied Lambda does not implement a custom OAuth service.
6. Open the Alexa app associated with the target household and run **Discover Devices**.
7. Confirm that only the devices marked for cloud exposure appear and that each device has the intended name.
8. Try power, volume and channel commands with an appliance where the IR output can be observed.
9. If a device was renamed or its exposure changed, run discovery again. If Alexa retains an old endpoint/name, remove the stale device from the Alexa app if available and rediscover.

The Lambda uses a retained catalogue, not a hard-coded list of appliances. When no valid catalogue is available, it has no hard-coded fallback; discovery will return no useful device list until the bridge/catalogue is healthy.

### Device IDs and TV channels

The firmware publishes IDs like `4A03_1`, where the first four hexadecimal digits identify the hub for directive routing and the number after the underscore identifies a stored device on that hub. The Lambda uses the prefix before the underscore to publish to `alexa/4A03/directive`. The Home Assistant bridge expects topics of the form `alexa/<four-hex-digit-tag>/directive`. Keep this identity format stable.

For direct channel selection, the updated Lambda handles `Alexa.ChannelController.ChangeChannel` as an **absolute channel number** and validates integer channel numbers from 1 through 999. It sends the digits as a logical `Channel` command so the hub can press the learned numeric keys. Channel Up/Down remains relative.

For named channels, Lambda supports a `channels` map (for example, `"channels":{"asianet":"22"}`) and checks the request's channel name/call sign. However, the current Home Assistant bridge's catalogue builder intentionally publishes only `id`, `name` and `type` for each device. Thus custom channel-name mappings are **not automatically carried from the hub through the current bridge**. Use a numeric channel command until the catalogue path is extended to carry channel metadata; otherwise a manually edited retained catalogue could be overwritten on the next sync.

Alexa natural-language interpretation is separate from Lambda code. Some phrases containing “TV” or “channel” may be routed by Alexa as video-skill requests and never reach this Smart Home skill as a ChangeChannel directive. Test numeric channel selection as well as channel up/down, and use clear endpoint names. A successful Lambda test event proves the handler's behavior only; it does not prove Alexa's voice router will send that event for every phrase.

## 17. Verify the full chain in this order

Test each layer independently before troubleshooting the whole system.

1. **IR hardware:** test receiver capture and IR LED transmission locally.
2. **External storage:** verify JEDEC ID, storage status, learn a test command, reboot, then replay it.
3. **Wi-Fi:** check the Web UI and router lease; confirm reconnection after a hub reboot.
4. **Hub MQTT:** confirm the broker connection and unique base topic.
5. **Home Assistant MQTT:** confirm discovery entities appear through the MQTT integration.
6. **OverKill integration:** confirm the hub config entry appears, entity commands work and logs show retained sync.
7. **AWS bridge TLS:** confirm connection on 8883, accepted directive SUBACK and successful retained catalogue publication.
8. **Lambda:** verify `IOT_ENDPOINT`, IAM permissions and the retained catalogue through a Discover test.
9. **Alexa discovery:** confirm endpoint names/categories in the Alexa app.
10. **Alexa command:** test a selected device, then watch the Lambda log, AWS topic, Home Assistant bridge log, hub MQTT/serial log and physical IR response in sequence.

## 18. Troubleshooting table

| Symptom | Check in this order |
|---|---|
| ESP8266 does not boot | Check 3.3 V stability, boot strap pins, GPIO0/GPIO2/GPIO15 levels, reset wiring and whether external modules pull a boot pin to the wrong level. |
| Firmware compiles with IRremote error | Install exactly IRremoteESP8266 2.9.0 and remove duplicate/stale library copies. |
| Flash not detected / RAM-only mode | Verify flash voltage, exact module pinout, CS conflict, MISO/MOSI/SCLK continuity and JEDEC response. |
| Flash works intermittently or erases fail | Check chip/module quality, wiring, supply decoupling and SPI signal integrity; run erase and read/write verification. |
| Recovery AP not visible | Wait at least 10 seconds after boot; inspect serial output for `Overkill-XXXXXX`; confirm Wi-Fi is not already configured. |
| Web UI cannot be reached | Use the recovery address while on its AP or the current LAN DHCP address after Wi-Fi connects. Make sure client and hub can communicate on the same network. |
| MQTT disconnected | Check broker host reachable from the ESP8266, port, credentials, ACLs, duplicate client ID and base topic. |
| Home Assistant entities missing | Confirm MQTT integration is connected, auto-discovery is enabled, broker/topic settings match and the custom integration logs hub discovery. |
| Hub found but Alexa catalogue empty | Verify the hub's cloud-exposure flag, completed Alexa sync, correct AWS bridge assignment, retained catalogue topic and bridge connection. |
| AWS bridge TLS connection fails | Verify endpoint hostname, port 8883, Root CA, certificate/private-key pair, active certificate, client ID policy and outbound network access. |
| AWS bridge connects but SUBACK is rejected | Check both `iot:Subscribe` on the topic-filter ARN and `iot:Receive` on the topic ARN. AWS policy wildcards differ from MQTT wildcard syntax. |
| Lambda fails at startup | Make sure `IOT_ENDPOINT` is set, `IOT_REGION` matches it, the function package includes `@aws-sdk/client-iot-data-plane` and the configured handler is `index.handler`. |
| Lambda returns no devices | Check for a retained, valid JSON array at `alexa/config/devices` and logs showing the number of normalized devices. |
| Command reaches AWS but appliance does not respond | Confirm topic uses the correct four-hex-digit hub tag, the hub is assigned to the same bridge, the device ID retains the `TAG_ID` format, and the `do` command references learned button names/numeric keys. |
| Wrong TV receives a command | Check Alexa's selected endpoint name and device IDs; do not assume Alexa chose the intended device just because the phrase named it. |
| Named channel is rejected | Use an integer channel number first. The current bridge catalogue strips custom `channels` metadata, so named-channel mappings are not automatically propagated. |
| Remote hub works locally but not remotely | Confirm cloud MQTT broker reachability/security, same host/port/credentials on hub and HA integration, the correct transport assignment and unique base topic. |

## 19. Backup, recovery and OTA

- Use **Configuration → Backup & Restore → Download Backup** to keep a copy of Wi-Fi, MQTT, hub configuration and related settings. The UI distinguishes these settings from learned IR commands/rules stored in external flash.
- Use the UI's database tools only when you understand that reset/wipe operations may delete learned data. Download backups before destructive operations.
- The firmware Web UI uses the OTA update server on port 8080 and path `/update`. The interface's Firmware Upgrade page is the intended route. Use a reliable power supply and upload a binary built for the exact board/firmware version.
- Do not perform OTA over an untrusted network. A failed upload or wrong firmware image may require serial re-flashing.
- After an upgrade, verify flash detection, Wi-Fi, MQTT, learned commands and Alexa discovery again.

## 20. HACS publication plan for the companion integration

The firmware/hardware repository and Home Assistant integration serve different release purposes. HACS requires one integration per repository under `custom_components/<domain>/`; this project has multiple components, so the integration should be extracted into a dedicated public repository with the domain directory at the required path. See [HACS integration publishing](https://www.hacs.dev/docs/publish/integration/) and [HACS default inclusion](https://www.hacs.dev/docs/publish/include/).

Before announcing a HACS release:

1. Create the separate integration repository.
2. Place the files under `custom_components/overkill_smart_hub/` and remove `__pycache__`/compiled bytecode.
3. Add an accurate root README, integration manifest, `hacs.json` and license.
4. Verify all manifest requirements and dependency assumptions against the target Home Assistant version.
5. Run the HACS Action and Home Assistant Hassfest checks.
6. Test in a clean Home Assistant installation, including local, cloud and both transport modes; multiple hubs; multiple bridge assignment; TLS errors; reconnect/reload; device removal; retained-catalogue refresh; and Alexa directive delivery.
7. Create a GitHub Release after passing checks. Only then submit a PR to HACS default if that is the intended distribution route.

The integration ZIP provided during this preparation included Python bytecode files, which must not be kept in the source repository.

## 21. Current release status and next actions

This is a detailed setup guide, but the project should not yet be advertised as a verified hardware release. The repository currently contains the firmware source files, the updated Lambda handler and text documentation. Binary assets such as the 3MF enclosure and original diagram still need to be committed to the repository, and the companion integration should be separated for its future HACS release.

Before tagging the first public release:

- [ ] Resolve the physical flash CS connection and make the firmware, drawing and written pin table agree.
- [ ] Decide how status LED and reset/learn controls are actually built, then update the firmware or diagram to match.
- [ ] Validate 12-LED branch current, resistor power, MOSFET and supply temperature from exact datasheets.
- [ ] Clean-build firmware with pinned library/core versions and record the result.
- [ ] Test flash persistence, IR learn/transmit, Wi-Fi, MQTT, HA discovery, OTA, cloud catalogue, Lambda directives and Alexa commands end to end.
- [ ] Add the 3MF, final verified circuit schematic, physical build photos and remaining release assets.
- [ ] Choose and add a real license. `LICENSE_PENDING.md` is a reminder, not a license.
- [ ] Publish the custom integration in its own repository and test HACS/Hassfest before submitting it.

### Official setup references

- [ESP8266 Arduino Core installation](https://arduino-esp8266.readthedocs.io/en/latest/installing.html)
- [AWS IoT: create devices, certificates and policies](https://docs.aws.amazon.com/iot/latest/developerguide/create-iot-resources.html)
- [AWS IoT publish/subscribe policies](https://docs.aws.amazon.com/iot/latest/developerguide/pub-sub-policy.html)
- [AWS IoT retained-message policy examples](https://docs.aws.amazon.com/iot/latest/developerguide/retained-message-policy-examples.html)
- [Alexa Smart Home / Lambda region guidance](https://developer.amazon.com/en-US/docs/alexa/custom-skills/host-a-custom-skill-as-an-aws-lambda-function.html)
- [HACS integration repository requirements](https://www.hacs.dev/docs/publish/integration/)
- [HACS default repository inclusion](https://www.hacs.dev/docs/publish/include/)

Project source: [sheminasalam/OverKill-Smart-IR-Hub](https://github.com/sheminasalam/OverKill-Smart-IR-Hub).
