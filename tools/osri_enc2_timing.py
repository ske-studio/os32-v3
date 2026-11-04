"""Component completion times; the preselected first index costs no H OUT."""
import numpy as np
from osri_enc2_spec import ACTIVE_NS, HBLANK_NS, OUT_NS, Config


def cuts(old, new, order, width, cfg=Config()):
    changed = set(np.flatnonzero(np.any(old != new, axis=1)).tolist())
    if len(order) != len(changed) or set(order) != changed:
        raise ValueError('order must permute exactly the changed slots')
    result = np.zeros((16, 3), dtype=np.int32)
    writes, j = [], 0
    for position, slot in enumerate(order):
        if position:
            j += 1
            writes.append((j, slot, -1, cfg.start_delay_ns + j * OUT_NS))
        for c in (1, 0, 2):  # G, R, B
            if old[slot, c] != new[slot, c]:
                j += 1
                t = cfg.start_delay_ns + j * OUT_NS
                numerator = (t - HBLANK_NS - cfg.pixel0_offset_ns) * width
                x = max(0, min(width, -((-numerator) // ACTIVE_NS)))
                result[slot, c] = x
                writes.append((j, slot, c, t))
    return result, writes


def display_palette(old, new, order, width, cfg=Config()):
    boundary, _ = cuts(old, new, order, width, cfg)
    x = np.arange(width)[:, None, None]
    return np.where(x < boundary[None, :, :], old[None, :, :], new[None, :, :]) * 17


def render_row(old, new, order, indices, cfg=Config()):
    palette = display_palette(old, new, order, len(indices), cfg)
    return palette[np.arange(len(indices)), indices]
