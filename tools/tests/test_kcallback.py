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
    # Both guards reject bad output. The mutant must kill in the collector,
    # then fail the assertion that the wrap rejected before collector entry.
    ('WM origin guard', 'ls',
     '!caller_access_get(&caller) || caller.origin != CALLER_USER',
     '!caller_access_get_user(&caller)', 'FAIL:'),
    ('ls out guard', 'public_wrapper', 'KAPI_OUT_LEN(out, sizeof(OS32_LsPacket))', 'KAPI_OUT_LEN(out, 0)', 'FAIL:'),
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
        'public_wrapper': function((ROOT / 'kapi/kapi_generated.c').read_text(),
                            'int __cdecl wrap_sys_ls_window(const char *path, u32 skip, OS32_LsPacket *out)'),
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
        (tmp / 'wm.inc').write_text('\n'.join(function(source, 'void ' + n + '(void)') for n in ('ring3_wm_enter', 'ring3_wm_leave')))
        (tmp / 'wrapper.inc').write_text(sources['wrapper'] + '\n' + sources['public_wrapper'])
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


def run_guest_result(args, mutant=None):
    """Guest PASS=0, sys_ls error/empty/bad callback=1 (PRIO-2)."""
    with tempfile.TemporaryDirectory(prefix='kcallback-result-') as folder:
        tmp = pathlib.Path(folder)
        source = (ROOT / 'userland/tests/kcallback_test.c').read_text()
        if mutant is not None:
            old = 'return failed ? 1 : 0;'
            assert source.count(old) == 1
            source = source.replace(old, 'return %d;' % mutant)
        (tmp / 'guest.inc').write_text(source)
        (tmp / 'os32api.h').write_text(
            'typedef struct { int (*sys_ls)(const char *, void *, void *); '
            'void (*kprintf)(int, const char *, ...); } KernelAPI;\n')
        exe = tmp / 'test'
        subprocess.run(['gcc', '-m32', '-static', '-fno-pie', '-no-pie', '-nostdlib',
                        '-std=gnu11', '-Wall', '-Wextra', '-Werror',
                        '-fno-stack-protector', '-ffreestanding', '-I' + str(tmp),
                        str(ROOT / 'tools/tests/kcallback_result_host.c'),
                        '-o', str(exe)], check=True, capture_output=True)
        result = host32.run([str(exe)], runner=args.runner, timeout=30,
                            capture_output=True, text=True)
        assert result.returncode == (0 if mutant is None else 1), (result.returncode, result.stderr)
        if mutant is None:
            print('PASS: guest main returns 0/1 for PASS/error/empty/bad ctx/bad entry')
        else:
            print('RED (compiled, runtime rc=1): guest main always returns', mutant)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--mutate', action='store_true')
    parser.add_argument('--runner', choices=('native', 'qemu'), default='qemu')
    args = parser.parse_args()
    try:
        run(args)
        run_guest_result(args)
        if args.mutate:
            for rc in (0, 1):
                run_guest_result(args, rc)
            for mutant in MUTANTS:
                run(args, mutant)
    except subprocess.CalledProcessError as exc:
        print('COMPILE FAILURE (not RED):', exc.stderr.decode())
        return 1
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
