"""C5VRX-5 by Twotoz and contributors: tools/dac_check.py diagnoses simulated DAC builds."""
import contextlib
import importlib.util
import io
from pathlib import Path
import unittest

spec = importlib.util.spec_from_file_location("dac_check", Path(__file__).parents[1] / "tools/dac_check.py")
dac = importlib.util.module_from_spec(spec)
spec.loader.exec_module(dac)

NOMINAL = list(dac.NOMINAL_OHMS)


def measure(branches, vdd=3300.0, load=200.0, display=None, driver=25.0):
    """Meter readings (mV) for a built network: per-pin resistor ohms (None = open)."""
    g = [0.0 if r is None else 1.0 / (r + driver) for r in branches]
    total = sum(g) + 1.0 / load + (1.0 / display if display else 0.0)
    pins = [vdd * gk / total for gk in g]
    return pins, vdd * sum(g) / total


def run(branches, **kw):
    pins, all_mv = measure(branches, **kw)
    return dac.analyze(pins, all_mv, kw.get("vdd", 3300.0), 0.0)


class DacCheckTest(unittest.TestCase):
    def test_correct_build_passes(self):
        rows, problems, notes = run(NOMINAL)
        self.assertEqual(problems, [])
        self.assertEqual(notes, [])
        self.assertTrue(all(r["verdict"] == "OK" for r in rows))
        # 1 % resistors and another 3V3 level still pass.
        rows, problems, _ = run([r * f for r, f in zip(NOMINAL, (1.01, 0.99, 1.01, 0.99, 1.0, 1.01))], vdd=3340.0)
        self.assertEqual(problems, [])

    def test_firmware_table_matches(self):
        # main/dac_check.h and the docs publish these, rounded.
        expected = [31, 64, 125, 251, 533, 1044]
        self.assertEqual([round(dac.expected_mv(1 << k, 3300)) for k in range(6)], expected)
        self.assertEqual(round(dac.expected_mv(63, 3300)), 2047)

    def test_swapped_pair(self):
        built = NOMINAL[:]
        built[1], built[2] = built[2], built[1]   # 2.0k on D5, 3.9k on D6
        rows, problems, _ = run(built)
        self.assertEqual([r["verdict"] for r in rows], ["OK", "SWAPPED", "SWAPPED", "OK", "OK", "OK"])
        self.assertEqual(len(problems), 1)
        self.assertIn("D5 and D6 are swapped", problems[0])

    def test_open_branch(self):
        built = NOMINAL[:]
        built[3] = None
        rows, problems, _ = run(built)
        self.assertEqual(rows[3]["verdict"], "OPEN")
        self.assertTrue(any(p.startswith("D7: no current") for p in problems))
        self.assertTrue(all(r["verdict"] == "OK" for k, r in enumerate(rows) if k != 3))

    def test_wrong_value(self):
        built = NOMINAL[:]
        built[0] = 82000.0                         # 82k read instead of 8.2k
        rows, problems, _ = run(built)
        self.assertEqual(rows[0]["verdict"], "WRONG")
        self.assertIn("too high", problems[0])
        built = NOMINAL[:]
        built[4] = 240.0                           # D8 got the D9 value
        rows, problems, _ = run(built)
        self.assertEqual(rows[4]["verdict"], "WRONG")
        self.assertIn("belongs on D9", problems[0])

    def test_display_left_connected(self):
        rows, problems, _ = run(NOMINAL, display=75.0)
        self.assertEqual(len(problems), 1)
        self.assertIn("display is still connected", problems[0])

    def test_wrong_rail_or_load(self):
        pins, all_mv = measure(NOMINAL, vdd=3300.0, load=220.0)   # 220R fitted as the load
        _, problems, _ = dac.analyze(pins, all_mv, 3300.0, 0.0)
        self.assertEqual(len(problems), 1)
        self.assertIn("load resistor", problems[0])
        _, problems, _ = dac.analyze(pins, all_mv, 3300.0, 0.0, load_ohms=220.0)   # measured and entered
        self.assertEqual(problems, [])

    def test_inconsistent_readings_and_bad_none(self):
        pins, all_mv = measure(NOMINAL)
        _, problems, notes = dac.analyze(pins, all_mv * 0.9, 3300.0, 12.0)
        self.assertTrue(any("NONE reads" in p for p in problems))
        self.assertTrue(any("add up to" in n for n in notes))
        _, problems, _ = dac.analyze(pins, 3400.0, 3300.0)
        self.assertIn("ALL must lie between", problems[0])

    def test_cli(self):
        pins, all_mv = measure(NOMINAL, vdd=3310.0)
        argv = ["--vdd", "3310", "--all", f"{all_mv:.1f}", "--none", "0.1"]
        for pin, mv in zip(dac.PINS, pins):
            argv += [f"--{pin.lower()}", f"{mv:.1f}"]
        out = io.StringIO()
        with contextlib.redirect_stdout(out):
            self.assertEqual(dac.main(argv), 0)
        self.assertIn("wired correctly", out.getvalue())


if __name__ == "__main__":
    unittest.main()
