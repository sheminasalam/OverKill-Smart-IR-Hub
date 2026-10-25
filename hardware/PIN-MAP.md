# Pin-map review — not a final schematic

| Function | Supplied firmware source | Conflicting reference |
|---|---|---|
| W25Qxx CS | PIN_FLASH_CS = 3 (GPIO3/RX0) | Stage Info output and supplied diagram say GPIO15 |
| Learn button | GPIO0 is initialized but source says physical button disabled/removed due to boot strap | Drawing shows a learn button on GPIO0 |
| Status output | GPIO2, active LOW | Drawing shows common-anode RGB LED on GPIO1/2/3 |
| Reset button | No dedicated reset GPIO constant found | Drawing shows a separate reset button on RST |
| IR TX | default GPIO4 | Diagram labels GPIO4 |
| IR RX | default GPIO5 | Diagram labels GPIO5 |
| DS18B20 | default GPIO16 | Diagram labels GPIO16 |
| SPI MISO/MOSI/SCLK | GPIO12/13/14 | Matches drawing |

Do not publish an alternate schematic that asserts one side of the CS conflict without testing the actual hardware. The actual constant PIN_FLASH_CS = 3 controls chip select, while the stage-info printout says GPIO15. Correct firmware and wiring together, then test startup, flash detection/read/write/erase, UART access and boot modes. The RGB and reset-button mismatch also needs a hardware decision.
