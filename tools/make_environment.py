#!/usr/bin/env python3
"""Generate a small equirectangular dusk city environment map (P6 PPM).

Run: python3 tools/make_environment.py
The output is intentionally dependency-free and is used by the GLES3
skybox + mipmapped environment-lighting path.
"""
import math
import os
import random

W, H = 512, 256
rng = random.Random(2026)
pixels = bytearray(W * H * 3)
# Deterministic skyline silhouette, repeated seamlessly around the horizon.
buildings = []
x = -4
while x < W + 8:
    width = rng.randint(10, 27)
    height = rng.randint(18, 62)
    buildings.append((x, x + width, height, rng.random() < 0.58))
    x += width + rng.randint(1, 5)

sun_x, sun_y = int(W * 0.73), int(H * 0.47)
for y in range(H):
    v = y / (H - 1)
    for x in range(W):
        u = x / W
        # Equirectangular sky above the horizon and dim reflected city below.
        if y < H // 2:
            t = min(1.0, y / (H * 0.50))
            sky_top = (18, 39, 79)
            sky_horizon = (205, 91, 43)
            blend = min(1.0, max(0.0, t ** 1.55))
            col = [round(a * (1 - blend) + b * blend) for a, b in zip(sky_top, sky_horizon)]
            # Soft sunset band with a broad radial glow.
            dx = min(abs(x - sun_x), W - abs(x - sun_x)) / W
            dy = (y - sun_y) / H
            glow = math.exp(-((dx * 9.0) ** 2 + (dy * 13.0) ** 2)) * 0.35
            col = [min(255, round(c * (1 - glow) + h * glow))
                   for c, h in zip(col, (255, 190, 116))]
            if dx * dx + dy * dy < (0.018 ** 2):
                col = [255, 220, 159]
        else:
            depth = (y - H // 2) / (H // 2)
            col = [round(14 - 7 * depth), round(22 - 10 * depth), round(34 - 12 * depth)]

        # Black-blue skyline above the horizon, with sparse warm windows.
        for left, right, bh, has_windows in buildings:
            wrapped_x = x
            if left <= wrapped_x < right:
                roof = H // 2 - bh
                if roof <= y < H // 2 + 2:
                    col = [12, 18, 29]
                    if has_windows and y < H // 2 - 4 and (x - left) % 6 in (1, 2) and y % 9 in (2, 3):
                        col = [157, 102, 55]
                    break
        i = (y * W + x) * 3
        pixels[i:i+3] = bytes(col)

out_dir = os.path.join(os.path.dirname(__file__), "..", "samples", "CarScene", "environment")
os.makedirs(out_dir, exist_ok=True)
path = os.path.join(out_dir, "city_dusk.ppm")
with open(path, "wb") as f:
    f.write(f"P6\n# Mini-Engine procedural equirectangular dusk city\n{W} {H}\n255\n".encode("ascii"))
    f.write(pixels)
print(f"Wrote {path} ({W}x{H}, {len(pixels)} RGB bytes)")
