# nfprobe: the C5's own sensitivity, and what our receive state costs

A separate scratch app (not C5VRX firmware). It uses the Wi-Fi PHY's
calibrated per-packet RSSI and noise floor as an absolute reference:

1. Noise floor per 5 GHz channel. On 2026-10-19 it read -94..-96 dBm from
   5180 to 5845 MHz, with packets decoded down to -97/-98 dBm: a noise figure
   of about 5-7 dB, at least RX5808 class (-90 dBm datasheet).
2. Self-desense. On a busy channel, each ingredient of the C5VRX receive
   state is switched on alone: IQ pins, dump registers, all modem clocks,
   DAC pins, forced G83, and all of them. Weak packets (< -84 dBm) vanish
   first when the front end goes deaf.

Build with ESP-IDF v6.0.2 (`idf.py build`), flash the app to 0x10000, and
read `NFPROBE` / `DESENSE` lines on the console. Reflash C5VRX afterwards.
