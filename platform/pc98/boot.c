/* PC-9821 boot wiring. Each callback retains its original boot boundary. */
#include "boot.h"
#include "boot_names.h"
#include "boot_disk.h"
#include "pci_resources.h"
#include "../../kernel/boot_display.h"
#include "../../drivers/kbd.h"
#include "../../drivers/mouse.h"
#include "../../drivers/fdc.h"
#include "../../drivers/dma8237.h"
#include "../../drivers/ide.h"
#include "../../drivers/pci.h"
#include "../../drivers/rtc.h"
#include "../../drivers/kcg.h"
#include "../../drivers/lgy98.h"
#include "dev.h"
#include "path.h"
#include "loop_dev.h"
#include "vfs.h"
#include "ext2.h"
#include "hostdrvfs.h"
#include "serialfs.h"
#include "io.h"
#include "pc98.h"
#include "tvram.h"
#include "kprintf.h"

extern int fatfs_init(void);
extern void pc98_d2_bind(void);

/* dev_findをPathDeviceValidatorとして使用するラッパー */
static int dev_find_validator(const char *name)
{
    return (dev_find(name) != (void *)0) ? 1 : 0;
}

static void pc98_boot_dma(const struct boot_context *ctx)
{
    (void)ctx;
    /* 8237 の共通部。0439h bit2 (1MB 超への DMA 禁止) を落とすのはここ —
     * **fdc_init より前**。プールもカーネルも 1MB 超にあるので、FDC だけの
     * 都合ではない (票 §1-2)。起動行 `[dma] 0439h xx -> yy state=…` を出す。 */
    dma8237_init();

}

static void pc98_boot_input(const struct boot_context *ctx)
{
    (void)ctx;
    /* キーボード初期化。カナ・CAPS の初期値は保存済み BDA (053Ah) から取る。
     * page0 は通常 NP (include/pc98.h BIOS_WORK_KB_SHIFT、memmap.h)。 */
    tvram_print(48, 1, "KBD...", TATTR_GREEN);
    kbd_init();
    tvram_print(54, 1, "OK", TATTR_WHITE);

    /* マウスドライバ初期化 (NP21/W検出→モード自動選択) */
    mouse_init();

}

static void pc98_boot_floppy(const struct boot_context *ctx)
{
    /* FDC初期化 (I/Oポート直接制御) */
    tvram_print(60, 1, "FDC...", TATTR_GREEN);
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
    fdc_set_media_by_daua(0, ctx->drive);
    if ((ctx->drive & BIOS_DA_TYPE_MASK) == BOOT_DRIVE_FDD_144 || (ctx->drive & BIOS_DA_TYPE_MASK) == BIOS_DA_FD_144_ALT) {
        u8 m3 = (u8)inp(FDC_IO_3MODE);
        tvram_print(69, 1, "144", TATTR_GREEN);
        tvram_print(72, 1, (m3 & FDC_3M_CUR_144) ? "M" : "?",
                    (m3 & FDC_3M_CUR_144) ? TATTR_WHITE : TATTR_RED);
    }

}

static void pc98_boot_dev_fs(const struct boot_context *ctx)
{
    (void)ctx;
    /* デバイス・パスシステム初期化 */
    tvram_print(58, 1, "DEV...", TATTR_GREEN);
    dev_init();
    loop_dev_init();  /* lo0..lo3 ループバックデバイス登録 */
    path_init();
    int fs_registration_failed = ext2_init() != VFS_OK;
    fs_registration_failed |= fatfs_init() != VFS_OK;
    fs_registration_failed |= hostdrvfs_init() != VFS_OK;
    fs_registration_failed |= serialfs_init() != VFS_OK;
    path_set_device_validator(dev_find_validator);
    tvram_print(64, 1, fs_registration_failed ? "NG" : "OK", TATTR_WHITE);
    if (fs_registration_failed)
        kprintf(TATTR_WHITE, "VFS: filesystem registration failed\n");

}

