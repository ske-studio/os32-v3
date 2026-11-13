#!/usr/bin/env python3
"""L0 KCG packing, CLI compatibility and real C decoder round trips."""
CHECK = dict(name='kcg-lz4', tier=1, needs=[], budget=60,
             covers=['tools/gen_font16.py', 'tools/gen_g16.py',
                     'tools/tests/test_kcg_lz4.py', 'assets/fonts/**',
                     'lib/lz4.c', 'lib/lz4.h', 'include/types.h', 'include/callconv.h'])
import builtins
import ctypes
import hashlib
import os
from pathlib import Path
import random
import struct
import subprocess
import sys
import tempfile
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'tools'))
import gen_font16 as generator

FONT = ROOT / 'assets/fonts/JF-Dot-Shinonome16.ttf'


def load(source, filename):
    namespace = {'__name__': 'kcg_candidate'}
    exec(compile(source, str(filename), 'exec'), namespace)
    return type('Candidate', (), namespace)


def check(g, script, tmp, decode):
    payload = g.build_ebdt_payload(FONT)
    plain = g.build_ebdt(FONT)
    assert plain == struct.pack('<4sIII', b'KCG1', 295940, 0, 0) + payload

    def cli(args, success=True):
        result = subprocess.run([sys.executable, '-B', str(script), *map(str, args)],
                                capture_output=True)
        assert (result.returncode == 0) == success, result.stderr
        return result

    output = tmp / 'out.kcg'
    for option in ([], ['--no-lz4'], ['--lz4']):
        cli(['--ebdt', FONT, output, *option])
        data = output.read_bytes()
        flags = int(bool(option and option[0] == '--lz4'))
        assert struct.unpack_from('<4sIII', data) == (b'KCG1', 295940, flags, 0)
        if flags:
            dst = ctypes.create_string_buffer(295940)
            assert decode(data[16:], len(data) - 16, dst, 295940) == 295940
            assert dst.raw == payload
        else:
            assert data == plain
    first = output.read_bytes()
    cli(['--ebdt', FONT, output, '--lz4'])
    assert output.read_bytes() == first
    assert hashlib.sha256(output.read_bytes()).digest() == hashlib.sha256(first).digest()
    for args in (['--ebdt'], ['--ebdt', FONT],
                 ['--ebdt', FONT, output, '--bad'],
                 ['--ebdt', FONT, output, '--lz4', '--no-lz4'],
                 ['--ebdt', tmp / 'missing', output]):
        cli(args, False)

    # Exercise the TTF branch with deterministic raster fixtures; packing stays real.
    for options in ([], ['--no-lz4'], ['--lz4']):
        with patch.object(sys, 'argv', [str(script), str(FONT), str(output), *options]), \
             patch.object(g.ImageFont, 'truetype', return_value=None), \
             patch.dict(g.main.__globals__,
                        build_ank=lambda f: (bytes(4096), 0),
                        build_kanji=lambda f: (bytes(282752), 0)):
            assert g.main() == 0
        data = output.read_bytes()
        expected_flags = 0 if options == ['--no-lz4'] else 1
        assert struct.unpack_from('<4sIII', data) == (b'KCG1', 295940, expected_flags, 0)
        if expected_flags:
            dst = ctypes.create_string_buffer(295940)
            assert decode(data[16:], len(data) - 16, dst, 295940) == 295940
            assert dst.raw == bytes(282752) + b'\1' * 8836 + bytes(4096) + b'\1' * 256

    # Random bytes expand under raw LZ4 and must retain flags=0.
    noise = random.Random(9821).randbytes(295940)
    import lz4.block
    assert len(lz4.block.compress(noise, store_size=False)) >= len(noise)
    assert g.pack_kcg(noise, 'lz4') == struct.pack('<4sIII', b'KCG1', 295940, 0, 0) + noise
    original_import = builtins.__import__

    def missing(name, *args, **kwargs):
        if name == 'lz4.block':
            raise ImportError('test: lz4 unavailable')
        return original_import(name, *args, **kwargs)

    with patch('builtins.__import__', side_effect=missing):
        try:
            g.pack_kcg(payload, 'lz4')
        except ImportError:
            pass
        else:
            raise AssertionError('explicit LZ4 hid missing dependency')
        assert g.pack_kcg(payload, 'auto') == plain
        with patch.object(sys, 'argv', [str(script), '--ebdt', str(FONT), str(output), '--lz4']):
            assert g.main() != 0


def main(mutate=False):
    try:
        import lz4
    except ImportError:
        print('SKIP kcg-lz4: Python lz4 unavailable; not counted as PASS')
        return 0
    print('Python lz4 version:', lz4.__version__)
    source = (ROOT / 'tools/gen_font16.py').read_text()
    with tempfile.TemporaryDirectory(dir=os.environ.get('TMPDIR')) as directory:
        tmp = Path(directory)
        library = tmp / 'lz4.so'
        subprocess.run(['cc', '-O0', '-shared', '-fPIC', '-I', str(ROOT / 'include'),
                        str(ROOT / 'lib/lz4.c'), '-o', str(library)], check=True)
        decode = ctypes.CDLL(str(library)).lz4_decode
        decode.argtypes = [ctypes.c_char_p, ctypes.c_int, ctypes.c_void_p, ctypes.c_int]
        decode.restype = ctypes.c_int
        check(generator, ROOT / 'tools/gen_font16.py', tmp, decode)
        if mutate:
            mutations = [
                ('argv', 'if len(args) not in (3, 4)', 'if len(args) != 3'),
                ('compression', "compression = 'lz4' if len(args) == 4 and args[3] == '--lz4' else 'none'", "compression = 'none'"),
                ('dependency', "if compression == 'lz4':\n                raise", "if compression == 'lz4':\n                pass"),
                ('expansion', 'if len(compressed) < len(body):', 'if len(compressed) >= len(body):'),
                ('reserved', "MAGIC, PAYLOAD_SIZE, flags, 0)", "MAGIC, PAYLOAD_SIZE, flags, 1)"),
            ]
            for name, before, after in mutations:
                assert source.count(before) == 1, name
                mutant = source.replace(before, after)
                script = tmp / 'mutant.py'
                script.write_text(mutant)
                # CLI imports the unchanged EBDT helper from tools.
                with patch.dict(os.environ, PYTHONPATH=str(ROOT / 'tools')):
                    try:
                        check(load(mutant, script), script, tmp, decode)
                    except AssertionError:
                        print('mutation', name, 'RED (runtime assertion)')
                    else:
                        raise AssertionError('mutation survived: ' + name)
    print('kcg-lz4 PASS')
    return 0


def run(ctx):
    return subprocess.call([sys.executable, '-B', __file__,
                            *(['--mutate'] if ctx.mutate else [])], cwd=ctx.root)


if __name__ == '__main__':
    sys.exit(main('--mutate' in sys.argv))
