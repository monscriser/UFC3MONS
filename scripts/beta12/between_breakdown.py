#!/usr/bin/env python3
"""B21: per traced frame, split time between draws (pre) and render-pass breaks by what happened before each draw."""
import re, sys, collections
R = re.compile(r"\[ufc-tdraw\] frame=(\d+) i=(\d+) .*?us=([\d.]+) pre=([\d.]+) disp=(\d+) b2i=(\d+) cbuf=(\d+) clr=(\d+) bar=(\d+)(?: rp=(\d+))?")
for path in sys.argv[1:]:
    fr = collections.defaultdict(lambda: collections.defaultdict(lambda: [0, 0.0, 0]))
    draw = collections.defaultdict(float)
    for line in open(path, errors="replace"):
        m = R.search(line)
        if not m: continue
        f, i, us, pre, disp, b2i, cbuf, clr, bar, rp = m.groups()
        f = int(f); draw[f] += float(us)
        if int(i) == 0: continue
        flags = [n for n, v in (("cbuf", cbuf), ("disp", disp), ("b2i", b2i), ("clr", clr), ("bar", bar)) if int(v)]
        k = "+".join(flags) or "none"
        c = fr[f][k]; c[0] += 1; c[1] += float(pre); c[2] += int(rp or 0)
    print("##", path.split("/")[-1], "frames", sorted(fr))
    tot = collections.defaultdict(lambda: [0, 0.0, 0])
    for f in fr:
        for k, c in fr[f].items():
            t = tot[k]; t[0] += c[0]; t[1] += c[1]; t[2] += c[2]
    n = len(fr)
    print(f"  draw_us/frame={sum(draw.values())/n:.1f}")
    for k, t in sorted(tot.items(), key=lambda x: -x[1][1]):
        print(f"  {k:22s} draws/frame={t[0]/n:7.1f} pre_us/frame={t[1]/n:8.1f} rp/frame={t[2]/n:6.1f}")
