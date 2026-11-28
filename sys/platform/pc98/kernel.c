#include "platform_kernel_entry.h"
#include "usb_resident.h"
#include "boot_token.h"
#include "kernel_start.h"
#include "legacy_guest.h"
/* ============================================================
 * kernel.c — PC-9801 32ビットOS カーネル
 * プロテクトモードで動作、テキストVRAMに直接書き込む
 * リニアアドレス 0x100000 (1MB) に配置される
 *
 * Phase 2+: IDT/PIC/PIT + キーボード + ミニシェル
 * ============================================================ */

#include "shell_loop.h"
#include "serial_diag.h"
#include "boot_cpu.h"
#include "platform_boot.h"
#include "../../arch/x86/kernel/idt.h"
#include "platform_boot.h"
#include "io.h"
#include "fatfs/diskio_os32.h"   /* diskio_print_fdd_cache */
/* fs.h 削除 — レガシーFS廃止 */
#include "../../kern/mm/kmalloc.h"
#include "palette.h"
#include "gfx.h"
#include "gf_production.h"
#include "gfx_hal.h"   /* H2b: GFX_PREF_* / gfx_set_backend_pref (GFX= の反映先) */
#include "../../dev/video/kcg/kcg_module.h"
#include "boot_splash.h"
#include "console.h"
#include "../../kern/console/console_dual.h"
#ifdef TICK_PROBE
#include "tick_probe.h"
#endif
#include "../../hal/time/cpu_calibrate.h"
#include "clock_source.h"
#include "kernel_tick.h"
#include "sysclk.h"   /* 0000:0501h のクロック判定 (pit_init より前に呼ぶ) */
#include "../../kern/mm/paging.h"
#include "../../kern/mm/memory_boot.h"
#include "../../kern/mm/pgalloc.h"     /* ledger_resource_import_pci (T1d ⑥-0) */
#include "../../kern/mm/shlib.h"
#include "../../kern/mm/shm.h"
#include "utf8.h"
#include "../../kern/console/unicode_file.h"
#include "system_surface.h"
#include "../../kern/kselftest.h"
#include "../../kern/kstack_hw.h"
#include "exec.h"
#include "../../kern/irq/irq.h"        /* irq_line_quarantined / irq_storm_masked (§1-1、結線表の line_state に差す) */
#include "../../kern/mm/dma_pool.h"   /* DMA プール (同 §1-3) */
#include "bootinfo.h"
#include "../../dev/ata/cd_module.h"
#include "vfs.h"
#include "../../fs/serialfs/serialfs.h"


#include "dev.h"
#include "text_screen.h"
#include "pc98.h"
#include "memmap.h"
#include "config.h"
#include "../../kern/init/sysconfig.h"
#include "machcfg_contract.h"
#include "kstring.h"
#include "kprintf.h"   /* kutoa_dec / kutoa_hex (状態行の数値) */
#include "sys.h"
#include "ime_boot.h"
#include "fd_redirect.h"
#include "pipe_buffer.h"
#include "../../dev/sound/fm/fm_module.h"
#include "../../dev/sound/cs4231/pcm_module.h"
#include "../../dev/net/e82557/lan_module.h"
#include "../../dev/video/cirrus/cirrus_module.h"
#include "../../dev/video/geforce/geforce_module.h"
#include "../../dev/video/trident/trident_module.h"
#include "os32_sqlite_vfs.h"
#include "os32_sqlite_outer.h"
#include "../../kern/sqlite_engine.h"
#include "../../arch/x86/kernel/double_fault.h"
#include "kapi_db.h"
#include "bootlog.h"
#include "idle_work.h"
#include "../../kern/module/module.h"

#define SHELL_RELOAD_DELAY 10

/* tvram_clear は console.c 側に実装済みのため削除 */
extern void console_hw_cursor_enable(void);   /* console.c */
extern void console_text_gdc_stop(void);      /* console.c (K4): GUI 時にカーソルを消す */
extern void console_text_gdc_start(void);     /* console.c (K4): CUI 復帰でカーソル再表示 */

#include "boot.h"
#include "boot_platform.h"
#include "boot_display.h"

