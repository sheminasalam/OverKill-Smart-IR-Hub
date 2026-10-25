import { IoTDataPlaneClient, PublishCommand, GetRetainedMessageCommand } from "@aws-sdk/client-iot-data-plane";
import { randomUUID } from "crypto";

/*
 * Config-driven Alexa Smart Home skill backend for the OverKill IR hub.
 * Lambda v6 publishes absolute TV channel numbers for Node-RED numeric-key handling and preserves relative channel up/down.
 * The retained catalogue can be minimal: [{id, name, type}].
 * Alexa capabilities default from the device type; optional per-device feature/settings
 * overrides remain supported for devices that need customization.
 * Devices come only from the retained MQTT catalogue (default: alexa/config/devices).
 * There is no hardcoded device fallback.
 * Optional Lambda environment variables:
 *   IOT_REGION, IOT_ENDPOINT, CONFIG_TOPIC, STATE_TOPIC ({id}), CMD_TOPIC ({id}),
 *   STATUS_TOPIC ({hub}), WRITE_STATE ("false" = hub is the only state writer)
 * Channel buttons are sent as logical "Channel Up" / "Channel Down" commands.
 * TV exposes standard power, channel and volume controls only; input selection/scenes are removed.
 * Direct channel selection sends the requested digits as a logical "Channel" command.
 */

const ENV = process.env;
const T = {
  config: ENV.CONFIG_TOPIC || "alexa/config/devices",
  state: ENV.STATE_TOPIC || "alexa/state/{id}",
  cmd: ENV.CMD_TOPIC || "alexa/{hub}/directive",
  status: ENV.STATUS_TOPIC || "alexa/status/{hub}"
};
const WRITE_STATE = ENV.WRITE_STATE !== "false";

const client = new IoTDataPlaneClient({
  region: ENV.IOT_REGION || "eu-west-1",
  endpoint: ENV.IOT_ENDPOINT || "https://a3b9l1ppl6jewh-ats.iot.eu-west-1.amazonaws.com"
});

// ---------- small helpers ----------
const fill = (tpl, vars) => tpl.replace(/\{(\w+)\}/g, (_, k) => vars[k] ?? "");
const clamp = (v, lo, hi) => Math.min(hi, Math.max(lo, v));
const num = (v, d) => (v !== null && v !== undefined && v !== "" && Number.isFinite(Number(v)) ? Number(v) : d);
const now = () => new Date().toISOString();

async function getRetained(topic) {
  try {
    const r = await client.send(new GetRetainedMessageCommand({ topic }));
    const text = Buffer.from(r.payload).toString();
    try { return JSON.parse(text); } catch { return text; }
  } catch (e) {
    if (e.name !== "ResourceNotFoundException") console.error("getRetained failed:", topic, e.name, e.message);
    return null;
  }
}

const pub = (topic, obj, retain = false) =>
  client.send(new PublishCommand({ topic, payload: Buffer.from(JSON.stringify(obj)), qos: 1, retain }));

// ---------- device categories ----------
// Minimal hub catalogues only need id, name and type. The defaults below
// describe the Alexa capabilities for each category. Optional per-device
// features/settings remain supported as overrides for future exceptions.
const CATS = {
  TV:       { display: "TV",               features: { channel: true, volume: true, playback: false } },
  SETTOP:   { display: "TV",               features: { channel: true, volume: true, playback: false } },
  STREAMER: { display: "STREAMING_DEVICE", features: { playback: true, volume: true } },
  SPEAKER:  { display: "SPEAKER",          features: { volume: true, playback: false } },
  FAN:      { display: "FAN",              features: { oscillate: true } },
  // IR AC temperature is handled by relative Temp Up/Temp Down button presses.
  // Keep PowerController off to avoid an unwanted separate TurnOn command.
  AC:       { display: "AIR_CONDITIONER", features: { power: true, swing: true, fanSpeed: true, turbo: true, sleep: true } },
  LIGHT:    { display: "LIGHT",            features: { brightness: true } },
  SWITCH:   { display: "SWITCH",           features: {} }
};
const ALIASES = {
  TELEVISION: "TV", STB: "SETTOP", SETTOPBOX: "SETTOP", AIRCON: "AC", AIRCONDITIONER: "AC",
  SOUNDBAR: "SPEAKER", PLUG: "SWITCH", RELAY: "SWITCH", GENERIC: "SWITCH", STREAMING: "STREAMER"
};
const PLAYBACK_OPS = ["Play", "Pause", "Stop", "Next", "Previous", "Rewind", "FastForward", "StartOver"];

