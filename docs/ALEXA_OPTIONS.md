# Alexa and Home Assistant connection options

This project has **two separate connection decisions**. Choose how Home Assistant discovers the hub, then choose how (or whether) Alexa should control it. The options can work together, but they are not interchangeable.

## At a glance

| Goal | Firmware discovery mode | Extra software/services | What you get |
|---|---|---|---|
| Home Assistant entities only | `Native HA MQTT (entities only, no Alexa)` | Home Assistant's built-in MQTT integration and a reachable MQTT broker | Hub command entities and supported sensors in Home Assistant. No Alexa cloud catalogue is sent through this discovery mode. |
| Simple, local Alexa buttons for learned IR commands | Configure **Emulated Hue** and enable exposure on each command | Compatible Echo/Alexa device on the same LAN; no AWS or OverKill Home Assistant custom integration required | Each exposed learned command appears to Alexa as an on/off light, for example `Living Room TV Volume Up`. Up to **32** commands can be exposed. |
| Alexa cloud Smart Home remote for whole devices | `Custom Integration (Alexa + HA entities)` | OverKill Smart Hub custom integration, AWS IoT bridge, deployed Lambda, Alexa Smart Home skill and account linking | Alexa discovers devices by category (TV, fan, AC, etc.) and translates supported directives into sequences of learned IR commands. |
| Home Assistant plus full Alexa cloud remote | `Custom Integration (Alexa + HA entities)` | Custom integration; HA MQTT for local hub transport; AWS/Lambda/skill setup for cloud Alexa | Home Assistant entities and cloud Alexa control. Local Emulated Hue can also be enabled for selected commands if you want both styles. |

### Important distinction

The firmware's **Native HA MQTT** option is for Home Assistant entity discovery. It does not supply the device catalogue and directive routing needed by the project's **Alexa cloud Smart Home** path. For that cloud path, set discovery to **Custom Integration**, install the OverKill Smart Hub custom integration, and assign the hub to the intended AWS bridge.

The **local Emulated Hue** server is a separate feature in the firmware. It can expose individual commands regardless of whether you use native MQTT or the custom integration for Home Assistant. It does not provide the rich device-type controls described in the cloud command table below.

## 1. Home Assistant: native MQTT or the custom integration?

### Native Home Assistant MQTT discovery

Choose this when you want the firmware's supported Home Assistant entities without installing the OverKill custom integration.

1. Configure Home Assistant's built-in MQTT integration and broker.
2. In the hub Web UI, open **Configuration → Configure Hub → Discovery** and select **Native HA MQTT (entities only, no Alexa)**.
3. Point the hub's MQTT settings to that broker and enable Home Assistant MQTT discovery.
4. Confirm the hub's discovered entities in Home Assistant.

This mode does not require AWS certificates, an Alexa skill or the custom integration. If you still want local Alexa button switches, configure **Emulated Hue** separately under **Configure Hue / Alexa**.

### OverKill Smart Hub custom integration

Choose this for the project's custom hub integration features and, importantly, for Alexa cloud Smart Home control.

