"""Turn the supplied seal-zoid image into setup art (sprite, icons, header)."""
import sys
from collections import Counter, deque
from pathlib import Path

import numpy as np
from PIL import Image

SRC = Path(sys.argv[1])
AFRO_HEADER = Path(sys.argv[2])
OUT = Path(sys.argv[3])
OUT.mkdir(parents=True, exist_ok=True)

img = np.asarray(Image.open(SRC).convert("RGB")).astype(np.int32)
H, W, _ = img.shape


def best_grid(profile, lo=12.0, hi=13.6):
    """Period and phase whose cell borders sit on the strongest edges."""
    best = None
    for T in np.arange(lo, hi, 0.02):
        for ph in np.arange(0, T, 0.25):
            idx = np.round(np.arange(ph, len(profile), T)).astype(int)
            idx = idx[idx < len(profile)]
            s = profile[idx].mean()
            if best is None or s > best[0]:
                best = (s, T, ph)
    return best[1], best[2]


dx = np.abs(np.diff(img, axis=1)).sum(axis=2).sum(axis=0).astype(float)
dy = np.abs(np.diff(img, axis=0)).sum(axis=2).sum(axis=1).astype(float)
Tx, px = best_grid(dx)
Ty, py = best_grid(dy)
print(f"grid x T={Tx:.2f} phase={px:.2f}  y T={Ty:.2f} phase={py:.2f}")

xs = np.arange(px + 0.5, W, Tx)
ys = np.arange(py + 0.5, H, Ty)
gw, gh = len(xs) - 1, len(ys) - 1
cells = np.zeros((gh, gw, 3), np.int32)
for j in range(gh):
    for i in range(gw):
        x0, x1 = int(xs[i] + Tx * 0.25), int(xs[i] + Tx * 0.75)
        y0, y1 = int(ys[j] + Ty * 0.25), int(ys[j] + Ty * 0.75)
        cells[j, i] = np.median(img[y0:y1 + 1, x0:x1 + 1].reshape(-1, 3), axis=0)
print("cells", gw, gh)

# Palette: k-means in RGB.
flat = cells.reshape(-1, 3).astype(float)
K = 28
rng = np.random.default_rng(1)
cent = flat[rng.choice(len(flat), K, replace=False)]
for _ in range(40):
    d = ((flat[:, None, :] - cent[None]) ** 2).sum(-1)
    lab = d.argmin(1)
    for k in range(K):
        if (lab == k).any():
            cent[k] = flat[lab == k].mean(0)
pal = np.round(cent).astype(np.int32)
q = pal[lab].reshape(gh, gw, 3)

# Background: flood fill from the border through cells close to the backdrop.
bg = np.array([31, 31, 33])
near = (np.abs(q - bg).sum(-1) <= 18)
alpha = np.full((gh, gw), 255, np.uint8)
seen = np.zeros((gh, gw), bool)
dq = deque([(j, i) for j in range(gh) for i in (0, gw - 1)] + [(j, i) for i in range(gw) for j in (0, gh - 1)])
while dq:
    j, i = dq.popleft()
    if not (0 <= j < gh and 0 <= i < gw) or seen[j, i] or not near[j, i]:
        continue
    seen[j, i] = True
    alpha[j, i] = 0
    dq.extend([(j + 1, i), (j - 1, i), (j, i + 1), (j, i - 1)])

ys_, xs_ = np.nonzero(alpha)
q = q[ys_.min():ys_.max() + 1, xs_.min():xs_.max() + 1]
alpha = alpha[ys_.min():ys_.max() + 1, xs_.min():xs_.max() + 1]
sh, sw = alpha.shape
rgba = np.dstack([q, alpha]).astype(np.uint8)
sprite = Image.fromarray(rgba, "RGBA")
sprite.save(OUT / "lomorsin_zoids_sprite.png")
print("sprite", sw, sh)


