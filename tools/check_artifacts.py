"""Prerequisite checks before tests consume make all artifacts.

GNU make -q may remake included makefiles (.d files included). This probe is
therefore not strictly read-only: call it only after make all, as required by
the real-artifact suites. Order-only dependencies do not affect freshness.
"""
import functools
import pathlib
import subprocess


@functools.lru_cache(maxsize=4)
def _dependencies(root):
    # Read make's expanded dependency graph, including compiler-generated .d
    # files. Ignore phony timestamps, but traverse their real prerequisites.
    result = subprocess.run(['make', '-qpRr', 'NP21W_DIR=/dev/null'], cwd=root,
                            capture_output=True, text=True, timeout=120,
                            stdin=subprocess.DEVNULL)
    if result.returncode not in (0, 1):
        raise RuntimeError('成果物の依存関係を確認できません。先に make all: ' + result.stderr)
    graph = {}
    for line in result.stdout.splitlines():
        if not line or line[0] in '#\t ' or ':=' in line or ':' not in line:
            continue
        target, deps = line.split(':', 1)
        if '=' in target or '%' in target:
            continue
        for name in target.split():
            graph.setdefault(name, set()).update(deps.split('|', 1)[0].split())
    return graph


def require_fresh(root, artifacts, inputs=(), targets=None):
    """Require files and their transitive inputs to precede the artifact.

    targets maps side effects (kernel.map, PKG/ISO files) to the make target
    that actually produces them. Synthetic fixtures can pass explicit inputs.
    """
    root = pathlib.Path(root)
    artifacts = list(artifacts)
    if not artifacts:
        raise RuntimeError('成果物がありません。先に make all を実行してください')
    for artifact in artifacts:
        if not (root / artifact).is_file():
            raise RuntimeError(f'{artifact} がありません。先に make all を実行してください')
    graph = _dependencies(str(root)) if (root / 'Makefile').exists() else {}
    targets = targets or {}
    for artifact in artifacts:
        path = root / artifact
        seen = set()
        def visit(name):
            if name in seen:
                return
            seen.add(name)
            for dep in graph.get(name, ()):
                visit(dep)
        visit(targets.get(str(artifact), str(artifact)))
        seen.discard(targets.get(str(artifact), str(artifact)))
        seen.update(map(str, inputs))
        for name in seen:
            dep = root / name
            if name in inputs and not dep.is_file():
                raise RuntimeError(f'入力 {name} がありません。先に make all を実行してください')
            if dep.is_file() and dep != path and dep.stat().st_mtime_ns > path.stat().st_mtime_ns:
                raise RuntimeError(f'{artifact} が入力 {name} より古いです。先に make all を実行してください')
