#!/usr/bin/env python3
"""Artifact verdicts include metadata, absent ELF and ordered ar payloads."""
import argparse
import contextlib
import io
from pathlib import Path
import sys
import tempfile
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import build_compare as B


def archive(members, time=0):
    data = b'!<arch>\n'
    for name, payload in members:
        header = f'{name + "/":16}{time:<12}{0:<6}{0:<6}{100644:<8}{len(payload):<10}`\n'.encode()
        data += header + payload + (b'\n' if len(payload) & 1 else b'')
    return data


def check():
    members = [('one.o', b'ABC'), ('one.o', b'DEF')]
    assert B.archive_members(archive(members)) == B.archive_members(archive(members, 123))
    assert B.archive_members(archive(members)) != B.archive_members(archive(members[::-1]))
    assert B.archive_members(archive(members)) != B.archive_members(archive(members[:1]))
    with tempfile.TemporaryDirectory() as directory:
        left, right = [Path(directory) / side for side in ('left', 'right')]
        for root in (left, right):
            for rel in B.artifacts(root):
                path = root / rel
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_bytes(b'fixture')
            (root / 'build/out/unreferenced.o').write_bytes(b'ELF metadata and code')
        with contextlib.redirect_stdout(io.StringIO()):
            assert not B.compare(left, right)
            (right / 'build/out/unreferenced.o').write_bytes(b'ELF metadata and codE')
            verdict = B.compare(left, right)
            assert 'build/out/unreferenced.o' in verdict
            (right / 'build/out/kernel.elf').unlink()
            assert 'build/out/kernel.elf' in B.compare(left, right)


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('--mutate', action='store_true')
    args = parser.parse_args()
    check()
    if args.mutate:
        try:
            original = B.artifacts
            def without_objects(root, all_products=False):
                return {p for p in original(root, all_products) if p.suffix != ".o"}
            B.artifacts = without_objects
            check()
        except AssertionError:
            print('RED: ignored object difference rejected at runtime')
        else:
            raise SystemExit('mutation survived')
    print('PASS build compare')
