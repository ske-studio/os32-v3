"""共有 BB と CPL=3 アプリの私有領域が重ならないこと (2026-09-30 の後退).

  python3 -B tools/tests/test_app_bb_overlap.py            # 肯定側
  python3 -B tools/tests/test_app_bb_overlap.py --mutate   # 否定側 + 肯定側

8MB + PEGC で CPL=3 アプリを起動・終了するたびに used_pages が 74 ずつ増えた
(私有領域を帯の上端 0x800000 まで張ってから BB [0x7B5000, 0x800000) を恒等で
重ねて写し、スタック / exec_heap の私有 PTE を上書きしていた)。修正は
exec/exec.c の ring3_band_set — 私有領域の上端を帯の上端と sys_usable_mem_end()
の低い方にする。BB は恒等のまま丸ごと写す (番地は変えない)。

実物の kernel/paging.c / pgalloc.c / physmem.c と、exec/exec.c から**テキストの
まま切り出した** ring3_band_set / app_map_region / exec_bb_overlaps_user /
exec_map_shared_bb / exec_teardown_app で起動と終了を 10 回まわし、used_pages が
毎回戻ること・私有 PTE が BB を指さないこと・BB の仮想番地が BB の物理を指す
こと・teardown が BB の物理を返そうとしないことを見る
(tools/tests/app_bb_overlap_host.c)。

exec_launch の呼び出し箇所は切り出せない (関数が大きく setjmp を含む) ので、
テキストで見る: gfx_bb_phys_range() を呼ぶのは exec_map_shared_bb だけ、
呼び出し箇所は `exec_map_shared_bb(&ctx->as, ctx->band_top)` の 1 か所、
CPL=3 のスタック上端は RING3_USTACK_TOP (旧実装の素の _keep 呼び出しに戻す
変異はここで RED)。

--mutate は否定側: 上端の切り詰めを外す / 重なりの検査を外す / teardown が
帯の上端を見る / BB を写さない / _keep でなく flags を上書き / 呼び出し箇所を
旧実装へ戻す、の各版で試験が RED になることを見る。変異は一時ディレクトリの
写しに当てる (コンパイルエラーは RED に数えない)。
"""
import pathlib
import subprocess
import sys
import tempfile

ROOT = pathlib.Path(__file__).resolve().parents[2]
FLAGS = ['-m32', '-march=i386', '-std=gnu11', '-ffreestanding', '-fno-pie',
         '-fno-stack-protector', '-Wall', '-Wextra', '-Werror']
SRC = ROOT / 'tools/tests/app_bb_overlap_host.c'

# 1 行の #define (順に並べる。RING3_USTACK_TOP が g_ring3_band_top を指す)
DEFINES = ('#define RING3_USTACK_TOP ',
           '#define RING3_USTACK_SIZE ',
           '#define RING3_GUARD_SIZE ',
           '#define RING3_STACK_BOTTOM ',
           '#define RING3_GUARD_BASE ',
           '#define RING3_HEAP_TOP ')
WANTED = ('static u32 g_ring3_band_top = ',
          'static u32 g_ring3_band_pdes = ',
          'static void ring3_band_set(',
          'static int app_map_region(',
          'static int exec_bb_overlaps_user(',
          'static int exec_map_shared_bb(',
          'static void exec_teardown_app(')

# exec_launch の呼び出し箇所 (切り出せないのでテキストで見る)
CALL_SITE = '        if (exec_map_shared_bb(&ctx->as, ctx->band_top) != 0) {\n'
STACK_TOP = '        stack_top = RING3_USTACK_TOP;\n'

OLD_CALL_SITE = ('        {\n'
                 '            u32 bb_base = 0, bb_size = 0;\n'
                 '            gfx_bb_phys_range(&bb_base, &bb_size);\n'
                 '            if (bb_size)\n'
                 '                paging_addrspace_map_user_keep(&ctx->as,\n'
                 '                    bb_base, bb_base + bb_size, PAGE_RW | PTE_USER);\n'
                 '        }\n'
                 '        if (0) {\n')

