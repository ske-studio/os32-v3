#include "appmem.h"
#include "kcg_module.h"
#include "kcg.h"
#include "v86_io.h"
#include "v86_mem.h"
/* ======================================================================== */
/*  KSELFTEST.C — カーネル内プリミティブの自己診断                          */
/*                                                                          */
/*  なぜカーネル内でやるのか:                                                */
/*    programs/tests/klibc_test.c は **newlib とリンクされる外部プログラム** */
/*    なので、そこで通る strlen/memcpy/malloc は newlib の実装であって       */
/*    カーネルの kstring_asm.asm / kmalloc.c ではない。つまりカーネルが      */
/*    実際に使うプリミティブはこれまで一度もテストされていなかった。         */
/*                                                                          */
/*  ここでは「境界ケースだけ」を見る。通常ケースはシェルが動いている時点で   */
/*  通っているので、n=0 / バッファぴったり / 重なりコピー / 二重解放 など、   */
/*  実際にバグが潜んでいた形だけを実機で毎回踏んで確認する。                 */
/*                                                                          */
/*  ブート時に 1 回走り、全部通れば 1 行だけ出す。落ちた項目は赤で名前を     */
/*  出す (実機で回帰した瞬間に画面で分かる)。                               */
/* ======================================================================== */

#include "kselftest.h"
#include "selftest_internal.h"
#include "kstack_hw.h"
#include "exec_heap.h"
#include "serial.h"
#include "pegc.h"
#include "gfx_hal.h"
#include "../gfx/gfx.h"
#include "kstring.h"
#include "kprintf.h"
#include "kmalloc.h"
#include "paging.h"
#include "lease.h"
#include "con_sink.h"
#include "pc98.h"
#include "tvram.h"
#include "console.h"
#include "kbd_inject.h"
#include "../drivers/input/kbd.h"          /* kbd_diag / KBD_CMD_* (POLICY_DEBUG §4-57) */
#include "appslot.h"
#include "launch.h"
#include "exec.h"
#include "fd_redirect.h"  /* fd_redirect_buf_write_ok (門はポインタの由来で決める) */
#include "kapi_db.h"
#include "cpu_calibrate.h"
#include "cpu_calibrate_math.h"
#include "idt.h"          /* pit_get_setup / PIT_MODE_TIMER0 */
#include "sysclk.h"       /* sysclk_hz / sysclk_detected */
#include "memmap.h"       /* PIT_HZ / MEM_DMA_POOL_* */
#include "vfs.h"
#include "dma_pool.h"     /* DMA プール (票 TASK_HAL_WIRING §1-3) */
#include "pcm_cs4231.h"
#include "dma8237.h"      /* 8237 の共通部 (同 §1-2) */
#include "irq.h"          /* 動的 IRQ 登録 (票 TASK_HAL_WIRING §1-1) */
#include "time_math.h"    /* time_clamped (票 §1-5) */
#include "ktime.h"        /* ktime_now: 実時計の単調性 */
#include "sys.h"          /* sys_time_now */
#include "io.h"           /* inp / irq_save */
#include "bootinfo.h"     /* ブート情報域 (票 TASK_HDD_INSTALL 段 0) */
#include "bootlog.h"      /* 起動ログ (/var/log/boot.log) */
#include "gui.h"          /* gui_ime_set_render (ime_set_render の門) */
#include "memory_boot.h"
#include "pgalloc.h"      /* pgalloc_model_state (T1a: legacy 経路の撤去) */
#include "unicode_file.h" /* private boot-table diagnostics; never a KAPI */

/* Data imports use accessors; include macros only after declarations. */
extern volatile int ring3_in_syscall;
#include "generated/kdebug_import_macros.h"

/* **`noinline` はカーネルの予算のため** (票 TASK_HAL_WIRING、A+B 合流後に
 * 残り 1.2KB まで詰まった)。-O2 はこの 6 行を呼び出し 194 か所すべてに
 * 展開していて、それだけで 6KB を食っていた。自己診断は起動時に 1 回しか
 * 走らないので速度は要らない。判定そのものは 1 バイトも変わらない。
 * (同じ理由の先例: kernel/time/cpu_calibrate.c の nop_loop) */