function normalize(raw, hub) {
  if (!raw || typeof raw !== "object") return null;
  const key = String(raw.cat || raw.type || "").toUpperCase().replace(/[^A-Z]/g, "");
  const cat = ALIASES[key] || key;
  const spec = CATS[cat];
  if (!spec || raw.id === undefined || raw.id === null || !String(raw.id).trim()
      || raw.name === undefined || raw.name === null || !String(raw.name).trim()) {
    return null;
  }

  const modes = (Array.isArray(raw.modes) && raw.modes.length ? raw.modes : ["COOL", "AUTO", "ECO"])
    .map((m) => String(m).toUpperCase())
    .filter((m) => ["COOL", "HEAT", "AUTO", "ECO"].includes(m));
  const minTemp = num(raw.minTemp, 16);
  const maxTemp = Math.max(minTemp, num(raw.maxTemp, 30));
  const rawFeatures = { ...(raw.features || {}) };
  // Input selection and input-cycle scenes are intentionally not exposed.
  delete rawFeatures.inputCycle;

  return {
    id: String(raw.id),
    name: String(raw.name),
    cat,
    hub: raw.hub || hub,
    description: raw.description || `IR ${cat.toLowerCase()} remote`,
    display: raw.display || spec.display,
    features: { ...spec.features, ...rawFeatures },
    channels: Object.fromEntries(Object.entries(raw.channels || {}).map(([k, v]) => [k.toLowerCase(), String(v)])),
    speeds: clamp(Math.round(num(raw.speeds, 6)), 1, 10),
    acFanSpeeds: clamp(Math.round(num(raw.acFanSpeeds, 5)), 1, 10),
    volumeStep: clamp(Math.round(num(raw.volumeStep, 3)), 1, 10),
    modes: modes.length ? modes : ["COOL"],
    minTemp,
    maxTemp
  };
}

let cache = { at: 0, list: [] };
async function getDevices(force = false) {
  if (!force && cache.list.length && Date.now() - cache.at < 30000) return cache.list;
  const raw = await getRetained(T.config);
  const arr = Array.isArray(raw) ? raw : Array.isArray(raw?.devices) ? raw.devices : [];
  const hub = Array.isArray(raw) ? undefined : raw?.hub;
  const seen = new Set();
  const list = arr.map((d) => normalize(d, hub)).filter((d) => d && !seen.has(d.id) && seen.add(d.id));
  if (!list.length) console.error("No valid devices found in", T.config);
  console.log("Device catalogue loaded:", JSON.stringify({
    topic: T.config,
    count: list.length,
    devices: list.map((device) => ({ id: device.id, name: device.name, type: device.cat,
      features: device.features, speeds: device.speeds, acFanSpeeds: device.acFanSpeeds }))
  }));
  cache = { at: Date.now(), list };
  return list;
}

async function isOnline(d) {
  if (!d.hub) return true;
  const s = await getRetained(fill(T.status, { hub: d.hub }));
  if (s === null) return true;
  const v = typeof s === "object" ? (s.status ?? s.state ?? (s.online === false ? "offline" : "online")) : s;
  return String(v).trim().toLowerCase() !== "offline";
}

const initialState = (d) => ({
  powerState: "OFF",
  channel: "1",
  muted: false,
  speed: 1,
  oscillate: false,
  acFanSpeed: 1,
  swing: false,
  turbo: false,
  sleep: false,
  brightness: 50,
  targetSetpoint: clamp(24, d.minTemp, d.maxTemp),
  thermostatMode: d.modes[0]
});

// ---------- discovery ----------
const iface = (name, extra = {}) => ({ type: "AlexaInterface", interface: name, version: "3", ...extra });
const withProps = (names, extra = {}) => ({
  properties: { supported: names.map((name) => ({ name })), proactivelyReported: false, retrievable: true },
  ...extra
});

