#!/usr/bin/env python3
"""Builds the shader list shipped with the launcher ("seed") by merging the shareable shader storage
(<title>.xsh + <title>.fbo.vk.xpso) of many play sessions. Only the list is merged: every player's
own driver compiles it on their own GPU (ArePipelineRequirementsMet drops what their GPU lacks).

usage: shader_seed.py <out_dir> <edition> <xsh_or_dir>...   (edition only names the output folder)
Pipeline records (68 bytes on Windows; Linux is 66, made by seed_to_linux.py) are checked with XXH3 like the game does; records are deduplicated by ucode hash (.xsh) and description hash (.xpso); a file whose header
differs from the newest version is skipped, so stale caches from older builds are never mixed in."""
import os, struct, sys
import xxhash  # the game checks XXH3_64(description) == stored hash; same test here

TITLE = "5451087D"
XSH_MAGIC, XPSO_MAGIC = 0x48534558, 0x53504558


def read_xsh(path):
    d = open(path, "rb").read()
    if len(d) < 8:
        return None, []
    magic, ver = struct.unpack_from("<II", d, 0)
    if magic != XSH_MAGIC:
        return None, []
    out, o = [], 8
    while o + 12 <= len(d):
        h, bits = struct.unpack_from("<QI", d, o)
        n = (bits & 0x7FFFFFFF) * 4
        if o + 12 + n > len(d):
            break
        out.append((h, d[o:o + 12 + n]))
        o += 12 + n
    return ver, out


def read_xpso(path, rec):
    d = open(path, "rb").read()
    if len(d) < 12:
        return None, []
    magic, api, ver = struct.unpack_from("<III", d, 0)
    if magic != XPSO_MAGIC:
        return None, []
    if (len(d) - 12) % rec:
        return None, []  # another build's record size
    out = []
    for i in range((len(d) - 12) // rec):
        b = d[12 + i * rec:12 + (i + 1) * rec]
        h = struct.unpack_from("<Q", b, 0)[0]
        if xxhash.xxh3_64_intdigest(b[8:]) != h:
            return None, []  # not this build's layout (or corrupted): skip the whole file
        out.append((h, b))
    return (api, ver), out


def pairs(args):
    for a in args:
        if os.path.isdir(a):
            for root, _, files in os.walk(a):
                if TITLE + ".xsh" in files and TITLE + ".fbo.vk.xpso" in files:
                    yield os.path.join(root, TITLE + ".xsh"), os.path.join(root, TITLE + ".fbo.vk.xpso")
        elif a.endswith(".xsh"):
            yield a, a[:-4] + ".fbo.vk.xpso"


def main():
    out, edition, srcs = sys.argv[1], sys.argv[2], sys.argv[3:]
    rec = int(os.environ.get("XPSO_RECORD", "68"))
    found = list(pairs(srcs))
    files = [(x, p, read_xsh(x), read_xpso(p, rec)) for x, p in found]
    newest = max((f[3][0] for f in files if f[3][0]), key=lambda h: h[1])
    shaders, pipes, used = {}, {}, 0
    for x, p, (xver, xs), (ph, ps) in files:
        ok = ph == newest and xver is not None
        print(("usa   " if ok else "pula  ") + f"{len(xs):5} shaders {len(ps):5} pipelines  {os.path.dirname(x)}")
        if not ok:
            continue
        used += 1
        for h, b in xs:
            shaders.setdefault(h, b)
        for h, b in ps:
            pipes.setdefault(h, b)
    d = os.path.join(out, edition)
    os.makedirs(d, exist_ok=True)
    with open(os.path.join(d, TITLE + ".xsh"), "wb") as f:
        f.write(struct.pack("<II", XSH_MAGIC, xver))
        for b in shaders.values():
            f.write(b)
    with open(os.path.join(d, TITLE + ".fbo.vk.xpso"), "wb") as f:
        f.write(struct.pack("<III", XPSO_MAGIC, newest[0], newest[1]))
        for b in pipes.values():
            f.write(b)
    print(f"{edition}: {used} sessoes, {len(shaders)} shaders, {len(pipes)} pipelines (registro {rec} bytes, versao {newest[1]:08X})")


if __name__ == "__main__":
    main()
