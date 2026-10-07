"""Run the real USER enumeration helper, including boundary and error contracts."""
import argparse
import pathlib
import os
import subprocess
import tempfile
import host32
ROOT = pathlib.Path(__file__).resolve().parents[2]
MUTANTS = [
    ('path snapshot', 'sys_ls_window(snapshot,', 'sys_ls_window(path,'),
    ('skip', 'skip += packet.count;', 'skip += 0;'),
    ('syscall error stop', 'if (rc < 0) return rc;', 'if (0) return rc;'),
    ('FS error truncation', 'if (packet.done) return packet.result;', 'if (packet.result < 0 || packet.done) return packet.result;'),
    ('late INVAL fallback', 'rc == OS32_ERR_INVAL && skip == 0', 'rc == OS32_ERR_INVAL'),
    ('first IO fallback', 'rc == OS32_ERR_INVAL && skip == 0', 'rc < 0 && skip == 0'),
    ('trusted', 'rc == OS32_ERR_INVAL && skip == 0', '0'),
]
def run(args, mutant=None):
    source = (ROOT / 'userland/lib/rt/ls.c').read_text()
    if mutant:
        name, old, new = mutant
        assert source.count(old) == 1
        source = source.replace(old, new)
    with tempfile.TemporaryDirectory(prefix='ls-client-') as folder:
        tmp = pathlib.Path(folder)
        (tmp / 'ls.c').write_text(source)
        exe = tmp / 'test'
        subprocess.run(['gcc', '-m32', '-static', '-no-pie', '-fno-pie', '-nostdlib',
            '-fno-stack-protector', '-ffreestanding', '-std=gnu11', '-Wall', '-Wextra', '-Werror',
            '-I' + str(ROOT / 'sdk/include'), '-I' + str(ROOT / 'sdk/include/os32'),
            str(tmp / 'ls.c'), str(ROOT / 'tools/tests/ls_client_host.c'), '-o', str(exe)],
            check=True, capture_output=True)
        result = host32.run([str(exe)], runner=args.runner, timeout=30, capture_output=True)
        assert (result.returncode != 0) if mutant else (result.returncode == 0), result
        print('RED runtime: ' + mutant[0] if mutant else 'PASS: counts/order/14/15/301/error/TRUSTED; 301 entries = 22 calls')
def sdk_link():
    """Compile a GNU89 consumer with the four CRT objects and USER allocator."""
    cross = pathlib.Path(os.environ.get('CROSS_DIR', '/home/hight/opt/cross'))
    sdk = ROOT / 'build/sdk'
    libgcc = pathlib.Path(subprocess.check_output(
        [str(cross / 'bin/i386-elf-gcc'), '-print-libgcc-file-name'], text=True).strip()).parent
    with tempfile.TemporaryDirectory(prefix='ls-sdk-') as folder:
        tmp = pathlib.Path(folder)
        (tmp / 'main.c').write_text('#include "os32/ls.h"\nstatic void cb(const DirEntry_Ext *entry, void *ctx);\nint main(int argc, char **argv, KernelAPI *api) {\n    (void)argc; (void)argv; (void)api;\n    return os32_ls("/", cb, 0);\n}\nstatic void cb(const DirEntry_Ext *entry, void *ctx) { (void)entry; (void)ctx; }\n')
        subprocess.run([str(cross / 'bin/i386-elf-gcc'), '-std=gnu89', '-ffreestanding',
            '-fno-pie', '-fno-stack-protector', '-Wall', '-Wextra', '-Werror',
            '-I'+str(sdk / 'include'), '-I'+str(sdk / 'include/os32'),
            '-include', str(sdk / 'include/os32/os32_unit_stamp.h'),
            '-c', str(tmp / 'main.c'), '-o', str(tmp / 'main.o')], check=True, capture_output=True)
        subprocess.run(['python3', str(sdk / 'bin/link_guard.py'), str(cross / 'bin/i386-elf-ld'),
            '-m', 'elf_i386', '-T', str(sdk / 'link/app.ld'), '-nostdlib', '--nmagic', '--gc-sections',
            '-L'+str(sdk / 'lib'), '-L'+str(cross / 'i386-elf/lib'), '-L'+str(libgcc),
            '-o', str(tmp / 'app.elf'),
            *[str(sdk / 'crt' / name) for name in ('crt0.o','crt0_c.o','syscalls.o','help.o')],
            str(tmp / 'main.o'), '-los32nano', '-lc', '-lgcc'], check=True, capture_output=True)
        symbols = subprocess.check_output([str(cross / 'bin/i386-elf-nm'), str(tmp / 'app.elf')], text=True)
        assert ' T os32_ls' in symbols
        print('PASS: standalone SDK GNU89 consumer links os32_ls with four CRT objects and libos32nano')

