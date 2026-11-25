"""Protótipo: desenha acentos nas maiúsculas acentuadas da IndustryMenu.xpr (Reborn), usando os acentos das
próprias minúsculas da fonte. Maiúscula reduzida verticalmente para caber o acento na célula de 40 px."""
import struct, sys
from PIL import Image
SRC = sys.argv[1]; d = bytearray(open(SRC, 'rb').read())
W, H = 1024, 512; BASE = 0x2000; X = 1828
size = struct.unpack_from('>I', d, 0x28)[0]; u = bytes(d[0x80:0x80 + size])
tr = struct.unpack_from(f'>{size // 2}H', u); g = lambda c: tr[144 + c]
rec = lambda i: struct.unpack_from('>8H', u, X + 16 * i)
px = list(struct.unpack_from(f'>{W * H}H', d, BASE))
def cell(i):
    x1, y1, x2, y2 = rec(i)[:4]
    return x1, y1, x2, y2, [[(px[(y1 + y) * W + x1 + x] >> 12) & 15 for x in range(x2 - x1)] for y in range(y2 - y1)]
# lowercase glyph index of a char: the texture order follows Latin-1; find by the 'a' glyph offset
lower_a = None
for i in range(320):
    pass
# accented uppercase -> (base uppercase, accented lowercase used as accent source, base lowercase)
PAIRS = {}
for up, lo in zip('ÀÁÂÃÄÈÉÊËÌÍÎÏÒÓÔÕÖÙÚÛÜÝÑ', 'àáâãäèéêëìíîïòóôõöùúûüýñ'):
    base = {'À':'A','Á':'A','Â':'A','Ã':'A','Ä':'A','È':'E','É':'E','Ê':'E','Ë':'E','Ì':'I','Í':'I','Î':'I','Ï':'I','Ò':'O','Ó':'O','Ô':'O','Õ':'O','Ö':'O','Ù':'U','Ú':'U','Û':'U','Ü':'U','Ý':'Y','Ñ':'N'}[up]
    PAIRS[up] = (base, lo, base.lower())
def lower_index(ch):
    # BroadcastDIN-style order: lowercase accented glyphs follow the uppercase block by 32 (Latin-1 order)
    return g(ord(ch.upper())) + 32 if ord(ch) >= 0xE0 else None
ACC_ROWS = 7          # rows 0..6 for the accent
ACC_SRC = {'grave': 'è', 'acute': 'é', 'circ': 'ê', 'tilde': 'ã', 'diaer': 'ë'}
KIND = {'À':'grave','È':'grave','Ì':'grave','Ò':'grave','Ù':'grave','Á':'acute','É':'acute','Í':'acute','Ó':'acute','Ú':'acute','Ý':'acute',
        'Â':'circ','Ê':'circ','Î':'circ','Ô':'circ','Û':'circ','Ã':'tilde','Õ':'tilde','Ñ':'tilde','Ä':'diaer','Ë':'diaer','Ï':'diaer','Ö':'diaer','Ü':'diaer'}
def accent_mark(kind):
    ch = ACC_SRC[kind]; x1, y1, x2, y2, c = cell(g(ord(ch.upper())) + 32)
    rows = [y for y in range(9) if any(v > 4 for v in c[y])]
    cols = [x for x in range(len(c[0])) if any(c[y][x] > 4 for y in rows)]
    return [[c[y][x] for x in range(min(cols), max(cols) + 1)] for y in range(min(rows), max(rows) + 1)]
def build(up):
    base = PAIRS[up][0]
    bx1, by1, bx2, by2, bc = cell(g(ord(base)))
    tx1, ty1, tx2, ty2, tc = cell(g(ord(up)))
    h = len(tc); w = len(tc[0]); bw = len(bc[0])
    out = [[0] * w for _ in range(h)]
    src_top, src_bot, dst_top, dst_bot = 2, 30, ACC_ROWS + 1, 30
    for y in range(dst_top, dst_bot + 1):
        sy = src_top + (y - dst_top) * (src_bot - src_top) / (dst_bot - dst_top)
        y0 = int(sy); f = sy - y0; y1 = min(y0 + 1, len(bc) - 1)
        for x in range(min(w, bw)):
            out[y][x] = round(bc[y0][x] * (1 - f) + bc[y1][x] * f)
    # ink columns of the cap (ignore 2-px edges that may hold neighbour pixels)
    cols = [x for x in range(2, min(w, bw) - 2) if any(out[y][x] > 6 for y in range(dst_top, dst_bot + 1))] or [w // 2]
    centre = (min(cols) + max(cols) + 1) / 2
    mark = accent_mark(KIND[up]); mh = len(mark); mw = len(mark[0])
    sc = min(1.0, (ACC_ROWS - 1) / mh)
    th = max(1, round(mh * sc)); tw = max(1, round(mw * sc)) if sc < 1 else mw
    top = ACC_ROWS - 1 - th
    left = round(centre - tw / 2)
    for y in range(th):
        for x in range(tw):
            v = mark[min(mh - 1, int(y / sc))][min(mw - 1, int(x / sc))] if sc < 1 else mark[y][x]
            X_, Y_ = left + x, top + y
            if 0 <= X_ < w and 0 <= Y_ < h: out[Y_][X_] = max(out[Y_][X_], v)
    return (tx1, ty1, tx2, ty2), out
def apply(cells):
    for (x1, y1, x2, y2), out in cells:
        for y in range(y2 - y1):
            for x in range(x2 - x1):
                i = (y1 + y) * W + x1 + x
                px[i] = (px[i] & 0x0FFF) | (min(15, out[y][x]) << 12)
if __name__ == '__main__':
    cells = [build(up) for up in PAIRS]
    apply(cells)
    im = Image.frombytes('L', (W, H), bytes(((p >> 12) & 15) * 17 for p in px)); im.crop((0, 120, 1024, 172)).resize((2048, 104)).save('tex_patch.png')
    # simulate words
    def word(s):
        imgs = []
        for ch in s:
            x1, y1, x2, y2 = rec(g(ord(ch)))[:4]; imgs.append(im.crop((x1, y1, x2, y2)))
        Wt = sum(i.size[0] for i in imgs); out = Image.new('L', (Wt, 40)); x = 0
        for i in imgs: out.paste(i, (x, 0)); x += i.size[0]
        return out
    words = ['SESSÃO DE TREINAMENTO', 'ESTRATÉGIA', 'NOTÍCIAS/CLASSIFICAÇÃO', 'AÇÃO', 'PRÊMIOS', 'ADVERSÁRIO', 'SUBMISSÃO', 'PODERÁ CRISER DERROTÁ-LO?', 'POSSÍVEIS COMBINAÇÕES']
    ws = [word(s) for s in words]; sheet = Image.new('L', (max(w.size[0] for w in ws), 44 * len(ws)), 20)
    for k, w in enumerate(ws): sheet.paste(w, (0, 44 * k))
    sheet.save('palavras.png'); print('ok', len(cells))
    if len(sys.argv) > 2:
        struct.pack_into(f'>{W * H}H', d, BASE, *px); open(sys.argv[2], 'wb').write(d); print('escrito', sys.argv[2])
