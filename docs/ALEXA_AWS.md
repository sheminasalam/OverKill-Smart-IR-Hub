# AWS IoT, Lambda, account linking and Alexa setup

## Create the AWS IoT Core bridge certificate

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
      "Resource": "arn:aws:iot:REGION:ACCOUNT_ID:topicfilter/alexa/+/directive"
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

For SUBSCRIBE, the policy ARN must match the exact MQTT filter, including the literal single-level wildcard token. For RECEIVE, `????` matches the four-character hub tag in an actual topic. If the deployment uses custom topics or client IDs, change the ARNs to match.

## Configure the AWS bridge in Home Assistant

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

## Deploy the updated Alexa Lambda

The Lambda source in this repository is the updated file provided by the project owner. The currently committed version **requires** the `IOT_ENDPOINT` environment variable; it no longer silently falls back to a fixed endpoint embedded in the source.

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

## Connect the Smart Home skill, Lambda and account linking — three copy/paste steps

This setup involves **three separate console objects**:

1. **Alexa Smart Home skill** — created in the Alexa Developer Console.
2. **AWS Lambda function** — runs `alexa_lambda/index.mjs`.
3. **Login with Amazon (LWA) security profile** — supplies the client ID/secret used by the skill's account-linking settings.

These are linked by copying a few console-generated values between pages. You do **not** need to search-and-replace user-specific values inside `index.mjs`. The Lambda's required environment setting is `IOT_ENDPOINT`; the skill ID, Lambda ARN and LWA credentials belong in the consoles described below.

### Before you start: keep this value sheet

Keep this in a private local text file while configuring the service. Fill in each value once, then copy it to the destination described in the three steps. Do not commit this file to GitHub.

| Placeholder | Where to get it | Where to paste it |
|---|---|---|
| `AWS_REGION` | AWS region used for the IoT endpoint and Lambda. For the project's English (India) skill setup, use `eu-west-1`; if using another Alexa locale, follow Amazon's current region mapping. | AWS Lambda region selector and `IOT_REGION` if you set it explicitly. |
| `IOT_ENDPOINT` | AWS IoT Core → Settings / device data endpoint. Copy your account's endpoint hostname. | AWS Lambda → Configuration → Environment variables → `IOT_ENDPOINT`. It may be the hostname or hostname prefixed with `https://`. |
| `SKILL_ID` | Alexa Developer Console → your Smart Home skill → Smart Home service endpoint → **Skill ID**. It usually begins with `amzn1.ask.skill.`. | AWS Lambda → Add trigger → Alexa Smart Home → **Skill ID**. This is a trigger/permission setting, **not** a Lambda environment variable. |
| `LAMBDA_ARN` | AWS Lambda → your function → Function overview → **ARN**. | Alexa Developer Console → Smart Home → **Smart Home service endpoint**. Paste it into Default endpoint and the regional endpoint for your skill's locale. |
| `LWA_CLIENT_ID` | Login with Amazon → your security profile → Client ID. | Alexa Developer Console → Account Linking → Your Client ID. |
| `LWA_CLIENT_SECRET` | Login with Amazon → same security profile → Client Secret. | Alexa Developer Console → Account Linking → Your Secret. Treat this as a password; don't put it in GitHub, the README, screenshots or Lambda environment variables. |
| `ALEXA_REDIRECT_URLS` | Alexa Developer Console → Account Linking, after you save the configuration. Copy **every** redirect URL shown. | Login with Amazon → security profile → Web Settings → Allowed Return URLs. Add every URL exactly as shown. |

Keep the AWS IoT X.509 certificate/private key separate from the LWA Client ID/Secret: they authenticate different connections and are not interchangeable.

### Step 1 of 3 — Create the Alexa Smart Home skill and copy its ID

