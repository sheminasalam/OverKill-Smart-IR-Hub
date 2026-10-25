# 3D-printed enclosure

## Model

The actual [`Overkill.3mf`](Overkill.3mf) model is committed in this directory. Open it directly in your slicer; the SVG/PNG previews are for reference and are not printable substitutes.

![Labeled enclosure render from the supplied 3MF](../hardware/OverKill-3MF-Labeled-Product.svg)

## Print material: PETG

**Print this enclosure in PETG.** Use the PETG profile recommended by your filament manufacturer for your printer and nozzle as a starting point. The repository does not publish verified temperatures, fan speed, layer height, infill, supports or orientation yet, so those should be validated with your particular printer.

## What the model provisions are for

- **Top plate — temperature sensor:** opening for the waterproof DS18B20 probe.
- **Top plate — IR learning receiver:** opening for the KY-022 / demodulating IR receiver. This is the receiver used to capture IR signals while learning commands from the original remote; there is no physical learning button.
- **Top plate — status LED:** opening for the single status LED.
- **Side wall — USB-C power input:** provision to mount the [4-pin USB-C female breakout PCB](https://quartzcomponents.com/collections/all/products/usb-3-1-female-socket-type-c-connector-4-pin-breakout-pcb-board). Connect VBUS (+5 V) and GND to the power rails; D+ and D− are not used for power-only operation.
- **Underside — reset button:** the physical reset button is underneath the device and connects to the D1 mini's RST input.

The author's build uses a **D1 mini**, which already contains a 3.3 V regulator. Builders using a bare ESP-12F rather than a D1 mini need a separate regulated 3.3 V supply; the suggested [AMS1117-3.3 800 mA module](https://quartzcomponents.com/collections/all/products/ams1117-3-3-ldo-800ma-dc-5v-to-3-3v-step-down-power-supply-module) is for that bare-module option, not required by the D1 mini variant.

### USB-C cable note

This 4-pin breakout exposes VBUS, GND, D+ and D− but does not expose CC1/CC2. Quartz Components lists the board as a power breakout and customer feedback reports using it with a USB-A-to-USB-C cable. Use a USB-A source with an A-to-C cable for this four-pin board unless you add and validate the proper USB-C CC resistor arrangement. Do not assume every C-to-C supply/cable combination will provide 5 V to a board with no CC resistors.

## Before full printing and assembly

1. Open `Overkill.3mf` and inspect every object and any colour/material assignments in the slicer.
2. Confirm the side cutout matches the actual USB-C breakout PCB dimensions and allow clearance for solder joints and wires.
3. Confirm that the three top openings line up with the actual probe, IR receiver and LED parts.
4. Test-fit the D1 mini, W25Q32 module, USB-C breakout, receiver, status LED, DS18B20 probe, cables and reset-button access before final assembly.
5. Confirm the transmitter and receiver have the intended optical path and that the reset button can be pressed from underneath.
6. Check screw lengths and board clearance; no fastener should touch exposed pads.
7. Print a fit-check first if dimensions or tolerances are uncertain, and record the working slicer profile after a successful print.

The D1 mini and connector are not included in the 3MF itself; the model contains the enclosure geometry and provisions for the parts.
