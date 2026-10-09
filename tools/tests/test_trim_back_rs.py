#!/usr/bin/env python3
"""g4: same Rust fixture/allocator + production loop/WM/kernel, i386 qemu.

Normal controls precede mutants. Product inputs are changed only in temporary
copies. Runtime rc=1 and an exact named FAIL are required; build errors are not RED.
"""
import argparse
import os
from pathlib import Path
import re
import subprocess
import tempfile
import host32
import test_trim_kernel as kernel

ROOT = Path(__file__).resolve().parents[2]
CACHE = 'userland/rust/trim_back_rs/src/cache.rs'
WM = 'userland/gshell/src/trim.rs'
SLOT = 'exec/appslot.c'
APP = 'userland/rust/libos32gui/src/app.rs'
STUB = 'userland/rust/libos32gui_stub/src/lib.rs'
TARGET_SRCS = ['exec/appslot.c', 'exec/exec.c', 'kernel/gui.c', 'kernel/kselftest.c',
         'kapi/kapi_sys.c', 'exec/exec_heap.c', 'exec/appmem.c', 'exec/appmem_map.c',
         'exec/appmem_unmap.c', 'kernel/paging_app.c', 'kernel/physmem.c',
         CACHE, WM, APP, STUB]
MUTANTS = [
    ('stop-leaves-bit', SLOT, '    slot_zero(a);\n    a->state = APP_STATE_FREE;',
     '    u32 pending = a->trim_pending, epoch = a->trim_epoch;\n'
     '    slot_zero(a); a->trim_pending = pending; a->trim_epoch = epoch;\n'
     '    a->state = APP_STATE_FREE;', 2, 'STOP slot trim cleared'),
    ('wm-forget-missing', WM,
     '(*SENT.0.get())[(id - multiapp::APP_ID_MIN) as usize] = false;',
     'let _ = id;', 1, 'WM sent cleared'),
    ('rust-large-cache', CACHE, 'pub const BLOCK_BYTES: usize = 4000;',
     'pub const BLOCK_BYTES: usize = 65536;', 0, 'Rust PREP ARENA=2 LARGE=0'),
    ('rust-raw-map-retry', CACHE, 'unsafe { (api().mem_map)(bytes, hint, flags) }',
     'unsafe { let p = (api().mem_map)(bytes, hint, flags); if p.is_null() { '
     '(api().sys_yield)(); (api().mem_map)(bytes, hint, flags) } else { p } }',
     0, 'raw map no retry'),
]


def rust_library(tmp, sources):
    work = tmp / 'rust'
    work.mkdir(exist_ok=True)
    (work / 'cache.rs').write_text(sources[CACHE])
    # Only leading module docs are adjusted for include!, executable text is exact.
    (work / 'wm_trim.rs').write_text(sources[WM].replace('//!', '//'))
    src = (ROOT / 'tools/tests/trim_back_rs_host.rs').read_text().replace('CACHE_SOURCE', 'cache.rs')
    fn = kernel.base.function
    src += '\n' + fn(sources[APP], 'pub fn run_vt(')
    stub = sources[STUB].split('struct HookCell', 1)[1].split('\n/* ===', 1)[0]
    src += '\nstruct HookCell' + stub
    abi = (ROOT / 'sdk/rust/os32api/src/kapi_generated.rs').read_text()
    fields = re.findall(r'pub (\w+): (.+),', abi.split('pub struct KernelAPI {')[1].split('\n}')[0])
    targets = dict(mem_alloc='g4_alloc', mem_free='g4_free', mem_stat='g4_stat',
                   mem_map='g4_map', mem_unmap='g4_unmap', gui_call='g4_gui',
                   sys_yield='g4_yield', exec_app_state='g4_state')
    src += '\nextern "C" {\n'
    for name in [*targets.values(), 'g4_unexpected']:
        src += f'fn {name}();\n'
    src += '}\nfn host_api() -> os32api::KernelAPI { unsafe { os32api::KernelAPI {\n'
    for name, ty in fields:
        value = f'core::mem::transmute::<*const (), {ty}>({targets.get(name, "g4_unexpected")} as *const ())' if ty.startswith('unsafe ') else 'core::mem::zeroed()'
        src += f'{name}: {value},\n'
    src += '} } }\n#[no_mangle] pub extern "C" fn g4_rust_raw_map() { check(cache::raw_map(4096, core::ptr::null_mut(), 0).is_null(), b"raw failure returned\\0"); }\n'
    (work / 'lib.rs').write_text(src)
    (work / 'Cargo.toml').write_text(f'''[package]
name = "g4_host"
version = "0.0.0"
edition = "2021"
[workspace]
[lib]
path = "lib.rs"
crate-type = ["staticlib"]
[dependencies]
os32api = {{ path = "{ROOT / 'sdk/rust/os32api'}" }}
[profile.release]
panic = "abort"
opt-level = 2
''')
    target = ROOT / 'sdk/rust/i686-os32-none.json'
    command = ['cargo', 'build', '--release', '--manifest-path', str(work / 'Cargo.toml'),
               '--target', str(target), '-Zjson-target-spec', '-Zbuild-std=core,alloc,compiler_builtins',
               '-Zbuild-std-features=compiler-builtins-mem']
    host32.build(command, cwd=ROOT, capture_output=True, text=True, check=True)
    return work / 'target/i686-os32-none/release/libg4_host.a'


