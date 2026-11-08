#!/usr/bin/env python3
"""Generates src/ta-sdr.ico and docs/icon.png: a glowing spectrum peak over a waterfall."""
import numpy as np
from PIL import Image, ImageDraw, ImageFilter, ImageChops
S = 1024
rng = np.random.default_rng(7)

def palette(t):
    st = [(0,(0,0,0)),(.15,(0,0,110)),(.30,(0,50,210)),(.45,(0,190,210)),(.60,(40,220,40)),
          (.75,(240,240,0)),(.90,(255,60,0)),(1,(255,255,255))]
    t = np.clip(t, 0, 1); out = np.zeros(t.shape + (3,))
    for (t0, c0), (t1, c1) in zip(st, st[1:]):
        m = (t >= t0) & (t <= t1); u = ((t - t0) / (t1 - t0))[m]
        for k in range(3): out[..., k][m] = c0[k] + u * (c1[k] - c0[k])
    return out

# --- background: deep navy vertical gradient + soft radial glow
y, x = np.mgrid[0:S, 0:S] / S
bg = np.zeros((S, S, 3))
bg[..., 0] = 14 + 10 * (1 - y); bg[..., 1] = 18 + 14 * (1 - y); bg[..., 2] = 44 + 40 * (1 - y)
glow = np.exp(-(((x - .5) ** 2) / .10 + ((y - .45) ** 2) / .16))
bg += glow[..., None] * np.array([10, 60, 90])
img = Image.fromarray(np.clip(bg, 0, 255).astype(np.uint8)).convert("RGBA")

peaks = [(.50, 1.00, .018), (.27, .45, .012), (.74, .62, .014), (.88, .25, .010), (.13, .22, .010)]
def spectrum(xs):
    v = 0.07 + 0.03 * rng.standard_normal(xs.shape)
    for c, a, w in peaks: v = v + a * np.exp(-((xs - c) ** 2) / (2 * w * w))
    return v

# --- waterfall (bottom band), fading with age
x0, x1, y0, y1 = int(.09 * S), int(.91 * S), int(.60 * S), int(.90 * S)
W, H = x1 - x0, y1 - y0
xs = np.linspace(0, 1, W); wf = np.zeros((H, W, 3))
for r in range(H):
    age = r / H; row = spectrum(xs) * (1 - .55 * age) * (0.9 + 0.2 * rng.random())
    wf[r] = palette(np.clip(row * 1.55 - 0.02, 0, 1))
wf_img = Image.fromarray(wf.astype(np.uint8)).convert("RGBA")
wf_img = wf_img.filter(ImageFilter.GaussianBlur(2.2))
fade = Image.linear_gradient("L").resize((W, H)).transpose(Image.FLIP_TOP_BOTTOM)   # opaque at top -> fades
hfade = Image.linear_gradient("L").rotate(90).resize((W, H))            # horizontal feather
edge = ImageChops.multiply(hfade, hfade.transpose(Image.FLIP_LEFT_RIGHT)).point(lambda v: min(255, v * 6))
wf_img.putalpha(ImageChops.multiply(fade.point(lambda v: int(40 + v * .85)), edge))
img.alpha_composite(wf_img, (x0, y0))

# --- spectrum trace with glow
sx0, sx1, base, top = x0, x1, int(.58 * S), int(.14 * S)
n = sx1 - sx0; xs2 = np.linspace(0, 1, n); tr = spectrum(xs2)
tr = np.convolve(tr, np.ones(5) / 5, mode="same")
ys = base - tr * (base - top) * .98
pts = [(sx0 + i, float(ys[i])) for i in range(n)]
fill = Image.new("RGBA", (S, S), 0); d = ImageDraw.Draw(fill)
d.polygon(pts + [(sx1, base), (sx0, base)], fill=(40, 170, 230, 70))
img.alpha_composite(fill)
glow_l = Image.new("RGBA", (S, S), 0); ImageDraw.Draw(glow_l).line(pts, fill=(80, 230, 255, 255), width=20, joint="curve")
img.alpha_composite(glow_l.filter(ImageFilter.GaussianBlur(14)))
line = Image.new("RGBA", (S, S), 0); ImageDraw.Draw(line).line(pts, fill=(210, 250, 255, 255), width=10, joint="curve")
img.alpha_composite(line)

# --- tuned-frequency marker
cx = sx0 + int(.50 * n)
mk = Image.new("RGBA", (S, S), 0); dm = ImageDraw.Draw(mk)
dm.line([(cx, int(.10 * S)), (cx, int(.92 * S))], fill=(255, 70, 70, 255), width=8)
img.alpha_composite(mk.filter(ImageFilter.GaussianBlur(10)))
img.alpha_composite(mk)

# --- glass highlight, rounded-square mask, border
hl = Image.new("RGBA", (S, S), 0); dh = ImageDraw.Draw(hl)
dh.ellipse((-S * .3, -S * .85, S * 1.3, S * .42), fill=(255, 255, 255, 30))
img.alpha_composite(hl.filter(ImageFilter.GaussianBlur(40)))
mask = Image.new("L", (S, S), 0); ImageDraw.Draw(mask).rounded_rectangle((8, 8, S - 8, S - 8), radius=int(S * .22), fill=255)
border = Image.new("RGBA", (S, S), 0)
ImageDraw.Draw(border).rounded_rectangle((8, 8, S - 8, S - 8), radius=int(S * .22), outline=(150, 200, 255, 120), width=6)
img.alpha_composite(border)
out = Image.new("RGBA", (S, S), 0); out.paste(img, (0, 0), mask)

out.resize((512, 512), Image.LANCZOS).save("docs/icon.png")
out.save("src/ta-sdr.ico", sizes=[(256, 256), (128, 128), (64, 64), (48, 48), (32, 32), (24, 24), (16, 16)])
print("ok")
