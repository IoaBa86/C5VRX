# Building and testing the resistor DAC

C5VRX produces composite video (CVBS) with six GPIO pins and a handful of
resistors. Each pin drives one resistor into a shared node; the resistor
values halve from one pin to the next, so the six pins act as a 6-bit
digital-to-analog converter. A seventh resistor from the node to ground sets
the output level. The node is the video output.

The circuit is simple, but it is unforgiving about order. A swapped pair or a
cold joint still produces a picture, just a wrong one, and it is hard to tell
from the goggles which resistor is at fault. The receiver therefore includes
a self-test that checks the DAC on its own, first by eye and then with a
multimeter.

![DAC schematic](c5vrx-dac-schematic.png)

## Parts

| Qty | Part | Notes |
| ---: | --- | --- |
| 1 | Seeed Studio XIAO ESP32-C5 | |
| 1 each | 8.2 kΩ, 3.9 kΩ, 2.0 kΩ, 1.0 kΩ, 470 Ω, 240 Ω | 1 % metal film, 1/4 W or smaller |
| 1 | 200 Ω | load resistor, 1 % |
| 1 | Video lead | the AV cable for your goggles, or 3.5 mm to RCA |
| | Perfboard or stripboard, hook-up wire | |

The 470 pF capacitor in the schematic is optional and its benefit is not
proven. Build without it first; the self-test gives you a baseline to compare
against later.

## Wiring

With the XIAO component side up and the USB-C port at the top, the six DAC
pins are the bottom three on each side:

```text
              USB-C
        ┌───────────────┐
   D0   │●             ●│  5V
   D1   │●             ●│  GND   ← video ground
   D2   │●             ●│  3V3   ← measure here for the multimeter check
   D3   │●             ●│  D10
   D4   │●  ← bit 0    ●│  D9    ← bit 5
   D5   │●  ← bit 1    ●│  D8    ← bit 4
   D6   │●  ← bit 2    ●│  D7    ← bit 3
        └───────────────┘
```

| Pin | Resistor to the VIDEO node | 4-band code | 5-band code |
| --- | ---: | --- | --- |
| D4 | 8.2 kΩ | grey, red, red | grey, red, black, brown |
| D5 | 3.9 kΩ | orange, white, red | orange, white, black, brown |
| D6 | 2.0 kΩ | red, black, red | red, black, black, brown |
| D7 | 1.0 kΩ | brown, black, red | brown, black, black, brown |
| D8 | 470 Ω | yellow, violet, brown | yellow, violet, black, black |
| D9 | 240 Ω | red, yellow, brown | red, yellow, black, black |
| GND | 200 Ω (from the VIDEO node) | red, black, brown | red, black, black, black |

Leave D0 to D3 and D10 unconnected. The receiver routes internal signals
through those pads.

## Assembly

1. Measure every resistor before you fit it. Colour bands are easy to
   misread, red against brown and orange against brown in particular.
2. Solder one resistor to each of D4 to D9 and bring their free ends together
   on one short strip or pad. That strip is the VIDEO node.
3. Fit the 200 Ω resistor between the VIDEO node and the XIAO GND pin.
4. Take two wires to the display: VIDEO from the node and ground from GND.
   Twist them and keep them short, ideally under 15 cm.
5. Before the first power-up, check with the meter that the VIDEO node is not
   shorted to 3V3 or 5V. To GND it should read about 200 Ω or less.

The DAC needs no supply of its own; the XIAO powers everything from USB-C.

## Example build

![Perfboard DAC build, close-up](dac-build-closeup.jpg)

A perfboard build by JulienChagnon. The resistors for D7 to D9 sit on one
side of the XIAO and those for D4 to D6 on the other. Each group is joined
by a solder trace, and the red jumper connects the two groups into a single
VIDEO node. The 200 Ω load runs diagonally from the node to a ground point
shared with the XIAO GND and the shield of the video lead.

![The same build with its RCA video lead and a 5.8 GHz antenna](dac-build-overview.jpg)

The video lead ends in an RCA plug, and a 5.8 GHz antenna is fitted to the
XIAO's U.FL connector.

<img src="dac-build-underside.jpg" alt="Underside of the board with the optional 470 pF capacitor" width="450">

This build also has the optional 470 pF capacitor, fitted on the underside
of the board between the VIDEO node and ground, in parallel with the 200 Ω
load.

Here the XIAO is mounted upside down, which is why the pin labels and BAT
pads are visible. It works, with two caveats. The BOOT and RESET buttons
face the board, so the menu is driven from the USB console or the web
flasher (`o`, `n`, `x`, `Z`) instead of the BOOT button. And the parts on
the XIAO's underside rest against the perfboard pads, so check 3V3 and VBUS
for shorts to GND before the first power-up, or put a layer of Kapton tape
under the module. Mounting the XIAO component side up avoids both.

## Connecting a display

