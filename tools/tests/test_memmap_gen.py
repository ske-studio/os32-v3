#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""tools/gen_memmap.py — 重なり・逆転の検出と、生成物から独立した配置検査。

票 docs/archive/kernel_v21/TASK_KSTACK_USER.md §4-bis / 記録: tools/tests/memmap_tdd.md

  python3 -B tools/tests/test_memmap_gen.py [--mutate]

**実物の include/memmap.h をそのまま使い、kernel.map だけを差し替える。**
番地の計算は全部 `__bss_end` から芋づるなので、これだけで「壊れた地図」と
「直った地図」の両方を作れる。試験が実物のツリーの状態に依存しないので、
票 §4 の 4 で番地を直しても腐らない。

  BROKEN (__bss_end=0x1A0000)  予算超過。SHM 帯がカーネル帯域を突き抜けて
                               SQLite 帯と重なり、後方予約が逆転する
  CLEAN  (__bss_end=0x140000)  予算内。矛盾 0

--mutate は**否定側**。この道具の存在理由を 1 つずつ外した版で、試験が RED に
なることを見る。
  no-budget    カーネル本体の予算を見ない → 予算超過の診断を失う
  no-escape    親領域からのはみ出しを見ない → 子領域の逸脱を見逃す
  no-overlap   重なりを見ない        → 壊れた地図を「矛盾なし」と言う
  no-reversed  逆転を見ない          → 空振りする範囲指定を見逃す
  guess-map    kernel.map が無いとき __bss_end を決め打ちにする
               → それらしい嘘の地図を出す (今回とまったく同じ失敗)
  no-mirror    リンカスクリプト / NASM / SDK に散らばった写しを照合しない
               → memmap.h だけ直して他が古いまま通る
"""
import argparse
import copy
import hashlib
import json
import pathlib
import re
import importlib.util
import shutil
import subprocess
import sys
import tempfile

ROOT = pathlib.Path(__file__).resolve().parents[2]
SCRIPT = ROOT / "tools/gen_memmap.py"

# 予算 MEM_KERNEL_IMAGE_MAX = 0x99000 (KHEAP 176KB、E11-BUD) → 上限は __bss_end = 0x199000。
BROKEN = 0x380000        # 予算超過。SHM がカーネル帯域を突き抜ける
CLEAN = 0x280000         # 予算内。重なりも逆転も無い

def map_symbols(resident):
    return {'__bss_end': 0x140000, '__sqlite_start': 0x140000,
            '__sqlite_end': resident - 0x21000, '__kernel_resident_end': resident,
            '__ledger_fixed_start': 0x13e000, '__ledger_fixed_end': 0x140000}


MUTATIONS = {
    "no-layout-bss-coverage": (
        '    for lo, hi in (("__bss_start", "__bss_end"), ("__sqlite_data_end", "__sqlite_end")):',
        '    for lo, hi in ():'),
    "no-layout-overlap": (
        '            out.append("半開区間の重複: %s / %s" % (a["name"], b["name"]))',
        '            pass'),
    "no-chain-budget": ('    if start > limit:', '    if False:'),
    "no-budget": ("    return limit, sym[\"__kernel_resident_end\"] - load",
                  "    return None"),
    "no-escape": ("def escaped_children(rows):",
                  "def escaped_children(rows):\n    return []\ndef _unused_escaped_children(rows):"),
    "no-fixed-bounds": ("    if m is not None:\n        names = (",
                        "    if False:\n        names = ("),
    "no-overlap": ("""    good = [r for r in concrete(rows) if r["start"] <= r["end"]]""",
                   """    good = []"""),
    "no-reversed": ("""    return [r for r in concrete(rows) if r["start"] > r["end"] + 1]""",
                    """    return []"""),
    "no-mirror": ("""def mirrors(root, m):""",
                  """def mirrors(root, m):
    return []
