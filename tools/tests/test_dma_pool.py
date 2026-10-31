"""DMA プールの表 (mm/dma_pool_math.c) のホスト試験。

記録: tools/tests/dma_pool_tdd.md
票  : docs/tasks/v3/TASK_HAL_WIRING.md §1-3 (DMA プール)、
      docs/archive/v3/TASK_T1_LEDGER.md §3-7 / §4-3 (T1c: dma_alloc の limit、
      {pa, va} の組、失敗時 *out 不変、R4 の最悪の並び)

実物の mm/dma_pool_math.c と lib/dma_contract.c を 1 行も写さずに
#include して回す。表を触るだけなので I/O も割り込みも要らない。

**NP21/W では踏めない**のは 64KB またぎの飛ばし。エミュレータは 8237 の
折り返しを模擬しないので、またいだ配置でも「動いて見える」(§4-51 と同じ型)。
番地は実物の MEM_DMA_POOL_BASE を使う。
64KB 境界を跨ぐ池は合成入力 (0x2E8000) で残し、実物の池の 64KiB 整列も確かめる。

  python3 -B tools/tests/test_dma_pool.py            # ホストで全ケース
  python3 -B tools/tests/test_dma_pool.py --target   # + i386-elf で実物を通す
  python3 -B tools/tests/test_dma_pool.py --mutate   # 否定側
"""
CHECK = dict(name='dma-pool-host', tier=1,
    covers=['drivers/dma/dma8237_math.c', 'mm/dma_pool.c', 'mm/dma_pool_math.c', 'tools/tests/dma_pool_host.c', 'tools/tests/dma_boot_host.c', 'init/kernel.c', 'mm/dma_pool.h', 'include/memmap.h', 'drivers/block/fdc.c', 'drivers/block/fdc.h', 'kdebug/kselftest.c', 'kdebug/kselftest/**', 'mm/pgalloc.h', 'kernel/module/module.h', 'kernel/sqlite_engine.h', 'drivers/base/dev.h', 'fs/vfs.h', 'include/pc98.h', 'init/boot_platform.h', 'init/boot_platform.c', 'init/boot_display.h', 'init/boot_display.c', 'platform/pc98/boot.h', 'platform/pc98/boot.c', 'include/dma_contract.h', 'lib/dma_contract.c', 'tools/tests/kernel_includes.py', 'tools/tests/kernel_boot_source.py', 'init/kernel_boot_cpu.inc', 'init/kernel_boot_memory.inc', 'init/kernel_gfx_config.inc', 'init/kernel_gui_shlib.inc', 'init/kernel_shell_fdd.inc'],
    needs=[], budget=2.26078)

from kernel_boot_source import kernel_source
from kernel_includes import include_flags
import os
import pathlib
import re
import subprocess
import sys
import tempfile

ROOT = pathlib.Path(__file__).resolve().parents[2]
HARNESS = ROOT / "tools/tests/dma_pool_host.c"
SRC = ROOT / "mm/dma_pool_math.c"
# dma_range_ok (dma_alloc と FDC の BSS が共有する検査) は独立契約にある。
# 変異の 4 番目の要素でこちらを指す (無ければ SRC)。
D8237 = ROOT / "lib/dma_contract.c"
TARGET_SRCS = ["mm/dma_pool_math.c", "mm/dma_pool.c",
               "lib/dma_contract.c"]

CASES = ["layout", "sizes", "skip_boundary", "alignment", "frees",
         "leaked", "exhaust", "range_ok", "limit", "keep_out", "worst"]

FLAGS = ["-std=gnu11", "-Wall", "-Wextra", "-Werror",
         "-D__cdecl="]
INCLUDES = include_flags(ROOT)

