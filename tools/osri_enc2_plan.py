"""Forward initialization and two-sided, strictly decreasing J sweeps."""
import time
import numpy as np
from osri_enc2_spec import Config, SPILL_WEIGHT, distance, window
from osri_enc2_master import master
from osri_enc2_tree import build_tree, row_candidates
from osri_enc2_row import RowSearch
from osri_enc2_order import OrderCost


def objective(pixels, palettes, orders, cfg=Config()):
    result = 0
    for y, p in enumerate(palettes):
        points, weights = window(pixels, y, cfg)
        minima = distance(points, p).min(axis=1).astype(np.int64)
        result += int(np.dot(minima, weights))
        if y:
            ids = distance(pixels[y], p).argmin(axis=1)
            cost = OrderCost(pixels[y], palettes[y - 1], p, ids)
            result += SPILL_WEIGHT * (cost.score(orders[y], cfg) - cost.base)
    return result


def prepare(pixels, cfg=Config()):
    start = time.perf_counter()
    colors, mass, losses = master(pixels, cfg)
    master_seconds = time.perf_counter() - start
    start = time.perf_counter()
    roots, layers = build_tree(colors, mass)
    return colors, roots, dict(master_seconds=master_seconds,
                               tree_seconds=time.perf_counter() - start,
                               master_sse=losses, tree_sizes=[len(x) for x in layers])


def plan(pixels, budget, cfg=Config(), prepared=None, active_rows=None):
    """active_rows explicitly limits S1 work; other rows are frozen after initialization.

    Such a plan is a partial experiment, never a completed full-image search.
    All boundaries, including the rows outside the experiment, remain valid.
    """
    pixels = np.asarray(pixels, dtype=np.int32)
    colors, roots, stats = prepared if prepared is not None else prepare(pixels, cfg)
    stats = dict(stats)
    start = time.perf_counter()
    active = set(range(1, len(pixels))) if active_rows is None else set(active_rows)
    if any(y <= 0 or y >= len(pixels) for y in active):
        raise ValueError('active rows must be between 1 and H-1')
    candidates = {y: row_candidates(pixels, y, roots, cfg) for y in sorted(active | {0})}
    stats['candidate_seconds'] = time.perf_counter() - start
    counters = {}
    start = time.perf_counter()
    initial = np.array([node.color for node in roots], dtype=np.int32)
    first = RowSearch(pixels, 0, None, None, (), budget, colors, candidates[0], cfg, counters)
    initial = first.search(initial, initial=True)
    palettes, orders = [initial], [()]
    for y in range(1, len(pixels)):
        old = palettes[-1]
        if y in active:
            search = RowSearch(pixels, y, old, None, (), budget, colors, candidates[y], cfg, counters)
            p = search.search(old)
            order = search.evaluate(p)[1]
        else:
            p, order = old.copy(), ()
        palettes.append(p)
        orders.append(order)
    stats['forward_seconds'] = time.perf_counter() - start
    start = time.perf_counter()
    trace = [objective(pixels, palettes, orders, cfg)]
    visits = []
    for _ in range(cfg.round_trips):
        changes = 0
        for direction in (sorted(active | {0}, reverse=True), sorted(active | {0})):
            for y in direction:
                old = palettes[y - 1] if y else None
                following = palettes[y + 1] if y + 1 < len(palettes) else None
                next_order = orders[y + 1] if following is not None else ()
                search = RowSearch(pixels, y, old, following, next_order, budget,
                                   colors, candidates[y], cfg, counters, (palettes[y], orders[y]))
                current = palettes[y]
                value = search.evaluate(current)
                # Preserve the current row order as an incumbent too.
                if y:
                    ids = distance(pixels[y], current).argmin(axis=1)
                    cost = OrderCost(pixels[y], old, current, ids)
                    retained = cost.score(orders[y], cfg)
                    best_order = cost.score(value[1], cfg)
                    incumbent_score = value[0] + SPILL_WEIGHT * (retained - best_order)
                else:
                    incumbent_score = value[0]
                if following is not None:
                    ids = distance(pixels[y + 1], following).argmin(axis=1)
                    cost = OrderCost(pixels[y + 1], current, following, ids)
                    incumbent_score += SPILL_WEIGHT * (cost.score(next_order, cfg) -
                                                        cost.score(value[2], cfg))
                best_p, best_value = current, value
                seeds = (np.array([n.color for n in roots]), current) if y == 0 else (old, current)
                for seed in seeds:
                    trial = search.search(seed, initial=(y == 0))
                    if trial is None:
                        continue
                    trial_value = search.evaluate(trial)
                    if search.key(trial, trial_value) < search.key(best_p, best_value):
                        best_p, best_value = trial, trial_value
                if best_value[0] < incumbent_score:
                    palettes[y], orders[y] = best_p, best_value[1]
                    if following is not None:
                        orders[y + 1] = best_value[2]
                    changes += 1
                    visits.append((y, incumbent_score, best_value[0]))
        trace.append(objective(pixels, palettes, orders, cfg))
        if changes == 0:
            break
    stats.update(sweep_seconds=time.perf_counter() - start, j_trace=trace,
                 visits=visits, active_rows=sorted(active), **counters)
    return np.array(palettes), orders, stats