RUST_MUTANTS = [
    ('Rust path snapshot', 'sys_ls_window)(snapshot.as_ptr(),', 'sys_ls_window)(path,'),
    ('Rust skip', 'skip.checked_add(packet.count)', 'skip.checked_add(0)'),
    ('Rust syscall error stop', 'if rc < 0 { return rc; }', 'if false { return rc; }'),
    ('Rust FS error truncation', 'if packet.done != 0 { return packet.result; }', 'if packet.result < 0 || packet.done != 0 { return packet.result; }'),
    ('Rust trusted', 'rc == OS32_ERR_INVAL && skip == 0', 'false'),
]
def rust_run(args):
    # make all supplies target core/compiler_builtins. Select a compatible
    # cached pair once; dependency probe failures are never mutation RED.
    deps = ROOT / 'userland/rust/target/i686-os32-none/release/deps'
    with tempfile.TemporaryDirectory(prefix='ls-rust-') as folder:
        tmp = pathlib.Path(folder)
        source = (ROOT / 'sdk/rust/os32api/src/ls.rs').read_text()
        (tmp / 'ls.rs').write_text(source)
        (tmp / 'host.rs').write_bytes((ROOT / 'tools/tests/ls_client_host.rs').read_bytes())
        c = (ROOT / 'tools/tests/ls_client_host.c').read_text()
        c = c.replace('static int window(', 'int window(').replace('static int legacy(', 'int legacy(')
        (tmp / 'host.c').write_text(c)
        cmd = ['rustc', '--crate-type', 'staticlib', '--edition=2021', '-Cpanic=abort',
            '-Copt-level=2', '-Zunstable-options', '--target', str(ROOT / 'sdk/rust/i686-os32-none.json'),
            '-Ldependency=' + str(deps), str(tmp / 'host.rs'), '-o', str(tmp / 'rust.a')]
        selected = None
        for core in sorted(deps.glob('libcore-*.rlib')):
            for builtins in sorted(deps.glob('libcompiler_builtins-*.rlib')):
                candidate = cmd + ['--extern', 'core=' + str(core), '--extern', 'compiler_builtins=' + str(builtins)]
                probe = subprocess.run(candidate, capture_output=True, text=True)
                if probe.returncode == 0:
                    selected = candidate
                    break
            if selected: break
        assert selected, 'make all must supply compatible target Rust core/compiler_builtins'
        for mutant in [None] + (RUST_MUTANTS if args.mutate else []):
            body = source
            if mutant:
                name, old, new = mutant
                assert body.count(old) == 1
                body = body.replace(old, new)
            (tmp / 'ls.rs').write_text(body)
            subprocess.run(selected, check=True, capture_output=True)
            exe = tmp / 'test'
            subprocess.run(['gcc', '-m32', '-static', '-no-pie', '-fno-pie', '-nostdlib',
                '-fno-stack-protector', '-ffreestanding', '-std=gnu11', '-Wall', '-Wextra', '-Werror',
                '-I' + str(ROOT / 'sdk/include'), '-I' + str(ROOT / 'sdk/include/os32'),
                str(tmp / 'host.c'), str(tmp / 'rust.a'), '-o', str(exe)], check=True, capture_output=True)
            result = host32.run([str(exe)], runner=args.runner, timeout=30, capture_output=True)
            assert (result.returncode != 0) if mutant else (result.returncode == 0), result
            print('RED runtime: ' + mutant[0] if mutant else 'PASS Rust ILP32: same boundary/order/error/TRUSTED/nested/path cases')

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--runner', default=None)
    parser.add_argument('--mutate', action='store_true')
    args = parser.parse_args()
    sdk_link()
    run(args)
    rust_run(args)
    if args.mutate:
        for mutant in MUTANTS: run(args, mutant)
if __name__ == '__main__': main()
