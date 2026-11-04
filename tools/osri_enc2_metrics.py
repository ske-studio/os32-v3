"""Final-index display metrics, independent of the planning proxy J."""
import math
import numpy as np
from osri_enc2_spec import Config, out_count
from osri_enc2_timing import cuts, render_row


def blur_numerator(rgb):
    a = np.pad(np.asarray(rgb, dtype=np.int64), ((0, 0), (1, 1), (0, 0)), mode='edge')
    a = a[:, :-2] + 2 * a[:, 1:-1] + a[:, 2:]
    a = np.pad(a, ((1, 1), (0, 0), (0, 0)), mode='edge')
    return a[:-2] + 2 * a[1:-1] + a[2:]


def metrics(pixels, palettes, orders, indices, cfg=Config(), *, timed=True):
    ideal = np.array([p[ix] * 17 for p, ix in zip(palettes, indices)])
    visible = ideal.copy()
    widths = [0] * (len(palettes) - 1)
    if timed:
        for y in range(1, len(palettes)):
            visible[y] = render_row(palettes[y - 1], palettes[y], orders[y], indices[y], cfg)
            boundary, _ = cuts(palettes[y - 1], palettes[y], orders[y], pixels.shape[1], cfg)
            widths[y - 1] = int(boundary.max())
    delta = np.asarray(pixels, dtype=np.int64) - visible
    sse = int(np.sum(delta * delta, dtype=np.int64))
    blur_delta = blur_numerator(pixels) - blur_numerator(visible)
    mse = sse / pixels.size
    k = np.any(visible != ideal, axis=2).sum(axis=1).tolist()[1:]
    outs = [out_count(a, b) for a, b in zip(palettes, palettes[1:])]
    slots = [int(np.any(a != b, axis=1).sum()) for a, b in zip(palettes, palettes[1:])]
    result = dict(SSE=sse, PSNR=10 * math.log10(255 ** 2 / mse) if mse else math.inf,
                  blur_MSE=int(np.sum(blur_delta * blur_delta)) / (256 * pixels.size),
                  changed_slots=slots, actual_OUT=outs, OUT_max=max(outs, default=0),
                  OUT_mean=sum(outs) / len(outs) if outs else 0,
                  K_y=k, K_total=sum(k), K_max=max(k, default=0),
                  spill_width=max(widths, default=0))
    return result, visible