// Text names are provided for the English locales most relevant to this skill.
// Keeping each locale explicit helps Alexa resolve custom AC controls consistently.
const textNames = (...names) => names.flatMap((text) =>
  ["en-GB", "en-IN", "en-US"].map((locale) => ({
    "@type": "text",
    value: { text, locale }
  }))
);

const toggleCapability = (instance, friendlyNames) =>
  iface("Alexa.ToggleController", withProps(["toggleState"], {
    instance,
    capabilityResources: { friendlyNames }
  }));

function capabilities(d) {
  const f = d.features;
  const c = [];
  // ACs expose PowerController only when explicitly enabled in the catalogue.
  // This prevents Alexa from sending a separate TurnOn before thermostat setpoint
  // commands for IR ACs whose SetTemperature command already represents the action.
  if (d.cat !== "AC" || f.power === true) {
    c.push(iface("Alexa.PowerController", withProps(["powerState"])));
  }

  if (f.channel) c.push(iface("Alexa.ChannelController", withProps(["channel"])));
  if (f.volume) c.push(iface("Alexa.StepSpeaker"));
  if (f.playback) c.push(iface("Alexa.PlaybackController", { supportedOperations: PLAYBACK_OPS }));
  if (d.cat === "FAN") {
    c.push(iface("Alexa.RangeController", withProps(["rangeValue"], {
      instance: "Fan.Speed",
      capabilityResources: { friendlyNames: [{ "@type": "asset", value: { assetId: "Alexa.Setting.FanSpeed" } }] },
      configuration: { supportedRange: { minimumValue: 1, maximumValue: d.speeds, precision: 1 } }
    })));
    if (f.oscillate) {
      c.push(iface("Alexa.ToggleController", withProps(["toggleState"], {
        instance: "Fan.Oscillate",
        capabilityResources: { friendlyNames: [{ "@type": "asset", value: { assetId: "Alexa.Setting.Oscillate" } }] }
      })));
    }
  }
  if (d.cat === "AC") {
    c.push(iface("Alexa.ThermostatController", withProps(["targetSetpoint", "thermostatMode"], {
      configuration: { supportsScales: ["CELSIUS"], supportedModes: d.modes }
    })));

    if (f.swing) {
      c.push(toggleCapability("AC.Swing", [
        { "@type": "asset", value: { assetId: "Alexa.Setting.Oscillate" } },
        ...textNames("swing", "air swing", "swing mode")
      ]));
    }
    if (f.turbo) {
      c.push(toggleCapability("AC.Turbo", textNames("turbo", "turbo mode")));
    }
    if (f.sleep) {
      c.push(toggleCapability("AC.Sleep", textNames("sleep", "sleep mode")));
    }
    if (f.fanSpeed) {
      c.push(iface("Alexa.RangeController", withProps(["rangeValue"], {
        instance: "AC.FanSpeed",
        capabilityResources: {
          friendlyNames: [
            { "@type": "asset", value: { assetId: "Alexa.Setting.FanSpeed" } },
            ...textNames("AC fan speed", "air conditioner fan speed")
          ]
        },
        configuration: {
          supportedRange: { minimumValue: 1, maximumValue: d.acFanSpeeds, precision: 1 }
        }
      })));
    }
  }
  if (f.brightness) c.push(iface("Alexa.BrightnessController", withProps(["brightness"])));

  c.push(iface("Alexa.EndpointHealth", withProps(["connectivity"])));
  c.push(iface("Alexa"));
  return c;
}

function discoverResponse(devices) {
  const deviceEndpoints = devices.map((d) => ({
    endpointId: d.id,
    manufacturerName: "OverKill",
    friendlyName: d.name,
    description: d.description,
    displayCategories: [d.display],
    cookie: {},
    capabilities: capabilities(d)
  }));
  return {
    event: {
      header: { namespace: "Alexa.Discovery", name: "Discover.Response", payloadVersion: "3", messageId: randomUUID() },
      payload: { endpoints: deviceEndpoints }
    }
  };
}

