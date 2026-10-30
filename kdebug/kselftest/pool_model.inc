/* ------------------------------------------------------------------------ */
/*  物理ページの池 (票 docs/archive/v3/TASK_T1_LEDGER.md §4-1、T1a)             */
/*                                                                          */
/*  legacy の pgalloc_init への fallback は撤去した。8MB でも 17MB でも池は  */
/*  モデル経路で ONLINE でなければならない (legacy に戻ったら落ちる)。       */
/*  exec の上端は台帳の置き場ではなくアリーナの上端から決まるので、8MB でも */
/*  exec の最小域 (ロード起点 + スタック + sbrk + exec_heap) が残る (B2)。    */
/*  置き場 (FIXED 型、core BSS 内の MEM_LEDGER_META_*) の RW / USER なしは   */
/*  test_memmap の MM 検査が見る (T1-U1)。                                  */
/* ------------------------------------------------------------------------ */
static void test_pool_model(void)
{
    check(pgalloc_model_state() == PGALLOC_ONLINE, "pool:model online");
    check(sys_usable_mem_end() >= MEM_EXEC_BOOT_MIN, "pool:exec range");
}
