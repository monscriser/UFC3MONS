"""Per 30 s bucket since the first fight sample (CCharaPhysicThread present): FPS, GPU-command ms/frame,
game ms/frame and on-demand ranges. Usage: trend_buckets.py tag=log ..."""
import datetime, re, sys
TS = re.compile(r"^\[(\d+-\d+-\d+ \d+:\d+:\d+\.\d+)\]")
FR = re.compile(r"\[ufc-wait-frame\] frame=(\d+)")
TC = re.compile(r"\[ufc-thread-cpu\].*?top: (.*)$")
OD = re.compile(r"\[ufc-ondemand\] cumulative .*? ranges=(\d+)")
ts = lambda l: datetime.datetime.strptime(TS.match(l).group(1), "%Y-%m-%d %H:%M:%S.%f")
for arg in sys.argv[1:]:
    tag, path = arg.split("=", 1); lines = open(path, errors="replace").readlines()
    fr = [(ts(l), int(FR.search(l).group(1))) for l in lines if FR.search(l)]
    cpu = []
    for l in lines:
        m = TC.search(l)
        if m:
            th = {}
            for t in re.finditer(r"([A-Za-z:_ ]+?[A-Za-z0-9]*?) \((F[0-9A-F]+)\)/\d+=(\d+)", m.group(1)):
                th[t.group(1).strip()] = th.get(t.group(1).strip(), 0) + int(t.group(3))
            cpu.append((ts(l), th))
    od = [(ts(l), int(OD.search(l).group(1))) for l in lines if OD.search(l)]
    start = next((c[0] for c in cpu if "CCharaPhysicThread" in c[1]), None)
    if not start: print(tag, "sem luta"); continue
    end = cpu[-1][0]; b = 0; row = []
    while start + datetime.timedelta(seconds=30 * (b + 1)) <= end:
        a0 = start + datetime.timedelta(seconds=30 * b); a1 = a0 + datetime.timedelta(seconds=30)
        f = [x for x in fr if a0 <= x[0] <= a1]; fps = (f[-1][1] - f[0][1]) / (f[-1][0] - f[0][0]).total_seconds()
        cs = [c[1] for c in cpu if a0 <= c[0] <= a1]
        g = sum(c.get("GPU Commands", 0) for c in cs) / len(cs); j = sum(sum(v for k, v in c.items() if k != "GPU Commands") for c in cs) / len(cs)
        r = [o[1] for o in od if a0 <= o[0] <= a1]
        row.append(f"{30*b:3d}s fps={fps:5.1f} gpucmd={g/fps:5.2f} jogo={j/fps:5.2f} faixas={r[-1] if r else '?'}")
        b += 1
    print(tag); [print("   ", x) for x in row]
