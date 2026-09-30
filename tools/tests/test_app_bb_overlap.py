"""共有 BB の写像がアプリ帯の私有ページを潰さないこと (2026-09-30 の後退).

  python3 -B tools/tests/test_app_bb_overlap.py            # 肯定側
  python3 -B tools/tests/test_app_bb_overlap.py --mutate   # 否定側 + 肯定側

8MB + PEGC で CPL=3 アプリを起動・終了するたびに used_pages が 74 ずつ増えた
(BB [0x7B5000, 0x800000) の恒等写像がスタック / exec_heap の私有 PTE を上書き)。
実物の kernel/paging.c / pgalloc.c / physmem.c と、exec/exec.c から**テキストの
まま切り出した** app_map_region / exec_map_shared_bb / exec_teardown_app で
起動と終了を 10 回まわし、used_pages が毎回戻ること・V86 の backing (159 の
連続) が取れることを見る (tools/tests/app_bb_overlap_host.c)。

--mutate は否定側: 帯との重なりを写してしまう版 (上側・下側の切り詰めを外す)
で試験が RED になることを見る。変異は一時ディレクトリの写しに当てる。
"""
import pathlib
import subprocess
import sys
import tempfile

ROOT = pathlib.Path(__file__).resolve().parents[2]
FLAGS = ['-m32', '-march=i386', '-std=gnu11', '-ffreestanding', '-fno-pie',
         '-fno-stack-protector', '-Wall', '-Wextra', '-Werror']
SRC = ROOT / 'tools/tests/app_bb_overlap_host.c'

WANTED = ('volatile u32 exec_bb_clipped_pages',
          'static int app_map_region(',
          'static void exec_map_shared_bb(',
          'static void exec_teardown_app(')

# (名前, 置き換え前, 置き換え後) — どれも exec/exec.c の写しに当てる
MUTATIONS = [
    # 旧式: 帯の上端で切らず BB を全部写す (上書きで 74 ページ漏れる)
    ('bb-hi-unclipped',
     '    hi_first = (bb_base > band_top) ? bb_base : band_top;',
     '    hi_first = bb_base;'),
    # 下側を帯の底で切らない (帯の中まで写す)
    ('bb-lo-unclipped',
     '    lo_end = (bb_end < MEM_APP_BAND_BASE) ? bb_end : MEM_APP_BAND_BASE;',
     '    lo_end = bb_end;'),
    # 帯の外の部分を写し忘れる (9801 / 12MB 機の BB が見えなくなる)
    ('bb-lo-dropped',
     '    if (bb_base < lo_end)\n',
     '    if (0)\n'),
    ('bb-hi-dropped',
     '    if (hi_first < bb_end)\n',
     '    if (0)\n'),
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


def extract(source):
    define = next((line for line in source.splitlines()
                   if line.startswith('#define RING3_USTACK_SIZE ')), None)
    if define is None:
        raise SystemExit('exec/exec.c: #define RING3_USTACK_SIZE が見つからない')
    parts = ['/* 生成物。exec/exec.c から test_app_bb_overlap.py が切り出した。 */',
             define]
    parts += [slice_out(source, sig) for sig in WANTED]
    return '\n'.join(parts) + '\n'


def run(mutation=None):
    source = (ROOT / 'exec/exec.c').read_text()
    if mutation:
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
        print('  変異 %-16s %s' % (m[0], 'RED (%s)' % result if ok
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
    print('HOST ILP32 PASS (10 回の起動と終了で used_pages が戻る)')
    return 0


if __name__ == '__main__':
    sys.exit(main())
