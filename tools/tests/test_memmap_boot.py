#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""ブート順を実物 (kernel/paging.c + kernel/shm.c) で再生し、地図と PTE を照合する。

票 docs/archive/kernel_v21/TASK_KSTACK_USER.md §4 の 2・3 / 記録: tools/tests/memmap_tdd.md

  python3 -B tools/tests/test_memmap_boot.py [--mutate]

シナリオは 2 つ。どちらも __bss_end を -D で与えるだけで、コードは実物のまま。

  典型       決裁 D1 後の配置。地図と実物が完全に一致する (食い違い 0 本)
  予算ちょうど SHM 後方予約が **空** になる形。空は正しい状態で、逆転ではない
  予算超過    リンカの ASSERT (build/os32.ld) が本来ここまで来させない形。
             SHM 後方ガードがカーネル帯域を突き抜け、SQLite 帯の先頭ページを
             not-present にする。**自己診断はこれを 1 本の食い違いとして見る** —
             期待値が「帯の境界は固定番地」を守っているから見える

--mutate は**否定側**。この票の中心規則を 1 つずつ壊した版で、試験が RED になる
ことを見る。中心は「浮動番地の規則を固定番地の境界より優先させない」こと —
そこを崩すと自己診断そのものが穴を隠す。
"""
import host32
import mutpar
import os
import argparse
import pathlib
import re
import subprocess
import sys
import tempfile

ROOT = pathlib.Path(__file__).resolve().parents[2]
# build/config.mk の INC_KERNEL と同じ並び。ホスト側は模型のヘッダだけ先頭に足す。
KERNEL_INCLUDES = ['-I' + str(ROOT / p) for p in
                   ('.', 'include', 'arch/x86', 'platform/pc98',
                    'sdk/include', 'sdk/include/os32', 'kernel', 'drivers',
                    'net', 'fs', 'exec', 'gfx', 'lib', 'kapi', 'lib/sqlite3')]
HARNESS = ROOT / "tools/tests/memmap_boot_host.c"
TARGET_SRCS = [ROOT / "kernel/paging.c"]
SHM_SRC = ROOT / "kernel/shm.c"
FLAGS = ['-m32', '-march=i386', '-std=gnu11', '-ffreestanding', '-fno-pie',
         '-fno-stack-protector', '-Wall', '-Wextra', '-Werror']
# -O0 では KHEAP_BASE (&__bss_end 由来) が畳まれず、shm.c の STATIC_ASSERT が
# 「variably modified at file scope」になる。出荷と同じ -O2 で組む。
HOST_OPT = ['-O2']

# (name, resident chain end, rejected ranges, audit discrepancies)
CASES = [("typical", 0x280000, 0, 0), ("empty SLACK", 0x375000, 0, 0)]

MUTATIONS = {
    # 逆転を撥ねても数えない = 空振りが成功に見えていた元の姿
    "no-reject": ("kernel/paging.c",
                  "if (start > end) { paging_range_reject_count++; return -1; }",
                  "if (start > end) return -1;"),
    # 浮動番地 (SHM) の規則をカーネル帯域の外へも広げる。境界を固定番地として
    # 守らなくなるので、予算を超えた配置が「期待どおり」に見えてしまう。
    "band-unbounded": ("kernel/paging.c",
                       "    if (a >= KERNEL_LOAD_ADDR && a <= MEM_KERNEL_BAND_END) {",
                       "    if (a >= KERNEL_LOAD_ADDR) {"),
    "slack-old-np": ("kernel/paging.c",
        "        return MM_RW;                           /* SLACK */",
        "        return MM_NP;                           /* SLACK */"),
    # 食い違いを 1 本にまとめない = 区間の本数が意味を失う
    "fixed-remap-missing": ("kernel/paging.c",
        "paging_map_range(MEM_FIXED_PAGING_BASE, MEM_FIXED_PAGING_FRAME_END,",
        "paging_map_range(MEM_FIXED_PAGING_BASE, MEM_FIXED_PAGING_BASE,"),
    "fixed-user": ("kernel/paging.c",
        "MEM_FIXED_PAGING_BASE, PAGE_RW) != 0 ||",
        "MEM_FIXED_PAGING_BASE, PAGE_RW | PTE_USER) != 0 ||"),
    "fixed-wrong-frame": ("kernel/paging.c",
        "page_directory[i] = V2P(page_tables[i]) | PAGE_RW;",
        "page_directory[i] = (V2P(page_tables[i]) + PAGE_SIZE) | PAGE_RW;"),
    "fixed-not-wb": ("kernel/paging.c",
        "MEM_FIXED_PAGING_BASE, PAGE_RW) != 0 ||",
        "MEM_FIXED_PAGING_BASE, PAGE_RW | PTE_PCD) != 0 ||"),
    "fixed-failure-if-lost": ("kernel/paging.c",
        "        irq_restore(flags);\n        return; /* PG",
        "        (void)flags;\n        return; /* PG"),
    "fixed-reinit": ("kernel/paging.c",
        "if (pg_enabled) { irq_restore(flags); return; }",
        "if (0) { irq_restore(flags); return; }"),
    "fixed-no-irq-save": ("kernel/paging.c",
        "unsigned int flags = irq_save();", "unsigned int flags = 0x202U;"),
    "fixed-unconditional-sti": ("kernel/paging.c",
        "    pg_enabled = 1;\n    irq_restore(flags);",
        "    pg_enabled = 1;\n    _enable();"),
    "no-coalesce": ("kernel/paging.c",
                    "        for (j = i + 1; j < (MEM_DMA_POOL_END + 1) / PAGE_SIZE; j++) {",
                    "        for (j = i + 1; j < i + 1; j++) {"),
}


def check_shm_replay():
    """ハーネスの shm_init_replay() が kernel/shm.c の shm_init() の写しのままか。

    ホストの gcc 15 は (u32)&__bss_end を含む定数式を畳まないので shm.c を
    そのまま #include できない (クロスの gcc 13.2 は畳む)。写しにする以上、
    **本物が変わったらここで気づく**ようにしておく。"""
    body = re.search(r"void shm_init\(void\)\s*\{(.*?)\n\}", SHM_SRC.read_text(encoding="utf-8"), re.S)
    if not body:
        raise SystemExit("kernel/shm.c の shm_init() が見つからない")
    calls = re.findall(r"paging_\w+", body.group(1))
    replay = re.findall(r"paging_\w+", HARNESS.read_text(encoding="utf-8")
                        .split("shm_init_replay")[1].split("}")[0])
    if calls != replay:
        raise SystemExit("shm_init() の写しがずれている: 本物 %s / 写し %s\n"
                         "  tools/tests/memmap_boot_host.c の shm_init_replay() を直すこと"
                         % (calls, replay))
    boot = HARNESS.read_text(encoding="utf-8")
    assert boot.index('    shm_init_replay();') < boot.index('paging_boot_user_shared(tramp)') < boot.index('paging_addrspace_create(&as'), 'shared boot before AS replay'


def build_and_run(tmp, case, mutation=None, kb=8192, initial_if=0x202):
    name, bss, reject, bad = case
    tmp = pathlib.Path(tmp)
    sources = {}
    for src in TARGET_SRCS + [ROOT / "kernel/pgalloc.c",
                               ROOT / "tools/tests/layout_boot_fixture.h"]:
        sources[src] = src.read_text(encoding="utf-8")
    if mutation:
        rel, old, new = MUTATIONS[mutation]
        path = ROOT / rel
        text = sources[path]
        if old not in text:
            raise SystemExit("変異 %s の当て先が見つからない: %s" % (mutation, rel))
        sources[path] = text.replace(old, new)
    path = ROOT / 'kernel/paging.c'
    sources[path] = sources[path].replace('P2V_BOOT(', 'host_boot_paging_ptr(')
    if case[0] == 'PG-failure':
        path = ROOT / 'kernel/paging.c'
        sources[path] = sources[path].replace(
            "paging_map_range(MEM_FIXED_PAGING_BASE, MEM_FIXED_PAGING_FRAME_END,",
            "paging_map_range(MEM_FIXED_PAGING_BASE, MEM_FIXED_PAGING_BASE - PAGE_SIZE,")
    (tmp / "paging_host_source.c").write_text(sources[ROOT / "kernel/paging.c"],
                                              encoding="utf-8")
    alloc = sources[ROOT / "kernel/pgalloc.c"]
    alloc = alloc.replace('irq_save()', '0').replace('irq_restore(flags)', '(void)flags')
    (tmp / "pgalloc_host_source.c").write_text(alloc, encoding="utf-8")

    (tmp / "layout_boot_fixture.h").write_text(
        sources[ROOT / "tools/tests/layout_boot_fixture.h"])
    includes = ['-I' + str(ROOT / 'tools/tests/host_arch'), '-I' + str(tmp)] + KERNEL_INCLUDES
    exe = tmp / ("memmap_" + re.sub(r"\W", "", str(bss)))
    defines = ['-DPHYSMEM_HOST_TEST=1',
               f'-DHOST_RESIDENT_END=0x{bss:X}',
               '-DHOST_BSS_END=0x%X' % (0x180000),
               '-DHOST_EXPECT_REJECT=%d' % reject,
               '-DHOST_EXPECT_BAD=%d' % bad, f'-DHOST_RAM_KB={kb}', f'-DHOST_INITIAL_IF={initial_if}', f'-DHOST_EXPECT_INIT_FAIL={int(case[0] == "PG-failure")}']
    subprocess.run(['gcc', *FLAGS, *HOST_OPT, *defines, '-nostdlib', '-static', '-no-pie',
                    *includes, '-ffunction-sections', '-Wl,--gc-sections', str(HARNESS),
                    str(ROOT / 'kernel/physmem.c'), str(ROOT / 'kernel/kmalloc.c'),
                    str(ROOT / 'exec/exec_heap.c'),
                    '-o', str(exe)], check=True)
    return host32.run([str(exe)], capture_output=True, timeout=60)


def mutation_result(name):
    # **全部の場面で回す。** 1 場面だけだと、その場面では無害な変異を
    # 「試験が見逃した」ではなく「変異が効かなかった」として取り違える
    # (resv-blind は「予算ちょうど」でしか差が出ない)。
    ok = True
    where = ''
    for case in CASES + [("IF=0", *CASES[0][1:]), ("PG-failure", *CASES[0][1:])]:
        with tempfile.TemporaryDirectory(prefix='os32-memmap-') as tmp:
            try:
                out = build_and_run(tmp, case, mutation=name, initial_if=2 if case[0] == "IF=0" else 0x202)
                if out.returncode != 0:
                    ok, where = False, case[0]
                    break
            except subprocess.CalledProcessError:
                return name, 'NOT COUNTED: compile failure', False
    return name, ('RED (%s で落ちた)' % where if not ok
                  else '**GREEN — 試験が穴を見逃した**'), not ok


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--mutate', action='store_true')
    args = ap.parse_args()

    check_shm_replay()

    for kb in (8088, 8192, 17408, 65536):
        for initial_if in (2, 0x202):
            with tempfile.TemporaryDirectory(prefix='os32-fixed-paging-') as tmp:
                out = build_and_run(tmp, CASES[0], kb=kb, initial_if=initial_if)
                if out.returncode:
                    raise SystemExit(f'FAILED {kb}KB IF={initial_if}: {out.stdout!r} rc={out.returncode}')
            print(f'fixed paging + shell heap PASS {kb}KB IF={initial_if}')

    for initial_if in (2, 0x202):
        with tempfile.TemporaryDirectory(prefix='os32-fixed-fail-') as tmp:
            out = build_and_run(tmp, ('PG-failure', *CASES[0][1:]), initial_if=initial_if)
            if out.returncode:
                raise SystemExit(f'FAILED pre-PG failure IF={initial_if}: {out.stdout!r}')
        print(f'pre-PG failure restores IF={initial_if} PASS')

    for case in CASES:
        with tempfile.TemporaryDirectory(prefix='os32-memmap-') as tmp:
            out = build_and_run(tmp, case)
            text = out.stdout.decode('utf-8', 'replace')
            print('--- %s (__bss_end=0x%06X)' % (case[0], case[1]))
            for line in text.strip().split('\n'):
                if line.strip():
                    print('    ' + line)
            if out.returncode != 0:
                sys.stderr.write(text)
                sys.stderr.write(out.stderr.decode('utf-8', 'replace'))
                raise SystemExit('FAILED: %s' % case[0])

    # 出荷するカーネルと同じフラグでも通ること (build/config.mk の KERNEL_CFLAGS)。
    # -Werror は付けない — 本番が付けていないし、shm.c の STATIC_ASSERT は
    # (u32)&__bss_end を含むので "variably modified at file scope" の警告が出る
    # (コンパイラが畳めないだけで、値としては正しい)。
    target = ['-std=gnu11', '-m32', '-march=i386', '-ffreestanding', '-fno-pie',
              '-fno-stack-protector', '-O2', '-Wall', '-D__KERNEL_BUILD__']
    with tempfile.TemporaryDirectory(prefix='os32-memmap-') as tmp:
        for src in ('kernel/paging.c', 'kernel/shm.c'):
            subprocess.run(['i386-elf-gcc', *target,
                            *KERNEL_INCLUDES,
                            '-c', str(ROOT / src),
                            '-o', str(pathlib.Path(tmp) / (src.replace('/', '_') + '.o'))],
                           check=True)
    print('HOST ILP32 + TARGET GNU11 PASS')
    if args.mutate:
        red = 0
        for name, verdict, detected in mutpar.run_ordered(mutation_result, MUTATIONS):
            print('  変異 %-14s %s' % (name, verdict))
            red += detected
        print('%d/%d の変異が RED' % (red, len(MUTATIONS)))
        return 0 if red == len(MUTATIONS) else 1

    return 0


if __name__ == '__main__':
    sys.exit(main())