/* 文字列表示 */
static void __attribute__((cold)) boot_diag_exec(const char *path, int rc, int gui, int streak)
{
    if (serial_gate_active()) return;
    if (path) {
        serial_puts_polled("[boot] exec "); serial_puts_polled(path);
    } else {
        serial_puts_polled("[boot] exec ret rc="); serial_diag_i32(rc);
        serial_puts_polled(" gui="); serial_diag_i32(gui);
        serial_puts_polled(" streak="); serial_diag_i32(streak);
    }
    serial_puts_polled("\r\n");
}

/* ============================================================
 * カーネルメイン — エントリポイント
 * ============================================================ */
extern u32 sys_mem_kb;


/* 起動段の補助は同じ翻訳単位の .inc に置く。 */
static void kernel_boot_cpu(u32 mem_kb, struct boot_context *boot);
static void kernel_boot_memory(u32 mem_kb, struct boot_context *boot);
static int boot_legacy_started;
static void kernel_gfx_config(void);
static void kernel_boot_serial(void);
static inline void kernel_shell_loop(void);
static inline void kernel_mount_children(const char *root_dev);

/* ローダからの受け渡し (kentry.asm が cdecl で渡す)。局面 1 が読む。 */
static u32 handoff_mem_kb, handoff_boot_drive;

void __cdecl kernel_main(u32 mem_kb, u32 boot_drive)
{
    handoff_mem_kb = mem_kb;
    handoff_boot_drive = boot_drive;
    kernel_start();
}

