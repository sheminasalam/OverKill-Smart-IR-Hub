# Home Assistant integration and HACS preparation

The submitted integration ZIP identifies the integration domain as overkill_smart_hub, manifest version 1.3.0. The integration source is bundled in the local release draft ZIP, under repo/home_assistant/custom_components/overkill_smart_hub/; it is not yet installed as a standalone HACS repository.

## Configuration modes

- **Local MQTT** — uses Home Assistant's configured MQTT integration.
- **Cloud MQTT** — connects to a broker configured in the integration, intended for hubs at a remote location.
- **Both** — configure the local and cloud transport paths.

The config flow supports independent AWS IoT bridge entries and explicit hub-to-bridge assignment. Choose the intended Alexa bridge when confirming each hub; keep local-only hubs unassigned. The implementation subscribes to hub Alexa configuration under <topic-prefix>/_system/alexa/#, aggregates retained device catalogue messages and mirrors the catalogue to AWS IoT. The AWS bridge subscribes to alexa/+/directive by default. Service overkill_smart_hub.rediscover_hubs requests rediscovery.

The local broker path uses Home Assistant's MQTT integration. The cloud MQTT runtime uses plain MQTT, as indicated in the supplied implementation. Do not expose an unauthenticated plain-MQTT broker directly to the internet; use strong credentials, restrictive ACLs and an appropriately protected network/VPN. The AWS bridge uses mutual TLS with a CA, client certificate and matching private key.

## Local install

1. Copy overkill_smart_hub to /config/custom_components/overkill_smart_hub/.
2. Configure Home Assistant's MQTT integration first if using local transport.
3. Restart Home Assistant.
4. Add OverKill Smart Hub from Settings → Devices & services and follow the bridge/transport/hub flow.
5. Check Home Assistant logs for MQTT discovery, broker and AWS connection messages.

## HACS plan

Current HACS guidance requires one integration per repository under custom_components/<domain>/, a root README, root hacs.json, a valid integration manifest, passing HACS and hassfest GitHub Actions, and a GitHub release before requesting inclusion in HACS default. This combined firmware repository should therefore stay as the hardware/firmware project; publish the integration as a separate repository for a clean HACS submission. Version in manifest.json should match the release tag. See the official [HACS integration publishing guide](https://www.hacs.dev/docs/publish/integration/) and [default submission guide](https://www.hacs.dev/docs/publish/include/).

The integration ZIP provided here contains Python __pycache__ bytecode; it is intentionally excluded from a clean source release. The integration has not yet passed a current HACS/hassfest or clean Home Assistant test run.
