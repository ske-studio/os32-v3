"""T2h 8MB shapes: actual guest main, two pipe stops, real pre-entry budget gate.
Compile/link failures and signals are errors, never mutation RED.
"""
import argparse
import os
import pathlib
import re
import select
import struct
import subprocess
import sys
import tempfile

import host32

ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'sdk'))
import os32x_hdr as H

TARGET_SRCS = ['userland/tests/h5_shape_i.c', 'userland/tests/h5_shape_ii.c',
               'userland/tests/h5_shape_iii.c', 'userland/tests/h5_shape.inc',
               'tools/tests/h5_shapes_host.c', 'tools/tests/test_h5_shapes.py',
               'exec/exec.c', 'exec/appslot.c']
MUTANTS = [
    ('PT omitted', ' - p1;', ';'),
    ('PT twice', ' - p1;', ' - p1 - p1;'),
    ('PT in add', 'H5_TARGET_PAGES - launch_private;', 'H5_TARGET_PAGES - launch_private - p1;'),
    ('wrong target', '#define H5_TARGET_PAGES 512U', '#define H5_TARGET_PAGES 511U'),
    ('argc', 'argc != 2', 'argc != 3'),
    ('first wait', '!h5_line(api, &p1)', '((p1 = 4), 0)'),
    ('second wait', 'continued = h5_line(api, 0);', 'continued = 1;'),
    ('release', 'if (allocation) api->mem_free(allocation);', '(void)allocation;'),
    ('short exit', '!continued || shortage', '!continued'),
    ('numeric input', "c < '0' || c > '9'", '0'),
    ('allocation count', 'allocated = (u32)add;', 'allocated = (u32)add + 1;'),
    ('touch pages', ' = H5_PATTERN;', ' = 0;'),
    ('large accounting', 'before.extents[H5_LARGE_INDEX] + 1', 'before.extents[H5_LARGE_INDEX]'),
]


def require(condition, message):
    if not condition:
        raise AssertionError(message)


def compile_guest(tmp, variant, body):
    (tmp / 'h5_fixture.c').write_bytes((ROOT / f'userland/tests/h5_shape_{variant}.c').read_bytes())
    (tmp / 'h5_shape.inc').write_text(body)
    exe = tmp / f'host-{variant}'
    host32.build(['gcc', '-m32', '-std=gnu11', '-O2', '-Wall', '-Wextra', '-Werror',
                  '-Werror=vla', '-fno-pie', '-no-pie', '-static', '-nostdlib',
                  '-ffreestanding', '-fno-builtin', '-fno-stack-protector',
                  '-I'+str(tmp), '-I'+str(ROOT / 'include'),
                  '-I'+str(ROOT / 'sdk/include/os32'),
                  str(ROOT / 'tools/tests/h5_shapes_host.c'), '-o', str(exe)],
                 capture_output=True, text=True, check=True, timeout=30)
    return exe


def env(free1=600, fail=False, bad=False):
    e = dict(os.environ, H5_FREE1=str(free1))
    e.pop('H5_ALLOC_FAIL', None)
    e.pop('H5_BAD_SNAPSHOT', None)
    if fail:
        e['H5_ALLOC_FAIL'] = '1'
    if bad:
        e['H5_BAD_SNAPSHOT'] = '1'
    return e


def line(process):
    result = b''
    while not result.endswith(b'\n'):
        if not select.select([process.stdout], [], [], 5)[0]:
            raise TimeoutError('timeout waiting for line')
        c = os.read(process.stdout.fileno(), 1)
        require(c, 'EOF waiting for line: '+repr(result))
        result += c
    return result.decode()


def interactive(exe, runner, launch):
    p1, free1 = 4, 600
    free0 = free1 + launch + p1
    p = subprocess.Popen(host32.command([str(exe), str(free0)], runner=runner),
                         stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                         bufsize=0, env=env(free1))
    try:
        require(line(p) == f'PREP free1={free1}\n', 'first stop')
        require(not select.select([p.stdout], [], [], 0.1)[0] and p.poll() is None,
                'PREP must wait without allocating')
        p.stdin.write(f'{p1}\n'.encode())
        add = 512 - launch
        require(line(p) == f'ALLOC launch_private={launch} add={add} allocated={add} short=0\n',
                'PT counted once; private target exactly 512')
        require(not select.select([p.stdout], [], [], 0.1)[0] and p.poll() is None,
                'ALLOC must hold allocation until continuation')
        p.stdin.write(b'continue\n')
        require(line(p) == 'PASS\n', 'PASS line')
        p.stdin.close()
        p.wait(timeout=5)
        host32.report_signal(p.returncode, runner)
        if p.returncode < 0:
            raise RuntimeError('interactive signal '+str(-p.returncode))
        require(p.returncode == 0 and not p.stderr.read(), 'interactive exit/host invariants')
    finally:
        if p.poll() is None:
            p.kill()
        p.wait()
        p.stdout.close()
        p.stderr.close()
        if not p.stdin.closed:
            p.stdin.close()