def _unused_mirrors(root, m):"""),
    "guess-map": ("""    if not os.path.isfile(map_path):
        raise MissingMap(""",
                  """    if not os.path.isfile(map_path):
        return {"__bss_end": 0x140000, "__sqlite_start": 0x200000,
                "__sqlite_end": 0x2BC060}
    if False:
        raise MissingMap("""),
}


# memmap.h の値を写している場所 (gen_memmap.py の MIRRORS / ASM_SYMBOLIC と同じ顔ぶれ)
MIRROR_FILES = ("build/os32.ld", "kernel/kentry.asm",
                "sdk/include/os32/os32_gui_shared.h",
                "sdk/rust/os32api/src/gui/proto.rs",
                # ホスト試験が kernel/shm.c を組むための偽 memmap.h を持つ。
                # 2026-09-17 に実際にずれたので写しの照合対象に入っている。
                "tools/tests/test_owner_reclaim.py",
                # ローダ (NASM) のブート情報域の番地 (票 TASK_HDD_INSTALL 段 0)
                "boot/bootinfo.inc",
                # VK32 の展開先の帯と HDD ローダのブート情報域 (票 TASK_SERIAL_HOSTFS A-4)
                "boot/boot_defs.h", "boot/loader_fat_new.asm")


def make_tree(tmp, bss_end):
    """実物の memmap.h と写しを置いた木を作る。kernel.map だけ合成。"""
    root = pathlib.Path(tmp)
    (root / "include").mkdir(parents=True)
    (root / "docs").mkdir(parents=True)
    (root / "build/out").mkdir(parents=True)
    for rel in ('sdk/os32x_hdr.py', 'tools/mkshlib.py', 'sdk/link/app.ld', 'sdk/link/app_sys.ld', 'sdk/link/shlib.ld', 'sdk/rust/os32api/src/gui/stub.rs'):
        dst = root / rel
        dst.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy(ROOT / rel, dst)
    shutil.copy(ROOT / "include/memmap.h", root / "include/memmap.h")
    shutil.copy(ROOT / "include/pegc.h", root / "include/pegc.h")
    shutil.copy(ROOT / "docs/02_memory.md", root / "docs/02_memory.md")
    for rel in MIRROR_FILES:
        dst = root / rel
        dst.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy(ROOT / rel, dst)
    if bss_end is not None:
        (root / "build/out/kernel.map").write_text(''.join('  0x%08x %s = .\n' % (v, k) for k, v in map_symbols(bss_end).items()),
                                                   encoding="utf-8")
    return root


def run(script, root, *args):
    return subprocess.run([sys.executable, "-B", str(script), "--root", str(root),
                           *args], capture_output=True, text=True, timeout=60)


def check(cond, what):
    if not cond:
        raise AssertionError(what)


def cases(script):
    """4 つの場面。戻り値は人が読む 1 行の記録。"""
    log = []

    # --- 1. 壊れた地図: 重なりも逆転も名指しで出し、非ゼロで終わる ---
    with tempfile.TemporaryDirectory(prefix="os32-genmm-") as tmp:
        root = make_tree(tmp, BROKEN)
        # 壊れた地図も診断用に出力でき、配置の検査は非ゼロで終わる。
        check(run(script, root, "--write").returncode == 0, "--write が失敗した")
        out = run(script, root, "--check")
        check(out.returncode != 0, "壊れた地図なのに 0 で終わった")
        check("古い" not in out.stdout, "鮮度ずれが残っていて矛盾の検出を見ていない")
        # 「矛盾の報告」の行だけを見る (表や差分に同じ語が出るので前置きで絞る)
        found = [x for x in out.stdout.split("\n") if x.startswith("gen_memmap: ")]
        rev = [x for x in found if x.startswith("gen_memmap: 逆転:")]
        ovl = [x for x in found if x.startswith("gen_memmap: 重なり:")]
        check(len(rev) == 1, "SLACKの逆転を 1 件報告しない: %r" % rev)
        check(any("共有メモリ本体" in x and "固定" in x for x in ovl),
              "SHM 帯と固定 PT 枠の重なりを報告しない: %r" % ovl)
        bud = [x for x in found if x.startswith("gen_memmap: 予算超過:")]
        check(len(bud) == 1, "カーネル本体の予算超過を報告しない: %r" % bud)
        log.append("BROKEN  --check = %d (予算超過 1 / 逆転 %d / 重なり %d)"
                   % (out.returncode, len(rev), len(ovl)))

    # GUI 子領域の末尾を親の SHM 領域から 1 ページはみ出させる。
    with tempfile.TemporaryDirectory(prefix="os32-genmm-escape-") as tmp:
        root = make_tree(tmp, CLEAN)
        header = root / "include/memmap.h"
        original = header.read_text()
        header.write_text(re.sub(r'(^#define\s+MEM_SHM_GUI_BASE\s+).*$',
                                 r'\1(MEM_SHM_BASE + MEM_SHM_GUI_OFFSET + MEM_GUARD_SIZE)',
                                 original, flags=re.M))
        out = run(script, root, "--check")
        check(out.returncode != 0, "子領域が親をはみ出しても 0 で終わった")
        esc = [x for x in out.stdout.splitlines() if x.startswith("gen_memmap: はみ出し:")]
        check(any("GUI 予約" in x and "共有メモリ本体" in x for x in esc),
              "GUI 子領域の親 SHM 領域からのはみ出しを報告しない: %r" % esc)
        log.append("ESCAPE  --check = %d (GUI 子領域のはみ出しを検出)" % out.returncode)

    # --- 2. 直した地図: --write して --check が 0 で終わる ---
    with tempfile.TemporaryDirectory(prefix="os32-genmm-") as tmp:
        root = make_tree(tmp, CLEAN)
        w = run(script, root, "--write")
        check(w.returncode == 0, "--write が失敗した: " + w.stderr)
        original = (root / "docs/02_memory.md").read_text(encoding="utf-8")
        doc = (root / "build/out/MEMMAP.md").read_text(encoding="utf-8")
        check("0xFFF00000" in doc and "0xFFF7FFFF" in doc and
              "PEGC リニア窓の別名" in doc, "PEGC alt window missing")
        check("0x3EB000" in doc and "固定 MMIO top PT" in doc, "top PT missing")
        check("0x140000" in doc, "生成ブロックに __bss_end が入っていない")
        # 表の行は必ず絶対番地で始まる (「+4KB - +260KB」のような相対表記を禁じる)
        block = doc.split("<!-- 生成")[1].split("<!-- /生成")[0]
        fenced = block.split("```")[1]
        for line in fenced.split("\n"):
            check(not line.startswith("+"),
                  "表に相対表記の行がある: %r" % line)
        check(sum(1 for x in fenced.split("\n") if x.startswith("0x")) > 15,
              "表の行が絶対番地で始まっていない")
        out = run(script, root, "--check")
        check(out.returncode == 0,
              "矛盾の無い地図なのに落ちた: " + out.stdout + out.stderr)
        log.append("CLEAN   --write → --check = 0")

        # --- 3. 生成物の改変・欠落は配置検査に影響せず、説明は変更しない ---
        artifact = root / "build/out/MEMMAP.md"
        artifact.write_text(doc.replace("0x140000", "0x999999", 1), encoding="utf-8")
        check(run(script, root, "--check").returncode == 0, "生成物の鮮度で落ちた")
        check(run(script, root, "--write").returncode == 0, "再生成できない")
        check(artifact.read_text() == doc, "再生成で地図が復元されない")
        artifact.unlink()
        check(run(script, root, "--check").returncode == 0, "生成物無しで落ちた")
        check((root / "docs/02_memory.md").read_text() == original, "説明を変更した")
        (root / "docs/02_memory.md").unlink()
        check(run(script, root, "--check").returncode == 0, "説明文書無しで落ちた")
        check(run(script, root, "--write").returncode == 0, "説明文書無しで生成できない")
        log.append("ARTIFACT  改変・欠落でも配置検査 = 0 / 説明文書は不変")

    # --- 4. 写しがずれたら気づく (リンカスクリプト / NASM / SDK) ---
    with tempfile.TemporaryDirectory(prefix="os32-genmm-") as tmp:
        root = make_tree(tmp, CLEAN)
        check(run(script, root, "--write").returncode == 0, "--write が失敗した")
        check(run(script, root, "--check").returncode == 0, "素の状態で落ちた")
        ld = root / "build/os32.ld"
        ld.write_text(ld.read_text(encoding="utf-8")
                      .replace("MEM_KSTACK_TOP = LAYOUT_MEM_KSTACK_TOP;",
                               "MEM_KSTACK_TOP       = 0x1FFFFC;"),
                      encoding="utf-8")
        out = run(script, root, "--check")
        check(out.returncode != 0, "os32.ld の写しがずれても気づかない")
        check("写しのずれ" in out.stdout and "MEM_KSTACK_TOP" in out.stdout,
              "ずれた場所と基準を名指ししない: %r" % out.stdout)
        # ASM が数値直書きに戻ったら気づく
        with tempfile.TemporaryDirectory(prefix="os32-genmm-asm-") as directory:
            root2 = make_tree(directory, CLEAN)
            asm = root2 / "kernel/kentry.asm"
            asm.write_text(asm.read_text(encoding="utf-8")
                           .replace("mov     esp, LAYOUT_MEM_KSTACK_TOP",
                                    "mov     esp, 002FFFFCh"), encoding="utf-8")
            out = run(script, root2, "--check")
            check(out.returncode != 0, "kentry.asm の数値直書きに気づかない")
            check("kentry.asm" in out.stdout, "どのファイルか言わない")
        log.append("MIRROR  --check = 1 (os32.ld のずれと ASM の直書きを検出)")

    # --- 5. kernel.map が無いときは推測しない ---
    with tempfile.TemporaryDirectory(prefix="os32-genmm-") as tmp:
        root = make_tree(tmp, None)
        out = run(script, root, "--check")
        check(out.returncode != 0, "kernel.map が無いのに 0 で終わった")
        check("kernel.map" in out.stderr, "止まった理由を言わない")
        check("0x" not in out.stdout, "kernel.map が無いのに地図を出した")
        out = run(script, root)
        check(out.returncode != 0 and "kernel.map" in out.stderr,
              "--write/--check 以外でも止まること")
        log.append("NOMAP   --check = %d (推測せず止まる)" % out.returncode)

    log.extend(fixed_paging_cases(script))
    log.extend(layout_cases(script))
    return log


def fixed_paging_cases(script):
    """地図と実リンカを一緒に検査。Cのコンパイル拒否をREDに数えない。"""
    spec = importlib.util.spec_from_file_location('genmm_test', script)
    gen = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(gen)
    changes = (
        ('old-512k-heap', 'MEM_SHELL_HEAP_SIZE', '0x080000UL'),
        ('unaligned-paging', 'MEM_FIXED_PAGING_BASE', '0x3E1001UL'),
        ('one-page-overlap', 'MEM_SHELL_HEAP_END', '(MEM_SHELL_GUARD + MEM_GUARD_SIZE)'),
        ('boot-count', 'MEM_FIXED_BOOT_PT_COUNT', '7'),
        ('top-overlap', 'MEM_FIXED_MMIO_TOP_PT_BASE', 'MEM_FIXED_APERTURE_PT_BASE'),
        ('old-tail', 'MEM_FIXED_PAGING_END', '(MEM_FIXED_MMIO_TOP_PT_BASE + 2 * MEM_GUARD_SIZE)'),
    )
    for label, name, expr in changes:
        with tempfile.TemporaryDirectory(prefix='os32-fixed-map-') as tmp:
            root = make_tree(tmp, CLEAN)
            header = root / 'include/memmap.h'
            header.write_text(re.sub(r'(^#define\s+' + name + r'\s+).*$',
                                     lambda m: m[1] + expr, header.read_text(), flags=re.M))
            # 写し一致の偶然の拒否で済ませず、境界の検査だけでも拒否させる。
            macros = gen.Macros(gen.read_defines(header), map_symbols(CLEAN))
            ld = root / 'build/os32.ld'
            text = ld.read_text()
            for rel, pattern, macro in gen.MIRRORS:
                if rel == 'build/os32.ld':
                    text = re.sub(pattern, lambda m: f'{macro} = 0x{macros.get(macro):X};', text, flags=re.M)
            ld.write_text(text)
            check(run(script, root, '--write').returncode == 0, 'write failed')
            out = run(script, root, '--check')
            check(out.returncode != 0 and '固定paging:' in out.stdout, label + ' escaped map checks')
            link = link_fixture(root)
            check(link.returncode != 0 and ('fixed paging' in link.stderr or 'fixed guard/PT' in link.stderr or 'shell image/heap' in link.stderr),
                  label + ' escaped linker ASSERT: ' + link.stderr)
    with tempfile.TemporaryDirectory(prefix='os32-top-mirror-') as tmp:
        root = make_tree(tmp, CLEAN)
        ld = root / 'build/os32.ld'
        ld.write_text(ld.read_text().replace('MEM_FIXED_MMIO_TOP_PT_BASE = LAYOUT_MEM_FIXED_MMIO_TOP_PT_BASE;',
                                            'MEM_FIXED_MMIO_TOP_PT_BASE = 0x3FA000;'))
        out = run(script, root, '--check')
        check(out.returncode != 0 and '写しのずれ' in out.stdout and
              'MEM_FIXED_MMIO_TOP_PT_BASE' in out.stdout, 'top PT mirror mismatch escaped')
    with tempfile.TemporaryDirectory(prefix='os32-fixed-link-') as tmp:
        root = make_tree(tmp, CLEAN)
        check(link_fixture(root).returncode == 0, 'baseline linker failed')
        # SQLite 末尾が旧PD案0x2DD000への上限を越えても、既存DMA予算内は許可。
        check(link_fixture(root, sqlite_end=0x2c0000).returncode == 0, 'SQLite growth restricted')
        out = link_fixture(root, sqlite_end=0x460000)
        check(out.returncode != 0 and 'resident chain exceeds SLACK ceiling' in out.stderr,
              'SQLite exceeded resident budget without rejection')
        ld = root / 'build/os32.ld'
        ld.write_text(ld.read_text().replace('MEM_FIXED_PD_BASE = LAYOUT_MEM_FIXED_PD_BASE;',
                                             'MEM_FIXED_PD_BASE = 0x3F2000;'))
        check(run(script, root, '--write').returncode == 0, 'write failed')
        out = run(script, root, '--check')
        check(out.returncode != 0 and '写しのずれ' in out.stdout, 'fixed paging mirror mismatch escaped')
    return ['FIXED   map/link rejected 512KiB heap / unaligned / overlap / count; mirror rejected',
            'SQLITE  growth past old PT limit accepted; resident chain budget excess rejected']


def link_fixture(root, sqlite_end=0x2bc060):
    """本物のos32.ldに最小入力を与え、ASSERTを実際に評価する。"""
    cc = 'i386-elf-gcc'
    ld = 'i386-elf-ld'
    for rel, asm in (('kernel/kentry', '.text\n.globl kentry\nkentry: nop\n.bss\n.space 16\n'),
                     ('lib/sqlite3/sqlite3', f'.bss\n.space {sqlite_end - 0x200000}\n'),
                     ('lib/sqlite3/os32_sqlite_vfs', '.text\n'),
                     ('lib/sqlite3/os32_sqlite_test', '.text\n')):
        src = root / (rel + '.s')
        src.parent.mkdir(parents=True, exist_ok=True)
        src.write_text(asm)
        subprocess.run([str(cc), '-c', str(src), '-o', str(root / (rel + '.o'))], check=True,
                       capture_output=True)
    sys.path.insert(0, str(ROOT / 'tools'))
    import gen_memmap
    source = (root / 'build/os32.ld').read_text()
    (root / 'build/out/fixture.ld').write_text(gen_memmap.preprocess_layout(root, source))
    return subprocess.run([str(ld), '-T', 'build/out/fixture.ld' , 'kernel/kentry.o',
                           'lib/sqlite3/sqlite3.o', '-o', 'build/out/fixture.elf'],
                          cwd=root, capture_output=True, text=True)


def layout_cases(script):
    """T3a0: real ELF acceptance, then one defect per PASS/FAIL pair.

    These are input defects, also run with MUTATE=0. --mutate separately checks
    whether disabling the checker makes the corresponding assertion fail.
    No rejected compilation counts as detection.
    """
    spec = importlib.util.spec_from_file_location('genmm_layout_test', script)
    gen = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(gen)
    elf = ROOT / 'build/out/kernel.elf'
    sections, symbols = gen.read_elf(elf)
    result = run(script, ROOT, '--manifest')
    check(result.returncode == 0, 'layout control failed: ' + result.stderr)
    manifest = json.loads(result.stdout)
    check(not gen.layout_defects(manifest, sections, symbols), 'layout control defects')
    old = json.loads((ROOT / 'build/out/generations-manifest.json').read_text())
    check(all(manifest[k] == old[k] for k in ('build_id', 'kernel_commit', 'kapi_version', 'generations')),
          'existing manifest generations differ')
    recorded = {x['path']: x for x in old['files']}
    for x in manifest['artifacts']:
        data = (ROOT / x['path']).read_bytes()
        check(x['size'] == len(data) and x['sha256'] == hashlib.sha256(data).hexdigest(),
              'artifact size/hash differs: ' + x['path'])
        if x['path'] in recorded:
            check(x == recorded[x['path']], 'existing artifact manifest differs')
    mapped = gen.read_symbols(ROOT / 'build/out/kernel.map')
    check(all(symbols[k] == mapped[k] == manifest['symbols'][k] for k in mapped),
          'ELF/map/manifest symbols differ')
    # Independent binutils reading guards against a self-consistent ELF parser error.
    dump = subprocess.run(['i386-elf-objdump', '-h', str(elf)], capture_output=True,
                          text=True, check=True).stdout
    table = {m[1]: (int(m[2], 16), int(m[3], 16)) for m in
             re.finditer(r'^\s*\d+\s+(\S+)\s+([0-9a-f]+)\s+([0-9a-f]+)', dump, re.M)}
    for s in sections:
        check(table[s['name']] == (s['end'] - s['start'], s['start']), 'ELF section differs from objdump')
        r = next(r for r in manifest['ranges'] if r['name'] == s['name'])
        check((r['start'], r['end'], r['noload']) == (s['start'], s['end'], s['noload']),
              'ELF section not emitted as half-open interval')
    check({'code', 'data', 'bss'} <= {r.get('kind') for r in manifest['ranges']}, 'section kinds missing')
    check(all(next(r for r in manifest['ranges'] if r['name'] == name)['noload']
              for name in ('.bss', '.sqlite_bss')), 'BSS must be NOLOAD')
    log = ['LAYOUT real ELF/map/manifest + objdump PASS; code/data/BSS/NOLOAD half-open PASS']

    defines = gen.read_defines(ROOT / 'include/memmap.h')
    macros = gen.Macros(defines, symbols)
    for name in ('MEM_FIXED_PAGING_GUARD_BASE',):
        limit = macros.get(name)
        for delta in (-gen.PAGE, 0):
            slack = gen.chain_slack(limit + delta, limit)
            check((slack['start'], slack['end'], slack['pages']) ==
                  (limit + delta, limit, -delta // gen.PAGE), 'chain boundary control failed')
        try:
            gen.chain_slack(limit + gen.PAGE, limit)
        except ValueError as e:
            check('鎖の予算超過' in str(e), 'wrong chain failure')
        else:
            raise AssertionError('chain +1 page escaped budget assertion: ' + name)
        log.append('BOUNDARY ' + name + ': -1/0 page PASS; +1 page budget FAIL')

    for name in ('.bss', '.sqlite_bss'):
        check(not gen.layout_defects(manifest, sections, symbols), 'BSS control failed')
        broken = copy.deepcopy(manifest)
        broken['ranges'] = [r for r in broken['ranges'] if r['name'] != name]
        errors = gen.layout_defects(broken, sections, symbols)
        check(any('BSS/NOLOAD省略' in e for e in errors) and
              any('ELF区間の欠落/不一致: ' + name in e for e in errors), 'BSS omission escaped: ' + name)
        log.append('BSS ' + name + ': control PASS / omission assertion FAIL')
    check(not gen.layout_defects(manifest, sections, symbols), 'overlap control failed')
    broken = copy.deepcopy(manifest)
    r = next(r for r in broken['ranges'] if r['name'] == 'KAPI')
    # Enlarge one runtime interval by one page, retaining consistent size/pages.
    r.update(gen.layout_range(r['name'], r['start'] - gen.PAGE, r['end'], r['purpose']))
    errors = gen.layout_defects(broken, sections, symbols)
    check(any('半開区間の重複: KHEAP / KAPI' in e for e in errors), 'overlap escaped assertion')
    log.append('OVERLAP control PASS / one-page overlap assertion FAIL')

    with tempfile.TemporaryDirectory(prefix='os32-layout-') as tmp:
        root = make_tree(tmp, CLEAN)
        for rel in ('build/out/kernel.elf', 'build/out/kernel.map',
                    'build/out/kernel.bin', 'build/out/sqlite.bin', 'build/out/vmkernel.lz4',
                    'build/out/generations-manifest.json', 'build/out/memmap_layout.h', 'sdk/kapi.json'):
            dst = root / rel
            dst.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy(ROOT / rel, dst)
        for rel, old_text, new_text in (
                ('boot/boot_defs.h', '#define VK32_LOAD_END      LAYOUT_MEM_LAYOUT_LOAD_END',
                 '#define VK32_LOAD_END      0x2E9000UL'),
                ('boot/loader_fat_new.asm', 'VK32_LOAD_END       EQU LAYOUT_MEM_LAYOUT_LOAD_END',
                 'VK32_LOAD_END     EQU 0x2E9000')):
            check(run(script, root, '--manifest', '--check').returncode == 0, 'mirror control failed')
            path = root / rel
            text = path.read_text()
            # Avoid silently exercising an unchanged fixture if spacing changes.
            if old_text not in text:
                pattern = r'(^\s*(?:#define\s+)?VK32_LOAD_END\s+(?:EQU\s+)?)(0x[0-9A-Fa-f]+)'
                changed, count = re.subn(pattern, lambda m: m[1] + '0x2E9000', text, flags=re.M)
                check(count == 1, 'mirror mutation target missing')
            else:
                changed = text.replace(old_text, new_text, 1)
            path.write_text(changed)
            failed = run(script, root, '--manifest', '--check')
            check(failed.returncode == 1 and '写しのずれ' in failed.stderr and rel in failed.stderr,
                  'C/ASM mismatch escaped layout assertion: ' + failed.stderr)
            path.write_text(text)
            log.append('MIRROR ' + rel + ': control PASS / mismatch assertion FAIL')
        auto = run(script, root, '--manifest')
        explicit = run(script, root, '--manifest', '--chain-limit', 'MEM_FIXED_PAGING_GUARD_BASE')
        check(auto.returncode == explicit.returncode == 0, 'ceiling selection failed')
        check(json.loads(auto.stdout) == json.loads(explicit.stdout), 'explicit/auto ceiling differs')
        check(json.loads(auto.stdout)['chain_limit'] ==
              {'constant': 'MEM_FIXED_PAGING_GUARD_BASE', 'value': 0x3E0000}, 'wrong ceiling')
        check(run(script, root, '--manifest', '--check').returncode == 0, 'hash control failed')
        binary = root / 'build/out/kernel.bin'
        binary.write_bytes(binary.read_bytes() + b'\0')
        failed = run(script, root, '--manifest', '--check')
        check(failed.returncode == 1 and 'artifact mismatch: build/out/kernel.bin' in failed.stderr,
              'stale artifact hash escaped')
    return log + ['CEILING auto/explicit future constant PASS; stale artifact control PASS / mismatch FAIL']


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--mutate", action="store_true")
    args = ap.parse_args()

    for line in cases(SCRIPT):
        print("  " + line)
    print("gen_memmap PASS")
    if args.mutate:
        source = SCRIPT.read_text(encoding="utf-8")
        red = 0
        for name, (old, new) in MUTATIONS.items():
            if old not in source:
                raise SystemExit("変異 %s の当て先が見つからない" % name)
            with tempfile.TemporaryDirectory(prefix="os32-genmm-mut-") as tmp:
                mutated = pathlib.Path(tmp) / "gen_memmap.py"
                mutated.write_text(source.replace(old, new, 1), encoding="utf-8")
                try:
                    cases(mutated)
                    ok = True
                except (AssertionError, subprocess.SubprocessError) as e:
                    ok = False
                    why = str(e)
            print("  変異 %-12s %s" % (name, "RED (%s)" % why if not ok
                                       else "**GREEN — 試験が穴を見逃した**"))
            if not ok:
                red += 1
        print("%d/%d の変異が RED" % (red, len(MUTATIONS)))
        return 0 if red == len(MUTATIONS) else 1

    return 0


if __name__ == "__main__":
    sys.exit(main())
