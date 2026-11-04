"""Finite, strictly improving partial-component exchanges and coordinate Lloyd."""
import numpy as np
from osri_enc2_spec import (Config, SPILL_WEIGHT, allowed, centroid, codes,
                            distance, out_count, partials, window)
from osri_enc2_order import optimize_order


class RowSearch:
    def __init__(self, pixels, y, old, following, following_order, budget, master,
                 candidates, cfg=Config(), counters=None, retained=None):
        self.pixels, self.y, self.old, self.following = pixels, y, old, following
        self.following_order, self.budget = following_order, budget
        self.master, self.candidates, self.cfg = master, candidates, cfg
        self.points, self.weights = window(pixels, y, cfg)
        self.cache = {}
        self.retained = retained
        self.counters = counters if counters is not None else {}
        # Reuse component distances across all candidate palettes.
        self.component = [(self.points[:, c, None] - np.arange(16)[None, :] * 17) ** 2
                          for c in range(3)]
        self.next_ideal, self.next_indices = 0, None
        if following is not None:
            next_distance = distance(pixels[y + 1], following)
            self.next_indices = next_distance.argmin(axis=1)
            self.next_ideal = int(next_distance.min(axis=1).sum(dtype=np.int64))

    def evaluate(self, p, minima=None, indices=None):
        key = tuple(int(v) for v in codes(p))
        if key in self.cache:
            return self.cache[key]
        self.counters['evaluations'] = self.counters.get('evaluations', 0) + 1
        if ((self.old is not None and not allowed(self.old, p, self.budget)) or
                (self.following is not None and not allowed(p, self.following, self.budget))):
            self.cache[key] = None
            return None
        if minima is None:
            d = sum(self.component[c][:, p[:, c]] for c in range(3))
            minima = d.min(axis=1).astype(np.int64)
            indices = d[:self.pixels.shape[1]].argmin(axis=1)
        f = int(np.dot(minima, self.weights))
        ideal = int(minima[:self.pixels.shape[1]].sum())
        if self.old is None:
            actual, order = ideal, ()
        else:
            retained_order = (self.retained[1] if self.retained is not None and
                              np.array_equal(p, self.retained[0]) else None)
            actual, order = optimize_order(self.pixels[self.y], self.old, p, self.cfg,
                                           retained_order, indices)
        score = f + SPILL_WEIGHT * (actual - ideal)
        next_order = ()
        if self.following is not None:
            actual_next, next_order = optimize_order(self.pixels[self.y + 1], p, self.following,
                                                     self.cfg, self.following_order, self.next_indices)
            score += SPILL_WEIGHT * (actual_next - self.next_ideal)
        result = (score, order, next_order)
        self.cache[key] = result
        return result

    def neighbors(self, p):
        d = sum(self.component[c][:, p[:, c]] for c in range(3))
        nearest = d.argmin(axis=1)
        rows = np.arange(len(d))
        first = d[rows, nearest]
        d[rows, nearest] = np.iinfo(d.dtype).max
        second_ids = d.argmin(axis=1)
        second = d[rows, second_ids]
        return nearest, first, second, second_ids

    def evaluate_slot(self, p, slot, q, neighbors):
        nearest, first, second, second_ids = neighbors
        without = np.where(nearest == slot, second, first)
        other = np.where(nearest == slot, second_ids, nearest)
        candidate = sum(self.component[c][:, q[c]] for c in range(3))
        select = (candidate < without) | ((candidate == without) & (slot < other))
        indices = np.where(select, slot, other)[:self.pixels.shape[1]]
        trial = p.copy()
        trial[slot] = q
        return trial, self.evaluate(trial, np.minimum(without, candidate), indices)

    def key(self, p, evaluation):
        return (evaluation[0], out_count(self.old, p) if self.old is not None else 0,
                tuple(int(v) for v in codes(p)), evaluation[1])

    def lloyd(self, palette, movable):
        p = palette.copy()
        for _ in range(self.cfg.row_iters):
            changed = False
            for slot in movable:
                ids = distance(self.points, p).argmin(axis=1)
                mask = ids == slot
                n = int(self.weights[mask].sum())
                if not n:
                    continue
                sums = np.sum(self.points[mask].astype(np.int64) * self.weights[mask, None], axis=0)
                center = centroid(n, sums)
                proposals = partials(p[slot], np.vstack((self.master, center)))
                # The current color is the first tie choice and is never replaced on equality.
                proposals.sort(key=lambda q: (n * sum((int(v) * 17) ** 2 for v in q) -
                                               2 * sum(int(v) * 17 * int(s) for v, s in zip(q, sums)), q))
                best_p, best = p, self.evaluate(p)
                neighbors = self.neighbors(p)
                for q in proposals:
                    trial, value = self.evaluate_slot(p, slot, q, neighbors)
                    if value is not None and value[0] < self.evaluate(p)[0]:
                        if best is None or self.key(trial, value) < self.key(best_p, best):
                            best_p, best = trial, value
                if not np.array_equal(best_p, p):
                    p, changed = best_p, True
            if not changed:
                break
        return p

    def search(self, seed, initial=False):
        p = seed.copy()
        if self.evaluate(p) is None:
            return None
        if initial:
            return self.lloyd(p, range(16))
        for _ in range(self.cfg.row_moves):
            best_p, best = p, self.evaluate(p)
            neighbors = self.neighbors(p)
            for slot in range(16):
                for q in partials(p[slot], self.candidates):
                    trial, value = self.evaluate_slot(p, slot, q, neighbors)
                    if value is not None and value[0] < self.evaluate(p)[0]:
                        if self.key(trial, value) < self.key(best_p, best):
                            best_p, best = trial, value
            if np.array_equal(best_p, p):
                break
            p = best_p
        # C2 completes the tree exchanges before C4/C5 refine the changed set.
        # A retained seed still gets Lloyd when no tree exchange improves it.
        movable = np.flatnonzero(np.any(p != self.old, axis=1))
        return self.lloyd(p, movable)
