"""Release binaries keep source locations reproducible across checkouts (G11)."""
import os
from pathlib import Path
import shutil
import subprocess
import tempfile

CHECK = dict(name='rust-build-paths', tier=1, needs=['make:userland/libos32gui.elf'], budget=5,
             covers=['.cargo/config.toml', 'rust-toolchain.toml', 'sdk/rustc_stamp.py',
                     'sdk/target_profile.py', 'sdk/os32_generations.py'], basis=['docs/POLICY_CI.md G11'])


def run(ctx):
    config = (ctx.root / '.cargo/config.toml').read_text()
    elf = (ctx.root / 'userland/libos32gui.elf').read_bytes()
    sysroot = subprocess.check_output(['rustc', '--print', 'sysroot'], cwd=ctx.root).strip()
    assert bytes(ctx.root) not in elf and sysroot not in elf
    with tempfile.TemporaryDirectory() as tmp:
        # The fixture runs on the host, so stamp its native objects with host binutils.
        for tool in ('ar', 'objcopy'):
            (Path(tmp) / ('i386-elf-' + tool)).symlink_to(shutil.which(tool))
        first = None
        host = subprocess.check_output(['rustc', '--print', 'host-tuple'], cwd=ctx.root, text=True).strip()
        cases = ['first', 'second']
        if ctx.mutate:
            cases.extend(['path-mutant', 'symbol-mutant'])
        for name in cases:
            root = Path(tmp) / name
            app = root / 'app'
            dep = root / 'dep'
            (root / '.cargo').mkdir(parents=True)
            shutil.copyfile(ctx.root / 'rust-toolchain.toml', root / 'rust-toolchain.toml')
            setting = config
            if name == 'path-mutant':
                anchor = 'trim-paths = "all"'
                assert config.count(anchor) == 1
                setting = config.replace(anchor, 'trim-paths = "none"')
            elif name == 'symbol-mutant':
                assert config.count('OS32_SOURCE_ROOT =') == 1
                setting = config.replace('OS32_SOURCE_ROOT =', 'UNUSED_ROOT =')
            (root / '.cargo/config.toml').write_text(setting)
            for crate in (app, dep):
                (crate / 'src').mkdir(parents=True)
                manifest = f'[package]\nname="{crate.name}"\nversion="0.1.0"\nedition="2021"\n'
                if crate == app:
                    manifest += '[dependencies]\ndep={path="../dep"}\n'
                (crate / 'Cargo.toml').write_text(manifest)
            (dep / 'src/lib.rs').write_text("pub fn source() -> &'static str { file!() }")
            (app / 'src/main.rs').write_text('fn main() { println!("{}", dep::source()) }')
            env = dict(os.environ, PATH=tmp + os.pathsep + os.environ['PATH'],
                       RUSTC_WRAPPER=str(ctx.root / 'sdk/rustc_stamp.py'))
            subprocess.run(['cargo', 'build', '--offline', '--release', '--target', host],
                           cwd=app, env=env, check=True)
            binary = app / 'target' / host / 'release/app'
            location = subprocess.check_output([binary]).strip()
            if name == 'path-mutant':
                assert bytes(dep) in location
                print('RED: disabling trim-paths exposed the dependency checkout')
            else:
                assert bytes(root) not in location and location.endswith(b'src/lib.rs')
                image = binary.read_bytes()
                if name == 'first':
                    first = image
                elif name == 'second':
                    assert image == first
                else:
                    assert image != first
                    print('RED: Cargo path-dependent crate identity changed the binary')
    return 0
