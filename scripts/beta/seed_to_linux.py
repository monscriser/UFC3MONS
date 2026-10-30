#!/usr/bin/env python3
"""Converts the Windows pipeline list (68-byte records, MS bitfield layout) to the Linux layout (66 bytes).
clang -fdump-record-layouts of PipelineStoredDescription for both recipes (29/10): every field has the same
byte/bit position up to description byte 41; Windows then has 2 padding bytes and render_targets[4] at
description byte 44, Linux has render_targets at byte 42 (PipelineRenderTarget is identical, 4 bytes).
So linux = description[0:42] + description[44:60], and description_hash = XXH3_64(linux description).

usage: seed_to_linux.py <windows .fbo.vk.xpso> <out .fbo.vk.xpso> [<linux .xpso to compare>...]
With linux files, every pipeline found in both (same description_hash after conversion) is compared byte by
byte; a converted record for a pipeline the Linux files also have must be identical to the Linux one."""
import struct, sys
import xxhash

W, L = 68, 66


def records(path, rec):
    d = open(path, "rb").read()
    out = []
    for i in range((len(d) - 12) // rec):
        b = d[12 + i * rec:12 + (i + 1) * rec]
        if xxhash.xxh3_64_intdigest(b[8:]) != struct.unpack_from("<Q", b, 0)[0]:
            raise SystemExit(f"registro {i} invalido em {path}")
        out.append(b)
    return d[:12], out


def convert(b):
    desc = b[8:]
    if desc[42:44] != b"\0\0":
        raise SystemExit("bytes de preenchimento nao zerados: layout inesperado")
    nd = desc[:42] + desc[44:]
    return struct.pack("<Q", xxhash.xxh3_64_intdigest(nd)) + nd


def main():
    header, recs = records(sys.argv[1], W)
    conv = [convert(b) for b in recs]
    with open(sys.argv[2], "wb") as f:
        f.write(header)
        for b in conv:
            f.write(b)
    print(f"convertidos {len(conv)} pipelines -> {sys.argv[2]}")
    mine = {b[:8]: b for b in conv}
    by_shaders = {b[8:40]: b for b in conv}  # vs hash/mod + ps hash/mod
    same = differ = absent = 0
    for lp in sys.argv[3:]:
        lh, lrecs = records(lp, L)
        if lh != header:
            print("cabecalho diferente:", lp); continue
        for b in lrecs:
            if b[:8] in mine:
                same += mine[b[:8]] == b
            elif b[8:40] in by_shaders:
                differ += 1  # same shaders, other state: not a layout proof either way
            else:
                absent += 1
    if len(sys.argv) > 3:
        print(f"Linux: {same} identicos apos conversao, {differ} mesmos shaders com outro estado, {absent} sem par no Windows")


if __name__ == "__main__":
    main()
