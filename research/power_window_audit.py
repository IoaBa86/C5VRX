"""Window-mean subtraction bias of the probe power (PR #228 comment, item 1)."""
import sys
from pathlib import Path
import numpy as np
sys.path.insert(0, str(Path(sys.argv[1]).resolve()))
import sim
for dev in (0.69, 1.0, 1.4):
    for porch in (-1.0e6, 0.0, 0.6e6):
        c = sim.make_case(7, 40.0, 'NTSC', dev, porch_hz=porch)      # nearly noise-free carrier
        s = c['clean'].astype(int)
        z = ((((s >> 4) ^ 8) - 8) + .5) + 1j * ((((s & 15) ^ 8) - 8) + .5)
        true = np.mean(abs(z - z.mean()) ** 2)
        out = []
        for w in (256, 1024, 4096):
            n = len(z) // w
            Z = z[:n * w].reshape(n, w)
            win = np.mean(np.abs(Z - Z.mean(1, keepdims=True)) ** 2, 1)
            out.append((w, 10 * np.log10(win.mean() / true), 10 * np.log10(np.percentile(win, 5) / true)))
        print(f'dev {dev:4.2f} porch {porch/1e6:+.1f} MHz: ' + '  '.join(f'w{w}: mean {m:+.2f} dB p5 {p:+.2f} dB' for w, m, p in out))
