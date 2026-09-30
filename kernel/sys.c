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

/* 管理する物理 RAM の末尾 (バイト)。legacy 経路の上限そのもの。 */
static u32 sys_phys_end(void);

int sys_device_reserve_core(u32 owner, const struct sys_device_span *spans,
                            u32 count, const struct sys_device_capability *cap)
{
    unsigned int flags;
    u32 end;
    int ok;
    flags = irq_save();
    /* sys low fixed extent の上端 = 管理する実 RAM の末尾。ホットデプロイ窓を
     * 撤去した (2026-09-09) ので、その分の上乗せは無い。 */
    end = sys_phys_end() / PAGE_SIZE;
    ok = 0;
    if (end && end <= PHYSMEM_MAX_PFN)
        ok = pgalloc_device_reserve(owner, spans, count, cap, end);
    irq_restore(flags);
    return ok;
}

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

/* 使用可能上限の直下にさらに固定予約した量 (バイト、ページ境界)。
 * 現状の唯一の利用者は PEGC 8bpp バックバッファ (H2)。9801 では 0 のまま
 * なので sys_usable_mem_end() は従来と同じ値を返す (回帰ゼロ)。 */
static u32 sys_top_reserved = 0;
static u32 sys_frozen_exec, sys_frozen_end;
static int sys_model_staged;

int sys_memory_bootstrap_model(struct physmem *m, const struct pgalloc_layout *l,
                               int (*verify)(u32, u32, void *))
{
    u32 top, bytes, count;
    struct pgalloc_layout layout;
    unsigned int flags;
    int ok;
    flags = irq_save();
    ok = 0;
    if (sys_frozen_end || sys_top_reserved || !m || !l || !verify ||
        !paging_boot_context()) goto done;
    /* Backing is about to be zeroed; retain no borrowed layout pointer. */
    layout = *l;
    l = &layout;
    top = physmem_legacy_end(m);
    bytes = pgalloc_metadata_bytes(m);
    if (!bytes || top != m->legacy_ceiling || l->metadata_first >= top ||
        bytes / PAGE_SIZE != top - l->metadata_first ||
        l->workspace_end != l->metadata_first ||
        l->workspace_first < (MEM_EXEC_LOAD_ADDR + MEM_EXEC_STACK_SIZE +
            MEM_EXEC_SBRK_MIN + MEM_EXEC_HEAP_MIN) / PAGE_SIZE ||
        top > PHYSMEM_LEGACY_MAX_PFN) goto done;
    /* ホットデプロイ窓を撤去したので、legacy 上端の直上に予約帯は無い。
     * 検証すべきは metadata / workspace の写像だけで、それは
     * pgalloc_init_layout が verify を通して行う。 */
    (void)count;
    if (!pgalloc_init_layout(m, l, verify)) goto done;
    sys_frozen_exec = l->workspace_first * PAGE_SIZE;
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
    if (sys_frozen_end || sys_top_reserved || !m || !verify) goto done;
    bytes = pgalloc_metadata_bytes(m);
    if (!bytes) goto done;
    top = physmem_legacy_end(m);
    minimum = (MEM_EXEC_LOAD_ADDR + MEM_EXEC_STACK_SIZE +
               MEM_EXEC_SBRK_MIN + MEM_EXEC_HEAP_MIN) / PAGE_SIZE;
    if (top != m->legacy_ceiling || first < minimum || first >= top ||
        bytes / PAGE_SIZE != top - first ||
        top > PHYSMEM_LEGACY_MAX_PFN) goto done;
    (void)count;
    if (!pgalloc_init_model(m, backing, capacity, first, verify)) goto done;
    sys_frozen_exec = first * PAGE_SIZE;
    sys_frozen_end = top * PAGE_SIZE;
    ok = 1;
done:
    irq_restore(flags);
    return ok;
}

/* 管理する物理 RAM の末尾 (バイト)。ホットデプロイ窓を撤去した (2026-09-09)
 * ので、ここが legacy アリーナの上端そのもの。固定予約 (sys_top_reserved) には
 * 影響されない。 */
static u32 sys_phys_end(void)
{
    u32 kb;
    if (sys_frozen_end) return sys_frozen_end;
    kb = sys_mem_kb;
    if (kb > PHYSMEM_LEGACY_MAX_PFN * (PAGE_SIZE / 1024))
        kb = PHYSMEM_LEGACY_MAX_PFN * (PAGE_SIZE / 1024);
    return (kb / (PAGE_SIZE / 1024)) * PAGE_SIZE;
}

/* 物理末尾から固定予約 (PEGC バックバッファ等) を除いた、割り当ててよい上限。
 * 子プロセスのスタックはここから下へ伸びる。 */
u32 sys_usable_mem_end(void)
{
    if (sys_frozen_end) return sys_frozen_exec;
    return sys_phys_end() - sys_top_reserved;
}

/* ======================================================================== */
/*  sys_reserve_top — 物理末尾側に固定領域を切り出す (GUI v1.1 H2)          */
/*                                                                          */
/*  **現在の使用可能上限 (sys_usable_mem_end) の直下**から bytes バイト        */
/*  (4KB 切り上げ) を予約し、その先頭物理アドレスを返す。以後               */
/*  sys_usable_mem_end() はその分だけ下がるので、exec の子プロセス          */
/*  (コード/ヒープ/スタック) はここへ伸びてこない。                         */
/*                                                                          */
/*  legacy 経路では上限 = ホットデプロイ窓の直下なので従来と同じ区間になる。 */
/*  モデル経路では上限と窓の間に metadata / workspace が居るので、下げて     */
/*  よいのは上限 (sys_frozen_exec) だけ。窓の位置は動かさない。             */
/*                                                                          */
/*  **exec_run より前 (ブート中) に 1 回だけ呼ぶこと。** 子プロセスが走って  */
/*  いる最中に上限を動かすと、その子のレイアウトと pgalloc の予約範囲が      */
/*  食い違う。同じサイズでの再呼び出しは冪等 (同じ先頭を返す)。             */
/*                                                                          */
/*  戻り値: 予約領域の先頭物理アドレス。0 = 予約できなかった                 */
/*  (メモリ不足、または既に別サイズで予約済み)。                             */
/* ======================================================================== */
u32 sys_reserve_top(u32 bytes)
{
    u32 ceiling, need, result, minimum;
    unsigned int flags;
    flags = irq_save();
    result = 0;
    if (!bytes || bytes > ~0UL - (PAGE_SIZE - 1)) goto done;
    need = (bytes + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);
    /* Carve below the current usable ceiling, not below the hotdeploy window.
     * On the model path metadata/workspace occupy the pages between the frozen
     * exec ceiling and hotdeploy, so only the ceiling is safe to lower. Legacy
     * has nothing in between, so both paths reduce to the same interval. */
    ceiling = sys_usable_mem_end();
    if (sys_top_reserved) {
        if (sys_top_reserved == need) result = ceiling;
        goto done;
    }
    minimum = MEM_EXEC_LOAD_ADDR + MEM_EXEC_STACK_SIZE +
              MEM_EXEC_SBRK_MIN + MEM_EXEC_HEAP_MIN;
    if (ceiling < minimum || need > ceiling - minimum) goto done;
    /* Old boot calls after pgalloc_init. Never publish a numeric-only claim. */
    if (!pgalloc_reserve_pfn((ceiling - need) / PAGE_SIZE,
                             ceiling / PAGE_SIZE)) goto done;
    sys_top_reserved = need;
    if (sys_frozen_end) sys_frozen_exec = ceiling - need;
    result = ceiling - need;
done:
    irq_restore(flags);
    return result;
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
