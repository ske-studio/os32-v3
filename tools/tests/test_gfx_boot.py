"""起動時の ⑥ gfx の識別 → 予約 → 写像 → BB → SURFACE と ⑨ の移譲 (T1e)。

  python3 -B tools/tests/test_gfx_boot.py            # 肯定側
  python3 -B tools/tests/test_gfx_boot.py --mutate   # 否定側 + 肯定側

票 docs/archive/v3/TASK_T1_LEDGER.md §3-3 ⑥・⑨、§3-6、§3-8、§4-5。
tools/tests/gfx_boot_host.c が実物の kernel/paging.c・pgalloc.c・sys.c・
physmem.c を ILP32 で組み、段つき起動で台帳を ONLINE にしてから、gfx/gfx_core.c
から切り出した本物の gfx_boot_reserve / gfx_bb_phys_range /
gfx_client_to_gshell を 1 構成 1 プロセスで走らせる (見るものはそのファイルの
冒頭)。切り出しは台帳・写像の呼び出しを記録係へ付け替えるだけで、記録係は
順序を記録して実物へ渡す (写像は指定の窓で失敗させられる — T1d の Codex P3
「map を実際に失敗させて予約と写像 (PTE) が残る」の写像部分)。

構成の表 (BB の量 = 候補の最大、§4-5 のホスト試験):
  GFX= auto / pegc / cirrus / pc98 × PEGC の識別あり / なし、9821 でない機械、
  8MB (FIXED 型) / 17MB / 64MB、PEGC・Xe10 の写像の失敗、BB 不足、
  模擬選択で使う装置 (Cirrus / PEGC / 無し = PC98)。

保証範囲: 実物の backend 選択処理・probe は呼ばず、HW と SURFACE の存在から
g_backend を代入する。予約・写像・確保・移譲と 17 変異の検証には有効だが、
「Cirrus probe 失敗 → PEGC 選択」や fb->planes[0] までの統合は保証しない。
予約拒否・SURFACE 登録拒否も 17 構成には含めていない。

加えてテキストの検査: backend が自分で写像しない (paging_map_phys を呼ばない)、
sys_reserve_top / sys_top_reserved が残っていない、kernel.c の順序
(GFX= の読み取り → gfx_boot_reserve → exec_init、gshell の exec の直前に
gfx_client_to_gshell)。

--mutate は要の行を写しで壊し、どれかの構成が実行時に RED になることを見る。
実物のソースは書き換えない。コンパイルの失敗は RED に数えない。
"""
import host32
import pathlib
import re
import subprocess
import sys
import tempfile
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[2]
SOURCES = ('gfx/gfx_core.c', 'kernel/paging.c', 'kernel/pgalloc.c', 'kernel/sys.c')
HOST = ROOT / 'tools/tests/gfx_boot_host.c'

SLICE_BEGIN = '#define GFX_CAND_PEGC'
SLICE_END = '/* ======================================================================== */\n/*  KAPI: 画面能力の問い合わせ'
# 切り出しの中だけ、台帳・写像の呼び出しを記録係へ付け替える。
HOOKS = (
    ('*(volatile u8 *)P2V_IO(arch)', 'host_bda(arch)'),
    ('ledger_reserve_set(', 'host_reserve('),
    ('paging_map_phys(', 'host_map('),
    ('pgalloc_alloc_n_owner(', 'host_alloc('),
    ('ledger_surface_create(', 'host_surface('),
    ('ledger_arena_freeze(', 'host_freeze('),
)

