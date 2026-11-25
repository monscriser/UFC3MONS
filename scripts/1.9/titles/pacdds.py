"""Leitura/escrita de texturas DXT5 embutidas nos pacotes .pac do Reborn (DDS linear, sem mipmaps úteis)."""
import io, re, struct
import numpy as np
from PIL import Image
HUD = 'E:/Undisputed Reborn 1.1/UFC Undisputed Reborn 1.1/DAT/hud/'

class Tex:
    def __init__(self, data, off):
        h = data[off + 4:off + 128]
        _, _, self.h, self.w = struct.unpack('<4I', h[:16])
        assert h[80:84] == b'DXT5'
        self.off = off; self.pix = off + 128
        self.bw = (self.w + 3) // 4; self.bh = (self.h + 3) // 4
        self.blocks = bytearray(data[self.pix:self.pix + self.bw * self.bh * 16])
    def image(self):
        hdr = bytearray(128); hdr[:4] = b'DDS '
        struct.pack_into('<7I', hdr, 4, 124, 0x81007, self.bh * 4, self.bw * 4, self.bw * self.bh * 16, 0, 0)
        struct.pack_into('<2I4s', hdr, 76, 32, 4, b'DXT5')
        struct.pack_into('<I', hdr, 108, 0x1000)
        im = Image.open(io.BytesIO(bytes(hdr) + bytes(self.blocks))); im.load()
        return im.convert('RGBA')  # padded to multiple of 4

def textures(data):
    return [m.start() for m in re.finditer(b'DDS ', data)]

def load(pac, k):
    d = open(HUD + pac, 'rb').read(); return d, Tex(d, textures(d)[k])

# ---- DXT5 block encoder (range fit, exhaustive alpha endpoints from min/max) ----
def _565(c):
    c = np.clip(np.round(c), 0, 255).astype(int)
    return ((c[0] >> 3) << 11) | ((c[1] >> 2) << 5) | (c[2] >> 3)
def _un565(v):
    r = (v >> 11) & 31; g = (v >> 5) & 63; b = v & 31
    return np.array([(r << 3) | (r >> 2), (g << 2) | (g >> 4), (b << 3) | (b >> 2)], float)

def encode_block(px):
    """px: (16,4) uint8 RGBA → 16 bytes DXT5."""
    px = px.astype(float); a = px[:, 3]; rgb = px[:, :3]
    # alpha
    a0, a1 = int(a.max()), int(a.min())
    if a0 == a1:
        abytes = bytes([a0, a1]) + bytes(6)
    else:
        pal = [a0, a1] + [((7 - i) * a0 + i * a1) / 7 for i in range(1, 7)]
        idx = np.argmin(np.abs(a[:, None] - np.array(pal)[None, :]), axis=1)
        bits = 0
        for i, v in enumerate(idx): bits |= int(v) << (3 * i)
        abytes = bytes([a0, a1]) + bits.to_bytes(6, 'little')
    # color: weight by alpha so transparent texels don't pull endpoints
    w = a / 255.0
    if w.sum() < 1e-3: w = np.ones(16)
    mean = (rgb * w[:, None]).sum(0) / w.sum()
    cov = ((rgb - mean) * w[:, None]).T @ (rgb - mean)
    ev, evec = np.linalg.eigh(cov); axis = evec[:, -1]
    proj = (rgb - mean) @ axis
    sel = w > 0.05
    if not sel.any(): sel = np.ones(16, bool)
    lo, hi = mean + axis * proj[sel].min(), mean + axis * proj[sel].max()
    c0, c1 = _565(hi), _565(lo)
    if c0 < c1: c0, c1 = c1, c0
    if c0 == c1:
        cbytes = struct.pack('<HHI', c0, c1, 0)
    else:
        e0, e1 = _un565(c0), _un565(c1)
        pal = np.array([e0, e1, (2 * e0 + e1) / 3, (e0 + 2 * e1) / 3])
        idx = np.argmin(((rgb[:, None, :] - pal[None]) ** 2).sum(2), axis=1)
        bits = 0
        for i, v in enumerate(idx): bits |= int(v) << (2 * i)
        cbytes = struct.pack('<HHI', c0, c1, bits)
    return abytes + cbytes

def encode_changed(tex, orig_img, new_img):
    """Re-encode only blocks whose decoded pixels changed; returns new block bytes."""
    o = np.asarray(orig_img); n = np.asarray(new_img); out = bytearray(tex.blocks)
    for by in range(tex.bh):
        for bx in range(tex.bw):
            a = o[by*4:by*4+4, bx*4:bx*4+4]; b = n[by*4:by*4+4, bx*4:bx*4+4]
            if np.array_equal(a, b): continue
            k = (by * tex.bw + bx) * 16
            out[k:k + 16] = encode_block(b.reshape(16, 4))
    return out
