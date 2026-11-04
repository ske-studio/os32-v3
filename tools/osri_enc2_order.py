"""Fixed-index prefix SSE makes each order evaluation O(16*3)."""
import numpy as np
from osri_enc2_spec import ACTIVE_NS, HBLANK_NS, OUT_NS, Config, distance


class OrderCost:
    def __init__(self, pixels, old, new, indices):
        self.old, self.new = old, new
        self.width = len(pixels)
        self.counts = np.count_nonzero(old != new, axis=1)
        self.changed = tuple(np.flatnonzero(self.counts).tolist())
        self.tables = {}
        pixels = np.asarray(pixels, dtype=np.int64)
        ideal = new[indices].astype(np.int64) * 17
        self.base = int(np.sum((pixels - ideal) ** 2))
        self.prefix = np.zeros((16, 3, self.width + 1), dtype=np.int64)
        for slot in self.changed:
            delta = ((pixels - old[slot] * 17) ** 2 - (pixels - new[slot] * 17) ** 2)
            delta[indices != slot] = 0
            self.prefix[slot, :, 1:] = np.cumsum(delta, axis=0, dtype=np.int64).T

    def score(self, order, cfg=Config()):
        if len(order) != len(self.changed) or set(order) != set(self.changed):
            raise ValueError('order must permute exactly the changed slots')
        return int(self.score_many([order], cfg)[0])

    def score_many(self, orders, cfg=Config()):
        # Precompute each slot's prefix cost for every possible preceding OUT
        # count. Swaps then need a few small integer gathers, never pixel scans.
        if cfg not in self.tables:
            table = np.zeros((16, 65), dtype=np.int64)
            preceding = np.arange(65, dtype=np.int64)
            for slot in self.changed:
                rank = 0
                for c in (1, 0, 2):
                    if self.old[slot, c] != self.new[slot, c]:
                        rank += 1
                        t = cfg.start_delay_ns + (preceding + rank) * OUT_NS
                        numerator = (t - HBLANK_NS - cfg.pixel0_offset_ns) * self.width
                        boundary = np.clip(-((-numerator) // ACTIVE_NS), 0, self.width)
                        table[slot] += self.prefix[slot, c, boundary]
            self.tables[cfg] = table
        if not self.changed:
            return np.full(len(orders), self.base, dtype=np.int64)
        slots = np.asarray(orders, dtype=np.int32)
        lengths = self.counts[slots] + 1
        preceding = np.cumsum(lengths, axis=1) - lengths
        return self.base + self.tables[cfg][slots, preceding].sum(axis=1, dtype=np.int64)


def optimize_order(pixels, old, new, cfg=Config(), retained=None, indices=None):
    if indices is None:
        indices = distance(pixels, new).argmin(axis=1)
    changed = tuple(np.flatnonzero(np.any(old != new, axis=1)).tolist())
    if not changed:
        delta = np.asarray(pixels, dtype=np.int64) - new[indices] * 17
        return int(np.sum(delta * delta)), ()
    first = {i: int(np.flatnonzero(indices == i)[0]) if np.any(indices == i)
             else len(pixels) for i in changed}
    initial = tuple(sorted(changed, key=lambda i: (first[i], i)))
    cost = OrderCost(pixels, old, new, indices)

    def improve(order):
        score = cost.score(order, cfg)
        for _ in range(cfg.order_passes):
            trials = []
            for i in range(len(order)):
                for j in range(i + 1, len(order)):
                    trial = list(order)
                    trial[i], trial[j] = trial[j], trial[i]
                    trial = tuple(trial)
                    trials.append(trial)
            if not trials:
                break
            values = cost.score_many(trials, cfg)
            best = min(zip(values.tolist(), trials))
            if best[0] >= score:
                break
            score, order = best
        return score, order

    start = min((cost.score(o, cfg), o) for o in (initial, changed))
    choices = [improve(start[1])]
    if retained is not None:
        # A changed previous palette can remove or introduce next-row changes.
        # Preserve the existing relative order and append newly changed slots.
        retained = tuple(i for i in retained if i in changed)
        retained += tuple(i for i in changed if i not in retained)
        # Keep an unchanged incumbent on an exact tie.
        current = (cost.score(tuple(retained), cfg), tuple(retained))
        choices.append(improve(current[1]))
        best = min(choices)
        return current if current[0] <= best[0] else best
    return min(choices)