# (名前, -D の組)。LEDGER_SF_PEGC = 2、LEDGER_SF_CIRRUS = 3、
# GFX_PREF_AUTO/PC98/PEGC/CIRRUS = 0/1/2/3。
PEGC, CIRRUS = 2, 3
AUTO, PC98, PREF_PEGC, PREF_CIRRUS = 0, 1, 2, 3
CASES = [
    ('17m-auto-mock-pegc', dict(CFG_KB=17408, HW=PEGC)),        # B3 の予約側 (probe は模擬)
    ('17m-auto-cirrus', dict(CFG_KB=17408, HW=CIRRUS)),
    ('17m-auto-no-hw-pc98', dict(CFG_KB=17408, HW=0)),
    ('64m-auto-pegc', dict(CFG_KB=65536, HW=PEGC)),                     # X14
    ('8m-auto-pegc', dict(CFG_KB=8192, HW=PEGC)),
    ('8m-auto-no-hw', dict(CFG_KB=8192, HW=0)),
    ('17m-auto-no-pegc-id', dict(CFG_KB=17408, PEGC_ID=0, HW=CIRRUS)),
    ('17m-pegc', dict(CFG_KB=17408, PREF=PREF_PEGC, HW=PEGC)),
    ('17m-pegc-no-id', dict(CFG_KB=17408, PREF=PREF_PEGC, PEGC_ID=0, HW=PEGC)),
    ('17m-cirrus', dict(CFG_KB=17408, PREF=PREF_CIRRUS, HW=CIRRUS)),
    ('17m-cirrus-no-pegc-id', dict(CFG_KB=17408, PREF=PREF_CIRRUS, PEGC_ID=0, HW=0)),
    ('17m-pc98', dict(CFG_KB=17408, PREF=PC98, HW=PEGC)),
    ('17m-pc98-no-pegc-id', dict(CFG_KB=17408, PREF=PC98, PEGC_ID=0)),
    ('17m-not-9821', dict(CFG_KB=17408, IS9821=0, HW=PEGC)),
    ('17m-pegc-map-fails', dict(CFG_KB=17408, FAIL_MAP=PEGC, HW=PEGC)),
    ('17m-cirrus-map-fails', dict(CFG_KB=17408, FAIL_MAP=CIRRUS, HW=PEGC)),
    ('17m-bb-short', dict(CFG_KB=17408, FAIL_BB=1, HW=PEGC)),
]


def load(mutation=None):
    texts = {rel: (ROOT / rel).read_text() for rel in SOURCES}
    if mutation:
        _, rel, old, new = mutation
        if texts[rel].count(old) != 1:
            raise SystemExit('変異 %s の当て先が見つからない: %s' % (mutation[0], rel))
        texts[rel] = texts[rel].replace(old, new, 1)
    return texts


def slice_gfx(core):
    if core.count(SLICE_BEGIN) != 1 or core.count(SLICE_END) != 1:
        raise SystemExit('gfx/gfx_core.c: 切り出しの目印が 1 個ずつではない')
    body = core.split(SLICE_BEGIN, 1)[1].split(SLICE_END, 1)[0]
    body = SLICE_BEGIN + body
    for old, new in HOOKS:
        body = body.replace(old, new)
    return '/* 生成物。gfx/gfx_core.c から test_gfx_boot.py が切り出した。 */\n' + body


def build(texts, tmp):
    for unit in ('paging', 'pgalloc', 'sys'):
        source = texts['kernel/%s.c' % unit]
        source = source.replace('irq_save()', 'host_irq_save()').replace(
            'irq_restore(flags)', 'host_irq_restore(flags)')
        (tmp / f'{unit}_host_source.c').write_text(source)
    (tmp / 'gfx_boot_slice.inc').write_text(slice_gfx(texts['gfx/gfx_core.c']))
    cmd = ['gcc', '-m32', '-march=i386', '-std=gnu11', '-Wall', '-Wextra', '-Werror',
           '-ffreestanding', '-fno-pie', '-fno-stack-protector', '-nostdlib', '-static',
           '-no-pie', '-ffunction-sections', '-Wl,--gc-sections', '-DPHYSMEM_HOST_TEST=1',
           '-Wno-unused-function',
           # 切り出しの weak の identify の番地の検査 (カーネルでは未リンクで 0
           # になりうる) は、ここでは定義があるので常に真になる。
           '-Wno-address']
    cmd += ['-I' + str(ROOT / 'tools/tests/host_arch')]
    cmd += ['-I' + str(ROOT / p) for p in ('include', 'arch/x86', 'platform/pc98', 'kernel',
                                           'lib', 'drivers', 'gfx', 'sdk/include/os32')]
    cmd += ['-I' + str(tmp)]
    return cmd


def run_case(texts, name, defs):
    with tempfile.TemporaryDirectory(prefix='os32-gfx-boot-') as tmp:
        tmp = pathlib.Path(tmp)
        cmd = build(texts, tmp)
        cmd += ['-D%s=%d' % kv for kv in defs.items()]
        out = subprocess.run(cmd + [str(HOST), str(ROOT / 'kernel/physmem.c'),
                                    '-o', str(tmp / 'test')], capture_output=True, text=True)
        if out.returncode:
            return 'compile', out.stderr
        run = host32.run([str(tmp / 'test')], capture_output=True, text=True, timeout=60)
        verdict = 'ok' if run.returncode == 0 else (
            'fail' if run.returncode == 1 and 'FAIL ' in run.stdout else 'runtime-error')
        return verdict, 'rc=%d %s%s' % (run.returncode, run.stdout, run.stderr)