static void __attribute__((noinline)) check(int cond, const char *name)
{
    if (cond) {
        kdi_kselftest_count(1);
    } else {
        kdi_kselftest_count(0);
        kprintf(0xC1, "[selftest] FAIL: %s\n", name);
    }
}

#include "kselftest/mem.inc"

#include "kselftest/str.inc"

#include "kselftest/utoa.inc"

#include "kselftest/heap.inc"

#include "kselftest/kprintf.inc"

#include "kselftest/kprintf_attr.inc"

#include "kselftest/ring3_pd.inc"

#include "kselftest/map_user_keep.inc"

#include "kselftest/app_band_pde.inc"

#include "kselftest/con_sink_render_gate.inc"

#include "kselftest/kbd_inject.inc"

#include "kselftest/kbd_cmd.inc"

#include "kselftest/gfx_owner.inc"

#include "kselftest/tramp_user_str.inc"

#include "kselftest/memmap.inc"

/* ------------------------------------------------------------------------ */
/*  DMA プールの写像 (票 TASK_HAL_WIRING §1-3 / 受入 W5)                     */
/*                                                                          */
/*  プールは予約域 (NP) の中に開けた 64KB の穴で、present / supervisor /     */
/*  R/W でなければならない。**USER が立ってはいけない** — 立つと CPL=3 の   */
/*  アプリが装置の記述子 (CB/RFD、PCM リング) を書き換えられる。            */
/*                                                                          */
/*  この検査が意味を持つのは `memmap_seen_at` が MM_RW と MM_RWU を         */
/*  分けてからで、それより前は RW を見た時点で MM_RW を返していた。         */
/*  **見分けられることを毎回確かめる**ために、PTE 1 本に USER を立てて       */
/*  検査が落ちるところまで見て、戻す (往復 3 B6)。                          */
/* ------------------------------------------------------------------------ */
/* The boot ledger records the pool only when its entire range is RAM. */
static int selftest_dma_available(void)
{
    for (u32 i = 0; i < ledger_region_count; i++) {
        const struct ledger_region *r = &ledger_regions[i];
        if (r->type == LEDGER_R_DMA &&
            r->first == MEM_DMA_POOL_BASE / PAGE_SIZE &&
            r->end == (MEM_DMA_POOL_END + 1) / PAGE_SIZE) return 1;
    }
    return 0;
}

#include "kselftest/memmap_pool_user.inc"

#include "kselftest/pool_model.inc"

/* ------------------------------------------------------------------------ */
/*  所有権台帳 (票 docs/archive/v3/TASK_T1_LEDGER.md §4-2、T1b)                 */
/*                                                                          */
/*  (1) 通常文脈では割り込み / 例外の深さが 0 (§3-5。longjmp の控えの戻し  */
/*      忘れや IRQ_LEAVE の抜けがあると 0 に戻らない)。                     */
/*  (2) 不変条件 (eligible で allocated ⇔ owner ≠ 0、pages と L2 の一致、   */
/*      区間の非重複) — ledger_selfcheck("boot")。                          */
/*  (3) R5 (a): AS を作り、その owner でページを取り、壊して回収すると AS    */
/*      owner のページが 0 になり、番号を返せる。                           */
/*  (4) R5 (b): 永続 owner の総量はその間に変わらない。                     */
/*  件数 (R1) は 1 行に出す。値そのものは kernel.map の番地で読む。          */
/* ------------------------------------------------------------------------ */
/* Internal f5a transaction smoke test: real owner/lease AS and enabled IF. */
extern i32 kapi_mem_stat(i32 app_id, void *out, u32 size);
#include "kselftest/appmem.inc"

#include "kselftest/ledger.inc"

#include "kselftest/gfx_ledger.inc"

#include "kselftest/pcm.inc"

#include "kselftest/kapi_layout.inc"

#include "kselftest/kcg_display_return.inc"

