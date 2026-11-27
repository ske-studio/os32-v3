"""Verify each graphics wrapper's pointer extent against a synthetic address range."""
import json
import re
import sys
import subprocess
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT))
from tests.support.media import main

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
    code += generated[generated.index('#define KAPI_OUT_LEN('):generated.index('\n}', generated.index('static int kapi_in_range(')) + 2]
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
    if not all('wrap_gfx2_' + name + '(' in code for name in EXPECTED):
        raise SystemExit("missing wrapper")
    code += 'static int test(void) {\n'+'\n'.join(tests)+'\nreturn 0; }\n'
    code += 'void _start(void) { int rc=test(); __asm__ volatile("int $0x80"::"a"(1),"b"(rc)); __builtin_unreachable(); }\n'
    source = work / 'probe.c'; source.write_text(code)
    exe = work / 'probe'
    subprocess.run(['gcc', '-m32', '-std=gnu11', '-ffreestanding', '-fno-pie', '-no-pie',
                  '-nostdlib', '-static', '-I'+str(ROOT / 'sdk/include/os32'), str(source), '-o', str(exe)], check=True)
    return subprocess.run(["qemu-i386", str(exe)], timeout=30).returncode


def verify(config):
    with tempfile.TemporaryDirectory() as directory:
        rc = observe(Path(directory), (ROOT / "sys/kapi/kapi_generated.c").read_text())
    if rc:
        raise SystemExit("FAIL wrapper pointer extent")


if __name__ == "__main__":
    main(lambda config: None, verify)