/* 局面 1 (割り込み禁止): 機器・CPU・メモリ・root のマウント・部品・exec。 */
void platform_boot_machine(void)
{
    static struct boot_context boot;
    u32 mem_kb = handoff_mem_kb;
    u32 boot_drive = handoff_boot_drive;

    kstack_hw_init();
    pc98_boot_bind(&boot, boot_drive);

    /* **最初に** ブート情報域 (0x7E00、ローダが INT 1Bh AH=84h の結果を
     * 書いた) を写す。0x7E00 はフォントキャッシュの内側で、フォント・
     * ヒープ・その他の低位の再利用より前でなければ上書きされる。
     * memory_boot_detect も保存した BDA (0594h) を読むので、順序を固定する
     * ためここに置く。以後は写しだけを使う (bootinfo_get / _hdd_geom)。 */
    platform_boot_capture();

    /* ローダの 512KB プローブは 16MB 手前までしか見ない (その先は PC-98 の
     * 16MB システム空間で、書き込みプローブが VRAM を壊す)。16MB 超は BIOS
     * ワークエリア 0594h を正典に、1MB ごとの再確認を通して足す (K6-RAM)。
     * **paging_init より前** に済ませる: ページングを張る範囲も pgalloc の
     * 表の大きさも、ここで決まった量から導かれる。 */
    mem_kb = memory_boot_detect(mem_kb);
    sys_mem_kb = mem_kb;
    
    kernel_boot_cpu(mem_kb, &boot);
    machcfg_begin(platform_machcfg_seed());

    boot_platform_run(BOOT_INPUT, &boot);

    boot_platform_run(BOOT_FLOPPY, &boot);

    boot_platform_run(BOOT_DEV_FS, &boot);

    /* パレット初期化 */
    palette_init();


    boot_platform_run(BOOT_PCI_ENUM, &boot);

    boot_platform_run(BOOT_DISK, &boot);

    /* HDD の幾何: BIOS (ローダの AH=84h) と IDENTIFY を 1 行ずつ。
     * 段 1 の設計値はこの 2 つで決める (票 TASK_HDD_INSTALL 段 0)。 */
    bootinfo_report();
    /* Build (日時 + コミット ID) と、ローダが検査して起動したイメージの CRC
     * (票 TASK_SERIAL_HOSTFS A-4 — 更新の証拠は Build ではなくこの CRC)。 */
    bootinfo_report_image();

    /* ヒープ初期化 (VFSマウントでkmalloc使用のため、マウント前に必要) */
    tvram_print(24, 2, "HEAP...", TATTR_GREEN);
    kmalloc_init((void *)KHEAP_BASE, KHEAP_SIZE);
    {
        /* KHEAP_SIZE から出す (決め打ちの "320K" が 2026-09-23 の切り直しで嘘になった) */
        char hb[8];
        kutoa_dec((u32)(KHEAP_SIZE / KIB), hb, (int)sizeof(hb));
        kstrncat(hb, "K", sizeof(hb));
        tvram_print(31, 2, hb, TATTR_WHITE);
    }

    /* root と HostDrv の初期マウント (部品の読込より前) */
    tvram_print(14, 3, "MOUNT...", TATTR_GREEN);
    {
        int rc;
        /* 1. ルートマウント */
        rc = vfs_mount("/", boot.root_dev, boot.root_fs);
        if (rc != VFS_OK) {
            tvram_print(23, 3, "root panic", TATTR_RED);
            for (;;) _halt();
        }
        tvram_print(23, 3, "root OK", TATTR_WHITE);

        boot_platform_run(BOOT_AUTO_MOUNT, &boot);
        machcfg_load_root();
        machcfg_report("pc98", 0, 0);
    }

    boot_platform_run(BOOT_CLOCK, &boot);

    kernel_boot_memory(mem_kb, &boot);

    /* FDリダイレクト初期化 (プログラムローダーより前に) */
    fd_redirect_init();

    /* パイプバッファ初期化 */
    pipe_buffer_init();

    kernel_boot_serial();
    kernel_gfx_config();

    /* ⑥ gfx の識別 → 候補の窓の一括予約 (owner = DEVICE gfx) → 写像 → PEGC の
     * BB (池のアリーナ内の上端、owner = boot) → SURFACE → CPL=0 子のアリーナの
     * 上端の凍結 → ledger_selfcheck("gfx") (TASK_T1_LEDGER §3-3・§3-8)。
     * live AS 0 の起動文脈で、exec_init より前。probe は ⑦ のまま。 */
    gfx_boot_reserve();
    kaudit_boot_pages();

    /* プログラムローダー初期化 (KernelAPIテーブル構築) */
    tvram_print(48, 2, "EXEC...", TATTR_GREEN);
    exec_init();
    /* トランポリンページができてからでないと踏めない自己診断 (票 T9 §12 R1)。
     * kselftest_run() はここより前に走っている。 */
    kaudit_post_exec();
    if (kdebug.state == KDEBUG_RUNNING) kdebug.ops.selftest_post_exec();
    tvram_print(56, 2, "OK", TATTR_WHITE);

    if (kdebug.state == KDEBUG_RUNNING) kdebug.ops.selftest_post_unicode();

    /* IME (FEP) 初期化 — 辞書は IRQ 有効化後、アプリ起動前に開く */
    tvram_print(0, 4, "IME..", TATTR_GREEN);
    kprintf(KLOG_ATTR, "[BOOT] ime_init...\n");
    ime_init();
    tvram_print(5, 4, "OK ", TATTR_WHITE);
    kprintf(KLOG_ATTR, "[BOOT] ime_init OK\n");

    /* サウンドエンジン初期化 */
    tvram_print(9, 4, "SND..", TATTR_GREEN);
    kprintf(KLOG_ATTR, "[BOOT] fm_module_load...\n");
    int snd_rc = fm_module_load();
    if (memory_boot_dma_available()) {
        pcm_module_load();
    }
    tvram_print(14, 4, snd_rc ? "-- " : "OK ", TATTR_WHITE);
    kprintf(KLOG_ATTR, "[BOOT] snd module rc=%d\n", snd_rc);

    if (!boot_legacy_started) {
        boot_platform_run(BOOT_NIC_LEGACY, &boot);
    }

    pc98_usb_boot();
    boot_token_init();

    /* File-backed settings/FEP I/O needs IRQs; no application exists yet. */
    tvram_print(71, 2, "IRQ_EN", TATTR_YELLOW);
}

