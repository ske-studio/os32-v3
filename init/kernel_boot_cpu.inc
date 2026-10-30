static void kernel_boot_cpu(u32 mem_kb, struct boot_context *boot)
{
    char tmp[BOOT_STATUS_NUM_BUFSIZE];
    int mb = mem_kb / KIB;

    tvram_clear();
    console_hw_cursor_enable();   /* GDC カーソル表示 ON (以後は出力に追従) */

    tvram_print(0, 0, "PC-9801 OS32 booting...", TATTR_CYAN);

    /* メモリ量表示 (例: " (16MB)") */
    {
        int i, digit, scale = 1000, seen = 0;
        tmp[0] = ' '; tmp[1] = '(';
        /* 16MB 上限を撤廃したので 4 桁ぶん取る (先行ゼロ抑制)。 */
        for (i = 0; i < 4; i++) {
            digit = (mb / scale) % 10;
            if (digit) seen = 1;
            tmp[2 + i] = (seen || i == 3) ? (char)('0' + digit) : ' ';
            scale /= 10;
        }
        tmp[6] = 'M'; tmp[7] = 'B'; tmp[8] = ')'; tmp[9] = '\0';
    }
    tvram_print(24, 0, tmp, TATTR_CYAN);

    /* GDT 再構築 (ブートローダーのGDTから安全な場所へ移行) */
    tvram_print(0, 1, "GDT...", TATTR_GREEN);
    extern void gdt_init(void);
    gdt_init();
    tvram_print(6, 1, "OK  ", TATTR_WHITE);

    /* TSS (V86 モードに必須: #GP 時の Ring0 スタックと I/O 許可ビットマップ)
     * GDT のエントリ3を使うので gdt_init の直後に呼ぶ。
     * ESP0 は V86 突入直前に v86_enter が実際の ESP へ差し替える。 */
    tss_init(MEM_KSTACK_TOP);
    double_fault_init();

    /* IDT/PIC/PIT 初期化 */
    tvram_print(12, 1, "IDT...", TATTR_GREEN);
    idt_init();
    tvram_print(18, 1, "OK  ", TATTR_WHITE);



    tvram_print(24, 1, "PIC...", TATTR_GREEN);
    pic_init();
    tvram_print(30, 1, "OK  ", TATTR_WHITE);

    /* PIT の分周は機械のシステムクロック次第 (1.9968MHz / 2.4576MHz)。
     * **pit_init より前に 1 回だけ** 0000:0501h を読む。paging_init より
     * 前なので PG=0、低位物理がそのまま見える (票 §1-0、§4-54)。 */
    sysclk_detect();

    boot_platform_run(BOOT_DMA, boot);

    tvram_print(36, 1, "PIT...", TATTR_GREEN);
    pit_init(PIT_HZ);
    tvram_print(42, 1, "OK  ", TATTR_WHITE);
    /* どちらのクロックで分周したかを出す。**NP21/W では常に 1.9968M** なので、
     * 実機で 2.4576M が出ていることがこの修正の目視確認になる。 */
    tvram_print(11, 2, sysclk_is_8mhz() ? "PIT 1.9968M" : "PIT 2.4576M",
                TATTR_WHITE);

    /* タイマとカスケード有効化 */
    irq_enable(0);
    irq_enable(ICU_SLAVEID);

    /* CPU速度キャリブレーション (PITベース) */
    tvram_print(0, 2, "CPU...", TATTR_GREEN);
    _enable();  /* タイマー割り込み許可 (キャリブレーションに必要) */
    cpu_calibrate();
    {
        u32 lpt = cpu_loops_per_tick();
        int k = (int)(lpt / 1000);
        char cpubuf[12];
        int pos = 0;
        /* 簡易数値→文字列変換 (0-999K) */
        if (k >= 100) { cpubuf[pos++] = '0' + (k / 100) % 10; }
        if (k >= 10)  { cpubuf[pos++] = '0' + (k / 10) % 10; }
        cpubuf[pos++] = '0' + k % 10;
        cpubuf[pos++] = 'K';
        cpubuf[pos] = '\0';
        tvram_print(6, 2, cpubuf, TATTR_WHITE);
    }
}