MUTATIONS = [
    (r"        if \(!dma_range_ok\(addr, bytes, limit\)\) continue;\n",
     "        (void)limit;\n",
     "64KB またぎ・limit 越えの候補を飛ばさない (0x2F0000 を跨いだ番地を装置へ渡す)"),
    (r"        if \(!dma_range_ok\(addr, bytes, limit\)\) continue;",
     "        if (!dma_range_ok(addr, bytes, limit)) return DMA_POOL_ERR_NOSPC;",
     "またぐ候補で**探索を諦める** (後半が空いていても取れない)"),
    (r"    if \(dma_crosses_64k\(pa, size\)\) return 0;\n", "",
     "dma_range_ok が 64KB またぎを見ない (FDC の BSS の検査も素通し)", D8237),
    (r"    if \(pa >= limit \|\| size > limit - pa\) return 0;",
     "    if (pa >= limit) return 0;",
     "limit を先頭だけで見る (終端が 16MB を越える番地を渡す)", D8237),
    (r"    if \(pa >= limit \|\| size > limit - pa\) return 0;\n",
     "    (void)limit;\n",
     "limit を見ない (8237 が届かない番地を渡す)", D8237),
    (r"    rc = dma_pool_state_alloc\(s, size, align, limit, &pa\);\n"
     r"    if \(rc != 0\) return rc;\n"
     r"    out->pa = pa;",
     "    rc = dma_pool_state_alloc(s, size, align, limit, &out->pa);\n"
     "    if (rc != 0) return rc;\n"
     "    pa = out->pa;",
     "内部の出力を *out に直接つなぐ (失敗時に out->pa = 0 が漏れる — Codex P3)"),
    (r"    out->va = P2V\(pa\);\n", "",
     "組の va を埋めない (装置の番地と CPU の番地が食い違う)"),
    (r"    rc = dma_pool_state_alloc\(s, size, align, limit, &pa\);",
     "    (void)limit;\n"
     "    rc = dma_pool_state_alloc(s, size, align, DMA_PHYS_LIMIT, &pa);",
     "呼び手の limit を捨てて 16MB で探す"),
    (r"    if \(bytes < DMA_POOL_MIN_BYTES \|\| bytes > DMA_POOL_MAX_BYTES\)\n"
     r"        return DMA_POOL_ERR_ARG;\n", "",
     "33KB〜64KB を受ける (空の池でも必ず失敗するのに呼び手を待たせる)"),
    (r"    if \(off % MEM_PAGE_SIZE != 0\) \{ s->bad_free\+\+; return DMA_POOL_ERR_ARG; \}\n\n    page = off / MEM_PAGE_SIZE;\n    slot = dma_pool_span_at\(s, page\);\n    if \(slot < 0\) \{ s->bad_free\+\+; return DMA_POOL_ERR_ARG; \}",
     "    if (off % MEM_PAGE_SIZE != 0) { s->bad_free++; return DMA_POOL_ERR_ARG; }\n\n    page = off / MEM_PAGE_SIZE;\n    slot = dma_pool_span_at(s, page);\n    if (slot < 0) { slot = 0; }",
     "span の先頭でなくても解放する (隣の span を巻き添えに外す)"),
    (r"    if \(s->span\[slot\].state != DMA_SPAN_USED\) \{\n        s->bad_free\+\+;\n        return DMA_POOL_ERR_ARG;\n    \}\n",
     "",
     "LEAKED / 二重解放を通す (装置がまだ書いているページを配り直す)"),
    (r"    s->span\[slot\].state = DMA_SPAN_LEAKED;\n    s->leaked\+\+;",
     "    s->span[slot].state = DMA_SPAN_FREE;\n    s->leaked++;",
     "mark_leaked が空きに戻す (止まった証拠の無いページを再利用する)"),
    (r"        if \(addr % align != 0\) continue;\n", "",
     "整列の要求を無視する"),
    (r"    if \(!dma_pool_align_ok\(align\)\) return DMA_POOL_ERR_ARG;\n",
     "    (void)dma_pool_align_ok(align);\n",
     "2 の冪でない整列を受ける (剰余の意味が壊れる)"),
    (r"    if \(!s \|\| !s->ready\) return DMA_POOL_ERR_STATE;\n    if \(bytes < DMA_POOL_MIN_BYTES",
     "    if (!s) return DMA_POOL_ERR_STATE;\n    if (bytes < DMA_POOL_MIN_BYTES",
     "dma_pool_init より前に配る (写像が張られていないページを装置へ渡す)"),
]