The VIDEO wire goes to the video contact of the display input, ground to its
shield or sleeve. Goggle AV jacks are not standardised, so check your lead
with the meter: find the contact that connects to the yellow (video) plug, or
use the AV cable that came with the goggles.

## Checking your build

### 1. Test pattern

Open the menu (hold BOOT for 0.6 s), press BOOT until **DAC TEST** is
selected, and the screen switches to the test pattern. From a computer, the
`Z` key on the USB console or the **DAC TEST** tab of the web flasher does the
same.

![The DAC TEST pattern on a display, from a correctly wired DAC](dac-test-pattern.jpg)

*The pattern from a correctly wired DAC, photographed on a display fed
through an AV-to-HDMI converter. The ramp's individual steps are visible and
its bright end is washed out by the display's brightness setting; both are
normal. The D4 bar shows no visible step, as expected. The hint under the
ramp now reads EVERY STEP BRIGHTER.*

The pattern has two parts:

- **A ramp** across the top, through every DAC level from black to full
  white. On a correct DAC every step is a little brighter than the one to
  its left. The 44 steps are often visible as fine stripes; that is normal.
  A step that is darker than its left neighbour is not.
- **Six bars** labelled D4 to D9. The two halves of each bar differ in one pin
  only, so each bar shows the contribution of one resistor. The contrast
  should roughly double from one bar to the next. The D4 bar is a single
  step out of 64 and may be hard to see in goggles; the multimeter check
  covers it.

| What you see | Likely cause |
| --- | --- |
| Smooth ramp, contrast grows from D4 to D9 | The DAC is correct |
| One bar with no contrast at all | That pin's resistor is missing, open or badly soldered |
| Two bars in the wrong order | Those two resistors are swapped |
| Ramp with repeating bands or steps backwards | At least one resistor is wrong; the bars show which |
| Picture rolls, tears or is missing | Check the ground wire, the video lead and the 200 Ω load first |

### 2. Multimeter check

The pattern is quick, but a multimeter gives a definite answer for every
resistor, including D4.

1. Unplug the display. Its 75 Ω input would add to the load and change every
   reading.
2. Measure the XIAO **3V3** pin to GND and note the value.
3. On the DAC TEST page, hold BOOT (or press `Z` again). The output now sits at
   a flat level with only D4 high. The goggles would lose their picture here,
   which is expected.
4. Measure VIDEO to GND in the millivolt range and note the reading. Hold BOOT
   again for the next step. The steps run D4, D5, D6, D7, D8, D9, ALL, NONE,
   then back to the pattern.
5. Press BOOT once to leave the page. The menu stays open for five minutes on
   this page, so there is time to measure.

Expected readings with a 3.30 V rail and no display connected:

| Step | D4 | D5 | D6 | D7 | D8 | D9 | ALL | NONE |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| mV | 31 | 64 | 125 | 251 | 533 | 1044 | 2047 | 0 |

Scale them by your 3V3 reading divided by 3.30. D8 and D9 typically read a few
percent low, because the pin's own driver adds a few tens of ohms to their
small resistors.

Then let the analysis tool do the arithmetic:

```text
python3 tools/dac_check.py --vdd 3310 --d4 30.8 --d5 64.3 --d6 125.4 \
    --d7 250.9 --d8 529 --d9 1031 --all 2032 --none 0.2
```

Run it without arguments and it asks for each reading. It works out the
effective resistance of every branch and names the fault, for example:

```text
Pin  Resistor  Measured   Expected   Effective  Result
---  --------  ---------  ---------  ---------  -------
D4      8.20k    31.8 mV    30.6 mV      8.24k  OK
D5      3.90k   129.3 mV    64.4 mV      2.03k  SWAPPED
D6      2.00k    66.7 mV   125.6 mV      3.93k  SWAPPED
...
Problems:
  - D5 and D6 are swapped: D5 carries the 2.00k resistor and D6 the 3.90k.
```

It also recognises an open branch, a wrong value, a display left connected
and a load resistor that is not 200 Ω. If you fitted a different load, measure
it and pass `--load`.

## How the self-test works

The DAC TEST page is drawn on the menu's own PAL/NTSC raster, so it uses the
same video timing as the menu. For the multimeter steps the firmware fills the
entire raster, sync and colour burst included, with one DAC code. The DMA
chain that streams the raster is unchanged, so the pins hold a constant level
until the next step, when the raster is rebuilt. Live video and the
demodulator are not involved; leaving the menu restores them as usual.

The expected levels come from treating the network as a conductance divider:
a high pin connects its resistor to 3V3, a low pin connects it to ground, and
the node settles at

    V = 3V3 × (sum of the conductances of the high pins) / (sum of all conductances, load included)

`main/dac_check.h` holds the model and the pattern levels, with a host test
in `test/test_dac_check.c`. `tools/dac_check.py` inverts the same model,
tested in `test/test_dac_check_tool.py` against simulated builds with
swapped, open and wrong resistors.
