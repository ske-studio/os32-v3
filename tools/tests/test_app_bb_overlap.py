"""T2c high private image/heap/variable stack and supervisor BB aliases; actual paging,
ledger and fixed KHEAP. Mutants must compile and fail during execution.
"""
import host32
import pathlib
import subprocess
import sys
import tempfile

ROOT = pathlib.Path(__file__).resolve().parents[2]
FLAGS = ['-m32', '-march=i386', '-std=gnu11', '-ffreestanding', '-fno-pie',
         '-fno-stack-protector', '-Wall', '-Wextra', '-Werror']
SRC = ROOT / 'tools/tests/app_bb_overlap_host.c'

# 1 行の #define (順に並べる。RING3_USTACK_TOP が g_ring3_band_top を指す)
DEFINES = ('#define RING3_USTACK_TOP ', '#define RING3_STACK_BOTTOM ', '#define RING3_HEAP_TOP ')
WANTED = ('int ring3_ptr_ok(', 'static const char *exec_image_reject_reason(', 'static int exec_stack_bytes(', 'static u8 launch_read_byte(', 'static int app_store(', 'static int app_map_region(', 'u32 exec_as_leftover_pages;',
          'static void exec_teardown_app(')
MUTATIONS = [
 ('peak-common-update', '        h->used += blk->size + BLK_HDR_SIZE;',
  '        h->used += blk->size + BLK_HDR_SIZE;\n        if (h->used > kmalloc_peak_bytes) kmalloc_peak_bytes = h->used;'),
 ('unicode-user-restored', '        /* --- K3:', '        paging_addrspace_map_user_range(ctx->as, MEM_UNICODE_TABLE_BASE, MEM_UNICODE_TABLE_BASE + MEM_UNICODE_TABLE_SIZE, PAGE_RW | PTE_USER);\n        /* --- K3:'),
 ('bb-user-restored', '        /* --- K3:', '        paging_addrspace_map_user_range(ctx->as, MEM_GFX_BB_BASE, MEM_GFX_BB_BASE + MEM_GFX_BB_SIZE, PAGE_RW | PTE_USER);\n        /* --- K3:'),
 ('guard-fixed-stack', '#define RING3_HEAP_TOP (RING3_STACK_BOTTOM - PAGE_SIZE)', '#define RING3_HEAP_TOP (MEM_APP_STACK_TOP - MEM_EXEC_STACK_SIZE - PAGE_SIZE)'),
 ('shell-shlib-accepted', 'else if (is_shell && hdr->shlib_protocol)', 'else if (((void)is_shell, 0) && hdr->shlib_protocol)'),
 ('argv-RO-rejected', 'as_va_to_pa_read(pd, (u32)p, &pa)', 'as_va_to_pa(pd, (u32)p, &pa)'),
 ('argv-failure-hidden', '        *failed = 1;', '        (void)failed;'),
 ('stack-default-wrong', '    u32 size = MEM_EXEC_STACK_SIZE;', '    u32 size = MEM_APP_STACK_MIN;'),
 ('stack-sign-unchecked', 'requested > 0x7fffffffUL - (PAGE_SIZE - 1)', '0'),
 ('stack-min-unchecked', '        if (size < MEM_APP_STACK_MIN) size = MEM_APP_STACK_MIN;', '        (void)size;'),
 ('image-not-returned', '                                         a->sbrk_heap_limit);', '                                         a->load_addr);'),
 ('argv-to-wrong-AS', 'as_va_to_pa(a->as->pd_phys, va, &pa)', 'as_va_to_pa(paging_kernel_pd_phys(), va, &pa)'),
 ('stack-not-returned', 'paging_addrspace_free_user_range(a->as, a->stack_base, a->stack_top);',
  'paging_addrspace_free_user_range(a->as, a->stack_top, a->stack_top);'),
 ('as-kheap-leak', '    kfree(a->as);', '    (void)a->as;'),
 ('backing-not-zeroed', '        kmemset(P2V(phys), 0, pages * PAGE_SIZE);',
  '        (void)phys;'),
]

