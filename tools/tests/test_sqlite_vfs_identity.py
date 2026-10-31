"""T4b-4: real SQLite callbacks/FD table; runtime mutation failures only."""
CHECK = dict(name='sqlite-vfs-identity-host', tier=1,
    covers=['tools/tests/sqlite_vfs_identity_host.c', 'tools/tests/sqlite_groups_host.c', 'tools/tests/sqlite_groups_backend.h', 'tools/tests/vfs_fd_sqlite_host.c', 'lib/sqlite3/os32_sqlite_vfs.c', 'lib/sqlite3/os32_sqlite_vfs.h', 'lib/sqlite3/sqlite3.c', 'lib/sqlite3/os32_sqlite_config.h', 'fs/vfs_fd.c', 'fs/vfs_fd_stdin.inc', 'fs/vfs_stdin.h', 'exec/exec.h', 'fs/vfs.c', 'fs/vfs.h', 'fs/ext2/ext2_vfs.c', 'tools/tests/input_fixture_host.h', 'kernel/input_service.c', 'include/pointer_service.h', 'include/input_service.h', 'include/kbd.h', 'tools/tests/kernel_includes.py'],
    needs=[], budget=6.285076)

from kernel_includes import include_flags
import os
import pathlib
import shutil
import subprocess
import sys
import tempfile

ROOT = pathlib.Path(__file__).resolve().parents[2]
CASES = ['absolute', 'relative', 'alias', 'parent_child', 'same_owner', 'parked', 'quarantine',
         'close_gap', 'separate', 'errors', 'readonly', 'unsupported', 'fullpath']
FILES = ['fs/vfs.h', 'fs/vfs_fd.c', 'fs/vfs_fd_stdin.inc', 'fs/vfs_stdin.h', 'lib/sqlite3/os32_sqlite_vfs.c',
         'lib/sqlite3/os32_sqlite_vfs.h', 'tools/tests/sqlite_vfs_identity_host.c',
         'tools/tests/sqlite_groups_host.c', 'tools/tests/sqlite_groups_backend.h',
         'tools/tests/vfs_fd_sqlite_host.c', 'tools/tests/input_fixture_host.h']
INC = include_flags(ROOT)
VFS = 'lib/sqlite3/os32_sqlite_vfs.c'
FD = 'fs/vfs_fd.c'
MUTANTS = [
    ('path_comparison', 'alias', [
        (VFS, 'other->main_identity.file_id == id.file_id', '0'),
        (FD, 'f->ino == id.file_id', '!__builtin_strcmp(f->path, open_files[fd].path)')]),
    ('early_release', 'close_gap', [(VFS,
        'Os32GroupSlot *g = group_lookup(&p->lease.cookie);',
        'Os32GroupSlot *g = group_lookup(&p->lease.cookie); g->has_main = 0;')]),
    ('truncate_success', 'errors', [(VFS,
        'return vfs_resize_fd(p->fd, (u32)size) == VFS_OK',
        'return (vfs_resize_fd(p->fd, (u32)size), 1)')]),
    ('sync_success', 'errors', [(VFS,
        'return vfs_sync_fd(p->fd) == VFS_OK', 'return (vfs_sync_fd(p->fd), 1)')]),
    ('delete_success', 'errors', [(VFS,
        'if (rc != VFS_OK) return SQLITE_IOERR_DELETE;',
        'if (rc != VFS_OK) return SQLITE_OK;')]),
    ('access_success', 'errors', [(VFS,
        'if (rc != VFS_OK) return SQLITE_IOERR_ACCESS;',
        'if (rc != VFS_OK) return SQLITE_OK;')]),
    ('out_flags_request', 'readonly', [(VFS,
        'if (pOutFlags) *pOutFlags = flags;',
        'if (pOutFlags) *pOutFlags = SQLITE_OPEN_READWRITE;')]),
    ('ro_write', 'readonly', [(VFS,
        'if (p->readonly) return SQLITE_READONLY;\n    vfs_seek', '(void)p->readonly;\n    vfs_seek')]),
    ('ro_truncate', 'readonly', [(VFS,
        'if (p->readonly) return SQLITE_READONLY;\n    if (size < 0',
        'if (size < 0')]),
    ('backend_policy', 'unsupported', [(VFS,
        'if (backend != VFS_SQLITE_EXT2)',
        'if (0 && backend != VFS_SQLITE_EXT2)'),
        (VFS, 'if (flags & SQLITE_OPEN_READWRITE) {\n            rc = vfs_sqlite_writable(zName);',
         'if (0 && (flags & SQLITE_OPEN_READWRITE)) {\n            rc = vfs_sqlite_writable(zName);')]),
    ('relative_fullpath', 'fullpath', [(VFS,
        'return vfs_resolve_path(zName, zOut, nOut) == VFS_OK\n        ? SQLITE_OK : SQLITE_CANTOPEN;',
        'kstrncpy(zOut, zName, (u32)nOut); return SQLITE_OK;')]),
]


def compile_host(root, obj, exe):
    subprocess.run(['gcc', '-std=gnu11', '-Wall', '-Wextra', '-Werror',
                    '-Wno-unused-parameter', '-Wno-sign-compare',
                    '-Wno-pointer-to-int-cast', '-Wno-missing-field-initializers',
                    '-D__cdecl=', '-Wno-unused-function',
                    '-I' + str(root),  *include_flags(ROOT), *INC,
                    str(root / 'tools/tests/sqlite_vfs_identity_host.c'), str(obj),
                    '-o', str(exe)], check=True)


def main():
    with tempfile.TemporaryDirectory(prefix='sqlite-identity-') as tmp:
        work = pathlib.Path(tmp)
        obj, exe = work / 'sqlite.o', work / 'normal'
        subprocess.run(['gcc', '-std=gnu89', '-O0', '-include',
                        str(ROOT / 'lib/sqlite3/os32_sqlite_config.h'), '-c',
                        str(ROOT / 'lib/sqlite3/sqlite3.c'), '-o', str(obj)], check=True)
        compile_host(ROOT, obj, exe)
        for case in CASES:
            subprocess.run([str(exe), case], check=True)
        print(f'GREEN {len(CASES)}/{len(CASES)}', flush=True)
        if os.environ.get('MUTATE', '0') != '1' and '--mutate' not in sys.argv:
            return
        for name, case, changes in MUTANTS:
            tree = work / name
            for file in FILES:
                dest = tree / file
                dest.parent.mkdir(parents=True, exist_ok=True)
                shutil.copyfile(ROOT / file, dest)
            for file, old, new in changes:
                path = tree / file
                source = path.read_text()
                assert source.count(old) == 1, (name, source.count(old))
                path.write_text(source.replace(old, new))
            mutant = tree / 'run'
            compile_host(tree, obj, mutant)  # Compilation failure is never RED.
            result = subprocess.run([str(mutant), case], capture_output=True, text=True)
            assert result.returncode == 1 and 'FAIL ' in result.stderr, (name, result)
            print(f'RED runtime {name}: rc={result.returncode} {result.stderr.strip()}', flush=True)


if __name__ == '__main__':
    main()


def run(ctx) -> int:
    import os
    import shlex
    import subprocess
    base_env = dict(os.environ, HOST32_RUNNERS=' '.join(ctx.runners))
    env = dict(base_env)
    argv = ['python3', 'tools/tests/test_sqlite_vfs_identity.py']
    rc = subprocess.call(argv, cwd=ctx.root, env=env, close_fds=False)
    if rc:
        return rc
    return 0