// ---------- state + responses ----------
function stateProps(d, st, online = true) {
  const t = now();
  const p = (namespace, name, value, instance, unc = 500) => ({
    namespace,
    ...(instance ? { instance } : {}),
    name,
    value,
    timeOfSample: t,
    uncertaintyInMilliseconds: unc
  });
  const f = d.features;
  const out = [
    p("Alexa.EndpointHealth", "connectivity", { value: online ? "OK" : "UNREACHABLE" }, null, 0)
  ];
  if (d.cat !== "AC" || f.power === true) {
    out.push(p("Alexa.PowerController", "powerState", st.powerState));
  }
  if (f.channel) out.push(p("Alexa.ChannelController", "channel", { number: String(st.channel) }));
  if (d.cat === "FAN") {
    out.push(p("Alexa.RangeController", "rangeValue", clamp(Math.round(num(st.speed, 1)), 1, d.speeds), "Fan.Speed"));
    if (f.oscillate) out.push(p("Alexa.ToggleController", "toggleState", st.oscillate ? "ON" : "OFF", "Fan.Oscillate"));
  }
  if (d.cat === "AC") {
    out.push(p("Alexa.ThermostatController", "targetSetpoint", { value: clamp(num(st.targetSetpoint, 24), d.minTemp, d.maxTemp), scale: "CELSIUS" }));
    out.push(p("Alexa.ThermostatController", "thermostatMode", d.modes.includes(st.thermostatMode) ? st.thermostatMode : d.modes[0]));
    if (f.swing) out.push(p("Alexa.ToggleController", "toggleState", st.swing ? "ON" : "OFF", "AC.Swing"));
    if (f.turbo) out.push(p("Alexa.ToggleController", "toggleState", st.turbo ? "ON" : "OFF", "AC.Turbo"));
    if (f.sleep) out.push(p("Alexa.ToggleController", "toggleState", st.sleep ? "ON" : "OFF", "AC.Sleep"));
    if (f.fanSpeed) {
      out.push(p("Alexa.RangeController", "rangeValue", clamp(Math.round(num(st.acFanSpeed, 1)), 1, d.acFanSpeeds), "AC.FanSpeed"));
    }
  }
  if (f.brightness) out.push(p("Alexa.BrightnessController", "brightness", clamp(Math.round(num(st.brightness, 50)), 0, 100)));
  return out;
}

function reply(directive, name, properties) {
  const r = {
    event: {
      header: {
        namespace: "Alexa",
        name,
        messageId: randomUUID(),
        correlationToken: directive.header.correlationToken,
        payloadVersion: "3"
      },
      endpoint: { endpointId: directive.endpoint.endpointId },
      payload: {}
    }
  };
  if (properties) r.context = { properties };
  return r;
}

const fail = (directive, type, message) => ({
  event: {
    header: {
      namespace: "Alexa",
      name: "ErrorResponse",
      messageId: randomUUID(),
      correlationToken: directive.header.correlationToken,
      payloadVersion: "3"
    },
    endpoint: { endpointId: directive.endpoint?.endpointId },
    payload: { type, message }
  }
});

// ---------- directive -> hub command + state patch ----------
// The IR AC remote has temperature UP/DOWN buttons, not an absolute setpoint
// command. The retained targetSetpoint is therefore an estimate of the last
// temperature commanded through Alexa, not a sensor reading from the AC.
function planTemperatureButtons(d, st, requestedTarget) {
  const current = clamp(Math.round(num(st.targetSetpoint, 24)), d.minTemp, d.maxTemp);
  const target = clamp(Math.round(num(requestedTarget, current)), d.minTemp, d.maxTemp);
  const delta = target - current;
  return {
    cmd: delta === 0 ? null : {
      type: delta > 0 ? "Temp Up" : "Temp Down",
      steps: Math.abs(delta)
    },
    patch: {
      targetSetpoint: target,
      ...(delta !== 0 ? { powerState: "ON" } : {})
    }
  };
}

// Hub message: "do" = steps separated by ';', alternatives by '|', repeat as '*N'.
const rep = (name, n) => (n > 1 ? `${name}*${n}` : name);
const withFallback = (exact, up, down, delta) =>
  delta === 0 ? exact : `${exact}|${rep(delta > 0 ? up : down, Math.abs(delta))}`;