def text_problems():
    problems = []
    core = (ROOT / 'gfx/gfx_core.c').read_text()
    body = core.split(SLICE_BEGIN, 1)[-1].split(SLICE_END, 1)[0]
    for old, _ in HOOKS:
        if old not in body:
            problems.append('gfx/gfx_core.c: 切り出しに %r が無い (記録係へ付け替えられない)' % old)
    for rel in ('gfx/backend_pegc.c', 'gfx/backend_cirrus.c'):
        if 'paging_map_phys(' in (ROOT / rel).read_text():
            problems.append('%s が自分で写像している (予約・写像は ⑥ の 1 回だけ)' % rel)
    for d in ('kernel', 'gfx', 'include', 'exec', 'drivers', 'kapi'):
        for path in (ROOT / d).rglob('*.[ch]'):
            text = path.read_text(errors='replace')
            if re.search(r'\bsys_reserve_top\s*\(|\bsys_top_reserved\b', text):
                problems.append('%s に sys_reserve_top / sys_top_reserved が残っている'
                                % path.relative_to(ROOT))
    k = (ROOT / 'kernel/kernel.c').read_text()
    try:
        gfx_cfg = k.index('sysconfig_get_str(SYS_SYSTEM_CFG, "GFX"')
        reserve = k.index('    gfx_boot_reserve();')
        exec_init = k.index('    exec_init();')
        to_gshell = k.index('gfx_client_to_gshell();')
        is_gui = k.rindex('if (is_gui) {', 0, to_gshell)
        run_shell = k.index('rc = exec_run(cur_shell);')
    except ValueError as e:
        return problems + ['kernel/kernel.c: 目印が見つからない (%s)' % e]
    if not gfx_cfg < reserve < exec_init:
        problems.append('kernel.c: GFX= の読み取り → gfx_boot_reserve → exec_init の順でない')
    if not is_gui < to_gshell < run_shell:
        problems.append('kernel.c: gfx_client_to_gshell が GUI の分岐で gshell の exec の前にない')
    if k.count('gfx_boot_reserve();') != 1 or k.count('gfx_client_to_gshell();') != 1:
        problems.append('kernel.c: ⑥ / ⑨ の呼び出しが 1 個ずつでない')
    return problems


class GfxBoot(unittest.TestCase):
    texts = None

    @classmethod
    def setUpClass(cls):
        cls.texts = load()

    def test_text(self):
        self.assertEqual(text_problems(), [])


def _make(name, defs):
    def test(self):
        result, log = run_case(self.texts, name, defs)
        self.assertEqual(result, 'ok', log)
    return test


for _name, _defs in CASES:
    setattr(GfxBoot, 'test_' + _name.replace('-', '_'), _make(_name, _defs))


def _swap_reserve_and_map():
    core = (ROOT / 'gfx/gfx_core.c').read_text()
    line = '    if (m && !ledger_reserve_set(LEDGER_OWNER_GFX, sp, n)) m = 0;\n'
    start = core.index(line)
    end = core.index('    /* 4. BB', start)
    maps = core[start + len(line):end]
    return core[start:end], maps + line


RESERVE_THEN_MAP, MAP_THEN_RESERVE = _swap_reserve_and_map()

