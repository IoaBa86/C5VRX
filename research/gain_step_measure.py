"""C5VRX by Twotoz and the C5VRX contributors: real gain per table row against
the model in gain.c (6 dB per RF stage and baseband doubling, 0.83 dB per
fine step), from IQ snapshots at fixed gains (console G) with a steady VTX.

    python gain_step_measure.py data/gainstep_2026-10-19 d2m

2026-10-19, VTX at ~2 m: G42 -> G36 (RF stage 5 -> 4) measured about -13.5 dB
where the model says -3.3 dB. Rows that fold or sit near the 4-bit
quantisation are flagged; more distances are needed for the other stages.
"""
import glob, sys, re, math
import numpy as np
folder, tag = sys.argv[1], sys.argv[2]
# model dB per row (same as gain.c)
T={24:(2,15,1),30:(4,7,5),36:(4,15,5),42:(5,7,1),48:(7,7,4),54:(8,7,5),60:(8,15,5),66:(8,31,5),72:(8,63,5),78:(8,127,5),83:(8,127,0)}
def model(g):
    rf,bb,f=T[g]; return 6*rf+6*math.log2(bb+1)+0.83*(5-f)
res={}
for fn in sorted(glob.glob(f'{folder}/{tag}_G*_*.bin')):
    g=int(re.search(r'_G(\d+)_',fn).group(1))
    s=np.fromfile(fn,np.uint8).astype(int)
    i=(((s>>4)^8)-8)+.5; q=(((s&15)^8)-8)+.5
    rails=((np.abs(i)==7.5)|(np.abs(q)==7.5)).mean()
    p=i.var()+q.var()-1/6
    res.setdefault(g,[]).append((p,rails))
print(' G  model_dB  power_cells2  rails%  measured_dB(rel)  error_vs_model')
ref=None
rows=[]
for g in sorted(res):
    p=np.median([x[0] for x in res[g]]); r=np.median([x[1] for x in res[g]])*100
    rows.append((g,model(g),p,r))
# reference: the unclipped row with the largest power and rails<0.5%
ok=[x for x in rows if x[3]<0.5 and x[2]>0.5]
gref=max(ok,key=lambda x:x[2])
for g,m,p,r in rows:
    meas=10*math.log10(max(p,1e-3))-10*math.log10(gref[2])+gref[1]
    flag='' if r<0.5 and p>0.5 else ('  (folding)' if r>=0.5 else '  (too small)')
    print(f'{g:3d} {m:8.1f} {p:12.2f} {r:7.2f} {meas:12.1f} {meas-m:+10.1f}{flag}')