function toDo(c, st, d) {
  switch (c.type) {
    case "Power": case "Swing": case "Turbo": case "Sleep": case "Oscillate":
      return `${c.type} ${c.state === "ON" ? "On" : "Off"}|${c.type}`;
    case "Mute": return "Mute";
    case "Volume": return rep(c.steps < 0 ? "Volume Down" : "Volume Up", Math.abs(c.steps));
    case "Temp Up": case "Temp Down": case "Channel Up": case "Channel Down":
      return rep(c.type, c.steps || 1);
    case "Channel": return String(c.number).split("").join(";");
    case "FanSpeed": {
      const delta = c.value - clamp(Math.round(num(st.speed, 1)), 1, d.speeds);
      return withFallback(`Speed ${c.value}`, "Speed Up", "Speed Down", delta);
    }
    case "ACFanSpeed": {
      const delta = c.value - clamp(Math.round(num(st.acFanSpeed, 1)), 1, d.acFanSpeeds);
      return withFallback(`Fan Speed ${c.value}`, "Fan Up", "Fan Down", delta);
    }
    case "Playback": return c.op;                 // Play, Pause, Stop, Next, Previous, Rewind, FastForward, StartOver
    case "SetMode": return `Mode ${c.mode}`;
    case "Brightness": return `Brightness ${c.value}`;
    default: return "";
  }
}