static void pc98_boot_pci_enum(const struct boot_context *ctx)
{
    (void)ctx;
    /* PCI の列挙 (読むだけ)。**IDE より前**に置く: 実機 PC-9821Ra266 の
     * 内蔵 LAN (Intel 82557) を見つけるのが目的で、IDE の probe ログに
     * 流される前に [pci] の行を出したい (票 TASK_LAN_82557 §2 L-A)。
     * NP21/W には 0CF8h が無いので「mech#1 absent」の 1 行で終わる —
     * 失敗ではなく正しい報告 ([V4])。 */
    pci_init();

}

static void pc98_boot_disk(const struct boot_context *ctx)
{
    (void)ctx;
    /* IDE/HDD 初期化と登録 (4ドライブ: IDE#0-#3) */
    tvram_print(0, 3, "IDE...", TATTR_GREEN);
    {
        int n = ide_init();
        if (n > 0) {
            int d;
            tvram_print(5, 3, "OK", TATTR_WHITE);
            for (d = 0; d < IDE_MAX_DRIVES; d++) {
                if (ide_drive_present(d)) dev_register_hdd(d);
            }
        } else {
            tvram_print(5, 3, "no drive", TATTR_CYAN);
        }
    }

}

static void pc98_boot_auto_mount(const struct boot_context *ctx)
{
    (void)ctx;
    /* HostDrv(NT) 自動マウント (/host) */
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

static void pc98_boot_clock(const struct boot_context *ctx)
{
    (void)ctx;
    /* RTC初期化 */
    tvram_print(26, 3, "RTC...", TATTR_GREEN);
    rtc_init();
    tvram_print(32, 3, "OK", TATTR_WHITE);


}

static void pc98_boot_font(const struct boot_context *ctx)
{
    (void)ctx;
    /* ROM/cache storage belongs to the POST_ROOT KCG module. */
    kcg_init();

}

static void pc98_boot_resources(const struct boot_context *ctx)
{
    (void)ctx;
    /* ⑥-0 (TASK_T1_LEDGER §3-3、B4): pci_init は台帳より前に走るので、g_pci の
     * メモリ BAR をここで資源表へ RAW (採取値、予約権限なし) として取り込む。
     * 実機では [pci] の行のメモリ BAR の本数と raw= が一致し ovf=0 のこと。 */
    pc98_boot_pci_resources();
}

static void pc98_boot_nic_legacy(const struct boot_context *ctx)
{
    (void)ctx;
    /* LAN (LGY-98)。CONFIG_LGY98_BASE=0 (既定) なら何もしない。
     * カード未装着・応答不良でも NIC だけ無効にして起動を続ける。 */
    lgy98_init();

}

static const struct boot_platform pc98_boot_ops = {
    .step = {
        [BOOT_DMA] = pc98_boot_dma,
        [BOOT_INPUT] = pc98_boot_input,
        [BOOT_FLOPPY] = pc98_boot_floppy,
        [BOOT_DEV_FS] = pc98_boot_dev_fs,
        [BOOT_PCI_ENUM] = pc98_boot_pci_enum,
        [BOOT_DISK] = pc98_boot_disk,
        [BOOT_AUTO_MOUNT] = pc98_boot_auto_mount,
        [BOOT_CLOCK] = pc98_boot_clock,
        [BOOT_FONT] = pc98_boot_font,
        [BOOT_PCI_RESOURCES] = pc98_boot_resources,
        [BOOT_NIC_LEGACY] = pc98_boot_nic_legacy,
    }
};

void pc98_boot_bind(struct boot_context *ctx, u32 drive)
{
    ctx->drive = drive;
    if (drive == BOOT_DRIVE_FDD || drive == BOOT_DRIVE_FDD_144) {
        ctx->root_dev = "fd0";
        ctx->root_fs = "fat";
    } else {
        ctx->root_dev = "hd0";
        ctx->root_fs = "ext2";
    }
    pc98_d2_bind();
    pc98_boot_disk_bind();
    pc98_boot_names_bind();
    boot_platform_register(&pc98_boot_ops);
}
