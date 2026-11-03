"""Regenerate B4 expectations from the pinned MIT reference, never OS32 code."""
import argparse
import hashlib
import os
from pathlib import Path
import struct
import subprocess
import tempfile

import host32

ROOT = Path(__file__).resolve().parents[2]
FIXTURE = ROOT / 'tools/tests/gf_command_b4_fixture.h'
REVISION = 'c03c1a14507dfaf8b856c2608f9ac216021fc222'
SOURCE_PATH = 'drivers/gpu/drm/nouveau/nvkm/engine/gr/ctxnv40.c'
# Pin the two upstream inputs so a different checkout cannot be mislabeled.
SOURCE_HASHES = {
    '.c': '5f9b8044490a0d5c96eb52330000aa1260183505f3e8990602059c6c59dafd15',
    '.h': '0143c6024fe279d7c5f4b26ee876a4a697a2f386e79d8bf8df285d3ef50b906f',
}
PROGRAM_WORDS = 256
IMAGE_WORDS = 80 * 1024 // 4
META_WORDS = 4


def generate(oracle):
    oracle = oracle.resolve()
    for suffix, digest in SOURCE_HASHES.items():
        source = oracle.with_suffix(suffix)
        if hashlib.sha256(source.read_bytes()).hexdigest() != digest:
            raise RuntimeError(f'{source}: expected MIT nouveau {REVISION}')
    runners = os.environ.get('HOST32_RUNNERS', 'qemu').split()
    if not runners or any(r not in ('native', 'qemu') for r in runners):
        raise RuntimeError('HOST32_RUNNERS must select native and/or qemu')
    with tempfile.TemporaryDirectory(prefix='gf-b4-oracle-') as tmp:
        directory = Path(tmp)
        (directory / 'core').mkdir()
        (directory / 'core/gpuobj.h').write_text('/* Services supplied by host. */\n')
        exe = directory / 'oracle'
        subprocess.run([
            'gcc', '-m32', '-march=i386', '-std=gnu11', '-O2', '-Wall', '-Wextra',
            '-Werror', '-Werror=vla', '-ffreestanding', '-fno-stack-protector',
            '-fno-pie', '-static', '-no-pie', '-nostdlib', '-I' + str(directory),
            '-DGF2_B4_ORACLE="' + str(oracle) + '"',
            str(ROOT / 'tools/tests/gf_command_b4_oracle.c'), '-o', str(exe)
        ], check=True)
        outputs = [host32.run([str(exe)], runner=r, check=True,
                              capture_output=True).stdout for r in runners]
    if any(output != outputs[0] for output in outputs):
        raise RuntimeError('B4 oracle runners disagree')
    data = outputs[0]
    count = META_WORDS + PROGRAM_WORDS + IMAGE_WORDS
    if len(data) != count * 4:
        raise RuntimeError('B4 oracle emitted an incomplete image')
    values = struct.unpack(f'<{count}I', data)
    words, size, prog_end, vals_end = values[:META_WORDS]
    if not (0 < words <= PROGRAM_WORDS and size == prog_end * 4 == vals_end * 4
            and size <= IMAGE_WORDS * 4):
        raise RuntimeError('B4 oracle program/image accounting disagrees')
    code = values[META_WORDS:META_WORDS + PROGRAM_WORDS]
    image = values[META_WORDS + PROGRAM_WORDS:]
    # Keep the upstream license with the derived data, without vendoring code.
    license_text = oracle.read_text().split('*/', 1)[0] + '*/'
    lines = [
        '/* Generated NV4a (chipset 0x4a) B4 grctx expectations: all uploaded',
        ' * program words, context byte size, final PROG/VALS word positions,',
        ' * and the complete zero-initialized 80 KiB context image (including tail).',
        ' * Omitted array entries are zero; indices are context DWORD offsets.',
        f' * Source: MIT nouveau {REVISION} (c03c1a1)',
        f' * Path: {SOURCE_PATH} and sibling ctxnv40.h',
        f' * SHA256 ctxnv40.c: {SOURCE_HASHES[".c"]}',
        f' * SHA256 ctxnv40.h: {SOURCE_HASHES[".h"]}',
        ' * Regenerate from the repository root (substitute the reference root):',
        ' * HOST32_RUNNERS=qemu ~/os32-tmp/bin/check_slot.sh gf2-b4oracle '
        f'python3 -B tools/tests/gf_command_b4_fixture.py --oracle <nouveau>/{SOURCE_PATH}',
        ' * No OS32 output is read.',
        ' * Verify without writing: use test_gf_command.py --component b4 --oracle',
        ' *   with the same source path, through check_slot.sh.',
        ' */', license_text, '',
        'enum {',
        f'    B4_PROGRAM_WORDS = {words}, B4_IMAGE_BYTES = {size},',
        f'    B4_PROG_END_WORDS = {prog_end}, B4_VALS_END_WORDS = {vals_end},',
        f'    B4_IMAGE_WORDS = {IMAGE_WORDS}',
        '};',
        'static const u32 b4_program[B4_PROGRAM_WORDS] = {',
    ]
    lines += [f'    0x{value:08x}U,' for value in code[:words]]
    lines += ['};', 'static const u32 b4_image[B4_IMAGE_WORDS] = {']
    lines += [f'    [{index}] = 0x{value:08x}U,'
              for index, value in enumerate(image) if value]
    lines += ['};', '']
    return '\n'.join(lines)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--oracle', type=Path, required=True,
                        help='pinned nouveau ctxnv40.c (ctxnv40.h must be beside it)')
    parser.add_argument('--output', type=Path, default=FIXTURE)
    args = parser.parse_args()
    args.output.write_text(generate(args.oracle))
    print(f'B4 expectations written: {args.output}')


if __name__ == '__main__':
    main()
