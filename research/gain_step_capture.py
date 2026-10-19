import re, sys, time, subprocess, os
import serial
PORT='COM10'
def cmd(k,w):
    s=serial.Serial();s.port=PORT;s.baudrate=115200;s.timeout=.2;s.dtr=False;s.rts=False;s.open()
    s.reset_input_buffer(); s.write(k.encode()); b=b''; e=time.monotonic()+w
    while time.monotonic()<e: b+=s.read(8192)
    s.close(); return b.decode(errors='replace')
def mode():
    m=re.search(r'GAIN owner=\S+ mode=(\S+) gain=(\d+)',cmd('T',3)); return m.groups() if m else None
tag=sys.argv[1]
print('start',mode(),flush=True)
for step in range(12):   # AUTO -> MAX, 78 ... 24, AUTO
    cmd('G',0.8); time.sleep(1.5)
    md=mode(); print(step,md,flush=True)
    if not md or md[0]=='AUTO': continue
    for k in range(4):
        subprocess.run([sys.executable,'-I','C5VRX/tools/capture_iq_snapshot.py',PORT,f'gsnap/{tag}_G{md[1]}_{k}.bin'],capture_output=True,timeout=60)
print('end',mode())