def cases(exe, runner):
    samples = [
        (['1000'], '4\nanything\n', env(), 0, 'allocated=116 short=0'),
        (['1000'], '4\n\n', env(fail=True), 1, 'allocated=0 short=116'),
        (['1000'], '4\n\n', env(bad=True), 1, 'allocated=0 short=116'),
        (['1200'], '4\n\n', env(), 1, 'add=-84 allocated=0 short=84'),
        (['1116'], '4\n\n', env(), 0, 'add=0 allocated=0 short=0'),
        (['1108'], '4\n\n', env(), 1, 'add=8 allocated=0 short=8'),
        (['1000'], '4\n', env(), 1, 'FAIL\n'),
        (['1000'], '', env(), 1, 'PREP FAIL'),
        (['599'], '4\n', env(), 1, 'PREP FAIL'),
        (['603'], '4\n', env(), 1, 'PREP FAIL'),
    ]
    for text in ('no', '', '-1', '+4', '4x', ' 4', '4294967296', '4294967295'):
        samples.append((['1000'], text+'\n', env(), 1, 'PREP FAIL'))
    for args in ([], ['1000', '4'], ['no'], [''], ['-1'], ['4294967296']):
        samples.append((args, '', env(), 1, 'PREP FAIL'))
    for args, input_text, environment, expected, marker in samples:
        r = host32.run([str(exe), *args], runner=runner, input=input_text,
                       capture_output=True, text=True, env=environment, timeout=5)
        if r.returncode < 0:
            raise RuntimeError('case signal '+str(-r.returncode))
        require(r.returncode == expected and marker in r.stdout and not r.stderr,
                f'case {args}/{input_text!r}: rc={r.returncode} {r.stdout} {r.stderr}')


def extract(source, signature):
    require(source.count(signature) == 1, 'unique extraction '+signature)
    body = source[source.index(signature):]
    return body[:body.index('\n}')+3]+'\n'


def preentry(tmp, headers, runner):
    # Use the real exec physical-budget rejection block and real admission
    # predicate. Entry/file I/O are host boundaries; no guest fault simulated.
    source = (ROOT / 'exec/exec.c').read_text()
    gate = source[source.index('    need_pages = 0;\n    if (want_ring3) {'):]
    gate = gate[:gate.index('    /* ======== 起動元のヒープ')]
    funcs = ''.join(extract(source, s) for s in
                    ('static u32 exec_ring3_extra_pages(', 'static u32 exec_ring3_pages('))
    funcs += extract((ROOT / 'exec/appslot.c').read_text(), 'int appslot_start_admit(')
    shapes = ','.join('{%d,%d,%d}' % (h['text_size'], h['bss_size'], h['stack_size'])
                      for h in headers)
    code = '''#include "os32api.h"
#include "memmap.h"
#define PAGE_SIZE MEM_PAGE_SIZE
#define PAGE_ALIGN_UP(x) (((x) + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1))
#define APP_ID_SHELL 1
static int g_cur=APP_ID_SHELL, entered, errors, restored;
static u32 available;
static int appslot_alloc_id(void) { return 2; }
static u32 shlib_data_pages(void) { return 10; }
static u32 pgalloc_free_pages(void) { return available; }
static void shell_print(const char *s, u8 attr) { (void)s; (void)attr; errors++; }
static void shell_print_dec(u32 n, u8 attr) { (void)n; (void)attr; }
static void exec_restore_band(int id) { (void)id; restored++; }
'''+funcs+'''
static int launch(u32 image, u32 stack) {
    u32 load_base=MEM_EXEC_LOAD_ADDR, sbrk_end=PAGE_ALIGN_UP(load_base+image)+PAGE_SIZE;
    u32 exec_heap_size=MEM_EXEC_HEAP_MIN, stack_size=stack ? stack : MEM_EXEC_STACK_SIZE;
    u32 need_pages;
    int want_ring3=1, gui=0, launcher_id=1;
'''+gate+'''
    entered++; /* host entry boundary, reached only after admission */
    return 0;
}
static const u32 shapes[][3]={'''+shapes+'''};
void _start(void) {
    int rc=0;
    for(u32 i=0;i<3;i++) {
        u32 image=shapes[i][0]+shapes[i][1];
        u32 stack=shapes[i][2] ? shapes[i][2] : MEM_EXEC_STACK_SIZE;
        u32 need=exec_ring3_pages(MEM_EXEC_LOAD_ADDR,
            PAGE_ALIGN_UP(MEM_EXEC_LOAD_ADDR+image)+PAGE_SIZE,MEM_EXEC_HEAP_MIN,stack);
        entered=errors=restored=0; available=need-1;
        if(launch(image,stack)!=EXEC_ERR_NOMEM || entered || !errors || restored!=1) rc=1;
        entered=errors=restored=0; available=need;
        if(launch(image,stack) || entered!=1 || errors || restored) rc=1;
    }
    __asm__ volatile("int $0x80" : : "a"(1), "b"(rc) : "memory");
    __builtin_unreachable();
}
'''
    src, exe = tmp / 'preentry.c', tmp / 'preentry'
    src.write_text(code)
    host32.build(['gcc', '-m32', '-std=gnu11', '-O2', '-Wall', '-Wextra', '-Werror',
                  '-ffreestanding', '-fno-pie', '-no-pie', '-fno-stack-protector',
                  '-nostdlib', '-static', '-Iinclude', '-Isdk/include/os32', src, '-o', exe],
                 cwd=ROOT, capture_output=True, text=True, check=True, timeout=30)
    r = host32.run([str(exe)], runner=runner, capture_output=True, text=True)
    require(r.returncode == 0, 'real pre-entry budget gate '+str(r.returncode))
    print('PASS real pre-entry budget: 3 shapes, need-1 rejects / need admits; entry boundary 0/1')


