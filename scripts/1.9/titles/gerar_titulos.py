"""1.9 — títulos de categoria do Reborn: peso-mosca no card antes da luta (EN) e tradução PT de todos os títulos
(card PPV, card Fight Night e placar em luta). Só os blocos DXT5 alterados são regravados; o resto do pacote fica igual.

Uso: python gerar_titulos.py <saida.h> [pasta_previa]
Variantes: tailoftape_{ppv,fn}: EN_FLY, PT, PT_FLY; exh_{ppv,ufn}: PT."""
import hashlib, os, sys
import numpy as np
from PIL import Image, ImageFilter
from pacdds import HUD, Tex, textures
from render import font, text_mask, line_boxes

PT = {'HW': 'PESO PESADO', 'LHW': 'PESO MEIO-PESADO', 'MW': 'PESO MÉDIO', 'WW': 'PESO MEIO-MÉDIO',
      'LW': 'PESO LEVE', 'FW': 'PESO-PENA', 'BW': 'PESO-GALO', 'FLY': 'PESO-MOSCA'}
EN = {'HW': 'HEAVYWEIGHT', 'LHW': 'LIGHT HEAVYWEIGHT', 'MW': 'MIDDLEWEIGHT', 'WW': 'WELTERWEIGHT',
      'LW': 'LIGHTWEIGHT', 'FW': 'FEATHERWEIGHT', 'BW': 'BANTAMWEIGHT', 'FLY': 'FLYWEIGHT'}

def card_lines(cls, champ, lang):
    if lang == 'EN': return [('UFC ' if champ else '') + EN[cls], 'CHAMPIONSHIP' if champ else 'BOUT']
    return [('UFC ' if champ else '') + PT[cls], 'CAMPEONATO' if champ else 'COMBATE']

def hud_line(cls, champ, lang):
    if lang == 'EN': return f'UFC {EN[cls]} CHAMPIONSHIP' if champ else f'{EN[cls]} BOUT'
    return f'CAMPEONATO UFC {PT[cls]}' if champ else f'COMBATE {PT[cls]}'

# ---- estilos (calibrados contra a arte original) ----
STYLES = {
    # (peso, largura, inclinação, dilatação no tamanho de render, cor, contorno, dx, dy, espessura do contorno)
    'ppv1': dict(tr=0.08, w=700, wd=85, shear=0.0, dil=5, fill=(0, 0, 0), rim=(235, 235, 235), dx=-1, dy=1, rw=1),
    'ppv2': dict(tr=0.14, w=600, wd=90, shear=0.25, dil=3, fill=(0, 0, 0), rim=(235, 235, 235), dx=-1, dy=1, rw=0),
    'fn1': dict(tr=0.07, w=700, wd=100, shear=0.0, dil=5, fill=(255, 255, 255), rim=(0, 0, 0), dx=-1, dy=1, rw=1),
    'fn2': dict(tr=0.15, w=600, wd=90, shear=0.25, dil=3, fill=(255, 255, 255), rim=(0, 0, 0), dx=-1, dy=1, rw=1),
    'hud': dict(tr=0.037, w=400, wd=100, shear=0.0, dil=0, fill=(255, 255, 255), rim=None, dx=0, dy=0, rw=0),
}
SIZE = 80

def mask_for(text, st, tracking, wd=None):
    f = font(SIZE, st['w'], wd or st['wd'])
    m = text_mask(text, f, st['shear'], tracking)
    if st['dil']: m = m.filter(ImageFilter.MaxFilter(st['dil'])); m = m.crop(m.getbbox())
    return m

def cap_ratio(text, st, tracking):
    """width/height of the rendered text (cap height only: accents excluded by measuring a no-accent copy)."""
    m = mask_for(text, st, tracking); return m.size[0] / m.size[1]

def solve_tracking(text, st, target_ratio):
    lo, hi = -0.05, 0.4
    for _ in range(30):
        mid = (lo + hi) / 2
        if cap_ratio(text, st, mid) < target_ratio: lo = mid
        else: hi = mid
    return (lo + hi) / 2

ACC = str.maketrans('ÉÊÁÍÓÚÃÕÇ', 'EEAIOUAOC')

def render_line(canvas, text, st, band, maxw, tracking, room=0):
    """band: (y0,y1,x0,x1) of the original line; the new line keeps the cap height and centre."""
    y0, y1, x0, x1 = band; h = y1 - y0; cx = (x0 + x1) / 2
    plain = mask_for(text.translate(ACC), st, tracking)
    full = mask_for(text, st, tracking)
    scale = h / plain.size[1]                       # cap height from the unaccented copy
    extra = full.size[1] - plain.size[1]            # accent height above the caps
    if extra * scale > room:                        # no room for the accent: shrink the whole line to fit
        scale = (h + room) / full.size[1]
    nh = round(full.size[1] * scale); nw = round(full.size[0] * scale)
    wd = st['wd']
    while nw > maxw and wd > 75:                    # narrower letters first, then squeeze
        wd -= 5; full = mask_for(text, st, tracking, wd); nw = round(full.size[0] * scale)
    nw = min(nw, maxw)
    m = full.resize((nw, nh), Image.LANCZOS)
    top = y1 - nh
    xo = round(cx - nw / 2)
    if st['rim'] is not None:
        rm = m.filter(ImageFilter.MaxFilter(2 * st['rw'] + 1)) if st['rw'] else m
        layer = Image.new('RGBA', m.size, st['rim'] + (0,)); layer.putalpha(rm)
        canvas.alpha_composite(layer, (xo + st['dx'], top + st['dy']))
    layer = Image.new('RGBA', m.size, st['fill'] + (0,)); layer.putalpha(m)
    canvas.alpha_composite(layer, (xo, top))

