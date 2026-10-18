"""T4b-4/4c: real SQLite callbacks/FD table; runtime mutation failures only."""
import os
import pathlib
import shutil
import subprocess
import tempfile

ROOT = pathlib.Path(__file__).resolve().parents[2]
CASES = ['absolute', 'relative', 'alias', 'parent_child', 'same_owner', 'parked', 'quarantine',
         'close_gap', 'separate', 'errors', 'readonly', 'unsupported', 'fullpath']
FILES = ['fs/vfs.h', 'fs/vfs_fd.c', 'lib/sqlite3/os32_sqlite_vfs.c',
         'lib/sqlite3/os32_sqlite_vfs.h', 'tools/tests/sqlite_vfs_identity_host.c',
         'tools/tests/sqlite_groups_host.c', 'tools/tests/sqlite_groups_backend.h',
         'tools/tests/vfs_fd_sqlite_host.c', 'tools/tests/sqlite_cd_host.c',
         'tools/tests/cd_read_host.c', 'kernel/cd_body.c', 'kernel/cd_module.h', 'fs/vfs.c', 'fs/iso9660.c',
         'drivers/atapi.c', 'tools/tests/atapi_hostshim/io.h']
INC = ['-I' + str(ROOT / p) for p in
       ('include', 'fs', 'drivers', 'sdk/include/os32', 'lib', 'lib/sqlite3')]
VFS = 'lib/sqlite3/os32_sqlite_vfs.c'
FD = 'fs/vfs_fd.c'
CD_CASES = ['normal', 'unpermitted', 'journal', 'wal', 'generation', 'remount']
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
        'if (p->readonly) return SQLITE_READONLY;', '(void)p->readonly;')]),
    ('ro_truncate', 'readonly', [(VFS,
        'if (p->readonly) return SQLITE_READONLY;\n    if (size < 0',
        'if (size < 0')]),
    ('backend_policy', 'unsupported', [(VFS,
        'if (backend != VFS_SQLITE_EXT2 && backend != VFS_SQLITE_IMMUTABLE_CD)',
        'if (0 && backend != VFS_SQLITE_EXT2 && backend != VFS_SQLITE_IMMUTABLE_CD)'),
        (VFS, 'if (flags & SQLITE_OPEN_READWRITE) {\n            rc = vfs_sqlite_writable(zName);',
         'if (0 && (flags & SQLITE_OPEN_READWRITE)) {\n            rc = vfs_sqlite_writable(zName);')]),
    ('relative_fullpath', 'fullpath', [(VFS,
        'return vfs_resolve_path(zName, zOut, nOut) == VFS_OK\n        ? SQLITE_OK : SQLITE_CANTOPEN;',
        'kstrncpy(zOut, zName, (u32)nOut); return SQLITE_OK;')]),
    ('ro_is_immutable', 'cd:unpermitted', [('fs/vfs.c',
        'if (!m->sqlite_immutable || !m->ops->sqlite ||',
        'if (!m->ops->sqlite ||')]),
    ('ignore_journal', 'cd:journal', [(VFS,
        'if (rc == VFS_OK) return SQLITE_CANTOPEN;',
        'if (rc == VFS_OK) continue;')]),
    ('ignore_wal', 'cd:wal', [(VFS,
        'if (rc == VFS_OK) return SQLITE_CANTOPEN;',
        'if (rc == VFS_OK) continue;')]),
    ('cd_lock_promotion', 'cd:normal', [(VFS,
        'if (((Os32File *)f)->immutable && l > SQLITE_LOCK_SHARED)',
        'if (0 && ((Os32File *)f)->immutable && l > SQLITE_LOCK_SHARED)')]),
    ('cd_generation', 'cd:generation', [(VFS,
        'if (vfs_sqlite_fd_identity(p->fd, &id) != VFS_OK) return 0;',
        '(void)vfs_sqlite_fd_identity(p->fd, &id);')]),
]


def compile_host(root, obj, exe, cd=False):
    subprocess.run(['gcc', '-std=gnu11', '-Wall', '-Wextra', '-Werror',
                    '-Wno-unused-parameter', '-Wno-sign-compare',
                    '-Wno-pointer-to-int-cast', '-Wno-missing-field-initializers',
                    '-D__cdecl=', '-Wno-unused-function',
                    '-I' + str(root / 'tools/tests/atapi_hostshim'),
                    '-I' + str(root), '-I' + str(ROOT), '-I' + str(ROOT / 'kernel'), *INC,
                    str(root / ('tools/tests/sqlite_cd_host.c' if cd else
                                'tools/tests/sqlite_vfs_identity_host.c')), str(obj),
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
        cd_exe = work / 'cd'
        compile_host(ROOT, obj, cd_exe, cd=True)
        for case in CD_CASES:
            subprocess.run([str(cd_exe), case], check=True)
        print(f'GREEN {len(CASES) + len(CD_CASES)}/{len(CASES) + len(CD_CASES)}', flush=True)
        if os.environ.get('MUTATE', '0') != '1':
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
                assert old in source, (name, old)
                path.write_text(source.replace(old, new))
            mutant = tree / 'run'
            is_cd = case.startswith('cd:')
            compile_host(tree, obj, mutant, cd=is_cd)  # Compilation failure is never RED.
            result = subprocess.run([str(mutant), case.removeprefix('cd:')], capture_output=True, text=True)
            assert result.returncode == 1 and 'FAIL ' in result.stderr, (name, result)
            print(f'RED runtime {name}: rc={result.returncode} {result.stderr.strip()}', flush=True)


if __name__ == '__main__':
    main()
