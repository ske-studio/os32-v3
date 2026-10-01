#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""tools/gen_memmap.py — 重なり・逆転の検出と、生成物の鮮度照合。

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
  no-overlap   重なりを見ない        → 壊れた地図を「矛盾なし」と言う
  no-reversed  逆転を見ない          → 空振りする範囲指定を見逃す
  guess-map    kernel.map が無いとき __bss_end を決め打ちにする
               → それらしい嘘の地図を出す (今回とまったく同じ失敗)
  no-mirror    リンカスクリプト / NASM / SDK に散らばった写しを照合しない
               → memmap.h だけ直して他が古いまま通る
"""
import argparse
import pathlib
import re
import importlib.util
import shutil
import subprocess
import sys
import tempfile

ROOT = pathlib.Path(__file__).resolve().parents[2]
SCRIPT = ROOT / "tools/gen_memmap.py"

# 予算 MEM_KERNEL_IMAGE_MAX = 0x95000 (KHEAP 192KB、2026-09-23) → 上限は __bss_end = 0x195000。
BROKEN = 0x1A0000        # 予算超過。SHM がカーネル帯域を突き抜ける
CLEAN = 0x140000         # 予算内。重なりも逆転も無い

MAP_TEMPLATE = (
    "                0x%08x                        __bss_end = .\n"
    "                0x00200000                        __sqlite_start = .\n"
    "                0x002bc060                        __sqlite_end = .\n")

MUTATIONS = {
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
    shutil.copy(ROOT / "include/memmap.h", root / "include/memmap.h")
    shutil.copy(ROOT / "docs/02_memory.md", root / "docs/02_memory.md")
    for rel in MIRROR_FILES:
        dst = root / rel
        dst.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy(ROOT / rel, dst)
    if bss_end is not None:
        (root / "build/out/kernel.map").write_text(MAP_TEMPLATE % bss_end,
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
        # 先に --write して鮮度ずれを消す。こうしておけば、このあとの
        # --check が非ゼロで終わる理由は **矛盾の検出だけ** になる。
        check(run(script, root, "--write").returncode == 0, "--write が失敗した")
        out = run(script, root, "--check")
        check(out.returncode != 0, "壊れた地図なのに 0 で終わった")
        check("古い" not in out.stdout, "鮮度ずれが残っていて矛盾の検出を見ていない")
        # 「矛盾の報告」の行だけを見る (表や差分に同じ語が出るので前置きで絞る)
        found = [x for x in out.stdout.split("\n") if x.startswith("gen_memmap: ")]
        rev = [x for x in found if x.startswith("gen_memmap: 逆転:")]
        ovl = [x for x in found if x.startswith("gen_memmap: 重なり:")]
        check(len(rev) == 1, "SHM 後方予約の逆転を 1 件報告しない: %r" % rev)
        check(any("共有メモリ本体" in x and "SQLite" in x for x in ovl),
              "SHM 帯と SQLite 帯の重なりを報告しない: %r" % ovl)
        bud = [x for x in found if x.startswith("gen_memmap: 予算超過:")]
        check(len(bud) == 1, "カーネル本体の予算超過を報告しない: %r" % bud)
        log.append("BROKEN  --check = %d (予算超過 1 / 逆転 %d / 重なり %d)"
                   % (out.returncode, len(rev), len(ovl)))

    # --- 2. 直した地図: --write して --check が 0 で終わる ---
    with tempfile.TemporaryDirectory(prefix="os32-genmm-") as tmp:
        root = make_tree(tmp, CLEAN)
        w = run(script, root, "--write")
        check(w.returncode == 0, "--write が失敗した: " + w.stderr)
        doc = (root / "docs/02_memory.md").read_text(encoding="utf-8")
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

        # --- 3. 生成ブロックを手で触ったら鮮度検査が落ちる ---
        (root / "docs/02_memory.md").write_text(
            doc.replace("0x140000", "0x999999", 1), encoding="utf-8")
        out = run(script, root, "--check")
        check(out.returncode != 0, "生成ブロックを書き換えても気づかない")
        check("古い" in out.stdout, "鮮度ずれだと言わない")
        log.append("STALE   --check = %d (鮮度ずれを検出)" % out.returncode)

    # --- 4. 写しがずれたら気づく (リンカスクリプト / NASM / SDK) ---
    with tempfile.TemporaryDirectory(prefix="os32-genmm-") as tmp:
        root = make_tree(tmp, CLEAN)
        check(run(script, root, "--write").returncode == 0, "--write が失敗した")
        check(run(script, root, "--check").returncode == 0, "素の状態で落ちた")
        ld = root / "build/os32.ld"
        ld.write_text(ld.read_text(encoding="utf-8")
                      .replace("MEM_KSTACK_TOP       = 0x2FFFFC;",
                               "MEM_KSTACK_TOP       = 0x1FFFFC;"),
                      encoding="utf-8")
        out = run(script, root, "--check")
        check(out.returncode != 0, "os32.ld の写しがずれても気づかない")
        check("写しのずれ" in out.stdout and "MEM_KSTACK_TOP" in out.stdout,
              "ずれた場所と基準を名指ししない: %r" % out.stdout)
        # ASM が数値直書きに戻ったら気づく
        root2 = make_tree(tempfile.mkdtemp(prefix="os32-genmm-asm-"), CLEAN)
        asm = root2 / "kernel/kentry.asm"
        asm.write_text(asm.read_text(encoding="utf-8")
                       .replace("mov     esp, MEM_KSTACK_TOP",
                                "mov     esp, 002FFFFCh"), encoding="utf-8")
        out = run(script, root2, "--check")
        check(out.returncode != 0, "kentry.asm の数値直書きに気づかない")
        check("kentry.asm" in out.stdout, "どのファイルか言わない")
        shutil.rmtree(root2, ignore_errors=True)
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
    return log


def fixed_paging_cases(script):
    """地図と実リンカを一緒に検査。Cのコンパイル拒否をREDに数えない。"""
    spec = importlib.util.spec_from_file_location('genmm_test', script)
    gen = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(gen)
    changes = (
        ('old-512k-heap', 'MEM_SHELL_HEAP_SIZE', '0x080000UL'),
        ('unaligned-paging', 'MEM_FIXED_PAGING_BASE', '0x3F1001UL'),
        ('one-page-overlap', 'MEM_SHELL_HEAP_END', '(MEM_FIXED_PAGING_BASE + MEM_GUARD_SIZE)'),
        ('boot-count', 'MEM_FIXED_BOOT_PT_COUNT', '7'),
    )
    for label, name, expr in changes:
        with tempfile.TemporaryDirectory(prefix='os32-fixed-map-') as tmp:
            root = make_tree(tmp, CLEAN)
            header = root / 'include/memmap.h'
            header.write_text(re.sub(r'(^#define\s+' + name + r'\s+).*$',
                                     lambda m: m[1] + expr, header.read_text(), flags=re.M))
            # 写し一致の偶然の拒否で済ませず、境界の検査だけでも拒否させる。
            macros = gen.Macros(gen.read_defines(header), {'__bss_end': CLEAN,
                                '__sqlite_start': 0x200000, '__sqlite_end': 0x2bc060})
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
            check(link.returncode != 0 and ('fixed paging' in link.stderr or 'shell heap' in link.stderr),
                  label + ' escaped linker ASSERT: ' + link.stderr)
    with tempfile.TemporaryDirectory(prefix='os32-fixed-link-') as tmp:
        root = make_tree(tmp, CLEAN)
        check(link_fixture(root).returncode == 0, 'baseline linker failed')
        # SQLite 末尾が旧PD案0x2DD000への上限を越えても、既存DMA予算内は許可。
        check(link_fixture(root, sqlite_end=0x2c0000).returncode == 0, 'SQLite growth restricted')
        out = link_fixture(root, sqlite_end=0x2c7001)
        check(out.returncode != 0 and 'SQLite band overruns DMA pool' in out.stderr,
              'SQLite exceeded DMA budget without rejection')
        ld = root / 'build/os32.ld'
        ld.write_text(ld.read_text().replace('MEM_FIXED_PD_BASE = 0x3F1000;',
                                             'MEM_FIXED_PD_BASE = 0x3F2000;'))
        check(run(script, root, '--write').returncode == 0, 'write failed')
        out = run(script, root, '--check')
        check(out.returncode != 0 and '写しのずれ' in out.stdout, 'fixed paging mirror mismatch escaped')
    return ['FIXED   map/link rejected 512KiB heap / unaligned / overlap / count; mirror rejected',
            'SQLITE  growth past old PT limit accepted; DMA budget excess rejected']


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
    return subprocess.run([str(ld), '-T', 'build/os32.ld', 'kernel/kentry.o',
                           'lib/sqlite3/sqlite3.o', '-o', 'build/out/fixture.elf'],
                          cwd=root, capture_output=True, text=True)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--mutate", action="store_true")
    args = ap.parse_args()

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

    for line in cases(SCRIPT):
        print("  " + line)
    print("gen_memmap PASS")
    return 0


if __name__ == "__main__":
    sys.exit(main())