def free_above(alpha, band, cap=8):
    y0, _, x0, x1 = band; a = np.asarray(alpha); n = 0
    while n < cap + 1 and y0 - n - 1 >= 0 and not (a[y0 - n - 1, x0:x1] > 8).any(): n += 1
    return max(0, n - 1)

def bands_in(alpha, rect, thr=40, gap=30):
    """Text lines inside rect (x0,y0,x1,y1), split into x-runs separated by more than `gap` empty columns."""
    x0, y0, x1, y1 = rect
    a = np.asarray(alpha)[y0:y1, x0:x1] > thr
    out = []
    for by0, by1, _, _ in line_boxes(Image.fromarray((a * 255).astype(np.uint8))):
        cols = a[by0:by1].any(0); xs = np.where(cols)[0]; start = prev = xs[0]
        for x in list(xs[1:]) + [None]:
            if x is None or x - prev > gap:
                out.append((y0 + by0, y0 + by1, x0 + start, x0 + prev + 1))
                if x is not None: start = x
            if x is not None: prev = x
    return out

def clear(img, rect):
    a = np.array(img); x0, y0, x1, y1 = rect
    a[max(0, y0):y1, max(0, x0):x1] = 0; return Image.fromarray(a)

# ---- slots: (pacote, textura) → lista de títulos {cls, champ, style pair, lines bands, clear rect} ----
def card_titles(img, rect, order, champ, kind):
    """Groups bands inside rect into (line1, line2) pairs in vertical order."""
    bs = sorted(bands_in(img.getchannel('A'), rect, gap=60), key=lambda b: b[0])
    assert len(bs) == 2 * len(order), (rect, bs)
    out = []
    for i, cls in enumerate(order):
        l1, l2 = bs[2 * i], bs[2 * i + 1]
        out.append(dict(cls=cls, champ=champ, kind=kind, lines=[l1, l2]))
    return out

def plan(pac, k, img):
    W, H = img.size
    if pac == 'hud_tailoftape_ppv.pac':
        if k == 4:
            return (card_titles(img, (0, 0, 400, 400), ['HW', 'LHW', 'MW', 'WW', 'LW'], True, 'ppv') +
                    card_titles(img, (400, 0, 760, 400), ['HW', 'LHW', 'MW', 'WW', 'LW'], False, 'ppv'))
        cls, champ = {5: ('BW', False), 6: ('BW', True), 7: ('FW', False), 8: ('FW', True)}[k]
        return card_titles(img, (0, 0, W, H), [cls], champ, 'ppv')
    if pac == 'hud_tailoftape_fn.pac':
        if k == 9:
            return (card_titles(img, (200, 0, 470, 150), ['LW', 'MW', 'WW'], False, 'fn') +
                    card_titles(img, (0, 150, 330, 245), ['HW', 'LHW'], False, 'fn'))
        cls = {6: 'BW', 7: 'FW'}[k]
        return card_titles(img, (0, 0, W, H), [cls], False, 'fn')
    # placar (exh_ppv tex13 / exh_ufn tex44)
    A = img.getchannel('A'); out = []
    rows = [((0, 611, 1800, 638), ['LHW', 'LHW', 'MW', 'MW']), ((0, 638, 1800, 666), ['WW', 'WW', 'LW', 'LW']),
            ((0, 666, 1800, 694), ['FW', 'FW', 'BW', 'BW']), ((0, 694, 860, 722), ['FLY', 'FLY']),
            ((1110, 388, 2048, 415), ['HW', 'HW'])]
    for rect, classes in rows:
        bs = sorted(bands_in(A, rect, gap=40), key=lambda b: b[2])
        assert len(bs) == len(classes), (rect, bs)
        for i, (b, cls) in enumerate(zip(bs, classes)):
            out.append(dict(cls=cls, champ=bool(i % 2), kind='hud', lines=[b]))
    return out

def title_rect(t, pad=3):
    ys = [b[0] for b in t['lines']] + [b[1] for b in t['lines']]
    xs = [b[2] for b in t['lines']] + [b[3] for b in t['lines']]
    return (min(xs) - pad, min(ys) - pad, max(xs) + pad, max(ys) + pad)

