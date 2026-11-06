import glob, re
for name, d in (("reborn", "E:/UFC3-RexGlue-Lab/sources/ufc3-native-audited-r9/generated"), ("original", "E:/UFC3-RexGlue-Lab/sources/ufc3-original-audited-v48/generated")):
    bad = []; total = 0
    for f in glob.glob(d + "/ufc3_recomp.*.cpp"):
        func = None; last = None; cur = None
        for line in open(f, encoding="utf-8", errors="replace"):
            if line.startswith("DEFINE_REX_FUNC("): func = line[16:line.index(")")]
            m = re.search(r"// cmplwi cr\d,(r\d+),(\d+)$", line.rstrip())
            if m: last = (m.group(1), int(m.group(2)))
            s = re.match(r"\s*switch \(ctx\.(r\d+)\.u32\)", line)
            if s: cur = [s.group(1), 0, last, func]; continue
            if cur is not None:
                if re.match(r"\s*case \d+:", line): cur[1] += 1
                elif "default:" in line:
                    total += 1
                    reg, n, l, fn = cur
                    if l and l[0] == reg and n < l[1] + 1: bad.append((fn, n, l[1] + 1))
                    cur = None
    print(name, "switches=%d cortados=%d" % (total, len(bad)), bad[:10])