def build(tmp, sources):
    cc = kernel.build_fixture(tmp, sources)
    p = tmp / 'mem_fixture.c'
    s = p.read_text().replace('#include "public_wrap_source.c"', '#undef caller_access_get\n#include "public_wrap_source.c"')
    p.write_text(s)
    fn = kernel.base.function
    (tmp / 'stop_source.c').write_text('\n'.join(fn(sources['exec/exec.c'], sig) for sig in
        ('static void exec_kill_one(', 'i32 exec_kill(')))
    objects = []
    for index, name in enumerate(('exec/appmem.c', 'exec/appmem_map.c', 'exec/appmem_unmap.c', 'kernel/paging_app.c', 'kernel/physmem.c')):
        src, obj = tmp / f'part{index}.c', tmp / f'part{index}.o'
        src.write_text(sources[name])
        extra = ['-Dpgalloc_alloc_phys=map_alloc', '-Dpgalloc_free_n_owner=map_free'] if name == 'kernel/paging_app.c' else []
        host32.build(cc + extra + ['-c', str(src), '-o', str(obj)], check=True, capture_output=True, text=True)
        objects.append(str(obj))
    return cc, objects, rust_library(tmp, sources)


def run(tmp, built, case, runner):
    cc, objects, lib = built
    output = tmp / f'g4-{case}'
    host32.build(cc + ['-DG4_CASE=' + str(case), '-nostdlib', '-static', '-no-pie', '-Wl,--gc-sections',
        str(ROOT / 'tools/tests/trim_stop_host.c'), *objects, str(lib), '-o', str(output)],
        check=True, capture_output=True, text=True)
    return host32.run([str(output)], runner=runner, capture_output=True, text=True, timeout=host32.RUN_TIMEOUT)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--mutate', action='store_true')
    parser.add_argument('--runner', choices=('native', 'qemu'), default=host32.selected_runner())
    args = parser.parse_args()
    assert not any(re.search(r'\bfn\s+mem_map\s*\(', p.read_text()) for p in (ROOT / 'sdk/rust/os32api/src').rglob('*.rs')), 'os32api must expose mem_map only as a KAPI field, without a wrapper'
    sources = {name: (ROOT / name).read_text() for name in TARGET_SRCS}
    with tempfile.TemporaryDirectory(prefix='g4-rust-') as directory:
        tmp = Path(directory)
        built = build(tmp, sources)
        for case in range(4):
            r = run(tmp, built, case, args.runner)
            print(r.stdout, end='')
            assert r.returncode == 0, (case, r.returncode, r.stdout, r.stderr)
        if args.mutate:
            for name, path, old, new, case, label in MUTANTS:
                assert sources[path].count(old) == 1, (name, sources[path].count(old))
                changed = dict(sources)
                changed[path] = sources[path].replace(old, new)
                r = run(tmp, build(tmp, changed), case, args.runner)
                assert r.returncode == 1 and 'FAIL: ' + label in r.stdout.splitlines(), (name, r.returncode, r.stdout, r.stderr)
                print('RED: ' + name + ' -> ' + label)
            print('trim back Rust: 4 runtime RED / 0 survived / 0 ERROR')
    return 0


if __name__ == '__main__':
    try:
        raise SystemExit(main())
    except subprocess.CalledProcessError as e:
        print((e.stdout or '') + (e.stderr or ''))
        raise