MUTATIONS = [
    ('planar-plane-stride-rounded', 'gfx/gfx_core.c',
     '.plane_offset = {0, GFX_BPL * GFX_HEIGHT, 2 * GFX_BPL * GFX_HEIGHT,\n                       3 * GFX_BPL * GFX_HEIGHT}',
     '.plane_offset = {0, GVRAM_PLANE_SIZE, 2 * GVRAM_PLANE_SIZE, 3 * GVRAM_PLANE_SIZE}'),
    ('planar-display-UC-lost', 'gfx/gfx_core.c',
     '.format = GFX_BB_PLANAR4, .planes = 1, .cache = LEDGER_CACHE_UC',
     '.format = GFX_BB_PLANAR4, .planes = 1, .cache = LEDGER_CACHE_WB'),
    ('planar-last-plane-missing', 'gfx/gfx_core.c',
     'for (i = 0; i < 4; i++) {\n        sf = gfx_planar_display;',
     'for (i = 0; i < 3; i++) {\n        sf = gfx_planar_display;'),
    # BB の探索範囲を池全体にする (高位 RAM へ行く、X14)
    ('bb-whole-pool', 'gfx/gfx_core.c',
     'pgalloc_arena_end(),\n                              LEDGER_TOP_DOWN',
     'pgalloc_limit_pfn(),\n                              LEDGER_TOP_DOWN'),
    # 1 つの窓の写像失敗で候補を全部捨てる (B3: Xe10 の失敗が PEGC を妨げる)
    ('map-failure-drops-all', 'gfx/gfx_core.c',
     '            m &= ~gfx_cand_of(i);', '            m = 0;'),
    # 写像を予約より先に行う (識別 → 予約 → 写像の順、D33)
    ('map-before-reserve', 'gfx/gfx_core.c', RESERVE_THEN_MAP, MAP_THEN_RESERVE),
    # アリーナの上端を凍結しない (CPL=0 の子が BB まで伸びる)
    ('no-freeze', 'gfx/gfx_core.c', '    ledger_arena_freeze();\n', ''),
    # BB を 0 で埋めない (前の中身が CPL=3 に見える)
    ('bb-not-zeroed', 'gfx/gfx_core.c',
     '        kmemset(P2V(pfn * PAGE_SIZE), 0, (u32)MEM_GFX_BB8_SIZE);\n',
     '        (void)0;\n'),
    # PEGC が候補でなくても BB を取る (量が候補の最大でない)
    ('bb-always', 'gfx/gfx_core.c',
     '    if ((m & GFX_CAND_PEGC) &&\n        pgalloc_alloc_n_owner',
     '    if ((m | 1) &&\n        pgalloc_alloc_n_owner'),
    # GFX=pc98 でも識別して予約する
    ('pc98-pref-ignored', 'gfx/gfx_core.c',
     '    if (g_backend_pref == GFX_PREF_PC98 ||\n', '    if (0 ||\n'),
    # 表示面を貸せる権限にする (契約 G4)
    ('display-lendable', 'gfx/gfx_core.c',
     '.perm_max = LEDGER_PERM_NONE', '.perm_max = LEDGER_PERM_RW'),
    # gfx_bb_phys_range が選択中の backend を見ない
    ('phys-range-not-selected', 'gfx/gfx_core.c',
     '        ledger_surface_find(gfx_sf_backend(), LEDGER_ROLE_CLIENT);\n    if (base)',
     '        ledger_surface_find(LEDGER_SF_PC98, LEDGER_ROLE_CLIENT);\n    if (base)'),
    # ⑨ が選択中でない CLIENT を移す
    ('gshell-wrong-client', 'gfx/gfx_core.c',
     '        ledger_surface_find(gfx_sf_backend(), LEDGER_ROLE_CLIENT);\n    if (sf && sf->owner != LEDGER_OWNER_GSHELL)',
     '        ledger_surface_find(LEDGER_SF_PEGC, LEDGER_ROLE_CLIENT);\n    if (sf && sf->owner != LEDGER_OWNER_GSHELL)'),
    # RAM の面の移譲で L2 のページを移さない
    ('transfer-surface-only', 'kernel/pgalloc.c',
     '    ok = from == to || sf->backing != LEDGER_SB_RAM ||\n'
     '         ledger_transfer(sf->first, (int)sf->npages, from, to);',
     '    ok = 1;'),
    # 固定 RAM の面の SURFACE_BACKING 区間を移さない
    ('fixed-region-not-moved', 'kernel/pgalloc.c',
     '            r->owner = (u8)to;\n', '            (void)r;\n'),
    # 凍結が AS owner のページも「永続確保」に数える
    ('freeze-counts-as', 'kernel/pgalloc.c',
     '        if (o && ledger_owners[o].kind == LEDGER_KIND_PERSIST) arena_top = p;',
     '        if (o) arena_top = p;'),
    # sys_usable_mem_end がアリーナの上端を見ない (旧 sys_reserve_top の役が消える)
    ('usable-ignores-arena', 'kernel/sys.c',
     '    return top && top < sys_frozen_exec ? top : sys_frozen_exec;',
     '    (void)top; return sys_frozen_exec;'),
]


def mutate():
    red = 0
    for m in MUTATIONS:
        texts = load(m)
        verdict = 'GREEN'
        for name, defs in CASES:
            result, _ = run_case(texts, name, defs)
            if result in ('compile', 'runtime-error'):
                verdict = result
                break
            if result == 'fail':
                verdict = 'RED (%s)' % name
                break
        ok = verdict.startswith('RED')
        print('  変異 %-24s %s' % (m[0], verdict if ok else
                                   '**%s — 試験が穴を見逃した**' % verdict), flush=True)
        red += ok
    print('%d/%d の変異が RED' % (red, len(MUTATIONS)))
    return 0 if red == len(MUTATIONS) else 1


if __name__ == '__main__':
    do_mutate = '--mutate' in sys.argv
    if do_mutate:
        sys.argv.remove('--mutate')
    result = unittest.main(verbosity=1, exit=False).result
    if not result.wasSuccessful():
        sys.exit(1)
    sys.exit(mutate() if do_mutate else 0)