def built():
    headers = []
    deploy = (ROOT / 'userland/deploy.yaml').read_text()
    for variant, text in [('i', 1500*1024), ('ii', 64*1024), ('iii', 200*1024)]:
        base = f'userland/tests/h5_shape_{variant}'
        h = H.parse_header((ROOT / (base+'.bin')).read_bytes())
        headers.append(h)
        require(h['flags'] & H.OS32X_FLAG_CUI_ONLY, base+' CUI header')
        require(h['stack_size'] == (524288 if variant == 'ii' else 0), base+' stack header')
        require(h['version'] == H.OS32X_HDR_VERSION and h['load_addr'] == H.OS32X_APP_LOAD_ADDR,
                base+' current high image')
        require(f'    - host: {base}.bin\n      guest: /usr/bin/\n      tags: [test]' in deploy,
                base+' deploy [test]')
        # Inspect ELF section flags and size: rodata/BSS padding is insufficient.
        elf = (ROOT / (base+'.elf')).read_bytes()
        shoff = struct.unpack_from('<I', elf, 32)[0]
        shentsize, shnum, shstr = struct.unpack_from('<HHH', elf, 46)
        sections = [struct.unpack_from('<IIIIIIIIII', elf, shoff+i*shentsize) for i in range(shnum)]
        names = elf[sections[shstr][4]:sections[shstr][4]+sections[shstr][5]]
        section = next(s for s in sections if names[s[0]:].split(b'\0')[0] == b'.text')
        require(section[2] & 4 and section[5] >= text, base+' executable .text size')
    print('PASS built .text 1500/64/200KiB, stack512, CUI headers and deploy [test]')
    return headers


@host32.control_session
def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--runner', choices=['native', 'qemu'], default='qemu')
    ap.add_argument('--mutate', action='store_true')
    args = ap.parse_args()
    host32.begin_control(args.mutate, args.runner, ROOT)
    headers = built()
    body = (ROOT / 'userland/tests/h5_shape.inc').read_text()
    with tempfile.TemporaryDirectory(prefix='os32-h5-') as directory:
        tmp = pathlib.Path(directory)
        with host32.control(args.mutate, args.runner, ROOT) as normal:
            if normal:
                for variant, launch in [('i', 460), ('ii', 166), ('iii', 140)]:
                    exe = compile_guest(tmp, variant, body)
                    interactive(exe, args.runner, launch)
                    cases(exe, args.runner)
                preentry(tmp, headers, args.runner)
                print(f'PASS two pipe stops, physical accounting, LARGE, cleanup and negative inputs; runner={args.runner}')
        if args.mutate:
            for name, old, new in MUTANTS:
                require(body.count(old) == 1, 'unique mutant '+name)
                exe = compile_guest(tmp, 'iii', body.replace(old, new))
                try:
                    interactive(exe, args.runner, 140)
                    cases(exe, args.runner)
                except AssertionError as error:
                    print(f'RED runtime: {name}: {error}')
                else:
                    raise AssertionError('survived mutant '+name)
    return 0


if __name__ == '__main__':
    try:
        sys.exit(main())
    except subprocess.CalledProcessError as error:
        print('COMPILE/LINK ERROR (not RED):\n'+(error.stderr or ''), file=sys.stderr)
        raise
