# 3D-printed enclosure

## Model

The original [`Overkill.3mf`](Overkill.3mf) is committed in this directory. Open the 3MF directly in your slicer; the preview image is not a printable substitute.

![Labeled enclosure openings and controls](../hardware/Overkill_labeled.png)

## Print material: PETG

**Print this enclosure in PETG.** Start with the PETG profile recommended by your filament manufacturer for your printer and nozzle. The project does not yet publish validated temperatures, fan speed, layer height, infill, supports or orientation, so those should be validated with your particular printer.

## Features shown in the enclosure model

- **Top plate — temperature sensor:** opening for the waterproof DS18B20 probe.
- **Top plate — IR receiver:** opening for the KY-022 / demodulating IR receiver. It captures the original remote's IR signal when learning commands from the Web UI. There is no physical learning button.
- **Top plate — status LED:** opening for the single status LED.
- **Side wall — USB-C 5 V power input:** provision to mount the [4-pin USB-C female breakout PCB](https://quartzcomponents.com/collections/all/products/usb-3-1-female-socket-type-c-connector-4-pin-breakout-pcb-board). Connect VBUS (+5 V) and GND to the power rails; D+ and D− are unused for power-only operation.
- **Underside — reset button:** the physical reset button is underneath the device and connects to the D1 mini's RST input.

The author's build uses a **D1 mini**, which already regulates 5 V to 3.3 V. If using a bare ESP-12F rather than a D1 mini, add a regulated 3.3 V supply such as the [AMS1117-3.3 800 mA module](https://quartzcomponents.com/collections/all/products/ams1117-3-3-ldo-800ma-dc-5v-to-3-3v-step-down-power-supply-module). That module is for the bare ESP-12F variant and is not needed for the D1 mini build.

## USB-C cable note

The [four-pin breakout](https://quartzcomponents.com/collections/all/products/usb-3-1-female-socket-type-c-connector-4-pin-breakout-pcb-board) exposes VBUS, GND, D+ and D− but does not expose CC1/CC2. The supplier's product page describes it as a four-pin power breakout, and a customer review reports that it works with a USB-A-to-USB-C cable. Use a USB-A source with an A-to-C cable for this board unless you add and validate the required USB-C CC pull-down arrangement. Do not assume every USB-C-to-USB-C source/cable combination will provide 5 V to a board without CC resistors.

## Before printing and assembly

1. Open `Overkill.3mf` and inspect every model object and any colour/material assignments in the slicer.
2. Confirm the side cutout matches the USB-C breakout PCB dimensions and leaves clearance for solder joints and wires.
3. Confirm the three top openings align with the actual probe, IR receiver and status LED.
4. Test-fit the D1 mini, W25Q32 module, USB-C breakout, receiver, status LED, DS18B20 probe, cables and reset-button access before final assembly.
5. Confirm the IR transmitter and receiver have the intended optical paths and that the reset button can be pressed from underneath.
6. Check screw lengths and board clearance; no fastener should touch exposed pads.
7. Print a fit-check first if dimensions or tolerances are uncertain. Record your working PETG slicer profile after a successful print.

The D1 mini and connector are not included in the 3MF itself; the model contains the enclosure geometry and provisions for the parts.
