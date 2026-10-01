"""T2c actual shlib and paging/ledger, fragmented supply and rollback (ILP32)."""
import pathlib
import subprocess
import sys
import tempfile
ROOT = pathlib.Path(__file__).resolve().parents[2]
FLAGS = ['-m32', '-march=i386', '-std=gnu11', '-ffreestanding', '-fno-pie', '-fno-stack-protector', '-Wall', '-Wextra', '-Werror']
SRC = ROOT / 'tools/tests/shlib_high_host.c'
MUTATIONS = [
 ('text-writable', 'g_pages[i], PAGE_RO | PTE_USER)', 'g_pages[i], PAGE_RW | PTE_USER)'),
 ('no-original-cleanup', 'if (g_pages[i]) pgalloc_free_n_owner', 'if (0) pgalloc_free_n_owner'),
 ('no-attach-rollback', '    shlib_addrspace_detach(as);', '    (void)as;'),
 ('no-private-copy', '        kmemcpy(P2V(phys), P2V(g_pages[g_text_pages + i]), PAGE_SIZE);', '        kmemset(P2V(phys), 0, PAGE_SIZE);'),
]
def run(mutation=None):
    source = (ROOT / 'kernel/shlib.c').read_text()
    if mutation:
        _, old, new = mutation
        if old not in source:
            raise SystemExit('変異 %s の当て先が見つからない' % mutation[0])
        source = source.replace(old, new, 1)
    with tempfile.TemporaryDirectory(prefix='os32-app-bb-') as tmp:
        tmp = pathlib.Path(tmp)
        (tmp / 'shlib_host_source.c').write_text(source)
        (tmp / 'paging_host_source.c').write_text((ROOT / 'kernel/paging.c').read_text())
        allocator = (ROOT / 'kernel/pgalloc.c').read_text()
        allocator = allocator.replace('irq_save()', '0').replace('irq_restore(flags)', '(void)flags')
        (tmp / 'pgalloc_host_source.c').write_text(allocator)
        includes = ['-I' + str(ROOT / p)
                    for p in ('include', 'arch/x86', 'platform/pc98', 'kernel',
                              'lib', 'exec', 'fs', 'sdk/include/os32')] + ['-I' + str(tmp)]
        host_includes = ['-I' + str(ROOT / 'tools/tests/host_arch')] + includes
        exe = tmp / 'app_bb_overlap'
        cmd = ['gcc', *FLAGS, '-DPHYSMEM_HOST_TEST=1', '-nostdlib', '-static', '-no-pie',
               *host_includes, str(SRC), str(ROOT / 'kernel/physmem.c'), str(ROOT / 'exec/os32x_hdr.c'), '-o', str(exe)]
        build = subprocess.run(cmd, capture_output=bool(mutation))
        if build.returncode != 0:
            if mutation:
                return 'compile'
            raise subprocess.CalledProcessError(build.returncode, cmd)
        out = subprocess.run([str(exe)], timeout=60, capture_output=bool(mutation))
        if mutation:
            return 'ok' if out.returncode == 0 else 'fail'
        if out.returncode != 0:
            raise subprocess.CalledProcessError(out.returncode, str(exe))
        return 'ok'


def mutate():
    red = 0
    for m in MUTATIONS:
        result = run(m)
        ok = result == 'fail'
        print('  変異 %-18s %s' % (m[0], 'RED (%s)' % result if ok
                                   else '**%s — 試験が穴を見逃した**' % result))
        red += ok
    print('%d/%d の変異が RED' % (red, len(MUTATIONS)))
    return 0 if red == len(MUTATIONS) else 1


def main():
    if '--mutate' in sys.argv:
        rc = mutate()
        if rc:
            return rc
    run()
    print('HOST ILP32 PASS shlib fragmented backing and complete rollback')
    return 0


if __name__ == '__main__':
    sys.exit(main())
