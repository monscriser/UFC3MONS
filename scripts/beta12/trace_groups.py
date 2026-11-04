#!/usr/bin/env python3
"""GPU trace summary per log (draw tracer, one traced frame): span, draws, and draw/pre time split between
render targets at most 320 px wide (native under the U4 threshold) and the rest."""
import re, sys
D = re.compile(r"\[ufc-tdraw\] frame=\d+ i=\d+ .*? w=(\d+) h=\d+ .*?us=([\d.]+) pre=([\d.]+)")
S = re.compile(r"\[ufc-tdraw\] summary frame=\d+ draws=(\d+) draw_us=([\d.]+) between_us=([\d.]+) span_us=([\d.]+)")
for path in sys.argv[1:]:
    small = [0.0, 0.0]; big = [0.0, 0.0]; summ = None
    for line in open(path, errors="replace"):
        m = D.search(line)
        if m:
            w, us, pre = int(m.group(1)), float(m.group(2)), float(m.group(3))
            t = small if w <= 320 else big
            t[0] += us; t[1] += pre
        s = S.search(line)
        if s:
            summ = s.groups()
    if summ:
        draws, dus, bus, span = summ
        print(f"{path.split('/')[-1]}: draws={draws} span_us={float(span):.0f} span_por_desenho_us={float(span)/int(draws):.2f} "
              f"nativos(<=320) desenho={small[0]:.0f} entre={small[1]:.0f} | resto desenho={big[0]:.0f} entre={big[1]:.0f}")
    else:
        print(path, "sem resumo")
