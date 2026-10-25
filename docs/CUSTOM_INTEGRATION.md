# Home Assistant: native MQTT vs OverKill Smart Hub integration

The firmware offers two Home Assistant **discovery modes** in **Configuration → Configure Hub → Discovery**. Select one based on what you want:

| Firmware discovery mode | What it does | What it does not do |
|---|---|---|
| **Native HA MQTT (entities only, no Alexa)** | Uses Home Assistant's built-in MQTT integration and the hub's native MQTT discovery to create supported Home Assistant entities. | Does not publish the custom integration's Alexa cloud device catalogue or route cloud Alexa directives. |
| **Custom Integration (Alexa + HA entities)** | Publishes the hub's custom discovery/configuration messages used by the OverKill Smart Hub integration, supports the companion integration's hub/entity features, and enables the integration to collect Alexa cloud device configuration. | Does not itself set up AWS, Lambda or an Alexa Smart Home skill. Those are separate optional components for cloud Alexa. |

**Important:** selecting **Native HA MQTT** is not the same as installing the **OverKill Smart Hub custom integration**. Native discovery is the firmware's built-in entity route. The custom integration is a separate Home Assistant add-on. For Alexa's *cloud Smart Home remote-control path*, use **Custom Integration** mode and install the custom integration. Local **Emulated Hue** is a third, separate firmware feature and can be used with either discovery mode.

Changing the firmware discovery mode clears retained topics associated with the previous mode; select the intended mode and allow its discovery sync to complete.

## Option A — Native Home Assistant MQTT entities

Choose this if you only want the supported Home Assistant entities and do not need the OverKill custom integration features or Alexa cloud remote.

1. Set up Home Assistant's built-in **MQTT** integration and a broker reachable by the ESP8266.
2. In the hub Web UI, open **Configuration → Configure Hub → Discovery** and choose **Native HA MQTT (entities only, no Alexa)**.
3. Open **Configuration → Configure MQTT** on the hub. Enable MQTT and point it at the same broker that Home Assistant uses.
4. Keep **Enable Home Assistant Auto Discovery** enabled, save and allow the hub's retained discovery messages to synchronize.
5. Confirm the discovered entities in Home Assistant.

No AWS IoT endpoint, certificates, Lambda, Alexa Smart Home skill, or OverKill custom integration is needed for this setup. If you still want Alexa to trigger selected IR commands on the local network, configure **Emulated Hue** separately; see [Alexa and Home Assistant connection options](ALEXA_OPTIONS.md).

## Option B — OverKill Smart Hub custom integration

Choose this when you want the companion integration's hub-specific discovery and entities, multi-hub/transport handling, or **Alexa cloud Smart Home remote control**.

1. Install [OverKill Smart Hub Integration](https://github.com/sheminasalam/OverKill-Smart-Hub-Integration). Until it is accepted into HACS default repositories, add it through **HACS → Integrations → Custom repositories** (category **Integration**) or follow the repository's manual installation instructions.
2. In the hub Web UI, set **Configuration → Configure Hub → Discovery** to **Custom Integration (Alexa + HA entities)**.
3. Configure the hub's MQTT connection.
4. In Home Assistant, configure the custom integration and select the appropriate MQTT transport:
   - **Local** — uses Home Assistant's existing MQTT integration/broker. Both the hub and Home Assistant must be able to reach the same broker.
   - **Cloud** — uses a separately configured MQTT broker reachable by the hub and Home Assistant.
   - **Both** — permits discovery from local and cloud MQTT sources.
5. Confirm the hub when it appears and assign a name. Select **None (no Alexa bridge)** if you are not setting up Alexa cloud.
6. If you want cloud Alexa, create/configure the AWS IoT bridge and assign the hub to the correct bridge. Then enable **Alexa Cloud** and choose a category for every device you want discovered by the Lambda.

### MQTT transport is not the AWS Alexa bridge

**Local / Cloud / Both** selects the MQTT transport used to find and communicate with hubs. The **AWS IoT bridge** is a separate TLS-secured link that carries the retained device catalogue and Alexa directives. You can use the custom integration without configuring an AWS bridge; cloud Alexa is the feature that needs the AWS/Lambda/skill path.

Security note: the integration's optional Cloud MQTT transport currently uses plain MQTT (typically port 1883) without TLS. Do not expose that broker directly to the public Internet; use a secured private network/VPN and appropriate credentials, ACLs and firewall restrictions.

## How Alexa exposure works in the firmware

The firmware has two separate exposure controls:

- **Emulated Hue (local Alexa):** enabled per learned command using **Edit Command → Expose learned command to Alexa**. Each exposed command appears as an on/off light. Turning it on transmits the learned IR command; the item returns to off. Up to 32 commands can be exposed locally. This behaves like a momentary button, not appliance state feedback.
- **Alexa Cloud (Smart Home):** enabled per device using **Edit Device → Device Category** and **Alexa Cloud**. The cloud Lambda exposes the device using its category and supports the richer command set listed in [Alexa and Home Assistant connection options](ALEXA_OPTIONS.md). This route requires **Custom Integration** discovery mode.

The two exposure types are independent. You can use both for one hub if you want specific commands as local light switches while also exposing full devices through the cloud. Avoid exposing duplicate commands unless you intentionally want both Alexa representations.

For the full AWS IoT, Lambda deployment, account linking, skill endpoint and certificate steps, see [AWS IoT, Lambda, account linking and Alexa](ALEXA_AWS.md).

## Troubleshooting

- **Native entities do not appear:** check that Home Assistant's MQTT integration and broker are connected, the hub points to that broker, MQTT is enabled, Home Assistant discovery is enabled and the firmware is set to Native HA MQTT mode.
- **The custom integration does not discover a hub:** check that the firmware is in Custom Integration mode, the correct transport/broker is configured, the hub's base topic is unique, and discovery sync completed. The integration's `overkill_smart_hub.rediscover_hubs` service can request rediscovery after correcting a broker or topic.
- **Alexa cloud discovers no devices:** confirm Custom Integration mode, the integration is running, the device has a category and **Alexa Cloud** is enabled, the retained catalogue is present at `alexa/config/devices`, and the hub is explicitly assigned to the intended AWS bridge.
- **Local Emulated Hue devices are missing:** verify that the hub and Echo are on the same LAN and that Wi-Fi multicast/SSDP discovery is allowed. Check the local Hue status in **Configure Hue / Alexa** and ensure each intended command is enabled.

When requesting help, include the discovery mode, MQTT transport, relevant sanitized logs and Home Assistant version. Redact broker passwords, AWS certificate private keys and secrets.
