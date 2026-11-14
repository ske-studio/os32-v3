#!/usr/bin/env python3
CHECK = dict(name='remote-x9b', tier=1, needs=[], budget=5,
    covers=['kernel/boot_token.c', 'include/boot_token.h', 'kapi/kapi_generated.c',
            'sdk/kapi.json', 'tools/tests/remote_x9b_host.c', 'tools/tests/test_remote_x9b.py'])
import argparse
from pathlib import Path
import re
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[2]

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--mutate', action='store_true')
    args = parser.parse_args()
    source = (ROOT / 'kernel/boot_token.c').read_text()
    generated = (ROOT / 'kapi/kapi_generated.c').read_text()
    match = re.search(r'void __cdecl wrap_boot_token_get\(u8 \*out\)\n\{.*?\n\}', generated, re.S)
    assert match
    wrapper = match.group().replace('__cdecl ', '').replace('KAPI_HIT(293);', '')
    wrapper = re.sub(r'    /\* 出力範囲.*?\n    }\n', '', wrapper, flags=re.S)
    assert 'boot_token_get(out);' in wrapper
    def run(src, wrap, expected):
        with tempfile.TemporaryDirectory() as td:
            path = Path(td)
            (path / 'token.c').write_text(src)
            (path / 'wrapper.c').write_text('#include "boot_token.h"\n' + wrap + '\n')
            subprocess.run(['cc', '-I', str(ROOT / 'include'), str(path / 'token.c'),
                            str(path / 'wrapper.c'), str(ROOT / 'tools/tests/remote_x9b_host.c'),
                            '-o', str(path / 'test')], check=True)
            result = subprocess.run([str(path / 'test')])
            assert (result.returncode == 0) == expected
    run(source, wrapper, True)
    if args.mutate:
        mutations = [('if (!ready) {', 'if (1) {'), ('i < 16; ++i) out[i] = token[i]', 'i < 8; ++i) out[i] = token[i]')]
        for old, new in mutations:
            assert source.count(old) == 1
            run(source.replace(old, new), wrapper, False)
            print('RED', old)
    print('PASS boot token and KAPI wrapper')

def run(ctx):
    return subprocess.call([sys.executable, '-B', __file__] + (['--mutate'] if ctx.mutate else []), cwd=ctx.root)

if __name__ == '__main__':
    main()