def host_build(tmp, source_text=None, d8237_text=None):
    src_dir = pathlib.Path(tmp)
    exe = src_dir / "dma-pool-host"
    cmd = ["gcc", *FLAGS, *INCLUDES, str(HARNESS), "-o", str(exe)]
    if source_text is not None or d8237_text is not None:
        if source_text is None:
            source_text = SRC.read_text(encoding="utf-8")
        d8237 = D8237
        if d8237_text is not None:
            d8237 = src_dir / "dma_contract.c"
            d8237.write_text(d8237_text, encoding="utf-8")
        mut = src_dir / SRC.relative_to(ROOT).parent
        mut.mkdir(parents=True, exist_ok=True)
        (mut / "dma_pool_math.c").write_text(source_text, encoding="utf-8")
        (mut / "dma_pool.h").write_text(
            (ROOT / "mm/dma_pool.h").read_text(encoding="utf-8"),
            encoding="utf-8")
        shim = src_dir / "harness.c"
        # 変異させた写しを先に引かせる。共通実体も別に差し替えられるので、
        # **絶対パスに直す** — 相対のままだと tmp からは辿れず、
        # 全部の変異が「コンパイルできない」で RED に見えてしまう。
        shim.write_text(
            HARNESS.read_text(encoding="utf-8")
            .replace('"../../mm/dma_pool_math.c"', '"mm/dma_pool_math.c"')
            .replace('"../../lib/dma_contract.c"',
                     '"%s"' % d8237),
            encoding="utf-8")
        cmd = ["gcc", *FLAGS, "-I" + str(src_dir), "-I" + str(mut),
               *include_flags(ROOT), str(shim), "-o", str(exe)]
    subprocess.run(cmd, cwd=ROOT, check=True)
    return exe


def boot_gate(tmp):
    d = pathlib.Path(tmp)
    kernel = kernel_source(ROOT)
    start = kernel.index('    if (!memory_boot_init(mem_kb))')
    end = kernel.index('    /* FDリダイレクト初期化', start)
    gate = '    struct boot_context boot = {0};\n' + kernel[start:end].replace('for (;;) { _stop(); }', 'longjmp(halt, 1);')
    mount_helper = re.search(
        r"static inline void kernel_mount_children\(const char \*root_dev\)\n\{.*?\n}\n",
        kernel, re.S).group()
    (d / "kernel_dma_gate.c").write_text(
        '#include "kprintf.h"\n' + mount_helper +
        'static void host_kernel_dma(u32 mem_kb) {\n' + gate + '\n}\n')
    selftests = (ROOT / "kdebug/kselftest.c").read_text()
    helper = re.search(r"static int selftest_dma_available\(void\).*?\n}\n", selftests, re.S).group()
    gate = selftests[selftests.index('    if (selftest_dma_available()) test_dma_pool();'):]
    gate = gate[:gate.index('    test_irq_dynamic();')]
    (d / "selftest_dma_gate.c").write_text(
        helper + 'static void host_selftest_dma(void) {\n' + gate + '}\n')
    (d / "dma_pool_source.c").write_text((ROOT / "mm/dma_pool.c").read_text())
    (d / "io.h").write_text(
        'static unsigned int irq_save(void) { return 0; }\n'
        'static void irq_restore(unsigned int f) { (void)f; }\n')
    exe = d / "dma-boot-host"
    subprocess.run(["gcc", *FLAGS, "-I" + str(d), *INCLUDES,
                    str(ROOT / "tools/tests/dma_boot_host.c"), "-o", str(exe)], check=True)
    for case in ("ready", "short", "conflict", "nodebug"):
        subprocess.run([str(exe), case], check=True)
    # FDC's early boot buffer must stay independent of the pool.
    fdc = (ROOT / "drivers/block/fdc.c").read_text()
    assert 'static u8 s_fdbuf[FDC_BUF_BYTES]' in fdc
    assert not re.search(r'\bdma_(?:alloc|free)\s*\(', fdc)
    assert kernel.index('boot_platform_run(BOOT_FLOPPY, &boot)') < start
    print("BOOT reservation/init/selftest/probe, short RAM, conflict, FDC PASS")