def draw(img, titles, lang, fly_from_bw):
    out = img; A0 = img.getchannel('A')
    for t in titles:
        out = clear(out, title_rect(t))
    for t in titles:
        cls = 'FLY' if (fly_from_bw and t['cls'] == 'BW') else t['cls']
        x0, _, x1, _ = title_rect(t, 0); maxw = x1 - x0
        if t['kind'] == 'hud':
            st = STYLES['hud']; b = t['lines'][0]
            ref = hud_line(t['cls'], t['champ'], 'EN')
            tr = st['tr']
            render_line(out, hud_line(cls, t['champ'], lang), st, b, maxw, tr, free_above(A0, b))
        else:
            refs = card_lines(t['cls'], t['champ'], 'EN'); news = card_lines(cls, t['champ'], lang)
            for i, (b, ref, new) in enumerate(zip(t['lines'], refs, news)):
                st = STYLES[t['kind'] + str(i + 1)]
                tr = st['tr']
                render_line(out, new, st, b, maxw, tr, free_above(A0, b) if i == 0 else 0)
    return out

TARGETS = {'hud_tailoftape_ppv.pac': [4, 5, 6, 7, 8], 'hud_tailoftape_fn.pac': [6, 7, 9],
           'exh_ppv.pac': [13], 'exh_ufn.pac': [44]}
VARIANTS = {'hud_tailoftape_ppv.pac': ['EN_FLY', 'PT', 'PT_FLY'], 'hud_tailoftape_fn.pac': ['EN_FLY', 'PT', 'PT_FLY'],
            'exh_ppv.pac': ['PT'], 'exh_ufn.pac': ['PT']}
FLY_TEX = {'hud_tailoftape_ppv.pac': (5, 6), 'hud_tailoftape_fn.pac': (6,)}

def main(out_h, preview=None):
    blobs = []; entries = []
    for pac, ks in TARGETS.items():
        data = open(HUD + pac, 'rb').read(); offs = textures(data)
        src_hash = hashlib.sha256(data).hexdigest()
        for var in VARIANTS[pac]:
            lang = var[:2]; fly = var.endswith('FLY'); new = bytearray(data)
            for k in ks:
                tex = Tex(data, offs[k]); img = tex.image(); titles = plan(pac, k, img)
                if lang == 'EN':
                    titles = [t for t in titles if k in FLY_TEX.get(pac, ())]
                    if not titles: continue
                res = draw(img, titles, lang, fly and k in FLY_TEX.get(pac, ()))
                from pacdds import encode_changed
                blocks = encode_changed(tex, img, res)
                new[tex.pix:tex.pix + len(blocks)] = blocks
                if preview:
                    chk = Tex(bytes(new), offs[k]).image()
                    bg = Image.new('RGBA', chk.size, (110, 110, 110, 255)); bg.alpha_composite(chk)
                    if chk.size[0] > 1024: bg = bg.crop((0, 380, 2048, 730))
                    bg.convert('RGB').save(os.path.join(preview, f'{pac[:-4]}_{var}_{k}.png'))
            runs = []; i = 0; n = len(data)
            while i < n:
                if new[i] != data[i]:
                    j = i
                    while j < n and (new[j] != data[j] or (j + 16 < n and new[j:j + 16] != data[j:j + 16])): j += 1
                    runs.append((i, bytes(new[i:j]))); i = j
                else: i += 1
            entries.append((pac, var, src_hash, hashlib.sha256(new).hexdigest(), len(data), runs))
            print(pac, var, 'runs', len(runs), 'bytes', sum(len(r[1]) for r in runs))
    write_header(out_h, entries)

def write_header(path, entries):
    blob = bytearray(); L = ['#pragma once',
         '// Generated by bancada/titulos/gerar_titulos.py (do not edit). Reborn 1.1 category titles: flyweight on the',
         '// pre-fight card (EN) and Portuguese titles (card PPV / Fight Night and in-fight scoreboard). DXT5 blocks only.',
         '#include <cstddef>', '#include <cstdint>', 'namespace ufc_titles_data {',
         'struct Run { uint32_t offset, size, blob; };',
         'struct Variant { const char* file; const char* name; const char* source; const char* target; size_t file_size; const Run* runs; size_t run_count; };']
    for idx, (pac, var, src, dst, size, runs) in enumerate(entries):
        items = []
        for off, b in runs: items.append(f'{{{off},{len(b)},{len(blob)}}}'); blob += b
        L.append(f'inline constexpr Run V{idx}[] = {{' + ','.join(items) + '};')
    L.append('inline constexpr Variant Variants[] = {')
    for idx, (pac, var, src, dst, size, runs) in enumerate(entries):
        L.append(f'  {{"{pac}", "{var}", "{src}", "{dst}", {size}, V{idx}, {len(runs)}}},')
    L.append('};')
    L.append(f'inline constexpr size_t BlobSize = {len(blob)};')
    L.append('inline constexpr unsigned char Blob[] = {')
    for i in range(0, len(blob), 40): L.append(','.join(str(x) for x in blob[i:i + 40]) + ',')
    L.append('};', ); L.append('}  // namespace ufc_titles_data')
    open(path, 'w', encoding='utf-8', newline='\n').write('\n'.join(L) + '\n')
    print('header', path, 'blob', len(blob))

if __name__ == '__main__':
    main(sys.argv[1], sys.argv[2] if len(sys.argv) > 2 else None)