/* 局面 2 (割り込み有効): サービス・スプラッシュ・表示の準備・共有ライブラリ・起動のログ。 */
void platform_boot_services(void)
{
    services_boot();
    double_fault_test();

    /* autoexec: シェルスクリプト(/etc/autoexec.bat)に移行済み。
     * シェル起動後に ui.c から script_source_file() で実行される。 */

    /* ブートスプラッシュ表示 (カーネル内蔵) */
#ifdef TICK_PROBE
    tick_probe_boot();
    tick_probe_splash(1);
#endif
    boot_splash();
#ifdef TICK_PROBE
    tick_probe_splash(0);
#endif

    /* splash は PC98 を強制する。ここでは Cirrus 登録前に PEGC の選択・
     * probe と同期状態の記録を master PD で済ませる (BIOS ワークはアプリ PD
     * に写像されない)。窓・BB は ⑥ (gfx_boot_reserve) が確保済み。
     * Cirrus は温めず、最初の GUI 境界、または CUI 描画アプリの gfx_init で
     * probe/enable する。GUI=0 の起動では識別・予約だけで enable しない。 */
    if (kdebug.ops.crt_diag_bootlog) kdebug.ops.crt_diag_bootlog();
    if (gfx_get_backend_pref() == GFX_PREF_TRIDENT) trident_module_load();
    gfx_prepare_backend();
    /* Register after boot preparation: GUI=0 must never probe/enable Cirrus. */
    cirrus_module_load();
    if (gfx_get_backend_pref() == GFX_PREF_GEFORCE) geforce_module_load();
    kaudit_post_probe();
    if (kdebug.state == KDEBUG_RUNNING) kdebug.ops.selftest_post_probe();

    /* 共有ライブラリ (0x80000000 帯、常駐シェルの 0x400000 帯とは別) を常駐させる — シェルを載せる **前** に
     * 1 回だけ (票 K3)。ここより後だと pgalloc が帯域のページを配ってしまう。
     * ライブラリが無ければ静かに未ロードで続行し、CUI は従来どおり動く。 */
    shlib_init();

    /* 最後の起動のログを /var/log/boot.log へ (前回分は .1、FAT は bootlog.1)。
     * ルートのマウントと自己試験・shlib の読み込みが済み、常駐シェルを
     * exec する直前。溜めるのはここまで (以後の出力は入らない)。ルートが
     * ext2 / FAT でなければ書かない。どこで失敗しても起動は続ける。 */
    idle_work_run();
    pc98_usb_flush_boot_log();
    bootlog_save();
}

/* 局面 3: 常駐シェルの起動ループ。戻らない (戻ったら kernel_start が停止する)。 */
void platform_boot_run(void)
{
    kernel_shell_loop();
}

#include "kernel_boot_cpu.inc"
#include "kernel_boot_memory.inc"
#include "boot_serial.inc"
#include "kernel_gfx_config.inc"

#include "kernel_mount_children.h"

/* 次の CUI 起動で表示する診断。 */
static const char *gshell_warning;
static const char *gshell_warning_help;

#include "kernel_gui_shlib.inc"
#include "kernel_shell_fdd.inc"

static int pc98_gui_default(void)
{
    return sysconfig_get_int(SYS_SYSTEM_CFG, "GUI", 0) == 1;
}

static int pc98_shell_prepare(int is_gui)
{
    if (is_gui && !kcg_module_ready()) return 1;
    if (is_gui && !kernel_gui_shlib_ready()) return 2;
    return 0;
}

static void clear_shell_warning(const char *previous_warning)
{
    if (!previous_warning) return;
    for (int y = 0; y < TVRAM_ROWS; y++) {
        int x = 0;
        while (previous_warning[x]) {
            u16 code = 0;
            u8 attr = 0;
            tvram_readchar_at(x, y, &code, &attr);
            if (code != (u8)previous_warning[x] || attr != TATTR_RED) break;
            x++;
        }
        if (!previous_warning[x]) {
            while (x > 0) tvram_putchar_at(--x, y, ' ', TATTR_GRAY);
            break;
        }
    }
}

