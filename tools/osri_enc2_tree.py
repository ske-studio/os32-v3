"""Four deterministic pair-and-halve layers, then per-row descent."""
from dataclasses import dataclass
import numpy as np
from osri_enc2_spec import Config, distance, window


@dataclass
class Node:
    id: int
    color: np.ndarray
    mass: int
    sums: np.ndarray
    minimum: tuple
    children: tuple = ()


def build_tree(master, mass):
    nodes = [Node(i, q.copy(), int(n), q.astype(np.int64) * 17 * n, tuple(q))
             for i, (q, n) in enumerate(zip(master, mass))]
    if len(nodes) != 256:
        raise ValueError('tree requires 256 leaves including duplicates')
    unique = np.unique(master, axis=0)
    e = unique.astype(np.int64) * 17
    layers = [nodes]
    for _ in range(4):
        pairs = []
        for i, a in enumerate(nodes):
            for b in nodes[i + 1:]:
                pairs.append((int(np.sum(((a.color - b.color) * 17) ** 2)), a.id, b.id))
        taken, matched = set(), []
        by_id = {n.id: n for n in nodes}
        for _, i, j in sorted(pairs):
            if i not in taken and j not in taken:
                taken.update((i, j))
                matched.append((i, j))
        parents = []
        for index, (i, j) in enumerate(sorted(matched)):
            a, b = by_id[i], by_id[j]
            n, sums = a.mass + b.mass, a.sums + b.sums
            minimum = min(a.minimum, b.minimum)
            q = unique[np.argmin(n * np.sum(e * e, axis=1) - 2 * (e @ sums))] if n else np.array(minimum)
            parents.append(Node(index, q.copy(), n, sums, minimum, (a, b)))
        nodes = parents
        layers.append(nodes)
    return nodes, layers


def row_candidates(pixels, y, roots, cfg=Config()):
    points, weights = window(pixels, y, cfg)
    result = []
    for root in roots:
        node = root
        while node.children:
            node = min(node.children, key=lambda child: (
                int(np.dot(distance(points, child.color[None, :])[:, 0].astype(np.int64), weights)),
                child.id))
        result.append(tuple(node.color))
    return np.array(sorted(set(result)), dtype=np.int32)
