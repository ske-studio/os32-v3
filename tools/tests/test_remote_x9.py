#!/usr/bin/env python3
CHECK = dict(name='remote-x9', tier=1, needs=[], budget=5,
    covers=['tools/tests/fixtures/build_profile_ppc.json', 'sdk/kapi.json', 'sdk/gen_kapi.py', 'sdk/kapi_target_ppc.py',
            'sdk/kapi_rust_gen.py', 'sdk/include/os32/os32_kapi_shared.h',
            'sdk/include/os32/os32_kapi_generated.h', 'sdk/include/os32/os32_kapi_slots.h',
            'sdk/rust/os32api/src/kapi_generated.rs', 'include/kapi_call_generated.h',
            'kapi/kapi_generated.c', 'exec/exec_kapi_init.inc',
            'build/targets/ppc-powermac.mk', 'kernel/console/con_sink.c', 'tools/tests/test_remote_x9.py'],
    basis=['LAN_OPERATOR_DESIGN §3', '[ABI]'],
)
import argparse
import json
from pathlib import Path
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[2]
OUTPUTS = ('sdk/include/os32/os32_kapi_generated.h', 'sdk/include/os32/os32_kapi_slots.h',
           'include/kapi_call_generated.h', 'kapi/kapi_generated.c', 'exec/exec_kapi_init.inc')

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--mutate', action='store_true')
    args = parser.parse_args()
    api = json.loads((ROOT / 'sdk/kapi.json').read_text())
    assert api['version'] == 87 and len(api['api']) == 298
    assert api['api'][292] == {'name': 'con_sink_capture', 'ret': 'int', 'args': ['u32 op']}
    with tempfile.TemporaryDirectory() as temp:
        out = Path(temp)
        for target in ('x86-pc98', 'ppc-powermac'):
            dest = out / target
            subprocess.run([sys.executable, '-B', str(ROOT / 'sdk/gen_kapi.py'),
                            '--target', target, '--out-dir', str(dest)], cwd=ROOT, check=True)
            generated = (dest / 'kapi/kapi_generated.c').read_text()
            assert 'return con_sink_capture(op);' in generated
            for name in ('open', 'status', 'read', 'write', 'close', 'notice_read', 'notice_serial'):
                assert 'return kapi_host_' + name + '(' in generated
            if target == 'ppc-powermac':
                assert 'void __cdecl wrap_sys_halt(void)\n{\n    extern i32 ppc_kapi_wait_once(void);\n    (void)ppc_kapi_wait_once();\n}\n' in generated
                # sys_yield stays on exec_sys_yield (parks exec_start children; arch-ctx-guest lifecycle).
                assert 'i32 __cdecl wrap_sys_yield(void)\n{\n    KAPI_HIT(200);\n    return exec_sys_yield();\n}\n' in generated
                assert 'rshell_active = active;' in generated
                assert 'int __cdecl wrap_serial_is_initialized(void)\n{\n    KAPI_HIT(38);\n    return port_is_initialized();\n}\n' in generated
            # The pinned link-SH source list (checked against make by build-selection-host).
            assert 'kernel/console/con_sink.c' in json.loads(
                (ROOT / 'tools/tests/fixtures/build_profile_ppc.json').read_text())['link-sh']['KB_C']
        x86 = out / 'x86-pc98'
        for name in OUTPUTS:
            assert (x86 / name).read_bytes() == (ROOT / name).read_bytes(), name
        if args.mutate:
            path = x86 / 'kapi/kapi_generated.c'
            text = path.read_text()
            anchor = 'return con_sink_capture(op);'
            assert text.count(anchor) == 1
            path.write_text(text.replace(anchor, 'return 0;'))
            assert path.read_bytes() != (ROOT / 'kapi/kapi_generated.c').read_bytes()
            print('RED generated wrapper tampering')
    print('PASS X9 slot, x86 and PPC provider, generated outputs')

def run(ctx):
    return subprocess.call([sys.executable, '-B', __file__] +
                           (['--mutate'] if ctx.mutate else []), cwd=ctx.root)

if __name__ == '__main__':
    main()