static void pc98_shell_loading(const char *cur_shell, int is_gui)
{
    /* dual のテキスト面はセッションを越えて残り、出力でスクロールもする。 */
    static const char *previous_warning, *previous_help;
    clear_shell_warning(previous_warning);
    clear_shell_warning(previous_help);
    previous_warning = is_gui ? NULL : gshell_warning;
    previous_help = is_gui ? NULL : gshell_warning_help;

    /* GUI は全画面 GFX を握るのでテキストカーソルを消す。CUI は出す。 */
    if (is_gui) {
        /* Graphics boundary, master PD; all windows were mapped by ⑥. */
        if (!gfx_cirrus_probed()) gfx_prepare_backend();
        /* ⑨ 選択中 backend の CLIENT を boot → gshell へ (2 回目以降は
         * 無操作) → ledger_selfcheck("gui")。TASK_T1_LEDGER §3-3 ⑨。 */
        gfx_client_to_gshell();
        if (con_dual_cfg())
            shell_print("Loading gshell...\n", TATTR_GRAY);
        console_text_gdc_stop();
        if (!con_dual_cfg())
            tvram_print(0, 0, "Loading gshell...", TATTR_GRAY);
    } else {
        /* prepare が TVRAM を消さなくなった (PEGC、2026-09-24) ので、
         * 0 行目には起動ログが残っている。文言の後ろを空白で消す。 */
        static const char loading[] = "Loading shell...";
        int col;
        console_text_gdc_start();
        if (con_dual_cfg()) {
            if (gshell_warning) {
                tvram_print(0, 0, gshell_warning, TATTR_RED);
                int row = 1;
                if (gshell_warning_help)
                    tvram_print(0, row++, gshell_warning_help, TATTR_RED);
                tvram_print(0, row++, loading, TATTR_GRAY);
                console_set_cursor(0, row);
            } else {
                shell_print("Loading shell...\n", TATTR_GRAY);
            }
        } else {
            if (gshell_warning) {
                tvram_print(0, 1, gshell_warning, TATTR_RED);
                if (gshell_warning_help) tvram_print(0, 2, gshell_warning_help, TATTR_RED);
                console_set_cursor(0, gshell_warning_help ? 3 : 2);
            }
            tvram_print(0, 0, loading, TATTR_GRAY);
            for (col = (int)sizeof(loading) - 1; col < TVRAM_COLS; col++)
                tvram_putchar_at(col, 0, ' ', TATTR_GRAY);
        }
    }

    gshell_warning = NULL;
    gshell_warning_help = NULL;
    if (!is_gui) geforce_heat_notice();
    boot_diag_exec(cur_shell, 0, 0, 0);
}

static void pc98_shell_result(const char *path, int rc, int gui, int streak)
{
    (void)path;
    boot_diag_exec(0, rc, gui, streak);
}

static void pc98_shell_failure(int gui, int rc, int streak)
{
    (void)streak;
    if (!gui) {
        kernel_shell_fdd_fallback(exec_layout_rejected());
    } else if (rc == EXEC_ERR_FAULT) {
        gshell_warning = "gshell crashed 3x -> CUI shell";
    } else {
        console_text_gdc_start();
        tvram_clear();
        gshell_warning = exec_layout_rejected() == EXEC_REJECT_MIN_API
                    ? "gshell requires newer kernel -> CUI shell"
                    : exec_layout_rejected()
                    ? "gshell: rebuild required (KAPI data layout) -> CUI shell"
                    : "gshell load failed -> CUI shell";
    }
}

static void pc98_shell_delay_clear(void)
{
    /* シェルが終了 (切替 or exit) またはクラッシュ復帰。
     * FDD負荷軽減用ウェイト (約100ms) の後、画面クリアして再起動。
     * tick_count は 100Hz で回る u32 で 497 日で一周する。差分を
     * 符号付きで見れば一周しても正しい (待ち << 2^31 tick である限り)。 */
    {
        u32 start = tick_count;
        while ((i32)(tick_count - start) < (i32)SHELL_RELOAD_DELAY) {
            _halt();
        }
    }
    console_session_clear();
}

static inline void kernel_shell_loop(void)
{
    static const struct shell_loop_ops ops = {
        .gui_default = pc98_gui_default,
        .prepare = pc98_shell_prepare,
        .loading = pc98_shell_loading,
        .result = pc98_shell_result,
        .failure = pc98_shell_failure,
        .delay_clear = pc98_shell_delay_clear,
    };
    shell_loop_run(&ops);
}
