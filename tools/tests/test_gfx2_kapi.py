#!/usr/bin/env python3
"""Run generated gfx2 guards against exact boundary ranges before target calls."""
CHECK = dict(name='gfx2-kapi-host', tier=1, needs=[], budget=5,
    covers=['tools/tests/test_gfx2_kapi.py', 'tools/tests/host32.py',
            'sdk/kapi.json', 'kapi/kapi_generated.c',
            'sdk/include/os32/os32_gfx2.h'])
import argparse
import json
from pathlib import Path
import re
import subprocess
import tempfile
import host32
ROOT = Path(__file__).resolve().parents[2]
# Independent expected sizes, including both directions and both output pointers.
EXPECTED = {
    'adapter_info': {'o': 'sizeof(GFX2_AdapterInfo)'},
    'adapter_gen': {'gen': '4'}, 'caps': {'o': '32'},
    'check_op': {'shape': 'sizeof(GFX2_Op)', 'how': '4'},
    'surface_create': {'sid': '4'}, 'surface_desc': {'o': 'sizeof(GFX2_SurfaceDesc)'},
    'screen_surface': {'sid': '4'}, 'lock': {'va': '4', 'pitch': '4'},
    'submit': {'ops': '2*sizeof(GFX2_Op)', 'done': '4', 'fence': '4'},
    'fence_query': {'completed': '4', 'failed_upto': '4'},
    'head_info': {'o': 'sizeof(GFX2_HeadInfo)'},
    'connector_info': {'o': 'sizeof(GFX2_ConnectorInfo)'},
    'connector_edid': {'out128': '128'}, 'mode_get': {'o': 'sizeof(GFX2_ModeInfo)'},
    'vblank_wait': {'seen': '4'}, 'palette': {'xrgb': '8'}, 'gamma': {'rgb768': '1536'},
}


def observe(work, generated):
    spec = json.loads((ROOT / 'sdk/kapi.json').read_text())
    code = '''#include "os32_gfx2.h"
typedef unsigned long u32;
typedef unsigned short u16;
typedef unsigned char u8;
typedef signed long i32;
typedef short i16;
#define __cdecl
#define KAPI_HIT(n) ((void)0)
#define ring3_fault_kill() return -99
static unsigned calls;
static u32 allowed, watched;
static int ring3_user_range_ok(u32 p,u32 n) { return p>=4096 && (p!=watched || n<=allowed); }
static int ring3_user_ranges_writable(u32 p,u32 n,u32 q,u32 m) {
 return (!n || ring3_user_range_ok(p,n)) && (!m || ring3_user_range_ok(q,m));
}
'''
    code += generated[generated.index('#define KAPI_OUT_LEN('):generated.index('/* 予約スロット')]
    tests = []
    for entry in spec['api']:
        name = entry['name']
        if name.removeprefix('gfx2_') not in EXPECTED:
            continue
        expected = EXPECTED[name.removeprefix('gfx2_')]
        decls = entry['args']
        names = [re.search(r'(\w+)$', d).group(1) for d in decls]
        code += 'int '+name+'('+', '.join(decls)+') { calls++; return 17; }\n'
        match = re.search(r'int __cdecl wrap_'+name+r'\([^\n]*\)\n\{.*?\n\}', generated, re.S)
        assert match, name
        code += match.group(0)+'\n'
        for arg, size in expected.items():
            vals = []
            for decl, param in zip(decls, names):
                typ = decl[:decl.rfind(param)].strip()
                vals.append('('+typ+')'+str(4096*(names.index(param)+1)) if '*' in decl else '32' if param=='size' else '2' if param in ('n', 'count') else '0')
            call = 'wrap_'+name+'('+', '.join(vals)+')'
            tests += [f'calls=0; watched={4096*(names.index(arg)+1)}; allowed={size}; if ({call}!=17 || calls!=1) return 1;',
                      f'calls=0; allowed=({size})-1; if ({call}!=-99 || calls) return 2;']
            vals[names.index(arg)] = '0'
            tests += ['calls=0; allowed=4096; if (wrap_'+name+'('+', '.join(vals)+')!=-99 || calls) return 3;']
    code += 'static int test(void) {\n'+'\n'.join(tests)+'\nreturn 0; }\n'
    code += 'void _start(void) { int rc=test(); __asm__ volatile("int $0x80"::"a"(1),"b"(rc)); __builtin_unreachable(); }\n'
    source = work / 'probe.c'; source.write_text(code)
    exe = work / 'probe'
    host32.build(['gcc', '-m32', '-std=gnu11', '-ffreestanding', '-fno-pie', '-no-pie',
                  '-nostdlib', '-static', '-I'+str(ROOT / 'sdk/include/os32'), str(source), '-o', str(exe)], check=True)
    return host32.run([str(exe)]).returncode


def main():
    parser = argparse.ArgumentParser(); parser.add_argument('--mutate', action='store_true')
    args = parser.parse_args()
    generated = (ROOT / 'kapi/kapi_generated.c').read_text()
    with tempfile.TemporaryDirectory() as td:
        work = Path(td)
        assert observe(work, generated) == 0
        if args.mutate:
            for anchor, replacement in [('KAPI_OUT_LEN(o, sizeof(GFX2_HeadInfo))', '0'),
                                        ('KAPI_OUT_LEN(rgb768, 1536u)', '1'),
                                        ('kapi_out_mul(KAPI_OUT_LEN(xrgb, count), 4u)', '1'),
                                        ('kapi_out_mul(KAPI_OUT_LEN(ops, n), (u32)(sizeof(GFX2_Op)))', '1')]:
                assert generated.count(anchor) == 1, anchor
                rc = observe(work, generated.replace(anchor, replacement))
                assert rc in (2, 3), (anchor, rc)
                print('RED runtime', anchor, 'rc='+str(rc))
    print('PASS all gfx2 pointer guards: exact length, short range, NULL, target ordering')


if __name__ == '__main__':
    main()


def run(ctx):
    return subprocess.call(['python3', '-B', __file__, *(['--mutate'] if ctx.mutate else [])], cwd=ctx.root)