function plan(d, st, header, pl) {
  const f = d.features;
  const bad = (message) => ({ error: { type: "INVALID_VALUE", message } });
  const key = `${header.namespace}.${header.name}`;

  if (header.namespace === "Alexa.PlaybackController" && PLAYBACK_OPS.includes(header.name)) {
    return f.playback ? { cmd: { type: "Playback", op: header.name }, patch: {} } : null;
  }

  switch (key) {
    case "Alexa.PowerController.TurnOn":
    case "Alexa.PowerController.TurnOff": {
      if (d.cat === "AC" && f.power !== true) return null;
      const state = header.name === "TurnOn" ? "ON" : "OFF";
      return { cmd: { type: "Power", state }, patch: { powerState: state } };
    }

    case "Alexa.ChannelController.ChangeChannel": {
      if (!f.channel) return null;

      // Direct channel selection must be absolute. Do not derive Channel Up/Down
      // presses from st.channel because the user may have changed channels with
      // the physical remote, making Lambda's retained state inaccurate.
      // Send the requested digits to Node-RED, which maps them to the learned
      // numeric IR keys. SkipChannels below remains relative.
      //
      const name = String(pl.channelMetadata?.name || "").trim().toLowerCase();
      const sign = String(pl.channel?.callSign || pl.channel?.affiliateCallSign || "").trim().toLowerCase();
      const ch = pl.channel?.number ?? d.channels[name] ?? d.channels[sign];
      if (ch === undefined || ch === null || String(ch).trim() === "") {
        return bad("A numeric channel number is required for this IR TV");
      }

      const targetText = String(ch).trim();
      if (!/^\d+$/.test(targetText)) {
        return bad("This TV accepts integer channel numbers only; decimal channels need a numeric-key mapping");
      }
      const target = Number(targetText);
      if (!Number.isSafeInteger(target) || target < 1 || target > 999) {
        return bad("Channel number must be an integer between 1 and 999");
      }

      return {
        cmd: { type: "Channel", number: targetText },
        patch: { channel: targetText }
      };
    }
    case "Alexa.ChannelController.SkipChannels": {
      if (!f.channel) return null;
      // Alexa normally sends +1 or -1. Clamp defensively in case a test client
      // sends a larger count, and publish learned-button names rather than the
      // abstract ChannelStep operation which a simple IR bridge cannot execute.
      const requestedSteps = Math.round(num(pl.channelCount, 1));
      const steps = clamp(requestedSteps, -20, 20);
      if (steps === 0) return bad("Channel count must be non-zero");
      const current = Math.max(1, parseInt(st.channel, 10) || 1);
      const nextChannel = String(Math.max(1, current + steps));
      return {
        cmd: {
          type: steps > 0 ? "Channel Up" : "Channel Down",
          steps: Math.abs(steps)
        },
        patch: { channel: nextChannel }
      };
    }

    case "Alexa.StepSpeaker.AdjustVolume": {
      if (!f.volume) return null;
      const dir = num(pl.volumeSteps, 1) < 0 ? -1 : 1;
      const n = pl.volumeStepsDefault ? d.volumeStep : clamp(Math.abs(Math.round(num(pl.volumeSteps, 1))), 1, 30);
      return { cmd: { type: "Volume", steps: dir * n }, patch: {} };
    }
    case "Alexa.StepSpeaker.SetMute": {
      if (!f.volume) return null;
      const mute = !!pl.mute;
      return { cmd: { type: "Mute", mute }, patch: { muted: mute } };
    }

    case "Alexa.RangeController.SetRangeValue":
    case "Alexa.RangeController.AdjustRangeValue": {
      if (d.cat === "AC" && f.fanSpeed && header.instance === "AC.FanSpeed") {
        const delta = pl.rangeValueDeltaDefault
          ? Math.sign(num(pl.rangeValueDelta, 1)) || 1
          : num(pl.rangeValueDelta, 0);
        const raw = header.name === "SetRangeValue"
          ? num(pl.rangeValue, st.acFanSpeed)
          : num(st.acFanSpeed, 1) + delta;
        const value = clamp(Math.round(raw), 1, d.acFanSpeeds);
        return { cmd: { type: "ACFanSpeed", value }, patch: { acFanSpeed: value, powerState: "ON" } };
      }

      if (d.cat !== "FAN" || header.instance !== "Fan.Speed") return null;
      const delta = pl.rangeValueDeltaDefault
        ? Math.sign(num(pl.rangeValueDelta, 1)) || 1
        : num(pl.rangeValueDelta, 0);
      const raw = header.name === "SetRangeValue" ? num(pl.rangeValue, st.speed) : num(st.speed, 1) + delta;
      const value = clamp(Math.round(raw), 1, d.speeds);
      return { cmd: { type: "FanSpeed", value }, patch: { speed: value, powerState: "ON" } };
    }

    case "Alexa.ToggleController.TurnOn":
    case "Alexa.ToggleController.TurnOff": {
      const on = header.name === "TurnOn";
      const state = on ? "ON" : "OFF";

      if (d.cat === "AC") {
        if (f.swing && header.instance === "AC.Swing") {
          return { cmd: { type: "Swing", state }, patch: { swing: on } };
        }
        if (f.turbo && header.instance === "AC.Turbo") {
          return { cmd: { type: "Turbo", state }, patch: { turbo: on } };
        }
        if (f.sleep && header.instance === "AC.Sleep") {
          return { cmd: { type: "Sleep", state }, patch: { sleep: on } };
        }
      }

      if (d.cat === "FAN" && f.oscillate && header.instance === "Fan.Oscillate") {
        return { cmd: { type: "Oscillate", state }, patch: { oscillate: on } };
      }
      return null;
    }
    case "Alexa.ThermostatController.SetTargetTemperature": {
      if (d.cat !== "AC") return null;
      // Best-effort absolute setpoint: compare against the last setpoint saved by
      // this Lambda and press Temp Up/Temp Down the required number of times.
      return planTemperatureButtons(d, st, num(pl.targetSetpoint?.value, st.targetSetpoint));
    }
    case "Alexa.ThermostatController.AdjustTargetTemperature": {
      if (d.cat !== "AC") return null;
      // Native Alexa relative adjustments map directly to the IR's temperature buttons.
      const current = clamp(Math.round(num(st.targetSetpoint, 24)), d.minTemp, d.maxTemp);
      const delta = num(pl.targetSetpointDelta?.value, 0);
      return planTemperatureButtons(d, st, current + delta);
    }
    case "Alexa.ThermostatController.SetThermostatMode": {
      if (d.cat !== "AC") return null;
      const m = String(pl.thermostatMode?.value ?? pl.thermostatMode ?? "").toUpperCase();
      if (!d.modes.includes(m)) return bad("Unsupported mode");
      return { cmd: { type: "SetMode", mode: m }, patch: { thermostatMode: m } };
    }

    case "Alexa.BrightnessController.SetBrightness":
    case "Alexa.BrightnessController.AdjustBrightness": {
      if (!f.brightness) return null;
      const delta = header.name === "AdjustBrightness" ? num(pl.brightnessDelta, 0) : null;
      const raw = delta === null ? num(pl.brightness, st.brightness) : num(st.brightness, 50) + delta;
      const v = clamp(Math.round(raw), 0, 100);
      return {
        cmd: { type: "Brightness", value: v, ...(delta !== null ? { delta } : {}) },
        patch: { brightness: v, ...(v > 0 ? { powerState: "ON" } : {}) }
      };
    }

    default:
      return null;
  }
}

