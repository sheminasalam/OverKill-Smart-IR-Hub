# Alexa Smart Home and AWS IoT setup

This guide follows the submitted Lambda handler and Home Assistant integration. Verify console steps against current Amazon/AWS documentation before deployment.

## Architecture

    Alexa Smart Home skill
      -> AWS Lambda (alexa_lambda/index.mjs)
      -> AWS IoT Data Plane (retained catalogue + directive topics)
      -> Home Assistant OverKill AWS bridge (mutual-TLS MQTT)
      -> local/cloud MQTT transport
      -> ESP8266 IR hub -> appliance

The Lambda defaults to IOT_REGION=eu-west-1, reads retained device catalogue alexa/config/devices, reads state from alexa/state/{id}, publishes directives to alexa/{hub}/directive, and checks alexa/status/{hub}. Optional environment variables in the source are IOT_ENDPOINT, CONFIG_TOPIC, STATE_TOPIC, CMD_TOPIC, STATUS_TOPIC, and WRITE_STATE. Set WRITE_STATE=false to make the hub the only state writer.

The updated handler requires the `IOT_ENDPOINT` environment variable and no longer falls back to an endpoint embedded in the source. Set it to the AWS IoT data endpoint for your own account. The value may be the bare hostname or start with `https://`; the code normalizes it. Alexa region compatibility depends on skill locale; Amazon's documentation maps English (India) to Europe (Ireland), eu-west-1, while the allowed-region table may differ by locale. Consult [Amazon's current region guidance](https://developer.amazon.com/en-US/docs/alexa/custom-skills/host-a-custom-skill-as-an-aws-lambda-function.html).

## AWS IoT certificate files for Home Assistant

The integration asks for three PEM files: Amazon Root CA certificate, AWS IoT device certificate and the matching private key. It validates PEM markers and copies uploads to /config/overkill_smart_hub/ with entry-specific filenames.

1. In the AWS account that owns the IoT endpoint, open AWS IoT Core and create/register a Thing or device identity.
2. Create and activate a device certificate. Download its certificate and private key when generated; AWS does not let you retrieve the original private key later.
3. Download the Root CA that matches the endpoint's certificate chain; the source uses the example filename AmazonRootCA1.pem. Follow [AWS's official resource-creation guide](https://docs.aws.amazon.com/iot/latest/developerguide/create-iot-resources.html).
4. Attach a least-privilege IoT policy granting the bridge only the connection, publish, subscribe and receive actions required on its configured topics. Avoid broad iot:* permissions and unrestricted topic resources.
5. Get the AWS IoT data endpoint hostname from your account. Enter the hostname (not a URL scheme) and port 8883 in the Home Assistant bridge config unless your setup differs.
6. Configure the integration's AWS bridge, uploading Root CA, certificate and private key. Confirm logs show connection and directive subscription.
7. Test the policy/topic flow before linking the skill. Protect the certificate directory and backups.

The supplied code proves which files the bridge consumes, but there is no AWS account, policy document or live certificate included here. The actual policy must be generated for your endpoint, certificates and selected topics.

## Lambda and skill

1. Create a Smart Home skill in the Alexa Developer Console using the locale/region supported for your target household.
2. Deploy the supplied ES module index.mjs. It imports @aws-sdk/client-iot-data-plane; package that dependency with the function/runtime as required.
3. Set IOT_REGION and IOT_ENDPOINT to your own AWS IoT Data Plane endpoint. Override topic environment variables only when your deployment uses different topics.
4. Give the Lambda execution role only the IoT Data Plane permissions needed to read retained catalogue/state and publish to command/state topics.
5. Add the function ARN to the Smart Home skill endpoint using the region required for the skill locale. Complete account linking if configured.
6. Verify that the retained device catalogue contains valid JSON before testing discovery from the Alexa app. The handler has no hard-coded device fallback.

## Device names and direct channel commands

The Lambda derives endpoints from the retained catalogue. Minimal records need id, name and type; optional feature/settings overrides are supported. For direct channel selection it sends an absolute Channel command with numeric digits separated by semicolons for the downstream numeric-key flow. Channel Up/Down remain relative button commands. Alexa natural-language routing may still resolve phrases such as “TV channel” as video-skill requests; test exact phrases and names.

## Multi-household setup

Use a separate AWS IoT endpoint/account and bridge certificate identity for each isolated Alexa household. In Home Assistant, explicitly assign each hub to the intended bridge. Do not reuse credentials or assign one household's hub to another household's bridge unless shared access is intentional.

## Troubleshooting

- **Bridge won't connect:** verify endpoint hostname/port 8883, active certificate, matching private key, Root CA, policy and outbound network access.
- **No Alexa devices:** verify skill endpoint region, Lambda environment/IAM role, retained catalogue and endpoint JSON.
- **AWS gets directives but the hub does not:** confirm alexa/+/directive subscription, correct hub bridge assignment, MQTT prefix and hub connection.
- **Wrong physical appliance responds:** check unique endpoint IDs/names and stale Alexa discovery entries.
- **Cross-household issues:** review bridge assignments and catalogue aggregation.

Never commit private keys, certificate material, AWS credentials, MQTT passwords or backups containing secrets.
