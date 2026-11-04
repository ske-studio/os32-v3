"""Integer contracts for the experimental host encoder (enc2-r3-supplement1).

No guest code consumes this module. RGB444 palettes are physical-slot ordered
int32 arrays of shape (16, 3), containing component values 0..15.
"""
from osri_profile import OSRI_DEFAULT_OUT_BUDGET, OSRI_COMPARE_OUT_LIMIT
from dataclasses import dataclass
import numpy as np

MASTER_K = 256
MASTER_ITERS = 20
LOOKAHEAD_ROWS = 3
LOOKAHEAD_WEIGHTS = (8, 4, 2, 1)
ROW_ITERS = 6
ROUND_TRIPS = 2
TREE_CANDIDATES = 16
ROW_SEEDS = 2
DIST_BLOCK = 4096
PY_SLOW_SECONDS = 600
BUDGETS = (15, 23, OSRI_DEFAULT_OUT_BUDGET, OSRI_COMPARE_OUT_LIMIT)
ROW_MOVES = 32
ORDER_PASSES = 2
HBLANK_NS, OUT_NS, LINE_NS = 5900, 650, 31800
ACTIVE_NS, PIXEL0_OFFSET_NS = 25900, 0
IN_NS, START_DELAY_NS = 1150, 575
I_PAIR_ENABLED, SPILL_WEIGHT = False, 8
VERSION = 'enc2-r3-supplement1-c2-before-c4'


@dataclass(frozen=True)
class Config:
    master_iters: int = MASTER_ITERS
    lookahead_rows: int = LOOKAHEAD_ROWS
    row_iters: int = ROW_ITERS
    round_trips: int = ROUND_TRIPS
    row_moves: int = ROW_MOVES
    order_passes: int = ORDER_PASSES
    start_delay_ns: int = START_DELAY_NS
    pixel0_offset_ns: int = PIXEL0_OFFSET_NS

    def __post_init__(self):
        if not 0 <= self.lookahead_rows <= LOOKAHEAD_ROWS:
            raise ValueError('lookahead must be 0..3')
        if min(self.master_iters, self.row_iters, self.round_trips,
               self.row_moves, self.order_passes) < 0:
            raise ValueError('negative search limit')


def components(colors):
    q = np.asarray(colors, dtype=np.int32)
    return np.stack((q >> 8, q >> 4 & 15, q & 15), axis=-1)


def codes(palette):
    p = np.asarray(palette, dtype=np.int32)
    return (p[..., 0] << 8) | (p[..., 1] << 4) | p[..., 2]


def distance(pixels, palette):
    # int32 products; reductions/weighted products are explicitly int64.
    d = np.asarray(pixels, dtype=np.int32)[:, None, :] - palette[None, :, :] * 17
    return np.sum(d * d, axis=2, dtype=np.int32)


def centroid(n, sums):
    """Minimum n*||E(q)||²-2*E(q).s; midpoint chooses low component."""
    e = np.arange(16, dtype=np.int64) * 17
    cost = n * e[None, :] ** 2 - 2 * np.asarray(sums, dtype=np.int64)[:, None] * e
    return np.argmin(cost, axis=1).astype(np.int32)


def out_count(old, new):
    diff = np.asarray(old) != np.asarray(new)
    slots = int(np.any(diff, axis=1).sum())
    return int(diff.sum()) + max(0, slots - 1)


def allowed(old, new, budget):
    return out_count(old, new) <= min(budget, OSRI_COMPARE_OUT_LIMIT)


def partials(current, candidates):
    """Sorted, deduplicated partial-component proposals, including M-external colors."""
    values = set()
    for candidate in candidates:
        for mask in range(1, 8):
            q = tuple(int(candidate[c] if mask & (1 << c) else current[c])
                      for c in range(3))
            if q != tuple(current):
                values.add(q)
    return sorted(values)


def window(pixels, y, cfg):
    rows = pixels[y:y + cfg.lookahead_rows + 1]
    weights = np.repeat(np.array(LOOKAHEAD_WEIGHTS[:len(rows)], dtype=np.int64),
                        pixels.shape[1])
    return rows.reshape(-1, 3).astype(np.int32), weights