def slice_out(source, signature):
    """exec.c から 1 定義をテキストのまま取り出す (関数は最初の行頭 '}' まで)。"""
    if source.count(signature) != 1:
        raise SystemExit('exec/exec.c: %r が 1 個ではない (実装が動いた?)' % signature)
    body = source.split(signature, 1)[1]
    if signature.endswith('('):
        if '\n}' not in body:
            raise SystemExit('exec/exec.c: %r の終端が見つからない' % signature)
        return signature + body.split('\n}', 1)[0] + '\n}\n'
    return signature + body.split('\n', 1)[0] + '\n'


def define_line(source, prefix):
    line = next((l for l in source.splitlines() if l.startswith(prefix)), None)
    if line is None:
        raise SystemExit('exec/exec.c: %r が見つからない' % prefix)
    return line


def extract(source):
    parts = ['/* 生成物。exec/exec.c から test_app_bb_overlap.py が切り出した。 */',
             'u32 sys_usable_mem_end(void);',
             'void gfx_bb_phys_range(u32 *base, u32 *size);']
    parts += [define_line(source, d) for d in DEFINES]
    for sig in WANTED:
        if sig == 'static int app_map_region(':
            parts += ['#define pgalloc_alloc_phys app_fail_alloc', slice_out(source, sig), '#undef pgalloc_alloc_phys']
        else:
            parts.append(slice_out(source, sig))
    launch = source[source.index('\n        }', source.index('        /* 3 領域を per-app 物理で張る。')) + len('\n        }'):source.index('        /* --- K3:')]
    parts += ['#define paging_addrspace_map_user_range launch_map_attempt', 'static void launch_shared_maps(AppSlot *ctx) {', '(void)ctx;', launch, '}', '#undef paging_addrspace_map_user_range']
    return '\n'.join(parts) + '\n'



def run(mutation=None):
    source = (ROOT / 'exec/exec.c').read_text()
    if mutation and mutation[0] != "peak-common-update":
        _, old, new = mutation
        if old not in source:
            raise SystemExit('変異 %s の当て先が見つからない' % mutation[0])
        source = source.replace(old, new, 1)
    with tempfile.TemporaryDirectory(prefix='os32-app-bb-') as tmp:
        tmp = pathlib.Path(tmp)
        (tmp / 'exec_bb_overlap.inc').write_text(extract(source))
        (tmp / 'paging_host_source.c').write_text((ROOT / 'kernel/paging.c').read_text())
        allocator = (ROOT / 'kernel/pgalloc.c').read_text()
        allocator = allocator.replace('irq_save()', '0').replace('irq_restore(flags)', '(void)flags')
        (tmp / 'pgalloc_host_source.c').write_text(allocator)
        heap_source = (ROOT / 'kernel/kmalloc.c').read_text()
        if mutation and mutation[0] == 'peak-common-update':
            _, old, new = mutation
            assert heap_source.count(old) == 1
            heap_source = heap_source.replace(old, new)
        (tmp / 'kmalloc_host_source.c').write_text(heap_source)
        includes = ['-I'  + str(ROOT / p)
                    for p in ('include', 'arch/x86', 'platform/pc98', 'kernel',
                              'lib', 'exec', 'sdk/include/os32')] + ['-I' + str(tmp)]
        host_includes = ['-I' + str(ROOT / 'tools/tests/host_arch')] + includes
        exe = tmp / 'app_bb_overlap'
        cmd = ['gcc', *FLAGS, '-DPHYSMEM_HOST_TEST=1', '-nostdlib', '-static', '-no-pie',
               *host_includes, str(SRC), str(ROOT / 'kernel/physmem.c'), str(tmp / 'kmalloc_host_source.c'), '-o', str(exe)]
        build = subprocess.run(cmd, capture_output=bool(mutation))
        if build.returncode != 0:
            if mutation:
                return 'compile'
            raise subprocess.CalledProcessError(build.returncode, cmd)
        out = host32.run([str(exe)], timeout=60, capture_output=bool(mutation))
        if mutation and mutation[0] == 'peak-common-update':
            assert out.returncode == 1 and b'FAIL: p && kmalloc_peak_bytes == peak' in out.stdout, out
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
    run()
    print('HOST ILP32 PASS high private bands, supervisor BB aliases, KHEAP and owner0')
    if '--mutate' in sys.argv:
        rc = mutate()
        if rc:
            return rc
    return 0


if __name__ == '__main__':
    sys.exit(main())
