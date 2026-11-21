"""Exercise the v90 table and shared stubs on both native target ABIs."""
CHECK = dict(name='av-kapi-abi-host', tier=1, needs=['make:all'], budget=120,
    covers=['tools/tests/test_av_abi.py', 'tools/tests/av_abi_host.c', 'tools/tests/av_abi_host.rs',
            'sdk/rust/os32api/src/av.rs', 'sdk/rust/os32api/src/kapi_generated.rs',
            'tools/check_av_portable.py', 'tools/checks/av_wiring.txt',
            'tools/checks/av_no_weak.sh', 'exec/exec_kapi_init.inc',
            'build/out/kernel.elf', 'include/av_contract.h', 'input/hal/*.[ch]', 'sound/hal/*.[ch]',
            'sdk/include/os32/*.h', 'kapi/kapi_generated.c'],
    basis=['docs/decisions/sound/AV0_V90_ADDENDUM.md §4'])

import os
from pathlib import Path
import re
import subprocess
import tempfile


def exercise(root, folder, machine, mutate):
    elf = 'i386-elf' if machine == 'i386' else 'powerpc-elf'
    key = 'CROSS_DIR' if machine == 'i386' else 'PPC_CROSS_DIR'
    cross = os.environ.get(key, '/home/hight/opt/' + ('cross' if machine == 'i386' else 'cross-ppc'))
    cc = str(Path(cross) / 'bin' / (elf + '-gcc'))
    api = (root / 'exec/exec_kapi_init.inc').read_text().splitlines()
    names = [line.split()[1] for line in (root / 'tools/checks/av_wiring.txt').read_text().splitlines()
             if line and not line.startswith('#')]
    decl = [line for line in api if any('wrap_' + n + '(' in line for n in names)]
    bind = [line for line in api if any('kapi->' + n + ' =' in line for n in names)]
    wrappers = (root / 'kapi/kapi_generated.c').read_text()
    bodies = [re.search(r'int __cdecl wrap_' + n + r'\([^\n]*\)\n\{.*?\n\}', wrappers, re.S)[0]
              for n in names]
    guards = wrappers[wrappers.index('#define KAPI_OUT_LEN'):wrappers.index('int kapi_v86s_check')]
    defaults = (root / 'sound/hal/a0_weak.c').read_text()
    exit_code = ('mov %eax,%ebx; mov $1,%eax; int $0x80' if machine == 'i386'
                 else 'li 0,1; sc')
    call = 'call av_abi_probe' if machine == 'i386' else 'bl av_abi_probe'
    (folder / 'start.S').write_text('.globl _start\n_start:\n ' + call + '\n ' + exit_code + '\n')
    (folder / 'wrap.c').write_text('#include "av_contract.h"\n#define KAPI_HIT(x) ((void)0)\n'
        'int ring3_user_ranges_writable(u32 a,u32 n,u32 b,u32 m){return 1;}\n'
        'int ring3_user_range_ok(u32 a,u32 n){return 1;}\n'
        'void ring3_fault_kill(void){__builtin_trap();}\n' + guards + '\n'.join(bodies))
    def change_stub(name, old, new):
        body = re.search(r'__attribute__\(\(weak\)\) int ' + name +
                         r'\([^)]*\)\n\{[^}]*\}', defaults)[0]
        assert defaults.count(body) == 1 and body.count(old) == 1
        return defaults.replace(body, body.replace(old, new))

    cases = [('', bind, defaults, 0)]
    if mutate:
        cases += [('adjacent providers', [s.replace('= wrap_snd2_stream_start;', '= wrap_snd2_stream_pause;') for s in bind], defaults, 10),
                  ('successful stub', bind, change_stub('snd2_dev_count', 'return OS32_ERR_NOSYS;', 'return 0;'), 1),
                  ('modified output', bind, change_stub('snd2_dev_info', '(void)o;', '((u8 *)o)[0] = 0;'), 40)]
    # Build core for each target from the installed nightly rust-src.
    (folder / 'Cargo.toml').write_text('[package]\nname="av_probe"\nversion="0.1.0"\nedition="2021"\n'
        '[lib]\npath="lib.rs"\n[profile.release]\npanic="abort"\n')
    (folder / 'lib.rs').write_text('#![no_std]\n' + ''.join(
        f'#[path="{root}/sdk/rust/os32api/src/{m}.rs"] pub mod {m};\n'
        for m in ('av', 'kapi_generated')) + f'#[path="{root}/tools/tests/av_abi_host.rs"] mod probe;\n')
    target = 'i686-unknown-linux-gnu' if machine == 'i386' else 'powerpc-unknown-linux-gnu'
    subprocess.run(['cargo', 'rustc', '--manifest-path', folder / 'Cargo.toml', '--offline', '-Zbuild-std=core', '--release',
                    '--target', target, '--', '--emit=obj', '-C', 'relocation-model=static'],
                   cwd=root, env=dict(os.environ, CARGO_TARGET_DIR=str(folder / 'target')), check=True)
    rust_obj, = (folder / 'target' / target / 'release/deps').glob('av_probe-*.o')
    for label, assignments, source, expected in cases:
        (folder / 'av_wrappers.inc').write_text('\n'.join(decl + ['static void av_bind(KernelAPI *kapi) {'] + assignments + ['}']))
        (folder / 'stub.c').write_text(source)
        binary = folder / 'probe'
        subprocess.run([cc, '-std=gnu89', '-ffreestanding', '-nostdlib', '-static', '-O1',
            '-ffunction-sections', '-Wl,--gc-sections', '-Werror=implicit-function-declaration', '-Wno-attributes', '-I' + str(folder),
            '-Isdk/include/os32', '-Iinclude', '-Isound/hal', 'tools/tests/av_abi_host.c',
            rust_obj, folder / 'wrap.c', folder / 'stub.c', 'input/hal/a0_weak.c', folder / 'start.S',
            '-o', binary], cwd=root, check=True)
        rc = subprocess.run(['qemu-' + ('i386' if machine == 'i386' else 'ppc'), binary]).returncode
        assert rc == expected, (machine, label, rc, expected)
        print(machine, 'RED' if label else 'PASS', label, flush=True)


def run(ctx):
    subprocess.run(['python3', '-B', 'tools/check_av_portable.py'], cwd=ctx.root, check=True)
    symbols = subprocess.check_output(['nm', '-g', '--defined-only', ctx.root / 'build/out/kernel.elf'], text=True)
    for row in (ctx.root / 'tools/checks/av_wiring.txt').read_text().splitlines():
        if row and not row.startswith('#'):
            name = row.split()[1]
            assert re.search(r'^[0-9a-f]+ W ' + name + r'$', symbols, re.M), name
    with tempfile.TemporaryDirectory() as td:
        for machine in ('i386', 'ppc'):
            exercise(ctx.root, Path(td), machine, ctx.mutate)
    return 0
