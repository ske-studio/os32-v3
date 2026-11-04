"""One left-to-right signed integer Floyd-Steinberg pass with frozen orders."""
import numpy as np
from osri_enc2_spec import Config, distance
from osri_enc2_timing import display_palette


def trunc16(n):
    return n // 16 if n >= 0 else -((-n) // 16)


def quantize(pixels, palettes, orders, cfg=Config(), *, timed=True, dither=True):
    height, width = pixels.shape[:2]
    indices = np.empty((height, width), dtype=np.uint8)
    error = [[0, 0, 0] for _ in range(width + 2)]
    for y in range(height):
        visible = (display_palette(palettes[y - 1], palettes[y], orders[y], width, cfg)
                   if y and timed else np.broadcast_to(palettes[y] * 17, (width, 16, 3)))
        if not dither:
            delta = pixels[y, :, None, :].astype(np.int32) - visible
            indices[y] = np.sum(delta * delta, axis=2).argmin(axis=1)
            continue
        nxt = [[0, 0, 0] for _ in range(width + 2)]
        for x in range(width):
            rgb = [max(0, min(255, int(pixels[y, x, c]) + error[x + 1][c])) for c in range(3)]
            delta = np.asarray(rgb, dtype=np.int32) - visible[x]
            index = int(np.sum(delta * delta, axis=1).argmin())
            indices[y, x] = index
            if dither:
                for c in range(3):
                    e = rgb[c] - int(visible[x, index, c])
                    error[x + 2][c] += trunc16(e * 7)
                    nxt[x][c] += trunc16(e * 3)
                    nxt[x + 1][c] += trunc16(e * 5)
                    nxt[x + 2][c] += trunc16(e)
        error = nxt
    return indices
