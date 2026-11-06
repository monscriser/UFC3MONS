#!/usr/bin/env python3
"""Compara, funcao a funcao, o numero de 'case' de cada switch (bctr) entre a geracao Reborn (r9) e a Original (v48).
Lista switches da Reborn com MENOS casos que a Original (tabela cortada -> __builtin_trap = crash 0xC000001D)."""
import glob, re, sys
R = "E:/UFC3-RexGlue-Lab/sources/ufc3-native-audited-r9/generated"
O = "E:/UFC3-RexGlue-Lab/sources/ufc3-original-audited-v48/generated"
fre = re.compile(r"^DEFINE_REX_FUNC\((sub_[0-9A-F]+)\)")
cre = re.compile(r"^\s*case (\d+):")


def scan(d):
    out = {}
    for f in glob.glob(d + "/ufc3_recomp.*.cpp"):
        func = None; sw = []; cur = None; last_cmp = None
        with open(f, encoding="utf-8", errors="replace") as h:
            for line in h:
                m = fre.match(line)
                if m:
                    if func: out[func] = sw
                    func = m.group(1); sw = []; cur = None; continue
                if "// cmplwi" in line:
                    last_cmp = line.strip()
                if line.lstrip().startswith("switch (ctx."):
                    cur = [last_cmp, 0]; sw.append(cur); continue
                if cur is not None:
                    if cre.match(line): cur[1] += 1
                    elif "default:" in line: cur = None
        if func: out[func] = sw
    return out


r, o = scan(R), scan(O)
bad = []
for fn, sw in r.items():
    so = o.get(fn)
    if not so or len(so) != len(sw):
        continue
    for i, (cmp_r, n_r) in enumerate(sw):
        cmp_o, n_o = so[i]
        if n_r < n_o:
            bad.append((fn, i, n_r, n_o, cmp_r))
print("funcoes Reborn=%d Original=%d comparaveis com switch cortado na Reborn=%d" % (len(r), len(o), len(bad)))
for b in bad:
    print("%s switch#%d reborn=%d original=%d  %s" % b)
