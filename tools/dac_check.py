#!/usr/bin/env python3
# C5VRX-5 by Twotoz and the C5VRX contributors: check a resistor DAC from
# multimeter readings taken in the receiver's DAC self-test.
#
# Put the receiver in the DAC TEST meter steps (menu DAC TEST, hold; or
# console Z), unplug the display, and measure CVBS to GND at every step.
# Measure the XIAO 3V3 pin too. Then:
#
#   python3 tools/dac_check.py --vdd 3312 --d4 30.9 --d5 64.1 --d6 125.4 \
#       --d7 250.8 --d8 531 --d9 1036 --all 2041 --none 0.2
#
# Missing values are asked for interactively. All readings are in mV.
#
# The six pins and the 200 ohm load form a conductance divider:
#   V(pin k alone) = VDD * G_k / (G_1 + .. + G_6 + G_load)
# The ALL reading fixes the total conductance against the known load, so
# each pin's effective resistance (resistor plus pin driver) follows from
# its own reading. The model matches main/dac_check.h.
import argparse
import math
import sys

PINS = ("D4", "D5", "D6", "D7", "D8", "D9")
NOMINAL_OHMS = (8200, 3900, 2000, 1000, 470, 240)
LOAD_OHMS = 200.0
# Accepted effective resistance: resistor tolerance below, plus up to this
# much pin driver resistance above (ESP32-C5 output stage, estimate).
TOLERANCE = 0.05
PIN_DRIVER_OHMS = 50.0
OPEN_MV = 1.0            # below this a branch carries no current
NONE_MAX_MV = 5.0        # all pins low: the node must sit at GND
SUPERPOSITION_TOL = 0.03  # sum of the single pins vs ALL
DISPLAY_LOAD_OHMS = 75.0


def expected_mv(code, vdd_mv, load_ohms=LOAD_OHMS):
    total = 1.0 / load_ohms + sum(1.0 / r for r in NOMINAL_OHMS)
    on = sum(1.0 / r for k, r in enumerate(NOMINAL_OHMS) if code & (1 << k))
    return vdd_mv * on / total


def window(nominal):
    return nominal * (1.0 - TOLERANCE), nominal * (1.0 + TOLERANCE) + PIN_DRIVER_OHMS


def fits(ohms, nominal):
    low, high = window(nominal)
    return low <= ohms <= high


def fmt_ohms(ohms):
    if ohms is None:
        return "open"
    return f"{ohms / 1000:.2f}k" if ohms >= 1000 else f"{ohms:.0f}R"


def analyze(pins_mv, all_mv, vdd_mv, none_mv=None, load_ohms=LOAD_OHMS):
    """Return (rows, problems, notes) for six single-pin readings in mV."""
    problems, notes = [], []
    if vdd_mv <= 0 or all_mv <= 0 or all_mv >= vdd_mv:
        problems.append(f"ALL must lie between 0 and the 3V3 reading ({vdd_mv:.0f} mV); got {all_mv:.0f} mV.")
        return [], problems, notes
    if none_mv is not None and abs(none_mv) > NONE_MAX_MV:
        problems.append(f"NONE reads {none_mv:.1f} mV, expected 0: check the meter range and that the receiver "
                        "is on the NONE step.")
    single_sum = sum(pins_mv)
    if abs(single_sum - all_mv) > SUPERPOSITION_TOL * all_mv:
        notes.append(f"The single pins add up to {single_sum:.0f} mV but ALL reads {all_mv:.0f} mV. The readings "
                     "should match within 3 %: was the display connected for some steps, or did the 3V3 rail move?")
    # Total conductance from the ALL reading and the known load.
    total_s = (1.0 / load_ohms) / (1.0 - all_mv / vdd_mv)
    rows = []
    for k, (pin, nominal, mv) in enumerate(zip(PINS, NOMINAL_OHMS, pins_mv)):
        ohms = None if mv < OPEN_MV else vdd_mv / (total_s * mv)
        rows.append({"pin": pin, "nominal": nominal, "mv": mv, "expected_mv": expected_mv(1 << k, vdd_mv, load_ohms),
                     "ohms": ohms, "verdict": "OK"})
    # Shared error first: a scale on every branch is not six bad resistors.
    # D4..D7 carry it cleanly; the pin driver blurs it on D8 and D9.
    ratios = [r["ohms"] / r["nominal"] for r in rows[:4] if r["ohms"]]
    if len(ratios) == 4:
        geo = math.exp(sum(math.log(x) for x in ratios) / len(ratios))
        spread = max(ratios) / min(ratios)
        if spread < 1.08 and not 0.95 < geo < 1.05:
            display = (1.0 / load_ohms + 1.0 / DISPLAY_LOAD_OHMS) / (1.0 / load_ohms)
            if abs(geo / display - 1.0) < 0.12:
                problems.append(f"Every pin reads about {geo:.1f}x its value: the display is still connected "
                                f"(its {DISPLAY_LOAD_OHMS:.0f} ohm input adds to the load). Unplug it and measure again.")
            else:
                problems.append(f"Every pin reads {100 * (geo - 1):+.0f} % together: check the 3V3 value you entered "
                                f"and the load resistor (expected {load_ohms:.0f} ohm), not the six DAC resistors.")
            return rows, problems, notes
    for row in rows:
        if row["ohms"] is None:
            row["verdict"] = "OPEN"
            problems.append(f"{row['pin']}: no current. Check the solder joints and the {fmt_ohms(row['nominal'])} "
                            "resistor; the pin may also be on the wrong pad.")
        elif not fits(row["ohms"], row["nominal"]):
            row["verdict"] = "WRONG"
    # A wrong branch that reads like another pin's resistor is most likely
    # a swap; two such branches that read like each other certainly are.
    for row in rows:
        if row["verdict"] != "WRONG":
            continue
        other = next((o for o in rows if o is not row and fits(row["ohms"], o["nominal"])), None)
        if other is None:
            low, high = window(row["nominal"])
            side = "high" if row["ohms"] > high else "low"
            problems.append(f"{row['pin']} reads {fmt_ohms(row['ohms'])}, too {side} for "
                            f"{fmt_ohms(row['nominal'])} (accepted {fmt_ohms(low)}..{fmt_ohms(high)}).")
        elif other["verdict"] == "WRONG" and fits(other["ohms"], row["nominal"]):
            row["verdict"] = other["verdict"] = "SWAPPED"
            problems.append(f"{row['pin']} and {other['pin']} are swapped: {row['pin']} carries the "
                            f"{fmt_ohms(other['nominal'])} resistor and {other['pin']} the {fmt_ohms(row['nominal'])}.")
        else:
            problems.append(f"{row['pin']} reads {fmt_ohms(row['ohms'])}, the value that belongs on {other['pin']}; "
                            f"it should be {fmt_ohms(row['nominal'])}.")
    return rows, problems, notes


