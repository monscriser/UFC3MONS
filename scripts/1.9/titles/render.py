"""Renderização dos títulos de categoria (card PPV/FN e placar) com a Bahnschrift do Windows (desenho DIN).
Mede as linhas de texto na arte original para reproduzir altura, centro e largura máxima."""
import numpy as np
from PIL import Image, ImageDraw, ImageFont, ImageFilter
FONT = 'C:/Windows/Fonts/bahnschrift.ttf'

def font(size, weight=700, width=85):
    f = ImageFont.truetype(FONT, size)
    f.set_variation_by_axes([weight, width])
    return f

def line_boxes(alpha, thr=40):
    """Horizontal text bands (y0,y1) and their x extents in an alpha image."""
    a = np.asarray(alpha) > thr
    rows = a.any(axis=1); bands = []; y = 0
    while y < len(rows):
        if rows[y]:
            y0 = y
            while y < len(rows) and rows[y]: y += 1
            cols = a[y0:y].any(axis=0); xs = np.where(cols)[0]
            bands.append((y0, y, int(xs[0]), int(xs[-1]) + 1))
        else: y += 1
    return bands

def text_mask(text, f, shear=0.0, tracking=0.0):
    """Glyph mask; tracking is extra space between letters as a fraction of the font size; shear > 0 slants right."""
    size = f.size; gap = tracking * size
    widths = [f.getlength(c) for c in text]
    total = int(sum(widths) + gap * max(0, len(text) - 1)) + 16
    l, t, r, b = f.getbbox(text)
    im = Image.new('L', (total, b - t + 16), 0); dr = ImageDraw.Draw(im); x = 8.0
    for c, w in zip(text, widths):
        dr.text((x, 8 - t), c, font=f, fill=255); x += w + gap
    if shear:
        w, h = im.size; extra = int(abs(shear) * h) + 2
        im = im.transform((w + extra, h), Image.AFFINE, (1, shear, -extra * (shear > 0) + shear * 0, 0, 1, 0), Image.BICUBIC)
    return im.crop(im.getbbox())

def fit(mask, height, maxw):
    w, h = mask.size; nw = max(1, round(w * height / h)); 
    if nw > maxw: nw = maxw
    return mask.resize((nw, height), Image.LANCZOS)

def compose_card_line(canvas, mask, cx, y0, fill=(0, 0, 0), rim=(235, 235, 235), rim_dx=-1, rim_dy=1, rim_w=1):
    """Black glyphs with a light rim offset to the lower left, like the PPV card art."""
    x0 = round(cx - mask.size[0] / 2)
    rim_mask = mask.filter(ImageFilter.MaxFilter(2 * rim_w + 1)) if rim_w else mask
    rim_layer = Image.new('RGBA', mask.size, rim + (0,)); rim_layer.putalpha(rim_mask)
    canvas.alpha_composite(rim_layer, (x0 + rim_dx, y0 + rim_dy))
    canvas.alpha_composite(Image.merge('RGBA', (*Image.new('RGB', mask.size, fill).split(), mask)), (x0, y0))

def compose_plain(canvas, mask, cx, y0, fill=(255, 255, 255), shadow=None):
    x0 = round(cx - mask.size[0] / 2)
    if shadow:
        sh = Image.merge('RGBA', (*Image.new('RGB', mask.size, shadow[0]).split(), mask.point(lambda v: v * shadow[1] // 255)))
        canvas.alpha_composite(sh, (x0 + shadow[2], y0 + shadow[3]))
    canvas.alpha_composite(Image.merge('RGBA', (*Image.new('RGB', mask.size, fill).split(), mask)), (x0, y0))
