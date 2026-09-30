/* ============================================================
 * kernel.c — PC-9801 32ビットOS カーネル
 * プロテクトモードで動作、テキストVRAMに直接書き込む
 * リニアアドレス 0x100000 (1MB) に配置される
 *
 * Phase 2+: IDT/PIC/PIT + キーボード + ミニシェル
 * ============================================================ */

#include "idt.h"
#include "tss.h"
#include "v86.h"
#include "io.h"
#include "kbd.h"
#include "fdc.h"
#include "fatfs/diskio_os32.h"   /* diskio_print_fdd_cache */
#include "disk.h"
#include "dev.h"
#include "path.h"
/* fs.h 削除 — レガシーFS廃止 */
#include "rtc.h"
#include "kmalloc.h"
#include "palette.h"
#include "gfx.h"
#include "gfx_hal.h"   /* H2b: GFX_PREF_* / gfx_set_backend_pref (GFX= の反映先) */
#include "kcg.h"
#include "boot_font.h"
#include "boot_splash.h"
#include "cpu_calibrate.h"
#include "sysclk.h"   /* 0000:0501h のクロック判定 (pit_init より前に呼ぶ) */
#include "paging.h"
#include "memory_boot.h"
#include "shlib.h"
#include "shm.h"
#include "utf8.h"
#include "kselftest.h"
#include "exec.h"
#include "pci.h"
#include "pci_bind.h"   /* PCI の結線表 (票 TASK_HAL_WIRING §1-4) */
#include "irq.h"        /* irq_line_quarantined / irq_storm_masked (§1-1、結線表の line_state に差す) */
#include "dma8237.h"    /* 8237 の共通部 + 0439h (同 §1-2) */
#include "dma_pool.h"   /* DMA プール (同 §1-3) */
#include "ide.h"
#include "bootinfo.h"   /* ブート情報域 0x7E00 (票 TASK_HDD_INSTALL 段 0) */
#include "atapi.h"
#include "vfs.h"
#include "ext2.h"
#include "iso9660.h"
#include "loop_dev.h"

/* FatFs VFSドライバ (fs/fatfs_vfs.c) */
extern void fatfs_init(void);

#include "hostdrvfs.h"
#include "serialfs.h"
#include "tvram.h"
#include "pc98.h"
#include "memmap.h"
#include "config.h"
#include "sysconfig.h"
#include "kstring.h"
#include "kprintf.h"   /* kutoa_dec / kutoa_hex (状態行の数値) */
#include "sys.h"
#include "ime.h"
#include "fd_redirect.h"
#include "pipe_buffer.h"
#include "snd_engine.h"
#include "mouse.h"
#include "lgy98.h"
#include "pcm_cs4231.h"
#include "os32_sqlite_vfs.h"
#include "kapi_db.h"
#include "bootlog.h"

#define SHELL_RELOAD_DELAY 10

/* tvram_clear は console.c 側に実装済みのため削除 */
extern void console_hw_cursor_enable(void);   /* console.c */
extern void console_text_gdc_stop(void);      /* console.c (K4): GUI 時にカーソルを消す */
extern void console_text_gdc_start(void);     /* console.c (K4): CUI 復帰でカーソル再表示 */

/* 次に起動するシェルのパス (契約 T9)。実体は kernel/gui.c (K1)。
 * gui.h を丸ごと取り込むと os32_gui_shared.h まで引くので、
 * 起動ループが使う 1 本だけを extern 宣言する。 */
extern int gui_take_next_shell(char *out, int cap);

/* 文字列表示 */
static void tvram_print(int x, int y, const char *str, u8 color)
{
    u32 offset;
    while (*str) {
        if (x >= TVRAM_COLS) { x = 0; y++; }
        if (y >= TVRAM_ROWS) break;
        offset = (u32)y * TVRAM_BPR + (u32)x * 2;
        *(volatile u16 *)(TVRAM_BASE + offset) = (u16)(u8)*str;
        *(volatile u8 *)(TVRAM_ATTR + offset) = color;
        str++; x++;
    }
}

/* 文字列を連結して終端の次を返す (状態行を組み立てる小道具)。
 * [C2] libc は使わない。kstrncat は毎回長さを数え直すので、
 * 組み立てのあいだは末尾の位置を持ち回る。 */
static char *tv_cat(char *dst, const char *src)
{
    while (*src) { *dst = *src; dst++; src++; }
    *dst = '\0';
    return dst;
}

/* 1 バイトを 2 桁の 16 進 (小文字) で連結する。kutoa_hex は桁を詰めるので
 * (0x0F → "f")、足りない分だけ '0' を先に置く。 */
static char *tv_cat_hex2(char *dst, u8 val)
{
    char tmp[8];
    int len;

    len = kutoa_hex((u32)val, tmp, (int)sizeof(tmp), 0);
    if (len < 2) { *dst = '0'; dst++; *dst = '\0'; }
    return tv_cat(dst, tmp);
}

