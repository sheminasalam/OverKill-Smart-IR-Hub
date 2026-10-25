# Home Assistant custom integration and HACS release

## Install the Home Assistant custom integration

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

## HACS publication plan for the companion integration

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


## Important behavior found during source review

- A fresh integration setup currently opens the AWS bridge form when no bridge entry exists; it asks for the endpoint and three PEM files before reaching the transport selector. The current first-run path therefore does not offer a straightforward skip-AWS route for a local-only user, even though Local MQTT is one of the later transport choices.
- The Lambda supports a per-device `channels` mapping, but the current bridge catalogue builder sends only `id`, `name` and `type`. Named-channel metadata is therefore not automatically carried through the bridge. Numeric channel selection is the documented dependable route until the catalogue builder is changed and tested.
- The cloud MQTT path in the provided integration is plain MQTT. Do not publish port 1883 openly to the Internet; use a protected network/broker setup and suitable ACLs.