# (名前, 置き換え前, 置き換え後) — どれも exec/exec.c の写しに当てる
MUTATIONS = [
    # 旧式: 私有領域の上端を帯の上端のまま (BB の上に重ねる → 起動を断られる)
    ('cap-removed',
     '    if (cap < top) top = cap;\n',
     '    (void)cap;\n'),
    # 重なりの検査を外す (上端が下がっていない構成で BB が私有 PTE を潰す)
    ('overlap-unchecked',
     '    if (exec_bb_overlaps_user(bb_base, bb_size, user_top)) return -1;\n',
     '    (void)exec_bb_overlaps_user(bb_base, bb_size, user_top);\n'),
    # teardown が私有領域の上端でなく帯の上端からスタックを返す
    # (BB の PTE を辿って BB の物理を返そうとし、本当のスタックは漏れる)
    ('teardown-band-top',
     '                                         a->band_top - RING3_USTACK_SIZE,\n'
     '                                         a->band_top);\n',
     '                                         MEM_APP_BAND_TOP - RING3_USTACK_SIZE,\n'
     '                                         MEM_APP_BAND_TOP);\n'),
    # BB を写さない (アプリ・カーネルの BB ポインタが届かない)
    ('bb-unmapped',
     '    if (bb_size)\n'
     '        paging_addrspace_map_user_keep(as, bb_base, bb_base + bb_size,\n',
     '    if (0)\n'
     '        paging_addrspace_map_user_keep(as, bb_base, bb_base + bb_size,\n'),
    # _keep でなく flags を上書き (Cirrus のクライアント面の PCD が落ちる)
    ('bb-no-keep',
     '        paging_addrspace_map_user_keep(as, bb_base, bb_base + bb_size,\n',
     '        paging_addrspace_map_user_range(as, bb_base, bb_base + bb_size,\n'),
    # 呼び出し箇所を旧実装 (素の _keep) に戻す
    ('callsite-old', CALL_SITE, OLD_CALL_SITE),
    # 呼び出し箇所が私有領域の上端でなく帯の上端を渡す (重なりの検査が素通り)
    ('callsite-band-max',
     CALL_SITE,
     '        if (exec_map_shared_bb(&ctx->as, MEM_APP_BAND_TOP) != 0) {\n'),
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
    parts += [slice_out(source, sig) for sig in WANTED]
    return '\n'.join(parts) + '\n'


def call_site_problems(source):
    """exec_launch の呼び出し箇所の形 (切り出せない部分のテキスト検査)。"""
    problems = []
    if source.count('gfx_bb_phys_range(') != 1:
        problems.append('gfx_bb_phys_range() の呼び出しが exec_map_shared_bb の 1 個ではない')
    if source.count(CALL_SITE) != 1:
        problems.append('exec_map_shared_bb(&ctx->as, ctx->band_top) の呼び出しが 1 個ではない')
    if source.count(STACK_TOP) != 1:
        problems.append('CPL=3 の stack_top = RING3_USTACK_TOP が 1 個ではない')
    if 'paging_addrspace_map_user_keep(&ctx->as' in source:
        problems.append('exec_launch が BB を素の _keep で写している (旧実装)')
    return problems


def run(mutation=None):
    source = (ROOT / 'exec/exec.c').read_text()
    if mutation:
        _, old, new = mutation
        if old not in source:
            raise SystemExit('変異 %s の当て先が見つからない' % mutation[0])
        source = source.replace(old, new, 1)
    problems = call_site_problems(source)
    if problems:
        if mutation:
            return 'fail'
        raise SystemExit('exec/exec.c: ' + '; '.join(problems))
    with tempfile.TemporaryDirectory(prefix='os32-app-bb-') as tmp:
        tmp = pathlib.Path(tmp)
        (tmp / 'exec_bb_overlap.inc').write_text(extract(source))
        (tmp / 'paging_host_source.c').write_text((ROOT / 'kernel/paging.c').read_text())
        allocator = (ROOT / 'kernel/pgalloc.c').read_text()
        allocator = allocator.replace('irq_save()', '0').replace('irq_restore(flags)', '(void)flags')
        (tmp / 'pgalloc_host_source.c').write_text(allocator)
        includes = ['-I' + str(ROOT / p)
                    for p in ('include', 'arch/x86', 'platform/pc98', 'kernel',
                              'lib', 'exec')] + ['-I' + str(tmp)]
        host_includes = ['-I' + str(ROOT / 'tools/tests/host_arch')] + includes
        exe = tmp / 'app_bb_overlap'
        cmd = ['gcc', *FLAGS, '-DPHYSMEM_HOST_TEST=1', '-nostdlib', '-static', '-no-pie',
               *host_includes, str(SRC), str(ROOT / 'kernel/physmem.c'), '-o', str(exe)]
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
    print('HOST ILP32 PASS (私有領域の上端は BB の下、10 回の起動と終了で used_pages が戻る)')
    return 0


if __name__ == '__main__':
    sys.exit(main())