/* 符号付き 10 進を連結する。 */
static char *tv_cat_dec(char *dst, int val)
{
    char tmp[16];

    if (val < 0) {
        *dst = '-'; dst++; *dst = '\0';
        /* 符号反転は u32 で行う — int で -INT_MIN を作ると未定義。 */
        kutoa_dec(0u - (u32)val, tmp, (int)sizeof(tmp));
    } else {
        kutoa_dec((u32)val, tmp, (int)sizeof(tmp));
    }
    return tv_cat(dst, tmp);
}

/* ======================================================================== */
/*  PCI の結線表 (票 docs/tasks/v3/TASK_HAL_WIRING.md §1-4)                 */
/*                                                                          */
/*  **いまは空**。82557 の driver は別票 (L-B) で、ここに 1 行足るだけ。    */
/*  空でも pci_bind_all を毎回通すのは、「候補が 0 本」と「PCI が無い」を   */
/*  同じ経路で扱うため — 実機で初めて動く経路を実機の日に初めて通す、を     */
/*  避ける。                                                                */
/*                                                                          */
/*  1 件ずつのポインタ配列にしてあるのは、将来 §3 の動的読み込みが来た      */
/*  ときに表を差し替えるのが 1 ポインタで済むから (`size` で版を確かめる)。 */
/* ======================================================================== */
static const struct pci_driver *const pci_drivers[] = {
    (const struct pci_driver *)0   /* C89 は空の初期化子を許さない番兵 */
};
#define PCI_DRIVER_COUNT  0

/* dev_findをPathDeviceValidatorとして使用するラッパー */
static int dev_find_validator(const char *name)
{
    return (dev_find(name) != (void *)0) ? 1 : 0;
}

/* ============================================================
 * カーネルメイン — エントリポイント
 * ============================================================ */
extern u32 sys_mem_kb;


/* 結線表 (実装 B) が読む「線の様子」を実装 A の隔離ビットから答える (合流時に PM が差した 3 行)。 */
static u8 kernel_pci_line_bits(unsigned int irq)
{
    u8 b = 0;
    if (irq_line_quarantined & (1u << irq)) b |= PCI_LINE_BIT_QUARANTINED;
    if (irq_storm_masked & (1u << irq)) b |= PCI_LINE_BIT_STORM;
    return b;
}

