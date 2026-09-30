/* ======================================================================== */
/*  CONFIG.H — OS32 システム全般設定                                         */
/*                                                                          */
/*  システムの挙動、デフォルトファイルパス、各種タイムアウト等の定数を定義します。*/
/* ======================================================================== */

#ifndef CONFIG_H
#define CONFIG_H

/* ====================================================================== */
/*  バージョン                                                              */
/*                                                                          */
/*  OS 全体のバージョン文字列。git のタグ (vX.Y.Z) と歩調を合わせること。   */
/*  ここが唯一の定義で、ver コマンドはこれを表示する。                       */
/* ====================================================================== */
#define SYS_VERSION           "2.0"

/* ====================================================================== */
/*  スタートアップ設定                                                       */
/* ====================================================================== */
#define SYS_SHELL_BIN         "/sys/shell.bin"     /* システムシェル (HDD/FDD共通) */
#define SYS_SHELL_BIN_FDD     "/sys/shell.bin"     /* FDDフォールバック (統一パス) */
#define SYS_GSHELL_BIN        "/bin/gshell.bin"    /* GUI シェル (WM 常駐、W1 が実体を作る、契約 T9) */
#define SYS_SYSTEM_CFG        "/etc/system.cfg"    /* 起動設定 (GUI=0/1、K4) */
#define SYS_PROFILE_SYS       "/etc/profile"       /* システムプロファイル */
#define SYS_UNICODE_BIN       "/sys/unicode.bin"   /* Unicodeテーブル */
#define SYS_SQLITE_BIN        "/sys/sqlite.bin"    /* SQLite拡張域バイナリ */
/* 既定の 16px フォント (KCG)。HDD / CD は長い名前。FD (FAT、FatFs は LFN なし)
 * に置くなら 8.3 の短い名前 — 2026-09-25 からフォントは NORMAL で、起動 FD
 * (= MINIMAL) には載せていない (build/packages.yaml)。カーネルは長い名前が
 * 読めず、**かつそのパスのマウントが FAT のときだけ**短い名前を読む
 * (kernel/boot_font.c)。HDD で長い名前が欠けても短い名前は掴まない。
 * どちらも無ければ本体のフォント ROM で描く (起動は止まらない)。 */
#define SYS_FONT_DEFAULT      "/sys/font/default.kcgfont"
#define SYS_FONT_DEFAULT_83   "/sys/font/default.kcg"
#define SYS_FONT_83_FSTYPE    "fat"      /* 短い名前を許す FS (vfs_fstype の名前) */
/* 起動ログ (kernel/bootlog.c)。最初の kprintf から常駐シェルを exec する
 * 直前までの出力を溜め、ルートが ext2 / FAT のときだけ書き出す。実機で
 * 流れてしまう [selftest] などを `cat /var/log/boot.log` で読むため。
 * FAT (FatFs、LFN なし) は 8.3 しか作れないので、前回分だけ名前が違う。 */
#define SYS_BOOTLOG_VAR_DIR   "/var"
#define SYS_BOOTLOG_DIR       "/var/log"
#define SYS_BOOTLOG_FILE      "/var/log/boot.log"
#define SYS_BOOTLOG_NEW       "/var/log/boot.new"     /* 今回分の一時ファイル (8.3) */
#define SYS_BOOTLOG_OLD       "/var/log/boot.log.1"   /* ext2 */
#define SYS_BOOTLOG_OLD_83    "/var/log/bootlog.1"    /* FAT (8.3) */
#define SYS_BOOTLOG_FS_EXT2   "ext2"     /* 書き出す FS (vfs_fstype の名前) */
#define SYS_BOOTLOG_FS_FAT    "fat"
#define SYS_BOOTLOG_TEXT_MAX  0x4000UL   /* 本文の容量 16KB (静的配列) */
#define SYS_SHLIB_GUI         "/sys/lib/libos32gui.shlib"
                                                   /* GUI 共有ライブラリ (K3/C3)。
                                                    * MEM_SHLIB_BASE 常駐。無ければ
                                                    * 静かに未ロード (CUI は動く) */

/* ====================================================================== */
/*  デフォルトコマンド検索パス                                                */
/* ====================================================================== */
#define SYS_DEFAULT_PATH      "/bin:/sbin:/usr/bin"
#define SYS_DEFAULT_HOME      "/home/user"
#define SYS_DEFAULT_SHELL     "/shell"

/* ====================================================================== */
/*  RS-232C シリアル設定                                                     */
/* ====================================================================== */
/* RS-232C の既定速度。
 *
 * **9600 は 1.9968MHz と 2.4576MHz の**どちらでもちょうど出る**唯一の標準速度。**
 * 速度は 8253 TCU カウンタ#2 の分周 (整数) で決まるので、割り切れない値は
 * 必ずずれる — 1.9968MHz (8MHz系) で 38400 を頼むと count=3 になり
 * 実効 41600bps (+8.3%)。UART の許容を超えるので実機では通らない。
 * 詳しい表と根拠は drivers/serial.h、経緯は docs/POLICY_DEBUG.md §4-49。
 *
 * 2026-09-18 まで 38400 だった。エミュレータは通信速度をモデル化して
 * いないので通っていただけで、**実機では未検証のまま壊れていた。**
 * 速くしたい実機では 0434h の 4 分周を外すか (極性が機種依存、serial.h)、
 * 2.4576MHz 機なら 38400 がちょうど出る。 */
#define SYS_SERIAL_BAUD       9600

/* ====================================================================== */
/*  ブロックデバイスのセクタ長 (ユーザーランドから見える値)                  */
/*                                                                          */
/*  drivers 側の正典は ATAPI_SECTOR_SIZE (drivers/atapi.h) と               */
/*  FDC_SECTOR_SIZE (drivers/fdc.h)。どちらもカーネル内部ヘッダなので、      */
/*  外部プログラム (シェルの dd 等) が読む口をここに置く。値を変えるときは    */
/*  必ず drivers 側と一緒に見ること。                                        */
/* ====================================================================== */
#define SYS_CDROM_SECTOR_SIZE 2048    /* = ATAPI_SECTOR_SIZE (cd*) */
#define SYS_HDD_SECTOR_SIZE   512     /* IDE の物理セクタ (hd*) */
#define SYS_BLOCK_SECTOR_SIZE 1024    /* = FDC_SECTOR_SIZE (fd* ほか既定) */

/* ====================================================================== */
/*  LAN (LGY-98, docs/tasks/network/PLAN.md)                                */
/*                                                                          */
/*  BASE 0 = 無効 (既定)。有効化はビルド時: make kernel-lgy98 (戻すのは        */
/*  kernel-nolgy98)。build/config.mk がスタンプを見て CONFIG_LGY98_* を -D で   */
/*  渡す。広域スキャンや IRQ の                                              */
/*  自動検出はしない。値は PIC IRQ (3 / 5 / 6)。                             */
/* ====================================================================== */
#ifndef CONFIG_LGY98_BASE
#define CONFIG_LGY98_BASE     0
#endif
#ifndef CONFIG_LGY98_IRQ
#define CONFIG_LGY98_IRQ      0
#endif
#ifndef CONFIG_LGY98_FLAGS
#define CONFIG_LGY98_FLAGS    0       /* LGY98_FLAG_DIAG 1 | LGY98_FLAG_LOOPBACK 2 */
#endif

#endif /* CONFIG_H */
