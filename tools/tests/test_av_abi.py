"""Exercise the v90 table, foreground transitions and remaining weak providers on both target ABIs."""
CHECK = dict(name='av-kapi-abi-host', tier=1, needs=['make:all', 'check:x87-lifecycle-host'], budget=120,
    covers=['tools/tests/test_av_abi.py', 'tools/tests/av_abi_host.c', 'tools/tests/av_abi_host.rs',
            'sdk/rust/os32api/src/av.rs', 'sdk/rust/os32api/src/kapi_generated.rs',
            'tools/check_av_portable.py', 'tools/checks/av_wiring.txt',
            'tools/checks/av_no_weak.sh', 'exec/exec_kapi_init.inc',
            'build/out/kernel.elf', 'kernel/av_focus.c', 'kapi/kapi_in2.c', 'tools/tests/host_arch/**', 'include/io.h', 'platform/pc98/platform_io.h', 'platform/pc98/platform_input_caps.h', 'kernel/gui.h', 'include/app_ids.h', 'exec/exec_gui.h', 'exec/exec_hooks.h', 'include/av_contract.h', 'input/hal/*.[ch]', 'sound/hal/*.[ch]',
            'sdk/include/os32/*.h', 'kapi/kapi_generated.c'],
    basis=['docs/decisions/sound/AV0_V90_ADDENDUM.md §4', 'docs/decisions/sound/AV_HAL_DESIGN.md §4'])

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
             if line and not line.startswith('#') and len(line.split()) == 2]
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
    focus = (root / 'kernel/av_focus.c').read_text()
    focus_cases = {}
    if mutate:
        for label, anchor, replacement, code in [
            ('notification omitted', 'in2_on_focus(old, owner);', '', 50),
            ('old and new swapped', 'in2_on_focus(old, owner);', 'in2_on_focus(owner, old);', 50),
            ('unchanged owner notified', 'if (old == owner) return;', '', 53),
            ('CUI return loses parent', 'select_owner(parent);', 'select_owner(0);', 58),
            ('background exit clears focus', 'else if (av_fg_owner == id)', 'else', 59),
            ('WM denied', 'ring3_wm_depth <= 0', '1', 51),
            ('owner gate omitted', 'res_owner_get() != APP_ID_SHELL', '0', 54),
            ('user gate omitted', 'ring3_call_from_user()', '0', 55),
            ('registration gate omitted', '!exec_gui_registered() ||', '0 ||', 56),
        ]:
            assert focus.count(anchor) == 1
            focus_cases[label] = focus.replace(anchor, replacement)
            cases.append((label, bind, defaults, code))
    (folder / 'lib.rs').write_text('#![no_std]\n' + ''.join(
        f'#[path="{root}/sdk/rust/os32api/src/{m}.rs"] pub mod {m};\n'
        for m in ('av', 'kapi_generated')) + f'#[path="{root}/tools/tests/av_abi_host.rs"] mod probe;\n')
    target = 'i686-unknown-linux-gnu' if machine == 'i386' else 'powerpc-unknown-linux-gnu'
    # The build runner installs rust-src, but has no registry for Cargo's sysroot graph.
    sysroot = Path(subprocess.check_output(['rustc', '--print', 'sysroot'], text=True).strip())
    library = sysroot / 'lib/rustlib/src/rust/library'
    rustc = ['rustc', '--target', target, '--crate-type=rlib', '-Cpanic=abort', '-Copt-level=2']
    externs = []
    for crate, source in [('core', 'core/src/lib.rs'),
                          ('compiler_builtins', 'compiler-builtins/compiler-builtins/src/lib.rs')]:
        metadata = folder / f'lib{crate}.rmeta'
        subprocess.run([*rustc, '--edition=2024', '--emit=metadata', '-Zalways-encode-mir', '--crate-name', crate,
                        *(['--cfg', 'feature="compiler-builtins"'] if crate == 'compiler_builtins' else []),
                        *externs, library / source, '-o', metadata], check=True)
        externs.extend(['--extern', f'{crate}={metadata}'])
    rust_obj = folder / 'av_probe.o'
    subprocess.run([*rustc, '--edition=2021', '--emit=obj', '-Crelocation-model=static',
                    *externs, folder / 'lib.rs', '-o', rust_obj], check=True)
    compiler = [cc]
    if machine == 'ppc' and not Path(cc).is_file():
        host = next(line.split()[1] for line in subprocess.check_output(
            ['rustc', '-vV'], text=True).splitlines() if line.startswith('host:'))
        linker = sysroot / 'lib/rustlib' / host / 'bin/gcc-ld/ld.lld'
        compiler = ['clang', '--target=powerpc-unknown-linux-gnu', '-fuse-ld=' + str(linker)]
    for label, assignments, source, expected in cases:
        (folder / 'av_wrappers.inc').write_text('\n'.join(decl + ['static void av_bind(KernelAPI *kapi) {'] + assignments + ['}']))
        (folder / 'stub.c').write_text(source)
        (folder / 'focus.c').write_text(focus_cases.get(label, focus))
        binary = folder / 'probe'
        subprocess.run([*compiler, '-std=gnu89', '-ffreestanding', '-nostdlib', '-static', '-O1',
            '-ffunction-sections', '-Wl,--gc-sections', '-Werror=implicit-function-declaration', '-Wno-attributes', '-I' + str(folder),
            '-Itools/tests/host_arch', '-Iplatform/pc98', '-Iinput/hal', '-Isdk/include/os32', '-Iinclude', '-Isound/hal', '-Iexec', '-Ikernel', '-DOS32_HOOK_GUI=0', '-DIN2_PUBLIC_EXCLUSIVE=1', folder / 'focus.c', 'tools/tests/av_abi_host.c',
            rust_obj, folder / 'wrap.c', folder / 'stub.c', '-std=gnu11', 'kapi/kapi_in2.c',
            *['input/hal/' + unit + '.c' for unit in ('in2_dev', 'in2_export', 'in2_ring', 'in2_excl', 'mouse_ext')], folder / 'start.S',
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
            binding = 'T' if row.split()[0] == 'IA' else 'W'
            assert re.search(r'^[0-9a-f]+ ' + binding + ' ' + name + r'$', symbols, re.M), name
    with tempfile.TemporaryDirectory() as td:
        for machine in ('i386', 'ppc'):
            exercise(ctx.root, Path(td), machine, ctx.mutate)
    return 0