int kselftest_run_post_unicode(void)
{
    u32 before = ksel_fail;
    test_kcg_display_return();
    u32 phys = unicode_file_debug_phys();
    int owned = (phys & (PAGE_SIZE - 1)) == 0;
    /* Reserved role 4 must never expose the table, with or without the file. */
    check(!ledger_surface_find(0, LEDGER_ROLE_UNICODE), "unicode:nonpublic");
    if (phys) {
        for (u32 i = 0; i < UNICODE_FILE_PAGES; i++)
            if (!pgalloc_page_owned(phys / PAGE_SIZE + i, LEDGER_OWNER_KERNEL))
                owned = 0;
        check(owned, "unicode:kernel owns 7 pages");
    }
    check(unicode_file_lookup(0x3041) == 0x2421 &&
          unicode_file_lookup(0x3093) == 0x2473 &&
          unicode_file_lookup(0x30A1) == 0x2521 &&
          unicode_file_lookup(0x30F6) == 0x2576, "unicode:kana endpoints");
    check(unicode_file_lookup(0x4E00) == (phys ? 0x306C : 0),
          "unicode:table or kana-only");
    check(unicode_file_lookup(0xFFFF) == 0 &&
          unicode_file_lookup(0x10000) == 0, "unicode:unmapped/BMP limit");
    return ksel_fail - before;
}

#include "kselftest/h4_backing.inc"

#include "kselftest/slack_cycle.inc"

int kselftest_run_post_exec(void)
{
    u32 before = ksel_fail;
    if (kdi_kaudit_skipped(KAUDIT_POST_EXEC)) return 0;

    check(kdi_kaudit_last_rc(KAUDIT_POST_EXEC) == 0, "post-exec: lifecycle audit");
    check(kst_lease() == 0, "post-exec: synthetic AS S/T/U");
    check((kstack_high_water() & 0xffffU) < MEM_KSTACK_TOP - MEM_KSTACK_BASE,
          "kstack bound");
    check(resident_heap_peak >= exec_heap_used(), "resident peak");
    test_tramp_user_str();
    test_kapi_layout();
    test_memmap();
    test_memmap_pool_user();
    test_pool_model();
    test_slack_cycle();
    test_ledger();
    test_h4_backing();
    test_appmem();
    test_gfx_ledger();
    test_pcm();

    if (ksel_fail != before) {
        kprintf(0xC1, "[selftest] %d FAILED after exec_init\n",
                ksel_fail - before);
    }
    return ksel_fail - before;
}

#include "kselftest/abort_admit.inc"

#include "kselftest/cpu_calibrate.inc"

#include "kselftest/bootinfo.inc"

#include "kselftest/pit_setup.inc"

#include "kselftest/dma_pool.inc"

#include "kselftest/dma8237.inc"

#include "kselftest/irq_dynamic.inc"

#include "kselftest/time_now.inc"

#include "kselftest/bootlog.inc"

#include "kselftest/ring3_wm_guard.inc"

#include "kselftest/ime_render_gate.inc"

#include "kselftest/fd_redirect_access_guard.inc"

int kselftest_run(void)
{
    kdi_kselftest_reset();

    test_mem();
    test_str();
    test_utoa();
    test_heap();
    test_kprintf();
    test_kprintf_attr();
    test_ring3_pd();
    test_map_user_keep();
    test_app_band_pde();
    check(kst_lease() == 0, "private lease S/T/U, master unchanged, owner zero");
    test_con_sink_render_gate();
    test_kbd_inject();
    test_kbd_cmd();
    test_gfx_owner();
    test_abort_admit();
    test_cpu_calibrate();
    test_pit_setup();
    test_bootinfo();
    test_bootlog();
    test_dma8237();
    if (selftest_dma_available()) test_dma_pool();
    else kprintf(0xA1, "[selftest] DMA pool skipped (RAM short)\n");
    test_irq_dynamic();
    test_time_now();
    test_ring3_wm_guard();
    test_fd_redirect_access_guard();
    test_ime_render_gate();

    if (ksel_fail == 0) {
        kprintf(0xA1, "[selftest] %d/%d passed\n", ksel_pass, ksel_pass);
    } else {
        kprintf(0xC1, "[selftest] %d FAILED (%d passed)\n",
                ksel_fail, ksel_pass);
    }
    return ksel_fail;
}

int kselftest_run_post_probe(void)
{
    u32 before = ksel_fail;
    check(kdi_kaudit_last_rc(KAUDIT_POST_PROBE) == 0, "post-probe: lifecycle audit");
    check(gfx_selected_selfcheck(), "post-probe: selected sources/bind/BB");
    return ksel_fail - before;
}

