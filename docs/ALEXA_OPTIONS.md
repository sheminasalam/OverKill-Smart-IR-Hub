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

## 3. Alexa cloud device types and exact learned command names

**The command names matter.** For Alexa cloud control, the Lambda translates an Alexa directive into one or more command names and the firmware looks for those names **inside the same learned device/remote**. Exposing a device to Alexa Cloud is not enough by itself: learn the buttons the device type needs, then name them according to the reference below.

### Command-name reference

| Cloud device category | Alexa action | Learned command name(s) to use in the firmware |
|---|---|---|
| **All categories with power** (TV, SETTOP, STREAMER, SPEAKER, FAN, AC, LIGHT, SWITCH) | Turn on | Prefer **`Power On`**. If the appliance only has one power toggle, **`Power`** is accepted as a fallback. |
| **All categories with power** | Turn off | Prefer **`Power Off`**. If the appliance only has one power toggle, **`Power`** is accepted as a fallback. A single toggle is less reliable for state because the firmware cannot know whether the appliance is already on. |
| **TV / SETTOP** | Increase/decrease volume | **`Volume Up`**, **`Volume Down`** |
| **TV / SETTOP** | Mute | **`Mute`** (see the important mute note below) |
| **TV / SETTOP** | Next/previous channel | **`Channel Up`**, **`Channel Down`** |
| **TV / SETTOP** | Select a numeric channel | Learn numeric buttons **`0`–`9`** on that TV/set-top-box device. The Lambda sends each digit as a separate step, e.g. channel 125 becomes `1`, `2`, `5`. Alexa must actually deliver a supported `ChangeChannel` directive for direct selection; voice routing varies, so test this in your locale. |
| **STREAMER** | Volume/mute | **`Volume Up`**, **`Volume Down`**, **`Mute`** |
| **STREAMER** | Playback | **`Play`**, **`Pause`**, **`Stop`**, **`Next`**, **`Previous`**, **`Rewind`**, **`Fast Forward`**, **`Start Over`** |
| **SPEAKER** | Volume/mute | **`Volume Up`**, **`Volume Down`**, **`Mute`**. Playback controls are not enabled for this category in the current Lambda. |
| **FAN** | Set fan speed | Prefer speed-level commands named **`Speed 1`** through **`Speed 6`** (the current default range). The firmware also tries the numeric command **`1`–`6`** if a matching `Speed N` command is missing. |
| **FAN** | Increase/decrease fan speed | **`Speed Up`**, **`Speed Down`** are the fallback buttons when the exact requested speed level is not available. |
| **FAN** | Oscillation on/off | Prefer **`Oscillate On`** and **`Oscillate Off`**. A single **`Oscillate`** toggle command is accepted as a fallback. |
| **AC** | Adjust target temperature | **`Temp Up`**, **`Temp Down`**. The Lambda repeats the relevant learned button to move from its saved temperature estimate to the requested setpoint. |
| **AC** | Set operating mode | **`Mode COOL`**, **`Mode AUTO`**, **`Mode ECO`**. These are the current default modes supplied to the cloud Lambda. A mode will only work if it is allowed by the device catalogue and its corresponding command has been learned. |
| **AC** | Set AC fan speed | Prefer **`Fan Speed 1`** through **`Fan Speed 5`** (the current default range). The firmware tries numeric commands **`1`–`5`** if the matching `Fan Speed N` command is missing. |
| **AC** | Increase/decrease AC fan speed | **`Fan Up`**, **`Fan Down`** are the fallback buttons when the exact requested fan-speed command is not available. |
| **AC** | Swing on/off | Prefer **`Swing On`** and **`Swing Off`**. A single **`Swing`** toggle command is accepted as a fallback. |
| **AC** | Turbo on/off | Prefer **`Turbo On`** and **`Turbo Off`**. A single **`Turbo`** toggle command is accepted as a fallback. |
| **AC** | Sleep on/off | Prefer **`Sleep On`** and **`Sleep Off`**. A single **`Sleep`** toggle command is accepted as a fallback. |
| **LIGHT** | Set/adjust brightness | Learn commands named for the brightness levels you expect to use, e.g. **`Brightness 0`**, **`Brightness 25`**, **`Brightness 50`**, **`Brightness 75`**, **`Brightness 100`**. The requested numeric value must match a learned command; this Lambda does not translate an arbitrary brightness value into an approximate learned level. |
| **SWITCH** | Basic on/off | Power commands only: **`Power On`**, **`Power Off`**, or **`Power`** as the single-toggle fallback. |

### Rules the firmware uses when matching names