def on_square(im, size, scale):
    canvas = Image.new("RGBA", (size, size), (0, 0, 0, 0))
    big = im.resize((im.width * scale, im.height * scale), Image.NEAREST)
    canvas.alpha_composite(big, ((size - big.width) // 2, size - big.height - (size - big.height) // 2))
    return canvas


def shrink(im, size):
    """Area-average down, then snap colours back to the palette and alpha to 0/255."""
    side = max(im.width, im.height)
    sq = Image.new("RGBA", (side, side), (0, 0, 0, 0))
    sq.alpha_composite(im, ((side - im.width) // 2, (side - im.height) // 2))
    a = np.asarray(sq).astype(float)
    pre = a.copy()
    pre[..., :3] *= a[..., 3:4] / 255.0
    small = np.asarray(Image.fromarray(pre.astype(np.uint8), "RGBA").resize((size, size), Image.BOX)).astype(float)
    al = small[..., 3]
    rgb = np.where(al[..., None] > 0, small[..., :3] * 255.0 / np.maximum(al[..., None], 1), 0)
    d = ((rgb[..., None, :] - pal[None, None].astype(float)) ** 2).sum(-1)
    rgb = pal[d.argmin(-1)]
    out = np.dstack([rgb, np.where(al >= 110, 255, 0)]).astype(np.uint8)
    return Image.fromarray(out, "RGBA")


master = on_square(sprite, 512, 512 // max(sw, sh))
master.save(OUT / "lomorsin_zoids_512.png")
icons = {}
for s in (256, 128, 64, 48, 32, 24, 16):
    scale = s // max(sw, sh)
    icons[s] = on_square(sprite, s, scale) if scale >= 1 else shrink(sprite, s)
    icons[s].save(OUT / f"icono_{s}.png")
icons[256].save(OUT / "lomorsin_zoids.ico", sizes=[(s, s) for s in (16, 24, 32, 48, 64, 128, 256)],
                append_images=[icons[s] for s in (16, 24, 32, 48, 64, 128)])

# Header: Afro's backdrop rows, its stripe recoloured blue, the seal zoid on the right.
afro = np.asarray(Image.open(AFRO_HEADER).convert("RGBA")).astype(np.int32)
hh, hw = afro.shape[:2]
head = np.repeat(afro[:, 100:101], hw, axis=1).copy()
red = (afro[..., 0] > afro[..., 1] + 60) & (afro[..., 0] > 120)
keep = red.copy()
keep[:hh - 6, 1000:] = False  # Afro's headband dot is red too
accent = np.array([86, 168, 232])  # the spikes' blue
lum = afro[..., 0] / 255.0
for c in range(3):
    head[..., c] = np.where(keep, np.clip(accent[c] * lum / (180 / 255.0), 0, 255), head[..., c])
header = Image.fromarray(head.astype(np.uint8), "RGBA")
scale = 2
big = sprite.resize((sw * scale, sh * scale), Image.NEAREST)
x = hw - big.width - 40
y = hh - 6 - big.height
header.alpha_composite(big, (x, max(y, 0)))
header.save(OUT / "cabecera_lomorsin_zoids_1200x240.png")
Image.open(SRC).convert("RGB").save(OUT / "previa_lomorsin_zoids.png")
print("header seal at", x, y, big.size)

# Small icons: the head reads better than the whole body at 16-32 px.
a = np.asarray(sprite)[..., 3]
cols = np.nonzero(a[: sh // 2].any(0))[0]
head_box = (int(cols.min()) if len(cols) else 0, 0, sw, int(sh * 0.55))
# Use the right-hand part (the head sits top-right) and make it square.
hx0 = max(0, sw - int(sh * 0.58)); head = sprite.crop((hx0, 0, sw, int(sh * 0.58)))
head.save(OUT / "lomorsin_zoids_head.png")
for s in (32, 24, 16):
    icons[s] = shrink(head, s)
    icons[s].save(OUT / f"icono_{s}.png")
icons[256].save(OUT / "lomorsin_zoids.ico", sizes=[(s, s) for s in (16, 24, 32, 48, 64, 128, 256)],
                append_images=[icons[s] for s in (16, 24, 32, 48, 64, 128)])
print("head crop", head.size)
