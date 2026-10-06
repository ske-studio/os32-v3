"""Execute the real wrapper, collector and relocated CPL3 shim (ILP32).

Only int80 is replaced by a host bridge; mutants must compile and fail at runtime.
"""
import argparse
import pathlib
import subprocess
import tempfile
import host32

ROOT = pathlib.Path(__file__).resolve().parents[2]
MUTANTS = (
    ('direct USER callback', 'wrapper',
     'if (ring3_call_from_user())', 'if (0)', 'FAIL: !user_call'),
    ('drop one entry', 'shim', 'call *12(%ebp)',
     'cmpl $1, %ebx\n    je .Lomit\n    call *12(%ebp)\n.Lomit:', 'FAIL:'),
    ('ignore skip', 'window', 'if (w->skip) { w->skip--; return; }',
     'if (0) { w->skip--; return; }', 'FAIL:'),
    ('ignore done', 'shim', 'je .Lbatch', 'nop', 'FAIL:'),
    ('replace ctx', 'shim', 'pushl 16(%ebp)', 'pushl $0', 'FAIL: ctx == expected_ctx'),
)


def function(source, signature):
    start = source.index(signature + '\n{')
    return source[start:source.index('\n}', start) + 2]


def run(args, mutant=None):
    source = (ROOT / 'exec/exec.c').read_text()
    sources = {
        'wrapper': function((ROOT / 'kapi/kapi_generated.c').read_text(),
                            'int __cdecl wrap_sys_ls(const char *path, void *cb, void *ctx)'),
        'window': (ROOT / 'fs/vfs.c').read_text().split('struct vfs_ls_window_ctx {', 1)[1].split('int vfs_ls(const char *path', 1)[0],
        'ext2_cb': function((ROOT / 'fs/ext2_vfs.c').read_text(),
                            'static void ext2_to_vfs_cb(const Ext2DirEntry *e, void *ctx)'),
        'ls': (ROOT / 'exec/ring3_ls.c').read_text(),
        'shim': (ROOT / 'exec/ring3_ls.S').read_text(),
    }
    if mutant:
        name, key, old, new, expected = mutant
        assert sources[key].count(old) == 1, (name, old)
        sources[key] = sources[key].replace(old, new)
    # The bridge is an absolute call so copying the shim tests relocation too.
    sources['shim'] = sources['shim'].replace('int $0x80', 'movl $host_gate, %eax\n    call *%eax')
    with tempfile.TemporaryDirectory(prefix='kcallback-') as folder:
        tmp = pathlib.Path(folder)
        (tmp / 'wrapper.inc').write_text(sources['wrapper'])
        (tmp / 'window.inc').write_text('struct vfs_ls_window_ctx {' + sources['window'])
        (tmp / 'ext2_cb.inc').write_text(sources['ext2_cb'])
        (tmp / 'ls_source.inc').write_text(sources['ls'])
        (tmp / 'shim.S').write_text(sources['shim'])
        offset = next(line for line in source.splitlines() if line.startswith('#define RING3_LS_SHIM_OFF'))
        (tmp / 'trampoline.inc').write_text(offset + '\n' + function(source, 'static void ring3_trampoline_init(void)'))
        exe = tmp / 'test'
        subprocess.run(['gcc', '-m32', '-static', '-fno-pie', '-no-pie', '-nostdlib',
                        '-std=gnu11', '-Wall', '-Wextra', '-Werror', '-fno-stack-protector', '-ffreestanding',
                        *['-I' + str(ROOT / p) for p in
                          ('include', 'fs', 'exec', 'kernel', 'lib', 'sdk/include/os32')],
                        '-I' + str(tmp), str(ROOT / 'tools/tests/kcallback_host.c'),
                        str(tmp / 'shim.S'), '-o', str(exe)], check=True, capture_output=True)
        result = host32.run([str(exe)], runner=args.runner, timeout=30, capture_output=True, text=True)
        if mutant:
            assert result.returncode == 1 and expected in result.stdout, (name, result)
            print('RED (compiled, runtime rc=1):', name)
        else:
            print(result.stdout, end='')
            assert result.returncode == 0, (result.returncode, result.stderr)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--mutate', action='store_true')
    parser.add_argument('--runner', choices=('native', 'qemu'), default='qemu')
    args = parser.parse_args()
    try:
        run(args)
        if args.mutate:
            for mutant in MUTANTS:
                run(args, mutant)
    except subprocess.CalledProcessError as exc:
        print('COMPILE FAILURE (not RED):', exc.stderr.decode())
        return 1
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