- Matching is **case-insensitive** and removes spaces, underscores, hyphens and other punctuation. For example, `Fast Forward`, `Fast_Forward` and `FastForward` resolve to the same normalized name.
- The word **`Temperature`** is normalized to **`Temp`**, so `Temperature Up` can match the Lambda's `Temp Up` request.
- The Lambda can provide alternatives such as **`Power On|Power`**. The firmware tries the alternatives in order and transmits the first matching learned command. Therefore, separate **On** and **Off** commands are preferred; a single **Power** command is only a toggle fallback.
- Repeated commands use the same learned key multiple times. Direct channel entry uses the learned digit keys in sequence.
- Names must belong to the **same device entry** you have selected for Alexa Cloud. A `Volume Up` command learned under the TV will not be found for a separate set-top-box device.

**Mute caveat:** in the current Lambda, both Alexa mute and unmute directives become the single learned **`Mute`** command. Most IR remotes implement Mute as a toggle, so Alexa's expected mute state can get out of sync. This implementation does not send separate `Mute On` and `Mute Off` names.

### Example voice requests for the cloud route

These are example request patterns for the capabilities above, not a guarantee that every phrase will be routed to this Smart Home skill in every Alexa locale. Use the device's friendly name in place of the bracketed name.

| Device category | Example requests |
|---|---|
| **TV / SETTOP** | “Alexa, turn on [TV]”; “Alexa, volume up on [TV]”; “Alexa, mute [TV]”; “Alexa, next channel on [TV]”. Direct requests such as “change the channel to 12” depend on Alexa routing a `ChangeChannel` directive to the Smart Home skill; test this before relying on it. |
| **STREAMER** | “Alexa, turn on [streamer]”; “Alexa, play [streamer]”; “Alexa, pause [streamer]”; “Alexa, skip to the next item on [streamer]”. |
| **SPEAKER** | “Alexa, turn on [speaker]”; “Alexa, turn the volume down on [speaker]”; “Alexa, mute [speaker]”. |
| **FAN** | “Alexa, turn on [fan]”; “Alexa, set [fan] speed to 3”; “Alexa, turn on oscillation on [fan]”. |
| **AC** | “Alexa, set [AC] to 24 degrees”; “Alexa, increase [AC] temperature”; “Alexa, set [AC] mode to cool”; “Alexa, set [AC] fan speed to 3”; “Alexa, turn on turbo mode on [AC]”. The reported setpoint is estimated from commands, not sensed from the AC. |
| **LIGHT** | “Alexa, turn on [light]”; “Alexa, set [light] brightness to 50 percent”. Brightness requests work only when that matching `Brightness N` command is learned. |
| **SWITCH** | “Alexa, turn on [switch]”; “Alexa, turn off [switch]”. |

For **local Emulated Hue**, the simpler pattern is “Alexa, turn on [device name + command name]”, for example “turn on Bedroom AC Temp Up”. That activates the individual learned command as a momentary switch; it is not the cloud Smart Home remote and does not require the special cloud command-name mapping above.

The Lambda also accepts category aliases: `TELEVISION` → `TV`; `STB`/`SETTOPBOX` → `SETTOP`; `AIRCON`/`AIRCONDITIONER` → `AC`; `SOUNDBAR` → `SPEAKER`; `PLUG`, `RELAY` and `GENERIC` → `SWITCH`; `STREAMING` → `STREAMER`.

### Additional limitations

- TV channel **up/down** is handled as a relative command. Direct channel selection requires numeric digits. The Lambda has a code path for `ChangeChannel`, but Alexa may route some TV/channel utterances away from a Smart Home skill, depending on the setup. Test it in your Alexa locale before relying on it.
- The current integration catalogue forwards basic `id`, `name` and `type` fields; it does **not** automatically pass the Lambda's optional named-channel mapping. Use numeric channel entry and learned digit keys as the dependable configuration for now.
- AC target temperature is an estimate based on the last temperature command sent through the cloud Lambda, not a measurement reported by the air conditioner. Using the physical remote or another control path can make that estimate inaccurate.
- Current catalogue defaults are 16–30 °C, modes COOL/AUTO/ECO, 6 fan speeds for FAN and 5 for AC.
- The Lambda deliberately does not expose TV input selection or input-cycle scenes in this version.

## 4. Which path should I choose?

- **Just Home Assistant entities:** choose Native HA MQTT.
- **Simple local Alexa commands as light switches:** enable local Emulated Hue per command. You can use it alongside either Home Assistant discovery mode.
- **A cloud-connected Alexa TV/fan/AC remote:** choose Custom Integration discovery and use the OverKill custom integration plus the AWS IoT/Lambda/Smart Home skill setup.
- **Both Home Assistant and the cloud remote:** use Custom Integration discovery and configure the custom integration. Add local Emulated Hue only for commands you specifically want as separate on/off lights.

For the AWS account setup and step-by-step Lambda/skill linking instructions, continue to [AWS, Lambda and Alexa setup](ALEXA_AWS.md).
