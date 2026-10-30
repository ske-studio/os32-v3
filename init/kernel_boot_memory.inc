static void kernel_boot_memory(u32 mem_kb, struct boot_context *boot)
{
    /* ページング初期化 (メモリ保護有効化) */
    tvram_print(37, 2, "PAGE...", TATTR_GREEN);
    paging_init(mem_kb);
    tvram_print(44, 2, "OK", TATTR_WHITE);

    /* コンベンショナルメモリ再利用 (ブート後のローダー領域等を解放) */
    paging_reclaim_conventional();

    boot_platform_run(BOOT_FONT, boot);

    /* FPU 初期化 (paging_init の後で CR0 に対して設定)
     * CR0.EM=0 (ネイティブFPU使用), CR0.TS=0 (タスクスイッチ不要)
     * SQLite が double 演算を使用するため必須 */
    {
        u32 cr0_val;
        __asm__ volatile("mov %%cr0, %0" : "=r"(cr0_val));
        cr0_val &= ~((u32)(CR0_EM | CR0_TS));  /* EM(bit2) と TS(bit3) をクリア */
        __asm__ volatile("mov %0, %%cr0" : : "r"(cr0_val) : "memory");
        __asm__ volatile("fninit");
    }

    /* 物理ページフレームアロケータ初期化 (paging_initの後) */
    if (!memory_boot_init(mem_kb)) {
        kprintf(KLOG_ATTR, "[MEM] bootstrap/stage failed; boot halted\n");
        for (;;) { _stop(); }
    }

    /* 共有メモリ初期化 (ガードページ設定 + R/W設定) */
    shm_init();
    if (paging_boot_user_shared(exec_tramp_page_addr()) != 0) {
        kprintf(KLOG_ATTR, "[MEM] shared USER boot failed; boot halted\n");
        for (;;) { _stop(); }
    }

    /* D の予約・恒等写像確認の後だけ池を使う。自己診断は池を
     * 再初期化するので、PCI/PCM が span を取るより前に済ませる。 */
    if (memory_boot_dma_available()) {
        dma_pool_init();
    } else {
        kprintf(KLOG_ATTR, "[dma] RAM short; pool/PCI/PCM skipped\n");
    }
    v86_module_load(); /* Core facades are ready before kdebug selftests. */
    kdebug_load();
    if (kdebug.state == KDEBUG_RUNNING) kdebug.ops.selftest_run();
    else kselftest_skipped();

    /* POST_ROOT: the loader needs root, paging and the page ledger. CD is
     * independent of services_boot, so MINIMAL/SQLITE_ONLY can read packages. */
    cd_module_load();
    /* Page owners exist now; failure retains the TVRAM CUI. */
    kcg_module_load();
    kernel_mount_children(boot->root_dev);

    services_stage = SERVICES_ROOT_READY;
    kprintf(KLOG_ATTR, "[services] ROOT_READY tick=%u\n", tick_count);
    unicode_file_load();
    services_stage = SERVICES_UNICODE;

    boot_platform_run(BOOT_PCI_RESOURCES, boot);

    if (memory_boot_dma_available()) {
        lan_module_load();
    }
    lan_module_pci_bind_fallback();

    /* CS4231 (MATE-X PCM) の検出だけ (票 TASK_PCM_CS4231 §2-1)。
     * 無い機械では `[pcm] none` を出して静かに戻る — 以降の起動は変わらない。 */
    if (memory_boot_dma_available()) {
        pcm_module_load();
    }
}
