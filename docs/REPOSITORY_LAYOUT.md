# Repository layout and release status

This project combines firmware, hardware design, a 3D model, a Home Assistant integration and an Alexa Lambda handler. The GitHub main branch currently contains the firmware text sources, Lambda handler, README, pin-map notes and technical documentation. The provided integration archive, 3MF and bitmap images are in the separately prepared release bundle; these binary/archive assets have not yet been added to this GitHub branch.

The integration should be published in a separate repository with the component under `custom_components/overkill_smart_hub/` for a clean HACS submission. Once the hardware CS conflict is resolved, add an editable schematic plus a verified rendered image; retain the provided diagram as historical reference.

The Lambda's default IoT endpoint is copied from the source supplied by the owner and must be verified or replaced for the deploying AWS account.