1. Open the [Alexa Developer Console](https://developer.amazon.com/alexa/console/ask).
2. Choose **Create Skill** and give it a name such as `OverKill Smart IR Hub`.
3. Choose the intended primary locale. Select **Smart Home** as the experience/type and model, and choose the option that lets you provide your own hosting (wording may vary in the console).
4. Create the skill.
5. Open the **Smart Home** page and copy the **Skill ID** into your private value sheet as `SKILL_ID`. Do not type or guess this ID.
6. Leave the Smart Home service endpoint page open—you will paste the Lambda ARN there in Step 2.

### Step 2 of 3 — Connect the skill to your Lambda

The Lambda package should already be deployed using the earlier section of this guide, with its IAM role and required environment variable `IOT_ENDPOINT` configured.

1. In the [AWS Lambda Console](https://console.aws.amazon.com/lambda/), open your deployed OverKill Lambda. Confirm it is in the region that matches the chosen Alexa locale and AWS IoT endpoint. For the project's English (India) setup this is `eu-west-1`.
2. Choose **Add trigger**, select **Alexa Smart Home** (not a custom Alexa Skills Kit trigger if the console offers separate trigger types), and paste the `SKILL_ID` from Step 1 into the **Skill ID** field. Save the trigger. This restricts invocation to the intended skill.
3. Copy the function's **ARN** from Function overview into your private value sheet as `LAMBDA_ARN`. It looks similar to this example; your account ID and function name will differ:

   ```text
   arn:aws:lambda:eu-west-1:123456789012:function:OverKillSmartHome
   ```

   The example is a format illustration only—**do not use this example ARN**.
4. Return to the Alexa Developer Console → your skill → **Smart Home** → **Smart Home service endpoint**.
5. Paste your actual `LAMBDA_ARN` into **Default endpoint** and the endpoint for the region matching your skill's locale. When the skill uses only one region, Amazon's guidance is to use the same ARN for the default and selected regional endpoint.
6. Save the skill. Recheck that the Lambda trigger uses the correct `SKILL_ID` and that the skill endpoint uses the correct `LAMBDA_ARN`. These values are related but are **not the same value**.

### Step 3 of 3 — Configure Login with Amazon account linking

Smart Home skills require account linking. The LWA security profile and the Smart Home skill are separate objects; this step connects them.

1. In the [Login with Amazon console](https://developer.amazon.com/loginwithamazon), create a **Security Profile** for OverKill. Record its **Client ID** and **Client Secret** privately as `LWA_CLIENT_ID` and `LWA_CLIENT_SECRET`.
2. Return to the Alexa Developer Console → your Smart Home skill → **Account Linking** → **Setup Account Linking**.
3. Choose **Authorization Code Grant**.
4. Enter these fixed LWA endpoints exactly:

   ```text
   Web Authorization URI: https://www.amazon.com/ap/oa
   Access Token URI:      https://api.amazon.com/auth/o2/token
   Authentication Scheme: HTTP Basic
   Scope:                 profile:user_id
   ```

5. Paste `LWA_CLIENT_ID` into **Your Client ID** and `LWA_CLIENT_SECRET` into **Your Secret**. Save the configuration.
6. On that same Account Linking page, find **Alexa Redirect URLs**. Copy every URL shown—not just the first—and keep them in your private value sheet.
7. Return to Login with Amazon → your security profile → **Web Settings** → **Allowed Return URLs**. Add each Alexa Redirect URL exactly as copied, then save.
8. Return to the Alexa Developer Console and confirm the Account Linking configuration is saved. Complete any privacy-policy or skill metadata requirements shown by the console.
9. Open the Alexa app associated with the target household, enable the development skill, and complete **Link Account**. Then run **Discover Devices**.
10. Confirm that only devices enabled for cloud exposure appear. Test a simple power command first, then volume/channel commands.

Official instructions: [Create a Smart Home skill](https://developer.amazon.com/docs/alexaplus/smarthome/create-addon-tutorial), [configure the Lambda service endpoint](https://developer.amazon.com/docs/alexaplus/smarthome/implement-your-addon), and [set up Login with Amazon account linking](https://developer.amazon.com/docs/alexaplus/smarthome/set-up-account-linking-tutorial).

## Alexa Event Gateway permission is a separate feature

The repository's currently committed `alexa_lambda/index.mjs` reads only these environment variables: `IOT_ENDPOINT`, `IOT_REGION`, `CONFIG_TOPIC`, `STATE_TOPIC`, `CMD_TOPIC`, `STATUS_TOPIC` and `WRITE_STATE`. It does **not** reference `ALEXA_EVENT_CLIENT_ID` or `ALEXA_EVENT_CLIENT_SECRET`, and its `AcceptGrant` handler only returns an acknowledgement; it does not exchange the authorization code for event-gateway tokens or persist/refresh those tokens.

Therefore, do not enable **Send Alexa Events** / Event Gateway permission for this Lambda as currently committed. If the deployed Lambda has additional `ALEXA_EVENT_*` variables, that deployed source differs from the repository copy and must be reviewed and committed before documenting or relying on those variables. Amazon requires a separate OAuth exchange, secure per-customer token storage and refresh handling when proactive Alexa events are enabled. See [Request access to the Alexa Event Gateway](https://developer.amazon.com/docs/alexaplus/smarthome/authenticate-a-customer-permissions.html) and [Send events to the Alexa Event Gateway](https://developer.amazon.com/docs/alexaplus/smarthome/send-events-to-the-alexa-event-gateway.html).

## Test Alexa discovery and commands

The Lambda uses the retained device catalogue, not a hard-coded list of appliances. If the catalogue is empty or invalid, Alexa discovery will not return the devices you expect.

1. In AWS IoT / the Home Assistant bridge, confirm a retained JSON array exists at `alexa/config/devices`.
2. In AWS Lambda, use the test console with an Alexa Smart Home **Discover** event and review the function logs.
3. In the Alexa app, run **Discover Devices** after account linking completes.
4. Confirm each expected device appears with the intended friendly name and type.
5. Test power first. If it fails, follow the path in order: Lambda logs → AWS IoT directive topic → Home Assistant bridge logs → hub MQTT/serial logs → physical IR output.

### Device IDs and TV channels

The firmware publishes IDs like `4A03_1`, where the first four hexadecimal digits identify the hub for directive routing and the number after the underscore identifies a stored device on that hub. The Lambda uses the prefix before the underscore to publish to `alexa/4A03/directive`. The Home Assistant bridge expects topics of the form `alexa/<four-hex-digit-tag>/directive`. Keep this identity format stable.

For direct channel selection, the updated Lambda handles `Alexa.ChannelController.ChangeChannel` as an **absolute channel number** and validates integer channel numbers from 1 through 999. It sends the digits as a logical `Channel` command so the hub can press the learned numeric keys. Channel Up/Down remains relative.

For named channels, Lambda supports a `channels` map (for example, `"channels":{"asianet":"22"}`) and checks the request's channel name/call sign. However, the current Home Assistant bridge's catalogue builder intentionally publishes only `id`, `name` and `type` for each device. Thus custom channel-name mappings are **not automatically carried from the hub through the current bridge**. Use a numeric channel command until the catalogue path is extended to carry channel metadata; otherwise a manually edited retained catalogue could be overwritten on the next sync.

Alexa natural-language interpretation is separate from Lambda code. Some phrases containing “TV” or “channel” may be routed by Alexa as video-skill requests and never reach this Smart Home skill as a ChangeChannel directive. Test numeric channel selection as well as channel up/down, and use clear endpoint names. A successful Lambda test event proves the handler's behavior only; it does not prove Alexa's voice router will send that event for every phrase.

## Verify the full chain in this order

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

## Troubleshooting table

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
