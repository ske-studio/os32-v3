#!/usr/bin/env python3
"""Artifact verdicts include metadata, absent ELF and ordered ar payloads."""
import argparse
import contextlib
import io
import os
import subprocess
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


def check_moves():
    cc = str(Path(os.environ.get('CROSS_DIR', '/home/hight/opt/cross')) / 'bin/i386-elf-gcc')
    with tempfile.TemporaryDirectory() as directory:
        left, right = [Path(directory) / side for side in ('left', 'right')]
        for root in (left, right):
            for rel in B.artifacts(root):
                path = root / rel
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_bytes(b'fixture')
        old = Path('build/out/obj/kernel/v86/v86.o')
        new = Path('build/out/obj/arch/x86/legacy/v86.o')
        def assemble(root, rel, label, symbol='callee', opcode='0xc3', data_symbol='data_callee'):
            path = root / rel
            path.parent.mkdir(parents=True, exist_ok=True)
            subprocess.run([cc, '-m32', '-x', 'assembler', '-c', '-o', str(path), '-'],
                input=f'.file "{label}"\n.text\n.globl entry\nentry: .long {symbol}\n.byte {opcode}\n.data\n.long {data_symbol}\n',
                text=True, check=True)
        assemble(left, old, 'base')
        assemble(right, new, 'target')
        assert (left / old).read_bytes() != (right / new).read_bytes()
        with contextlib.redirect_stdout(io.StringIO()):
            assert not B.compare(left, right)
            for symbol, opcode, data_symbol in [('other_callee', '0xc3', 'data_callee'),
                                                 ('callee', '0x90', 'data_callee'),
                                                 ('callee', '0xc3', 'other_data_callee')]:
                assemble(right, new, 'target', symbol, opcode, data_symbol)
                assert str(old) in B.compare(left, right)
            assemble(right, new, 'target')
            (right / 'build/out/kernel.elf').write_bytes(b'changed final image')
            assert 'build/out/kernel.elf' in B.compare(left, right)


def check():
    check_moves()
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
            (right / 'build/out/kernel.elf').write_bytes(b'fixture')
            (right / 'build/out/unreferenced.o').write_bytes(b'ELF metadata and code')
            rel = Path('build/out/lib/libfixture.a')
            for root in (left, right):
                (root / rel).parent.mkdir(parents=True, exist_ok=True)
                (root / rel).write_bytes(archive(members))
            assert not B.compare(left, right)
            for changed in (members[::-1], [('other.o', b'ABC'), members[1]],
                            [('one.o', b'ABD'), members[1]]):
                (right / rel).write_bytes(archive(changed))
                assert str(rel) in B.compare(left, right)
            (right / rel).write_bytes(archive(members, 123))
            assert not B.compare(left, right)
            for name in ('link_guard.py', 'target_profile.py'):
                rel = Path('build/sdk/bin') / name
                a, b = left / rel, right / rel
                b.parent.mkdir(parents=True, exist_ok=True)
                b.write_bytes((B.ROOT / 'sdk' / name).read_bytes())
                if name == 'link_guard.py':
                    a.parent.mkdir(parents=True, exist_ok=True)
                    a.write_bytes(B.git(B.ROOT, 'show', '6d44e368:sdk/link_guard.py'))
                output = io.StringIO()
                with contextlib.redirect_stdout(output):
                    assert not B.compare(left, right, True)
                assert 'ALLOW ' + str(rel) in output.getvalue()
                assert '--- base/' + str(rel) in output.getvalue()
                assert '+++ target/' + str(rel) in output.getvalue()
                assert any(line.startswith('+') and not line.startswith('+++')
                           for line in output.getvalue().splitlines())
                approved = b.read_bytes()
                b.write_bytes(approved + b'\n# unapproved change\n')
                assert str(rel) in B.compare(left, right, True)
                b.write_bytes(approved)
                b.unlink()
                if a.is_file():
                    assert str(rel) in B.compare(left, right, True)
                b.write_bytes(approved)
                if a.is_file():
                    before = a.read_bytes()
                    a.write_bytes(before + b'\n# different baseline\n')
                else:
                    a.parent.mkdir(parents=True, exist_ok=True)
                    a.write_bytes(approved)
                    before = None
                    b.write_bytes(approved + b'\n')
                assert str(rel) in B.compare(left, right, True)
                if before is None:
                    a.unlink()
                    b.write_bytes(approved)
                else:
                    a.write_bytes(before)
            unexpected = right / 'build/sdk/bin/other.py'
            unexpected.write_text('unexpected tool')
            assert 'build/sdk/bin/other.py' in B.compare(left, right, True)



if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('--mutate', action='store_true')
    args = parser.parse_args()
    check()
    if args.mutate:
        original_artifacts = B.artifacts
        original_allow = B.allowed_tool_difference
        original_moves = B.H2_OBJECT_MOVES
        original_execution = B.object_execution_relocations
        mutations = {
            'missing move mapping': lambda: setattr(B, 'H2_OBJECT_MOVES', {}),
            'ignored execution and relocation': lambda: setattr(B, 'object_execution_relocations', lambda path: ''),
            'ignored archives': lambda: setattr(B, 'artifacts', lambda root, all_products=False:
                {p for p in original_artifacts(root, all_products) if p.suffix != '.a'}),
            'ignored objects': lambda: setattr(B, 'artifacts', lambda root, all_products=False:
                {p for p in original_artifacts(root, all_products) if p.suffix != '.o'}),
            'no approved differences': lambda: setattr(B, 'allowed_tool_difference', lambda *args: False),
            'path-only permission': lambda: setattr(B, 'allowed_tool_difference',
                lambda rel, a, b: str(rel) in B.ALLOWED_TOOL_DIFFS),
            'permit every difference': lambda: setattr(B, 'allowed_tool_difference', lambda *args: True),
        }
        for name, apply in mutations.items():
            try:
                apply()
                check()
            except AssertionError:
                print('RED: ' + name + ' rejected at runtime')
            else:
                raise SystemExit('mutation survived: ' + name)
            finally:
                B.artifacts = original_artifacts
                B.allowed_tool_difference = original_allow
                B.H2_OBJECT_MOVES = original_moves
                B.object_execution_relocations = original_execution
    print('PASS build compare')