void __cdecl kernel_main(u32 mem_kb, u32 boot_drive)
{
    char tmp[16];
    int mb;

    /* **最初に** ブート情報域 (0x7E00、ローダが INT 1Bh AH=84h の結果を
     * 書いた) を写す。0x7E00 はフォントキャッシュの内側で、フォント・
     * ヒープ・その他の低位の再利用より前でなければ上書きされる。
     * memory_boot_detect も低位 (0594h) を読むだけだが、順序を固定する
     * ためここに置く。以後は写しだけを使う (bootinfo_get / _hdd_geom)。 */
    bootinfo_capture();

    /* ローダの 512KB プローブは 16MB 手前までしか見ない (その先は PC-98 の
     * 16MB システム空間で、書き込みプローブが VRAM を壊す)。16MB 超は BIOS
     * ワークエリア 0594h を正典に、1MB ごとの再確認を通して足す (K6-RAM)。
     * **paging_init より前** に済ませる: ページングを張る範囲も pgalloc の
     * 表の大きさも、ここで決まった量から導かれる。 */
    mem_kb = memory_boot_detect(mem_kb);
    mb = mem_kb / 1024;
    sys_mem_kb = mem_kb;
    
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

    /* 8237 の共通部。0439h bit2 (1MB 超への DMA 禁止) を落とすのはここ —
     * **fdc_init より前**。プールもカーネルも 1MB 超にあるので、FDC だけの
     * 都合ではない (票 §1-2)。起動行 `[dma] 0439h xx -> yy state=…` を出す。 */
    dma8237_init();

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

    /* キーボード初期化。**paging_init より前** に呼ぶ — kbd_init は BIOS ワーク
     * エリア 0000:053Ah (カナ・CAPS の初期値) を読み、ページング後はページ 0 が
     * NOT PRESENT になる (include/pc98.h BIOS_WORK_KB_SHIFT、memmap.h)。 */
    tvram_print(48, 1, "KBD...", TATTR_GREEN);
    kbd_init();
    tvram_print(54, 1, "OK", TATTR_WHITE);

    /* マウスドライバ初期化 (NP21/W検出→モード自動選択) */
    mouse_init();

    /* FDC初期化 (I/Oポート直接制御) */
    tvram_print(60, 1, "FDC...", TATTR_GREEN);
    /* IRQ11 (FDD) 有効化 */
    irq_enable(FDC_IRQ);
    _enable();  /* FDC初期化前にIRQを許可 */
    {
        int fdc_ret = fdc_init();
        if (fdc_ret == 0) {
            tvram_print(66, 1, "OK", TATTR_WHITE);
        } else {
            tvram_print(66, 1, "ER", TATTR_RED);
        }
    }
    /* FDC の中身を状態行にも出す。**成功しても出す**。
     * kprintf の [fdc] 行は属性が PC/AT 流のまま属性 VRAM へ書かれていて、
     * 実機 PC-9821Ra266 では 1 行も読めなかった (2026-09-22、lib/kprintf_attr.c)。
     * 変換を入れて直したが、ここは属性を直に指定する tvram_print なので
     * 同じ穴に落ちない — 保険としてこの 1 行を残す ([V4])。
     * 書式: FDC rc=<n> st0=<xx> 0439h=<xx>-><xx> */
    {
        char line[48];
        char *q = line;
        u8 st0 = 0, dma_before = 0, dma_after = 0;
        int frc = fdc_get_last_init_status(&st0, &dma_before, &dma_after);

        q = tv_cat(q, "FDC rc=");
        q = tv_cat_dec(q, frc);
        q = tv_cat(q, " st0=");
        q = tv_cat_hex2(q, st0);
        q = tv_cat(q, " 0439h=");
        q = tv_cat_hex2(q, dma_before);
        q = tv_cat(q, "->");
        q = tv_cat_hex2(q, dma_after);
        *q = '\0';   /* 念のための終端 (tv_cat も写す) */
        /* 最下行に置く。console の通常出力は行 0 から進むので、行 5 だと IDE の
         * probe ログに root panic の前に上書きされる (Codex 往復 3 の指摘)。
         * 最下行は console が 19 行以上流れるまで無事で、root panic までに
         * それだけ流れることはない。 */
        tvram_print(0, TVRAM_ROWS - 1, line, (frc == 0) ? TATTR_WHITE : TATTR_YELLOW);
        /* 流れる側 (kprintf) にも同じ内容を残す — 属性変換で実機でも読める。 */
        kprintf(TATTR_WHITE, "[fdc] %s\n", line);
    }
    /* FD から起動したなら、その DA/UA でメディアを選ぶ。
     * 1.44MB (DA/UA 0x30 系) は 512B×18セクタで、2HD のまま読むと
     * ゴミを掴んで root マウントが落ちる (2026-09-18 に実測)。
     * **dev_init() より前**でなければデバイス記述子が古い値で登録される。
     *
     * 1.44MB を選ぶと fdc_set_media が 04BEh でアクセスモードも切り替える。
     * **失敗しても起動は止めない** — BIOS が起動時に既に正しいモードへ
     * 入れている場合があり (エミュレータはそう)、しかも 04BEh の読みは
     * 00BEh のデコードイメージで嘘をつく機種がある (io_fdd.md)。
     * 表示だけ残して進む ([V4]: 黙って成功にしない)。 */
    fdc_set_media_by_daua(0, boot_drive);
    if ((boot_drive & 0xF0) == 0x30 || (boot_drive & 0xF0) == 0xB0) {
        u8 m3 = (u8)inp(FDC_IO_3MODE);
        tvram_print(69, 1, "144", TATTR_GREEN);
        tvram_print(72, 1, (m3 & FDC_3M_CUR_144) ? "M" : "?",
                    (m3 & FDC_3M_CUR_144) ? TATTR_WHITE : TATTR_RED);
    }

    /* デバイス・パスシステム初期化 */
    tvram_print(58, 1, "DEV...", TATTR_GREEN);
    dev_init();
    loop_dev_init();  /* lo0..lo3 ループバックデバイス登録 */
    path_init();
    ext2_init();
    fatfs_init();
    vfs_register_fs(&iso9660_ops);

    hostdrvfs_init();
    serialfs_init();   /* /host をシリアル越しに (sfs run の中だけ、票 TASK_SERIAL_HOSTFS B) */
    path_set_device_validator(dev_find_validator);
    tvram_print(64, 1, "OK", TATTR_WHITE);

    /* パレット初期化 */
    palette_init();


    /* PCI の列挙 (読むだけ)。**IDE より前**に置く: 実機 PC-9821Ra266 の
     * 内蔵 LAN (Intel 82557) を見つけるのが目的で、IDE の probe ログに
     * 流される前に [pci] の行を出したい (票 TASK_LAN_82557 §2 L-A)。
     * NP21/W には 0CF8h が無いので「mech#1 absent」の 1 行で終わる —
     * 失敗ではなく正しい報告 ([V4])。 */
    pci_init();

    /* IDE/HDD 初期化と登録 (4ドライブ: IDE#0-#3) */
    tvram_print(0, 3, "IDE...", TATTR_GREEN);
    {
        int n = ide_init();
        if (n > 0) {
            int d;
            tvram_print(5, 3, "OK", TATTR_WHITE);
            for (d = 0; d < 4; d++) {
                if (ide_drive_present(d)) dev_register_hdd(d);
            }
        } else {
            tvram_print(5, 3, "no drive", TATTR_CYAN);
        }
    }

    /* HDD の幾何: BIOS (ローダの AH=84h) と IDENTIFY を 1 行ずつ。
     * 段 1 の設計値はこの 2 つで決める (票 TASK_HDD_INSTALL 段 0)。 */
    bootinfo_report();
    /* Build (日時 + コミット ID) と、ローダが検査して起動したイメージの CRC
     * (票 TASK_SERIAL_HOSTFS A-4 — 更新の証拠は Build ではなくこの CRC)。 */
    bootinfo_report_image();

    /* ATAPI CD-ROM 検出 */
    {
        int cd = atapi_init();
        if (cd > 0) {
            tvram_print(14, 3, "CD", TATTR_WHITE);
            dev_register_cdrom();
        }
    }

    /* ヒープ初期化 (VFSマウントでkmalloc使用のため、マウント前に必要) */
    tvram_print(24, 2, "HEAP...", TATTR_GREEN);
    kmalloc_init((void *)KHEAP_BASE, KHEAP_SIZE);
    {
        /* KHEAP_SIZE から出す (決め打ちの "320K" が 2026-09-23 の切り直しで嘘になった) */
        char hb[8];
        kutoa_dec((u32)(KHEAP_SIZE / 1024UL), hb, (int)sizeof(hb));
        kstrncat(hb, "K", sizeof(hb));
        tvram_print(31, 2, hb, TATTR_WHITE);
    }

    /* 自動マウント処理 */
    tvram_print(14, 3, "MOUNT...", TATTR_GREEN);
    {
        int i, rc;
        /* MAX_DEVICES 分確保する。lo0-lo3 が hd0/cd0 より先に登録されるため、
         * 固定8個だと後続デバイスが自動マウント対象から漏れる。 */
        const char *names[MAX_DEVICES];
        int num = dev_get_names(names, MAX_DEVICES);
        const char *root_dev;
        const char *root_fs;

        if (boot_drive == BOOT_DRIVE_FDD || boot_drive == BOOT_DRIVE_FDD_144) {
            root_dev = "fd0";
            root_fs = "fat";
        } else {
            root_dev = "hd0";
            root_fs = "ext2";
        }

        /* 1. ルートマウント */
        rc = vfs_mount("/", root_dev, root_fs);
        if (rc != VFS_OK) {
            tvram_print(23, 3, "root panic", TATTR_RED);
            for (;;) _halt();
        }
        tvram_print(23, 3, "root OK", TATTR_WHITE);

        /* 2. サブマウント (ルート以外) */
        for (i = 0; i < num; i++) {
            const char *dname = names[i];
            char mnt[VFS_MNTPATH_MAX];
            int j;
            Device *d = dev_find(dname);

            if (!d || d->type != DEV_BLOCK) continue;

            /* ルートと一致する場合はスキップ */
            {
                int match = 1;
                for (j = 0; root_dev[j] || dname[j]; j++) {
                    if (root_dev[j] != dname[j]) { match = 0; break; }
                }
                if (match) continue;
            }

            /* パス生成: "/dname/" */
            mnt[0] = '/';
            for (j = 0; dname[j] && j < VFS_MNTPATH_MAX - 4; j++) {
                mnt[1 + j] = dname[j];
            }
            mnt[1 + j] = '\0';

            /* フォールバックしながらマウント試行 */
            {
                int mounted = 0;
                /* CDデバイスにはiso9660のみ試行 */
                if (dname[0] == 'c' && dname[1] == 'd') {
                    if (vfs_mount(mnt, dname, "iso9660") == VFS_OK) mounted = 1;
                } else {
                    if (vfs_mount(mnt, dname, "ext2") == VFS_OK) mounted = 1;
                    if (!mounted) {
                        if (vfs_mount(mnt, dname, "iso9660") == VFS_OK) mounted = 1;
                    }
                    if (!mounted) {
                        vfs_mount(mnt, dname, "fat");
                    }
                }
            }
        }

        /* 3. HostDrv(NT) 自動マウント (/host) */
        if (hostdrvfs_detect()) {
            int hrc = vfs_mount("/host", "hostdrv", "hostdrv");
            if (hrc == VFS_OK) {
                tvram_print(32, 3, "HDRV OK", TATTR_WHITE);
            } else {
                tvram_print(32, 3, "HDRV NG", TATTR_RED);
            }
        } else {
            tvram_print(32, 3, "HDRV --", TATTR_CYAN);
        }
    }

    /* RTC初期化 */
    tvram_print(26, 3, "RTC...", TATTR_GREEN);
    rtc_init();
    tvram_print(32, 3, "OK", TATTR_WHITE);


    /* ページング初期化 (メモリ保護有効化) */
    tvram_print(37, 2, "PAGE...", TATTR_GREEN);
    paging_init(mem_kb);
    tvram_print(44, 2, "OK", TATTR_WHITE);

    /* コンベンショナルメモリ再利用 (ブート後のローダー領域等を解放) */
    paging_reclaim_conventional();

    /* KCGフォントキャッシュ初期化
     * コンベンショナルメモリ (0x01000〜) にキャッシュを配置しているため、
     * paging_reclaim 後にフラグをゼロクリアする必要がある。
     * これがないとBIOSデータの残骸がキャッシュ済みと誤認される。 */
    kcg_init();

    /* [DEBUG] カーネル初期化中にフォントロードをテスト。
     * 外部プログラム経由のクラッシュが、スタック/コンテキストの問題か
     * lz4_decode 自体の問題かを切り分けるための一時的なテスト。
     * 注意: Unicode テーブル (0x4A000) はまだロードされていないので
     *        LZ4 一時バッファとして安全に使用可能。 */
    tvram_print(0, 3, "FONT..", TATTR_GREEN);
    {
        int fret;
        /* FD (FAT) だけ 8.3 の短い名前へ落ちる (kernel/boot_font.c) */
        fret = boot_font_load();
        if (fret == 0) {
            tvram_print(6, 3, "OK ", TATTR_WHITE);
        } else {
            tvram_print(6, 3, "NG ", TATTR_RED);
            kprintf(0x07, "[KCG] kernel-init load failed: %d\n", fret);
        }
        /* FD の読みの数 (シーク / 省略 / 期限切れ / まとめ読み)。FD から
         * 起動したときフォントの読み込みが遅い件 (2026-09-24) の切り分け用。
         * **FD 起動のときだけ出す** — HDD 起動でも /fd0 のサブマウントの
         * 試行で数が付き、「font」と名乗るのは嘘になる (ラリー 2 の Fable)。 */
        if (boot_drive == BOOT_DRIVE_FDD || boot_drive == BOOT_DRIVE_FDD_144) {
            fdc_print_stats("font");
            diskio_print_fdd_cache("font");
        }
    }

    /* TTF由来フォント: 外部プログラムから kcg_load_font() (KAPI) で呼ぶ */

    /* FPU 初期化 (paging_init の後で CR0 に対して設定)
     * CR0.EM=0 (ネイティブFPU使用), CR0.TS=0 (タスクスイッチ不要)
     * SQLite が double 演算を使用するため必須 */
    {
        u32 cr0_val;
        __asm__ volatile("mov %%cr0, %0" : "=r"(cr0_val));
        cr0_val &= ~((u32)0x0C);  /* EM(bit2) と TS(bit3) をクリア */
        __asm__ volatile("mov %0, %%cr0" : : "r"(cr0_val) : "memory");
        __asm__ volatile("fninit");
    }

    /* 物理ページフレームアロケータ初期化 (paging_initの後) */
    if (!memory_boot_init(mem_kb)) {
        kprintf(0x07, "[MEM] bootstrap/stage failed; boot halted\n");
        for (;;) { _stop(); }
    }

    /* 共有メモリ初期化 (ガードページ設定 + R/W設定) */
    shm_init();

    /* DMA プール (票 §1-3)。paging_init が 0x2E8000-0x2F7FFF を present/RW で
     * 張った後、**pci_bind_all より前**。probe が dma_pool_alloc を呼ぶ。 */
    dma_pool_init();


    /* カーネル内プリミティブの自己診断。
     * 外部プログラムの klibc_test は newlib 側を試すだけで、カーネルが
     * 実際に使う kstring_asm / kmalloc / kprintf は一度も踏んでいない。
     * ここで境界ケースだけを実機で毎回確認する (全通過なら 1 行)。 */
    kselftest_run();

    /* PCI の結線 (票 §1-4)。**pgalloc と DMA プールの初期化の後**で、
     * pci_init() の直後ではない。**kselftest_run より後**なのは、
     * プールの自己診断が最後に dma_pool_init() で池を作り直すから —
     * 先に結線すると driver が取った span をそこで消してしまう。
     * 表はいまのところ空 (82557 の driver は別票 L-B) で、ここは
     * 「候補が 0 本でも安全に回る」ことを起動のたびに踏むための呼び出し。 */
    pci_bind_set_line_state_hook(kernel_pci_line_bits);
    pci_bind_all(pci_drivers, PCI_DRIVER_COUNT);

    /* CS4231 (MATE-X PCM) の検出だけ (票 TASK_PCM_CS4231 §2-1)。
     * 無い機械では `[pcm] none` を出して静かに戻る — 以降の起動は変わらない。 */
    pcm_init();

    /* FDリダイレクト初期化 (プログラムローダーより前に) */
    fd_redirect_init();

    /* パイプバッファ初期化 */
    pipe_buffer_init();

    /* プログラムローダー初期化 (KernelAPIテーブル構築) */
    tvram_print(48, 2, "EXEC...", TATTR_GREEN);
    exec_init();
    /* トランポリンページができてからでないと踏めない自己診断 (票 T9 §12 R1)。
     * kselftest_run() はここより前に走っている。 */
    kselftest_run_post_exec();
    tvram_print(56, 2, "OK", TATTR_WHITE);

    /* Unicodeテーブルロード */
    tvram_print(60, 2, "UNI...", TATTR_GREEN);
    kprintf(0x07, "[BOOT] UNI load: dst=%x size=%x\n",
            (unsigned)MEM_UNICODE_TABLE_BASE, (unsigned)MEM_UNICODE_TABLE_SIZE);
    {
        int bytes = vfs_read(SYS_UNICODE_BIN, (void *)MEM_UNICODE_TABLE_BASE, MEM_UNICODE_TABLE_SIZE);
        kprintf(0x07, "[BOOT] UNI load: bytes=%d\n", bytes);
        if (bytes == (int)MEM_UNICODE_TABLE_SIZE) {
            utf8_set_jis_table_ready(1);
            tvram_print(67, 2, "OK", TATTR_WHITE);
        } else {
            /* 表を有効化しない。ロード失敗のまま引くと、0x4A000 に
             * 残っていた別のデータを変換表として読んでしまう。 */
            utf8_set_jis_table_ready(0);
            tvram_print(67, 2, "ER", TATTR_RED);
        }
    }

    /* IME (FEP) 初期化 — 辞書は初回使用時に遅延ロード */
    tvram_print(0, 4, "IME..", TATTR_GREEN);
    kprintf(0x07, "[BOOT] ime_init...\n");
    ime_init();
    tvram_print(5, 4, "OK ", TATTR_WHITE);
    kprintf(0x07, "[BOOT] ime_init OK\n");

    /* サウンドエンジン初期化 */
    tvram_print(9, 4, "SND..", TATTR_GREEN);
    kprintf(0x07, "[BOOT] snd_init...\n");
    snd_init();
    tvram_print(14, 4, "OK ", TATTR_WHITE);
    kprintf(0x07, "[BOOT] snd_init OK\n");

    /* LAN (LGY-98)。CONFIG_LGY98_BASE=0 (既定) なら何もしない。
     * カード未装着・応答不良でも NIC だけ無効にして起動を続ける。 */
    lgy98_init();

    tvram_print(18, 4, "SQ..", TATTR_GREEN);
    kprintf(0x07, "[BOOT] sqlite_init...\n");

    /* SQLite エンジン初期化 (ブートローダーが 0x200000 にロード済み、
     * kentry.asm が .sqlite_bss をゼロクリア済み) */
    tvram_print(0, 4, "SQLite...", TATTR_GREEN);
    {
        int sq_rc;
        sq_rc = os32_sqlite_init();
        kprintf(0x07, "[SQ] init rc=%d\n", sq_rc);
        if (sq_rc == 0) {
            tvram_print(9, 4, "OK", TATTR_WHITE);
            /* 代替スタック(128KB)でSQLiteテスト実行 */
            {
                extern int os32_sqlite_test(void);
                extern int sqlite_call_on_alt_stack(
                    int (*func)(void), u32 stack_top);
                int trc = sqlite_call_on_alt_stack(
                    os32_sqlite_test, MEM_SQLITE_STACK_TOP);
                kprintf(0x07, "[SQ] kernel test rc=%d\n", trc);
            }
        } else {
            tvram_print(9, 4, "ER", TATTR_RED);
        }
    }

    /* 割り込み有効化 */
    tvram_print(71, 2, "IRQ_EN", TATTR_YELLOW);
    _enable();

    /* autoexec: シェルスクリプト(/etc/autoexec.bat)に移行済み。
     * シェル起動後に ui.c から script_source_file() で実行される。 */

    /* グラフィクスバックエンドの強制指定 (票 H2b、契約 G5)。
     * /etc/system.cfg の GFX= を **最初の gfx_init より前** に読んで HAL へ
     * 渡す (この直後の boot_splash が gfx_init を呼ぶ)。
     *   GFX=pc98   9801 プレーン強制 (NP21/W でプレーン経路を回帰試験する)
     *   GFX=pegc   PEGC 強制 (probe が通らなければ 9801)
     *   GFX=cirrus Cirrus GD54xx 強制 (probe が通らなければ 9801、票 H3)
     *   未指定     auto = probe 順 (Cirrus → PEGC → 9801)
     * K4 の GUI= と同じ流儀: ここで読み、サブシステム側の変数へ渡す。 */
    {
        char gfxmode[16];
        if (sysconfig_get_str(SYS_SYSTEM_CFG, "GFX", gfxmode, (int)sizeof(gfxmode)) > 0) {
            if (kstrcmp(gfxmode, "pc98") == 0) {
                gfx_set_backend_pref(GFX_PREF_PC98);
            } else if (kstrcmp(gfxmode, "pegc") == 0) {
                gfx_set_backend_pref(GFX_PREF_PEGC);
            } else if (kstrcmp(gfxmode, "cirrus") == 0) {
                gfx_set_backend_pref(GFX_PREF_CIRRUS);
            } else {
                gfx_set_backend_pref(GFX_PREF_AUTO);   /* auto / 未知の値 */
            }
        }
    }

    /* ブートスプラッシュ表示 (カーネル内蔵) */
    boot_splash();

    /* splash は PC98 を強制するので probe を走らせない。バックエンドの選択と
     * probe のキャッシュ温めはここで 1 回だけ、**カーネル文脈 (master PD)** で
     * 行う。PEGC の probe は BIOS ワークエリア 0x045C / 0x0597 を読み、そこは
     * アプリ PD に写像が無いので、初回 probe をアプリに繰り延べると CPL=3 で
     * #PF が起きてアプリが死ぬ (2026-09-09 に gdi_test で実測)。さらに
     * PEGC のバックバッファは主記憶の物理末尾側から取る予約なので、アプリが
     * 走っている時点ではそのスタック/ヒープに阻まれて必ず失敗する
     * (Cirrus の面はカード VRAM のリニア窓内で、予約は使わない)。
     * init まで済ませたら表示はテキストへ戻る。 */
    gfx_prepare_backend();

    /* 共有ライブラリ (0x400000 帯) を常駐させる — シェルを載せる **前** に
     * 1 回だけ (票 K3)。ここより後だと pgalloc が帯域のページを配ってしまう。
     * ライブラリが無ければ静かに未ロードで続行し、CUI は従来どおり動く。 */
    shlib_init();

    /* 最後の起動のログを /var/log/boot.log へ (前回分は .1、FAT は bootlog.1)。
     * ルートのマウントと自己試験・shlib の読み込みが済み、常駐シェルを
     * exec する直前。溜めるのはここまで (以後の出力は入らない)。ルートが
     * ext2 / FAT でなければ書かない。どこで失敗しても起動は続ける。 */
    bootlog_save();

    /* 外部シェル起動 — CUI(shell.bin) と GUI(gshell.bin) を同じシェル帯
     * (0x300000, Level 1) で入れ替えながら回す (契約 T9)。両者は同時に
     * 載らない。終了/クラッシュ時のフォールバックとして再起動ループにする。
     *
     * 起動シェルの決定順:
     *   1. シェルが sys_switch_shell() で残した切替要求 (gui_take_next_shell
     *      で取り出して消す。consume 方式、レビュー #3 ④)。
     *   2. 記録が無ければ、起動時に読んだ /etc/system.cfg の GUI= に従う
     *      (GUI=1 → gshell、既定 → shell)。 */
    {
        const char *cur_shell = SYS_SHELL_BIN;
        char consumed[OS32_MAX_PATH];   /* 取り出した切替パス (cur_shell が指す) */
        int gui_fault_streak = 0;       /* gshell 連続クラッシュ回数 */
        consumed[0] = '\0';

        /* 起動時: system.cfg の GUI=1 なら gshell を既定にする */
        if (sysconfig_get_int(SYS_SYSTEM_CFG, "GUI", 0) == 1) {
            cur_shell = SYS_GSHELL_BIN;
        }

        for (;;) {
            int rc;
            int is_gui;

            /* 切替要求を取り出して消す (consume 方式、レビュー #3 ④)。
             * 取り出した後は要求が空になるので、gshell のロード失敗や連続
             * fault で CUI に落ちた後でも、次の os32gui が普通に採用される。 */
            if (gui_take_next_shell(consumed, OS32_MAX_PATH)) {
                cur_shell = consumed;
                gui_fault_streak = 0;   /* 別シェルへ切替 → 連続失敗をリセット */
            }

            is_gui = (kstrcmp(cur_shell, SYS_GSHELL_BIN) == 0);

            /* 共有ライブラリを断っていたら GUI は動かない。CUI に落として
             * 案内する。**直し方は理由で逆向き**なので案内を分ける:
             *   配置違い … GUI アプリが共有メモリの番地を取り違える。/sys を
             *              作り直して配備 (票 TASK_KAPI_DATA_FIELDS)
             *   要求版   … ライブラリの方が新しい。カーネルを先に更新する
             *              (実装レビュー ラリー 2 の B1。以前は案内が無く、
             *              gshell の読み込み失敗としか出なかった) */
            if (is_gui && shlib_reject_reason() == SHLIB_REJECT_LAYOUT) {
                kprintf(0xC1, "[boot] %s: rebuild required (KAPI data layout)"
                        " -> CUI shell. rebuild /sys and deploy\n", SYS_SHLIB_GUI);
                console_text_gdc_start();
                tvram_clear();
                tvram_print(0, 0, "GUI shlib: rebuild required (KAPI data layout) -> CUI shell",
                            TATTR_RED);
                cur_shell = SYS_SHELL_BIN;
                is_gui = 0;
            } else if (is_gui && shlib_reject_reason() == SHLIB_REJECT_MIN_API) {
                kprintf(0xC1, "[boot] %s: needs KAPI v%u > kernel v%u -> CUI shell."
                        " update the kernel first\n", SYS_SHLIB_GUI,
                        shlib_reject_min_api(), (u32)KAPI_VERSION);
                console_text_gdc_start();
                tvram_clear();
                tvram_print(0, 0, "GUI shlib: needs a newer kernel -> CUI shell",
                            TATTR_RED);
                tvram_print(0, 1, "  update the kernel first (hsync boot / deploy-kernel)",
                            TATTR_RED);
                cur_shell = SYS_SHELL_BIN;
                is_gui = 0;
            }

            /* GUI は全画面 GFX を握るのでテキストカーソルを消す。CUI は出す。 */
            if (is_gui) {
                console_text_gdc_stop();
                tvram_print(0, 0, "Loading gshell...", TATTR_GRAY);
            } else {
                /* prepare が TVRAM を消さなくなった (PEGC、2026-09-24) ので、
                 * 0 行目には起動ログが残っている。文言の後ろを空白で消す。 */
                static const char loading[] = "Loading shell...";
                int col;
                console_text_gdc_start();
                tvram_print(0, 0, loading, TATTR_GRAY);
                for (col = (int)sizeof(loading) - 1; col < TVRAM_COLS; col++)
                    tvram_putchar_at(col, 0, ' ', TATTR_GRAY);
            }

            rc = exec_run(cur_shell);

            /* gshell がロードできない (未存在、不正バイナリ等) → 警告して CUI に
             * 落ちる。fault (実行後のクラッシュ) は別扱い。要求は取り出し済みで
             * 空なので、修復後の os32gui は普通に採用される。 */
            if (is_gui && rc < 0 && rc != EXEC_ERR_FAULT) {
                console_text_gdc_start();
                tvram_clear();
                tvram_print(0, 0, exec_layout_rejected()
                            ? "gshell: rebuild required (KAPI data layout) -> CUI shell"
                            : "gshell load failed -> CUI shell", TATTR_RED);
                cur_shell = SYS_SHELL_BIN;
                gui_fault_streak = 0;
                continue;   /* ウェイト無しで即 CUI を起動 */
            }

            /* gshell がクラッシュ (fault_kill) を繰り返すなら CUI に落とす保険。
             * 連続 3 回で CUI へ (完了条件: gshell 再起動の無限ループ回避)。 */
            if (is_gui) {
                if (rc == EXEC_ERR_FAULT) {
                    gui_fault_streak++;
                    if (gui_fault_streak >= 3) {
                        tvram_print(0, 0, "gshell crashed 3x -> CUI shell", TATTR_RED);
                        cur_shell = SYS_SHELL_BIN;
                        gui_fault_streak = 0;
                    }
                } else {
                    gui_fault_streak = 0;   /* 正常終了なら連続失敗をリセット */
                }
            } else if (rc < 0 && rc != EXEC_ERR_FAULT) {
                /* CUI シェルのロード自体が失敗 — FDD フォールバックを試す */
                int layout_bad = exec_layout_rejected();
                rc = exec_run(SYS_SHELL_BIN_FDD);
                if (rc < 0 && rc != EXEC_ERR_FAULT) {
                    layout_bad |= exec_layout_rejected();
                    if (layout_bad) {
                        /* 常駐シェルが KAPI データ欄の配置違い (票
                         * TASK_KAPI_DATA_FIELDS)。走らせても malloc が壊れる
                         * ので止める。直し方は /sys の作り直しと配備 — NHD は
                         * エミュレータ停止中に一式、実機は FD / CD の入れ直し。 */
                        kprintf(0xC1, "[boot] FATAL: shell.bin was built for another"
                                " KAPI data layout (kernel %x)\n",
                                (u32)KAPI_DATA_FIELDS_OFF);
                        tvram_print(0, 0, "FATAL: shell.bin: rebuild required (KAPI data layout)",
                                    TATTR_RED);
                        tvram_print(0, 1, "  rebuild /sys (make clean && make all) and deploy it",
                                    TATTR_RED);
                        tvram_print(0, 2, "  (NHD: whole set while stopped / real HW: boot the new FD)",
                                    TATTR_RED);
                        for (;;) _halt();
                    }
                    /* シェルバイナリのロード自体が失敗 — 致命的エラー */
                    tvram_print(0, 0, "FATAL: shell.bin load failed", TATTR_RED);
                    for (;;) _halt();
                }
            }

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
            tvram_clear();
        }
    }

    /* 失敗時フォールバック */
    for (;;) {
        _halt();
    }
}