def run_cases(exe, cases):
    failed = 0
    for case in cases:
        rc = subprocess.run([str(exe), case], cwd=ROOT).returncode
        print(f"EXIT {case}={rc}", flush=True)
        failed += rc != 0
    print(f"SUMMARY {len(cases) - failed}/{len(cases)} PASS", flush=True)
    return failed


def build_target(tmp):
    for rel in TARGET_SRCS:
        cmd = ["i386-elf-gcc", "-std=gnu11", "-m32", "-march=i386",
               "-ffreestanding", "-fno-pie", "-fno-stack-protector", "-nostdlib",
               "-mno-red-zone", "-fcommon", "-fsigned-char", "-fno-short-enums",
               "-O2", "-Wall", "-Werror",
               "-D__KERNEL_BUILD__",
                *include_flags(ROOT),
               "-c", str(ROOT / rel),
               "-o", str(pathlib.Path(tmp) / (rel.replace("/", "_") + ".o"))]
        subprocess.run(cmd, cwd=ROOT, check=True)
    print("TARGET i386-elf GNU11 -Werror PASS", flush=True)


def mutate(tmp):
    bad = 0
    for i, (pattern, repl, why, *where) in enumerate(MUTATIONS, 1):
        target = where[0] if where else SRC
        original = target.read_text(encoding="utf-8")
        assert len(re.findall(pattern, original)) == 1, why
        mutated, n = re.subn(pattern, repl, original, count=1)
        if n != 1:
            print(f"MUTATION {i} NOT APPLICABLE: {why}", flush=True)
            bad += 1
            continue
        try:
            if target == SRC:
                exe = host_build(tmp, mutated)
            else:
                exe = host_build(tmp, None, mutated)
        except subprocess.CalledProcessError:
            # 組み立ての失敗は RED に数えない (実行時の検査で落ちることを見る)。
            print(f"MUTATION {i} COMPILE ERROR (RED に数えない): {why}",
                  flush=True)
            bad += 1
            continue
        hits = sum(subprocess.run([str(exe), c], cwd=ROOT,
                                  stderr=subprocess.DEVNULL).returncode != 0
                   for c in CASES)
        status = "RED" if hits else "**GREEN (見逃し)**"
        print(f"MUTATION {i} {status} ({hits} 件): {why}", flush=True)
        bad += not hits
    return bad


if __name__ == "__main__":
    args = sys.argv[1:]
    with tempfile.TemporaryDirectory(prefix="os32-dma-pool-") as tmp:
        boot_gate(tmp)
        exe = host_build(tmp)
        print("HOST GNU11 -Werror compile PASS (real mm/dma_pool_math.c)",
              flush=True)
        if "--target" in args:
            build_target(tmp)
        rc = run_cases(exe, [a for a in args if not a.startswith("--")] or CASES)
        if "--mutate" in args:
            rc += mutate(tmp)
        sys.exit(bool(rc))


def run(ctx) -> int:
    import os
    import shlex
    import subprocess
    base_env = dict(os.environ, HOST32_RUNNERS=' '.join(ctx.runners))
    env = dict(base_env)
    argv = ['python3', '-B', 'tools/tests/test_dma_pool.py', '--target', *shlex.split(('--mutate' if ctx.mutate else ''))]
    rc = subprocess.call(argv, cwd=ctx.root, env=env, close_fds=False)
    if rc:
        return rc
    return 0
