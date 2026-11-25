"""Gera as variantes UFC e PRIDE do figacce.mpc com aliases (1.9, camisas por organização).

O índice do .mpc é uma árvore de registros de 40 bytes (u32 first, u32 count, char name[32]); os grupos de
camisa 6NNN (sob ts1) apontam para dois filhos (shd.dds, shirt_b.dds) com (offset, size). Os aliases v3
reaproveitam os filhos de outros grupos, inclusive nos grupos originais. Para cada grupo 6NNN cuja textura atual
não serve na organização, copia (first, count) de outro ponteiro existente cuja textura serve e cujo dono dos
dados (grupo original que tinha esses filhos) tem o mesmo deslocamento de ssmd.ymx no NNN_acce.mpc
(cascata: dono atual -> próprio modelo -> dono sem ssmd). Saída: dois .mpc do mesmo tamanho e relatorio.csv."""
import argparse, collections, csv, hashlib, io, os, struct, zlib

RECORD = struct.Struct(">II32s")

def index(data):
    isz, _, _, doff = struct.unpack(">4I", data[:16])
    recs = []
    for i, (a, b, n) in enumerate(RECORD.iter_unpack(data[0x1C:0x1C + (isz - 0x1C) // 40 * 40])):
        recs.append((a, b, n.split(b"\0", 1)[0].decode("latin1"), 0x1C + i * 40))
    return recs, doff

def shirt_groups(recs):
    out = {}
    for a, b, n, pos in recs:
        if len(n) == 4 and n.startswith("6") and n.isdigit() and b <= 64 and a + b <= len(recs):
            kids = {recs[j][2]: recs[j] for j in range(a, a + b)}
            if "shirt_b.dds" in kids: out[n] = (a, b, pos, kids["shirt_b.dds"][0])
    return out

def texture_sha(data, doff, offset):
    p = doff + offset
    if data[p:p + 4] != b"YZLI": return None
    size = struct.unpack_from("<I", data, p + 4)[0]
    z = zlib.decompressobj(); tex = z.decompress(data[p + 16:p + 16 + size * 2 + 65536], size + 1)
    return hashlib.sha256(tex).hexdigest()

def ssmd(game, model):
    p = os.path.join(game, "DAT", "chara", model, f"{model}_acce.mpc")
    if not os.path.isfile(p): return None
    d = open(p, "rb").read(0x800)
    n = struct.unpack(">I", d[0xBC:0xC0])[0]
    lay = {d[0xC4+i*0x28:0xE4+i*0x28].split(b"\0")[0].decode("latin1"): struct.unpack(">I", d[0xE4+i*0x28:0xE8+i*0x28])[0] for i in range(n)}
    return lay.get("ssmd.ymx")

def main():
    a = argparse.ArgumentParser(); a.add_argument("game"); a.add_argument("alias"); a.add_argument("inv_orig")
    a.add_argument("classes"); a.add_argument("outdir"); a.add_argument("--header"); x = a.parse_args()
    orig = open(os.path.join(x.game, "DAT", "chara", "figacce.mpc"), "rb").read()
    data = open(x.alias, "rb").read()
    ro, odoff = index(orig); rn, ndoff = index(data)
    og, ng = shirt_groups(ro), shirt_groups(rn)
    cls_first = {r["group"]: r["classe"] for r in csv.DictReader(open(x.classes))}
    sha_cls = {r["sha256"]: cls_first.get(r["first_group_with_texture"], "?") for r in csv.DictReader(open(x.inv_orig))}
    # owner of a texture payload: original group whose shirt_b.dds sits at the same place in the data area
    owner_by_sha = {}
    for name, (fa, fb, pos, off) in sorted(og.items()):
        owner_by_sha.setdefault((texture_sha(orig, odoff, off), name), name)
    owner_at = {}
    for name, (fa, fb, pos, off) in og.items(): owner_at.setdefault(off, name)
    ptrs = {}
    for name, (fa, fb, pos, off) in ng.items():
        if (fa, fb) in ptrs: continue
        sha = texture_sha(data, ndoff, off)
        own = owner_at.get(off - (ndoff - odoff)) or owner_at.get(off)
        ptrs[(fa, fb)] = dict(cls=sha_cls.get(sha, "?"), owner=own, layout=ssmd(x.game, str(int(own) - 6000).zfill(3)) if own else "?")
    print("grupos", len(ng), "ponteiros distintos", len(ptrs), "sem dono", sum(1 for p in ptrs.values() if not p["owner"]))
    allowed = {"UFC": ("UFC", "NEUTRA"), "PRIDE": ("PRIDE", "NEUTRA")}
    rep = []; stats = collections.Counter(); patches = {}; hashes = {}
    for org in ("UFC", "PRIDE"):
        out = bytearray(data); patches[org] = []
        for name, (fa, fb, pos, off) in sorted(ng.items()):
            cur = ptrs[(fa, fb)]
            if cur["cls"] in allowed[org]: stats[(org, "mantida", cur["cls"])] += 1; continue
            own_layout = ssmd(x.game, str(int(name) - 6000).zfill(3))
            pool = []; rule = ""
            for rule, key in (("dono-atual", cur["layout"]), ("proprio-modelo", own_layout), ("dono-sem-ssmd", None)):
                cands = [(p, v) for p, v in ptrs.items() if v["cls"] in allowed[org] and v["owner"] and v["layout"] == key]
                own = [c for c in cands if c[1]["cls"] == org]; pool = own or cands  # prefer the organization's shirts
                if pool: break
            if not pool: stats[(org, "sem-doador", cur["cls"])] += 1; rep.append((org, name, cur["cls"], "", "", "sem-doador")); continue
            pool.sort(key=lambda c: c[0]); p, v = pool[int(hashlib.sha256((org + name).encode()).hexdigest(), 16) % len(pool)]
            struct.pack_into(">II", out, pos, p[0], p[1]); patches[org].append((pos, p[0], p[1]))
            stats[(org, "trocada", rule)] += 1; rep.append((org, name, cur["cls"], v["owner"], v["cls"], "trocada:" + rule))
        h = hashlib.sha256(out).hexdigest(); hashes[org] = h
        path = os.path.join(x.outdir, f"figacce-{org.lower()}-{h}.mpc"); open(path, "wb").write(out); print(org, h, len(out))
    with open(os.path.join(x.outdir, "relatorio.csv"), "w", newline="") as f:
        w = csv.writer(f); w.writerow(("org", "camisa", "classe_atual", "dono_doador", "classe_doador", "acao")); w.writerows(rep)
    for k, v in sorted(stats.items()): print(k, v)
    if x.header:
        L = ["#pragma once", "// Generated by bancada/camisas-org/gerar_variantes.py from the v3 alias figacce.mpc (do not edit).",
             "#include <cstdint>", "#include <cstddef>", "namespace ufc_shirt_org {", "struct Patch{uint32_t pos,first,count;};",
             f'inline constexpr const char* SourceHash="{hashlib.sha256(data).hexdigest()}";', f"inline constexpr size_t SourceSize={len(data)};"]
        for org in ("UFC", "PRIDE"):
            L.append(f"inline constexpr Patch {org.capitalize()}Patches[]={{")
            L += [f"{{{pos},{first},{count}}}," for pos, first, count in patches[org]]
            L += ["};", f'inline constexpr const char* {org.capitalize()}Hash="{hashes[org]}";']
        L.append("}  // namespace ufc_shirt_org")
        open(x.header, "w", newline="").write(chr(10).join(L) + chr(10))
        print("header", x.header)

if __name__ == "__main__": main()