// ---------- handler ----------
export const handler = async (event) => {
  // Discovery and AcceptGrant requests do not require an endpoint object.
  // Validate the header first; endpoint-specific directives are checked later.
  if (!event?.directive?.header) {
    console.error("Malformed Lambda event: expected event.directive.header");
    throw new TypeError("Expected Alexa Smart Home event with directive.header");
  }
  const directive = event.directive;
  const { header } = directive;
  const payload = directive.payload || {};
  const { scope, ...safePayload } = payload; // never log the bearer token
  console.log("Directive:", JSON.stringify({
    namespace: header.namespace,
    name: header.name,
    instance: header.instance,
    endpointId: directive.endpoint?.endpointId,
    payload: safePayload
  }));

  if (header.namespace === "Alexa.Authorization" && header.name === "AcceptGrant") {
    return {
      event: {
        header: { namespace: "Alexa.Authorization", name: "AcceptGrant.Response", messageId: randomUUID(), payloadVersion: "3" },
        payload: {}
      }
    };
  }

  if (header.namespace === "Alexa.Discovery" && header.name === "Discover") {
    const devices = await getDevices(true);
    console.log("Discovery capabilities:", JSON.stringify({
      devices: devices.map((device) => ({
        id: device.id,
        type: device.cat,
        capabilities: capabilities(device).map((capability) => ({ interface: capability.interface, instance: capability.instance }))
      })),
    }));
    return discoverResponse(devices);
  }

  if (!directive.endpoint) {
    console.error("Malformed endpoint-specific directive: missing directive.endpoint");
    throw new TypeError("Expected endpoint-specific Alexa directive with directive.endpoint");
  }

  const id = directive.endpoint.endpointId;
  const devices = await getDevices();

  let dev = devices.find((x) => x.id === id);
  if (!dev) dev = (await getDevices(true)).find((x) => x.id === id);
  if (!dev) return fail(directive, "NO_SUCH_ENDPOINT", "Unknown device");

  const [saved, online] = await Promise.all([getRetained(fill(T.state, { id: dev.id })), isOnline(dev)]);
  const st = { ...initialState(dev), ...(saved && typeof saved === "object" ? saved : {}) };

  if (header.namespace === "Alexa" && header.name === "ReportState") {
    return reply(directive, "StateReport", stateProps(dev, st, online));
  }
  if (!online) return fail(directive, "ENDPOINT_UNREACHABLE", "Hub is offline");

  const p = plan(dev, st, header, payload);
  if (!p) {
    console.log("Unsupported directive:", header.namespace, header.name, header.instance || "");
    return fail(directive, "INVALID_DIRECTIVE", "Unsupported directive");
  }
  if (p.error) return fail(directive, p.error.type, p.error.message);

  const next = { ...st, ...p.patch };
  try {
    // Preserve the working baseline MQTT command contract: command fields plus
    // deviceId only. State is published separately to the retained state topic.
    if (p.cmd) {
      const command = { id: header.messageId, dev: dev.id, do: toDo(p.cmd, st, dev) };
      const topic = fill(T.cmd, { id: dev.id, hub: String(dev.id).split("_")[0] });
      await pub(topic, command);
      console.log("Published", JSON.stringify({ topic, command }));
    } else {
      console.log("No IR button press required", JSON.stringify({ endpointId: dev.id, namespace: header.namespace, name: header.name }));
    }
    if (WRITE_STATE) await pub(fill(T.state, { id: dev.id }), next, true);
  } catch (err) {
    console.error("IoT publish failed:", err);
    return fail(directive, "ENDPOINT_UNREACHABLE", "Publish failed");
  }
  return reply(directive, "Response", stateProps(dev, next, true));
};