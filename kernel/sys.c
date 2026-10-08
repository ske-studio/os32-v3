/* ======================================================================== */
/*  SYS.C — システム制御およびブザー制御                                      */
/* ======================================================================== */

#include "sys.h"
#include "pgalloc.h"
#include "memmap.h"
#include "io.h"
#include "pc98.h"
#include "rtc.h"
#include "os_time.h"

/* 管理する物理 RAM の末尾 (バイト)。legacy アリーナの上端そのもの。 */
static u32 sys_phys_end(void);

void sys_reboot(void)
{
    /* PC-98 ハードウェアリセット (FreeBSD実装準拠) */
    outp(SYSPORT_C_BSR, BSR_SHUT0_SET);  /* SHUT0 = 1 */
    outp(SYSPORT_C_BSR, BSR_SHUT1_SET);  /* SHUT1 = 1 */
    outp(CPU_RESET_PORT, 0x00);           /* CPUリセット */
    /* ここには来ない */
    for (;;) { _halt(); }
}

void sys_halt(void)
{
    _halt();
}

/* 内蔵ブザー。0037h の BSR で 0035h bit3 (BUZ) だけを操作する。
 * **極性は UNDOCUMENTED** (bit3 = 1 で停止、06h = 鳴動 / 07h = 停止)。
 * 2026-09-24 までは pc98.h が Bible の向きで名前を付けていたので、
 * buz_off() は実機で**鳴らし**、buz_on() は止めていた (rshell は応答の
 * たびに buz_off() を呼ぶ — POLICY_DEBUG §4-59)。 */
void buz_on(void)
{
    outp(SYSPORT_C_BSR, BSR_BUZ_ON);
}

void buz_off(void)
{
    outp(SYSPORT_C_BSR, BSR_BUZ_OFF);
}

u32 sys_mem_kb = 1024; /* 初期値(1MB) */

u32 sys_get_mem_kb(void)
{
    return sys_mem_kb;
}

static u32 sys_frozen_exec, sys_frozen_end;
static int sys_model_staged;

int sys_memory_bootstrap_model(struct physmem *m, const struct pgalloc_layout *l,
                               int (*verify)(u32, u32, void *))
{
    u32 top, bytes, count, minimum;
    struct pgalloc_layout layout;
    unsigned int flags;
    int ok;
    flags = irq_save();
    ok = 0;
    if (sys_frozen_end || !m || !l || !verify ||
        !paging_boot_context()) goto done;
    /* Backing is about to be zeroed; retain no borrowed layout pointer. */
    layout = *l;
    l = &layout;
    top = physmem_legacy_end(m);
    bytes = pgalloc_metadata_bytes(m);
    minimum = (MEM_EXEC_BOOT_MIN) / PAGE_SIZE;
    if (!bytes || top != m->legacy_ceiling || top > PHYSMEM_LEGACY_MAX_PFN)
        goto done;
    /* layout の検査は backing の種類で分ける (TASK_T1_LEDGER §3-3、B2)。
     * ARENA_TOP: metadata は legacy 上端に接し、workspace はその直下で exec の
     * 最小域より上。FIXED: 置き場は memmap.h の固定区間 (pgalloc が検査) で、
     * exec はアリーナ全体 = 低位 RAM の上端まで使うので、その上端が exec の
     * 最小域を満たすことだけを見る。 */
    if (l->kind == PGALLOC_BACKING_ARENA_TOP) {
        if (l->metadata_first >= top ||
            bytes / PAGE_SIZE != top - l->metadata_first ||
            l->workspace_end != l->metadata_first ||
            l->workspace_first < minimum) goto done;
    } else if (l->kind == PGALLOC_BACKING_FIXED) {
        if (top < minimum) goto done;
    } else goto done;
    /* 検証すべきは metadata / workspace の写像だけで、それは
     * pgalloc_init_layout が verify を通して行う。 */
    (void)count;
    if (!pgalloc_init_layout(m, l, verify)) goto done;
    /* exec の上端はアリーナの上端から決める (B2)。ARENA_TOP では
     * workspace_first と同じ値、FIXED では低位 RAM の上端 (backing の位置と
     * 無関係)。 */
    sys_frozen_exec = pgalloc_arena_end() * PAGE_SIZE;
    sys_frozen_end = top * PAGE_SIZE;
    sys_model_staged = 1;
    ok = 1;
done:
    irq_restore(flags);
    return ok;
}

int sys_memory_stage_online(void)
{
    return sys_model_staged && pgalloc_stage_online();
}

int sys_memory_init_model(struct physmem *m, void *backing, u32 capacity,
                          u32 first, int (*verify)(u32, u32, void *))
{
    u32 bytes, top, count, minimum;
    unsigned int flags;
    int ok;
    flags = irq_save();
    ok = 0;
    if (sys_frozen_end || !m || !verify) goto done;
    bytes = pgalloc_metadata_bytes(m);
    if (!bytes) goto done;
    top = physmem_legacy_end(m);
    minimum = (MEM_EXEC_BOOT_MIN) / PAGE_SIZE;
    if (top != m->legacy_ceiling || first < minimum || first >= top ||
        bytes / PAGE_SIZE != top - first ||
        top > PHYSMEM_LEGACY_MAX_PFN) goto done;
    (void)count;
    if (!pgalloc_init_model(m, backing, capacity, first, verify)) goto done;
    sys_frozen_exec = pgalloc_arena_end() * PAGE_SIZE;
    sys_frozen_end = top * PAGE_SIZE;
    ok = 1;
done:
    irq_restore(flags);
    return ok;
}

/* 管理する物理 RAM の末尾 (バイト)。ホットデプロイ窓を撤去した (2026-09-09)
 * ので、ここが legacy アリーナの上端そのもの。 */
static u32 sys_phys_end(void)
{
    u32 kb;
    if (sys_frozen_end) return sys_frozen_end;
    kb = sys_mem_kb;
    if (kb > PHYSMEM_LEGACY_MAX_PFN * (PAGE_SIZE / 1024))
        kb = PHYSMEM_LEGACY_MAX_PFN * (PAGE_SIZE / 1024);
    return (kb / (PAGE_SIZE / 1024)) * PAGE_SIZE;
}

/* 割り当ててよい上限 (バイト)。子プロセスのスタックはここから下へ伸びる。
 * = min(凍結した exec 上端, CPL=0 子のアリーナの上端) (TASK_T1_LEDGER §3-6)。
 * アリーナの上端は ⑥ で PEGC の BB (owner = boot) を池のアリーナ内の上端から
 * 取った後に台帳が凍結する (ledger_arena_freeze) — 旧 sys_reserve_top が
 * 上限を下げていた役はここが引き継ぐ (T1e で撤去)。 */
u32 sys_usable_mem_end(void)
{
    u32 top;
    if (!sys_frozen_end) return sys_phys_end();
    top = ledger_arena_top() * PAGE_SIZE;
    return top && top < sys_frozen_exec ? top : sys_frozen_exec;
}

os_time_t sys_time(void)
{
    RTC_Time t;
    int y;
    rtc_read(&t);
    y = t.year;
    /* PC-98のRTCは年号下2桁のみ。80以上なら1900年代、未満なら2000年代と仮定 */
    if (y < 80) y += 2000;
    else y += 1900;
    return datetime_to_epoch(y, t.month, t.day, t.hour, t.min, t.sec);
}
