/* ======================================================================== */
/*  pgalloc_host_fixture.h — ホスト試験の「新しい起動」の池                  */
/*                                                                          */
/*  製品の legacy pgalloc_init() は T1a で撤去した (票 docs/tasks/v3/         */
/*  TASK_T1_LEDGER.md §4-1、D32: どの構成もモデル経路)。多くのホスト試験は   */
/*  それを「メモリ量 kb の池を 1 回で作る」ための足場に使っていたので、同じ  */
/*  形の足場をここに置く。**製品に reset / legacy の口は足さない** — 試験が  */
/*  取り込んだ kernel/pgalloc.c の私有の状態を直接組む (pgalloc_range_host.c */
/*  が initialized を戻すのと同じ流儀)。                                    */
/*                                                                          */
/*  前提: kernel/pgalloc.c の本文 (と physmem.h) を先に取り込んでいること。  */
/*                                                                          */
/*  host_pool_boot(kb)                                                      */
/*      physmem_bootstrap_legacy(kb) の RAM を全部配る池を ONLINE にする。   */
/*      旧 pgalloc_init と同じく 1 回きり (initialized の間は何もしない)。   */
/*  host_pool_boot_ws(kb, ws_first, ws_end)                                  */
/*      同じ池に master PT 用の workspace [ws_first, ws_end) (PFN) を付ける。 */
/*      workspace は RAM から予約する (ARENA_TOP 型と同じ)。恒等写像と       */
/*      ホスト側の実メモリは呼び手が用意する。                               */
/* ======================================================================== */
#ifndef PGALLOC_HOST_FIXTURE_H
#define PGALLOC_HOST_FIXTURE_H

/* 旧 pgalloc_init と同じく IRQ 保存区間で組む (試験の IF 検査が見る)。
 * irq_save を別名に差し替えた試験は、取り込む前にこの 2 つを定義する。 */
#ifndef HOST_POOL_IRQ_SAVE
#define HOST_POOL_IRQ_SAVE() irq_save()
#define HOST_POOL_IRQ_RESTORE(f) irq_restore(f)
#endif

static u32 host_pool_storage[((PHYSMEM_LEGACY_MAX_PFN + 31) / 32) * 2];

static void __attribute__((unused)) host_pool_boot_ws(u32 kb, u32 ws_first,
                                                      u32 ws_end)
{
    struct physmem m;
    u32 i, limit;
    unsigned int flags;
    flags = HOST_POOL_IRQ_SAVE();
    (void)flags;
    if (initialized) goto done;
    physmem_bootstrap_legacy(&m, kb);
    if (ws_first < ws_end && !physmem_reserve_ram(&m, ws_first, ws_end))
        goto done;
    limit = MEM_POOL_BASE / PAGE_SIZE;
    for (i = 0; i < m.count; i++)
        if (m.ranges[i].kind == PHYSMEM_RAM) limit = m.ranges[i].end;
    init_core(&m, host_pool_storage, limit);
    generic_end = limit;
    arena_end = physmem_legacy_end(&m);
    workspace_first = ws_first < ws_end ? ws_first : 0;
    workspace_end = ws_first < ws_end ? ws_end : 0;
    for (i = 0; i < WORKSPACE_WORDS; i++) workspace_used[i] = 0;
    model_mode = 1;
    online = 1;
done:
    HOST_POOL_IRQ_RESTORE(flags);
}

static void __attribute__((unused)) host_pool_boot(u32 kb)
{
    host_pool_boot_ws(kb, 0, 0);
}

#endif /* PGALLOC_HOST_FIXTURE_H */