def report(rows, problems, notes, vdd_mv, load_ohms=LOAD_OHMS):
    out = [f"DAC check at 3V3 = {vdd_mv / 1000:.3f} V, {load_ohms:.0f} ohm load, no display", "",
           "Pin  Resistor  Measured   Expected   Effective  Result",
           "---  --------  ---------  ---------  ---------  -------"]
    for r in rows:
        out.append(f"{r['pin']:<4} {fmt_ohms(r['nominal']):>8}  {r['mv']:>6.1f} mV  {r['expected_mv']:>6.1f} mV"
                   f"  {fmt_ohms(r['ohms']):>9}  {r['verdict']}")
    out.append("")
    if problems:
        out.append("Problems:")
        out += [f"  - {p}" for p in problems]
    else:
        out.append("All six branches are within tolerance. The DAC is wired correctly.")
    if notes:
        out.append("Notes:")
        out += [f"  - {n}" for n in notes]
    out.append("")
    out.append("Effective resistance includes the pin's own driver (a few tens of ohms), so D8 and D9 "
               "may read slightly above their resistor value.")
    return "\n".join(out)


def ask(label, default=None):
    hint = f" [{default:g}]" if default is not None else ""
    while True:
        text = input(f"{label} reading in mV{hint}: ").strip().replace(",", ".")
        if not text and default is not None:
            return default
        try:
            return float(text)
        except ValueError:
            print("  Please enter a number, for example 125.4")


def main(argv=None):
    parser = argparse.ArgumentParser(description="Check a C5VRX resistor DAC from DAC TEST multimeter readings (mV).")
    parser.add_argument("--vdd", type=float, help="XIAO 3V3 pin to GND, mV (default: ask; 3300 if left empty)")
    for pin in PINS:
        parser.add_argument(f"--{pin.lower()}", type=float, help=f"CVBS to GND with only {pin} high, mV")
    parser.add_argument("--all", type=float, help="CVBS to GND with all six pins high, mV")
    parser.add_argument("--none", type=float, help="CVBS to GND with all pins low, mV (optional)")
    parser.add_argument("--load", type=float, default=LOAD_OHMS, help="load resistor, ohm (measure it; default 200)")
    args = parser.parse_args(argv)
    vdd = args.vdd if args.vdd is not None else ask("3V3 pin", 3300.0)
    pins = [getattr(args, pin.lower()) for pin in PINS]
    pins = [v if v is not None else ask(f"{pin} only") for pin, v in zip(PINS, pins)]
    all_mv = args.all if args.all is not None else ask("ALL")
    rows, problems, notes = analyze(pins, all_mv, vdd, args.none, args.load)
    print(report(rows, problems, notes, vdd, args.load))
    return 1 if problems else 0


if __name__ == "__main__":
    sys.exit(main())