1. Install the integration from [OverKill Smart Hub Integration](https://github.com/sheminasalam/OverKill-Smart-Hub-Integration). Until it is accepted into HACS default repositories, add it in **HACS → Integrations → Custom repositories**, or install it manually.
2. In the hub Web UI, select **Custom Integration (Alexa + HA entities)** under **Configuration → Configure Hub → Discovery**.
3. Configure the integration's MQTT transport. **Local** uses Home Assistant's configured MQTT integration; **Cloud** or **Both** is for a broker reachable from a remote hub as well as Home Assistant.
4. If you want Alexa cloud control, add an AWS bridge in the integration and assign this hub to the correct bridge. If you only want Home Assistant integration features, the AWS bridge is optional.
5. Enable **Alexa Cloud** and select a device category in the firmware's **Edit Device** screen for every device you want Alexa to discover.

The integration's optional Cloud MQTT transport is separate from the AWS IoT Alexa bridge. The current Cloud MQTT transport is not TLS-protected; do not expose a plain MQTT broker directly to the public Internet. Use a suitable private network/VPN and strict broker access controls.

## 2. Alexa: local Emulated Hue or cloud Smart Home?

### Option A — Local Emulated Hue (no cloud bridge)

Use this when you want Alexa to trigger individual learned IR buttons as simple switches and want to avoid AWS/Lambda setup.

1. Ensure the hub and the Echo/Alexa device are on the same local network and that SSDP/multicast discovery is allowed.
2. In the hub Web UI, open **Configure Hue / Alexa**. This shows the local HTTP and SSDP services.
3. Open a learned command's **Edit Command** screen and enable **Expose learned command to Alexa**.
4. Discover devices from the Alexa app. The exposed items appear as **on/off lights**, named using the device and command names (for example, `Bedroom AC Temp Up`).

**How these switches behave:** turning a Hue item **on** transmits the learned IR command. The hub then reports the Hue item back as **off**, ready for the next press. Turning it off does not send an IR command. These are momentary command buttons, not feedback about the appliance's actual power or operating state. The Alexa app showing the item as off after activation is expected.

There is a firmware limit of **32 exposed local Hue commands**. Expose only commands you actually use, give the parent device and commands short, distinct names, and test each one. This path doesn't require the OverKill custom integration, AWS IoT, Lambda or a cloud Alexa skill, but it does rely on local network discovery working.

### Option B — Alexa Smart Home cloud (device-aware remote)

Use this when you want Alexa to see a device as a TV, fan, air conditioner, streamer, light or switch and use the cloud Lambda's supported controls.

The command path is:

```text
Alexa Smart Home skill
  -> Login with Amazon account linking
  -> AWS Lambda (alexa_lambda/index.mjs)
  -> AWS IoT Core
  -> OverKill Smart Hub custom integration / AWS bridge
  -> hub MQTT directive
  -> ESP8266 -> learned IR command(s) -> appliance
```

For this route, **the OverKill Smart Hub custom integration is required**. It publishes the retained Alexa device catalogue to AWS IoT and forwards directives back to the correct hub. The hub must be in **Custom Integration** discovery mode, and the device must have both a category and **Alexa Cloud** exposure enabled. See [AWS, Lambda and Alexa setup](ALEXA_AWS.md) for the full account, certificate and skill steps.

## 3. Alexa cloud device types and supported controls

The table below is based on the committed `alexa_lambda/index.mjs` and the firmware's command-name resolver. It describes capabilities implemented by this version—not a guarantee that every phrasing will be recognized identically in every Alexa locale.

| Firmware device category | Controls handled by the cloud Lambda | Learned commands the hub needs |
|---|---|---|
| **TV** | Power on/off; volume up/down; mute; channel up/down; direct numeric channel selection when Alexa sends a supported channel directive | Prefer separate `Power On` and `Power Off` commands (or a `Power` toggle as a fallback); `Volume Up`, `Volume Down`, `Mute`, `Channel Up`, `Channel Down`, and numeric keys `0`–`9` |
| **SETTOP** | Same cloud controls as TV: power, volume, mute and channel commands | Same learned commands as TV. Choose **SETTOP** for a set-top box. |
| **STREAMER** | Power; volume up/down; mute; Play, Pause, Stop, Next, Previous, Rewind, Fast Forward and Start Over | Power command(s), `Volume Up`, `Volume Down`, `Mute`, and the corresponding playback commands |
| **SPEAKER** | Power; volume up/down; mute | Power command(s), `Volume Up`, `Volume Down`, `Mute`. Playback controls are not enabled for this category in the current Lambda. |
| **FAN** | Power; set/increase/decrease fan speed; oscillation on/off | Power command(s); speed levels such as `Speed 1`–`Speed 6` or numeric buttons `1`–`6`; `Speed Up`/`Speed Down` can be used as fallbacks; `Oscillate On`/`Oscillate Off` or a single `Oscillate` toggle |
| **AC** | Power; set/adjust target temperature in °C; thermostat mode; AC fan speed; swing, turbo and sleep toggles | Power command(s); `Temp Up`, `Temp Down`; `Mode COOL`, `Mode AUTO`, `Mode ECO`; `Fan Speed 1`–`Fan Speed 5` or numeric speed buttons, with `Fan Up`/`Fan Down` as fallbacks; `Swing On`/`Swing Off`, `Turbo On`/`Turbo Off`, and `Sleep On`/`Sleep Off` (or the corresponding single toggle commands) |
| **LIGHT** | Power; set/adjust brightness | Power command(s), plus learned commands matching levels such as `Brightness 25`, `Brightness 50`, `Brightness 100`. A requested brightness value needs a matching learned command; the hub cannot synthesize an arbitrary brightness IR signal. |
| **SWITCH** | Basic power on/off only | Power command(s). Use this for generic devices, plugs or relays without a richer supported type. |

The Lambda also accepts category aliases: `TELEVISION` → `TV`; `STB`/`SETTOPBOX` → `SETTOP`; `AIRCON`/`AIRCONDITIONER` → `AC`; `SOUNDBAR` → `SPEAKER`; `PLUG`, `RELAY` and `GENERIC` → `SWITCH`; `STREAMING` → `STREAMER`.

### Naming and state caveats

- The firmware compares learned command names without regard to letter case, spaces, underscores or hyphens. It also treats “Temperature” as “Temp”. The Lambda may try a preferred command name and then a fallback name. If no learned command matches, that step is skipped and the directive can finish partially.
- If possible, learn distinct **on** and **off** buttons. A single physical power-toggle button is only a fallback; a software state estimate can get out of sync when the appliance is controlled by its original remote.
- TV channel **up/down** is handled as a relative command. Direct channel selection requires numeric digits. The Lambda has a code path for `ChangeChannel`, but Alexa may route some TV/channel utterances away from a Smart Home skill, depending on the setup. Test it in your Alexa locale before relying on it.
- The current integration catalogue forwards basic `id`, `name` and `type` fields; it does **not** automatically pass the Lambda's optional named-channel mapping. Use numeric channel entry and learned digit keys as the dependable configuration for now.
- AC target temperature is an estimate based on the last temperature command sent through the cloud Lambda, not a measurement reported by the air conditioner. Using the physical remote or another control path can make that estimate inaccurate. Current catalogue defaults are 16–30 °C, modes COOL/AUTO/ECO, 6 fan speeds for FAN and 5 for AC unless the catalogue supplies overrides.
- The Lambda deliberately does not expose TV input selection or input-cycle scenes in this version.

## 4. Which path should I choose?

- **Just Home Assistant entities:** choose Native HA MQTT.
- **Simple local Alexa commands as light switches:** enable local Emulated Hue per command. You can use it alongside either Home Assistant discovery mode.
- **A cloud-connected Alexa TV/fan/AC remote:** choose Custom Integration discovery and use the OverKill custom integration plus the AWS IoT/Lambda/Smart Home skill setup.
- **Both Home Assistant and the cloud remote:** use Custom Integration discovery and configure the custom integration. Add local Emulated Hue only for commands you specifically want as separate on/off lights.

For the AWS account setup and step-by-step Lambda/skill linking instructions, continue to [AWS, Lambda and Alexa setup](ALEXA_AWS.md).
