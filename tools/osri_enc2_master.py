"""Deterministic RGB444 master, fitted to the original RGB8 pixels."""
import numpy as np
from osri_enc2_spec import MASTER_K, DIST_BLOCK, Config, centroid, codes, components, distance


def assign(points, centers):
    ids = np.empty(len(points), dtype=np.int32)
    losses = np.empty(len(points), dtype=np.int32)
    for start in range(0, len(points), DIST_BLOCK):
        d = distance(points[start:start + DIST_BLOCK], centers)
        ix = d.argmin(axis=1)
        ids[start:start + len(d)] = ix
        losses[start:start + len(d)] = d[np.arange(len(d)), ix]
    return ids, losses


def master(pixels, cfg=Config()):
    points, freq = np.unique(np.asarray(pixels).reshape(-1, 3), axis=0, return_counts=True)
    points = points.astype(np.int32)
    freq = freq.astype(np.int64)
    quant = codes((points + 8) // 17)
    colors, inverse = np.unique(quant, return_inverse=True)
    counts = np.zeros(len(colors), dtype=np.int64)
    np.add.at(counts, inverse, freq)
    source = components(colors)
    first = int(np.argmax(counts))
    selected = [first]
    nearest = distance(source * 17, source[first:first + 1])[:, 0]
    while len(selected) < min(MASTER_K, len(colors)):
        score = counts * nearest.astype(np.int64)
        score[selected] = -1
        k = int(score.argmax())
        selected.append(k)
        nearest = np.minimum(nearest, distance(source * 17, source[k:k + 1])[:, 0])
    centers = source[selected].copy()
    centers = np.concatenate((centers, np.tile(source[0], (MASTER_K - len(centers), 1))))
    ids, loss = assign(points, centers)
    history = [int(np.dot(freq, loss.astype(np.int64)))]
    for _ in range(cfg.master_iters):
        counts = np.zeros(MASTER_K, dtype=np.int64)
        sums = np.zeros((MASTER_K, 3), dtype=np.int64)
        np.add.at(counts, ids, freq)
        np.add.at(sums, ids, points.astype(np.int64) * freq[:, None])
        proposal = centers.copy()
        for i in np.flatnonzero(counts):
            proposal[i] = centroid(int(counts[i]), sums[i])
        new_ids, new_loss = assign(points, proposal)
        sse = int(np.dot(freq, new_loss.astype(np.int64)))
        if sse >= history[-1]:
            break
        centers, ids, loss = proposal, new_ids, new_loss
        history.append(sse)
    order = np.argsort(codes(centers), kind='stable')
    centers = centers[order]
    ids, _ = assign(points, centers)
    mass = np.zeros(MASTER_K, dtype=np.int64)
    np.add.at(mass, ids, freq)
    return centers, mass, history
