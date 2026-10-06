/* ======================================================================== */
/*  OS32_KAPI_SHARED.H — KernelAPI 共有定義                                  */
/*                                                                          */
/*  カーネル (exec.h) と外部プログラム (os32api.h) の両方がインクルードする   */
/*  唯一の情報源 (Single Source of Truth)。                                  */
/*                                                                          */
/*  ■ 制約:                                                                */
/*    - このヘッダはカーネル内部ヘッダに一切依存してはならない               */
/*    - 基本型 (u8/u16/u32等) はここで自己完結的に定義する                   */
/*    - KernelAPI構造体のレイアウト変更は必ずここで行い、                    */
/*      exec.h / os32api.h で重複定義しないこと                              */
/* ======================================================================== */

#ifndef OS32_KAPI_SHARED_H
#define OS32_KAPI_SHARED_H

/* ======================================================================== */
/*  基本型定義 (フリースタンディング環境用)                                   */
/* ======================================================================== */

typedef unsigned char  u8;
typedef unsigned short u16;
typedef unsigned long  u32;
typedef signed char    i8;
typedef signed short   i16;
typedef signed long    i32;

#ifndef NULL
#define NULL ((void *)0)
#endif

#ifndef __cdecl
#define __cdecl __attribute__((cdecl))
#endif

/* ======================================================================== */
/*  KernelAPI バージョン                                                     */
/* ======================================================================== */

#define KAPI_VERSION      69   /* v69 = T2c: OS32X v4 (60B)、独立4世代、高位APP/shlib・可変stack。slot追加なし。v68 = 実機の ROM の INT 18h AH=31h/30h の I/O 記録 (票 TASK_PEGC480_REALHW §3 段 1): v86_gdc_capture(mode, out) — mode = V86G_MODE_ROM は AH=31h で今のモードを読み、その bit の並びから 640x480 の AH=30h を決めて呼び、同じ AH=30h で元のモードへ戻し (戻れなければ OS32 の表で)、CUI を作り直す。その間に捕まえた OUT を畳まずに最大 512 件、IN をポートごとの回数で V86Gcap (8460 バイト) へ写す。V86G_MODE_SELFTEST は決まった I/O 列の試験ゲストで記録器を確かめる (実機へ通さない)。戻り = status (V86G_ST_*、>= 0) / OS32_ERR_INVAL (GUI 中・mode 不正。out が NULL は CPL=0/WM のみ、CPL=3 は wrap で kill) / OS32_ERR_BUSY (V86 使用中・480 ライン表示中) / OS32_ERR_NOSPC (slot 239)。v86 -g が使う。v67 = キーボードの受信記録 (票 TASK_KBD_NAV §3 の準備): kbd_diag_log(after_seq, out, max) — IRQ1 が 0041h から使うバイトを読むたびに積む 32 件の循環リング (KbdDiagLogEnt 8 バイト: seq / 生の code (bit7 = break) / 処理後の修飾 / KBD_DLOG_F_*) から、after_seq より新しい分を古い順に最大 max 件写す。戻り = 件数 / OS32_ERR_INVAL (max <= 0、または CPL=0/WM で out が NULL。CPL=3 の NULL + max > 0 は wrap で kill)。EMPTY / ERROR で捨てたバイトは積まない。上書きで失われた分は写した先頭の seq の飛びで分かる (slot 238)。シェルの kbdstat -w が使う。v66 = シリアル越しの /host (票 TASK_SERIAL_HOSTFS 部品 B): sfs_begin — 常駐シェル (owner 1) の `sfs run` だけがセッションを開く (/host が空いていること → 送受信のゲートを上げる → HELLO → /host に SerialFS をマウント)。0 / OS32_ERR_INVAL (owner 1 以外・シリアル未初期化・IF=0) / OS32_ERR_BUSY (セッション中・/host 使用中) / OS32_ERR_IO (HELLO に答えが無い、ゲートは下ろして戻る) / vfs_mount の負値。sfs_end(exit_code) — BYE → アンマウント → 隔離 (500ms 静まるまで、上限 5 秒で打ち切り) → 溜めた出力と終了コードを長さ付きのフレームで送る → ゲートを下ろす。0 / KAPI_SFS_END_NOT_QUIET (1、線が静まらなかった) / OS32_ERR_INVAL (セッションが無い・owner 1 以外)。serial_diag — SerialDiag (16 バイト: 受信の OE / FE / PE の数と受信リング溢れ) を写す。0 / OS32_ERR_INVAL (CPL=0/WM で out が NULL。CPL=3 は wrap で kill) (slot 235〜237)。同じ版で vfs_mount が同じ prefix の二重登録を OS32_ERR_EXIST で断るようにした。v65 = 起動したイメージの識別 (票 TASK_SERIAL_HOSTFS 部品 A-4): boot_image_info — BootImageInfo (40 バイト: ローダが検査して起動した vmkernel.lz4 (VK32 v2) のファイル全体の CRC32・長さ・記録の有無・どのローダか、カーネルを組んだ git のコミット ID) を呼び手のバッファへ写す (slot 234)。0 = 成功 / OS32_ERR_INVAL = CPL=0/WM で out が NULL (CPL=3 は wrap で kill)。同じ版で VK32 を v2 (エントリごとの CRC32 + 完全長 + ファイル全体の CRC32) に、ブート情報域を v2 (0x30〜0x3F のイメージ欄) にした — ローダ (FD / HDD) とカーネルを揃えて入れ替える。v64 = HDD の一時置き場 (票 TASK_HDD_INSTALL 段 1): ext2_format_at (区画表を読まずに [start, start+length) だけに ext2 を作る。ディスク総数超過・LBA 0〜17 に掛かる・桁あふれは 1 バイトも書かずに EXT2_ERR_INVAL、大きさは最終グループに全メタデータが収まる長さへ固定点で切り下げ) / dev_mount_count (hd<drv> を指すマウントの数、ルートも数える) / sys_umount_checked (sync を先に呼び失敗なら外さずその負値、ルートは OS32_ERR_BUSY、未マウントは OS32_ERR_NOTFOUND。既存 sys_umount の void は変えない) / hdd_geom_info (HddGeom 32 バイト: BIOS 幾何 INT 1Bh AH=84h の生の値と規則の判定、IDENTIFY の既定・現在・word 49/53・総数、I/O の方式 HDD_AMODE_*) の 4 本 (slot 230〜233)。データ欄は動かない (v63 で固定) ので crt の kapi の実名は os32_kapi_v63 のまま。同じ版で ATA I/O を word 49 bit9 なら LBA28 に (無ければ現在の CHS、それも無ければ既定の CHS)、区画表を PC-98 標準配置に、ext2 の区画探索を「見つからなければ失敗」(旧 LBA 1088 のフォールバック廃止) に変えた。v63 = データ欄の固定配置 (票 TASK_KAPI_DATA_FIELDS): 関数表の容量 KAPI_FUNC_CAPACITY (R = 300) を予約し、sbrk_heap_limit / shm_base を KAPI_DATA_FIELDS_OFF (0x4B8 / 0x4BC) に固定した。以後関数を足してもデータ欄は動かない。予約スロットはカーネル側が kapi_reserved_nosys (OS32_ERR_NOSYS)、CPL=3 のトランポリンは int 0x80 のスタブ (slot >= KAPI_FUNC_COUNT で kill)。OS32X ヘッダを v3 (48 バイト、末尾に kapi_data_off) にし、exec / shlib ローダ / 常駐シェルの起動は kapi_data_off がカーネルと違えば断る (v2 以前も断る = 全再ビルド)。crt の大域変数 kapi の実名を os32_kapi_v63 に変え、作り直し忘れの .o をリンクで落とす。関数の追加は無い。v62 = キーボード 8251 の診断 (実機の打鍵不達、POLICY_DEBUG §4-57): kbd_diag — KbdDiag (24 バイト: irq_count / empty_count / err_count / flushed / init_st_before / init_st_after / last_st / last_code / cmd / now_st / reserved[2]、now_st は呼んだ時点の 0043h) を呼び手のバッファへ写す。0 = 成功 / OS32_ERR_INVAL = CPL=0/WM で out が NULL (CPL=3 は wrap で kill)。出力が読み取り専用の USER ページなら ring3_fault_kill (生成ラッパの out 検査)。シェルの kbdstat が 1 行で出す。同じ変更でカーネルが 0043h に書くコマンド語を 0x14 → 0x16 (DTR = 1 = RTY# HIGH、BIOS の定常値) に直した。v61 = CS4231 (MATE-X PCM) の再生 (票 TASK_PCM_CS4231 §2-2): pcm_open / pcm_write / pcm_status / pcm_close / pcm_set_volume の 5 本。16 ビット・ステレオ・44.1k / 22.05kHz だけ、単位は frame (左右 1 組 = 4 バイト)。カーネルが DMA リング 16KB とステージング 16KB を持ち、**アプリのバッファを IRQ から読むことはしない** — pcm_write はステージングへ写して受け取ったバイト数 (frame の倍数、0 = 満杯 → sys_yield して再試行) を返す。pcm_open は 44100 / 22050 だけを受け、0 / OS32_ERR_NOSYS (装置なし) / OS32_ERR_FULL (他の owner が open 中・IRQ10 に結べない) / OS32_ERR_INVAL (rate) / OS32_ERR_NOSPC (DMA プール) / OS32_ERR_IO (初期化の期限)。pcm_status は free_bytes (ステージングの空き) と counters = (underruns<<24) | (repeats<<16) | resyncs (8/8/16 ビットで飽和) を**出力 2 本**で返す (v59 と同じ出力保護)。pcm_close は drain して止まるまで待つ (期限つき。drain が失敗していたら OS32_ERR_IO)。pcm_set_volume は percent 1〜100 を I6/I7 の 6 ビット減衰へ線形に写し、0 はミュート、101 以上は OS32_ERR_INVAL。所有者は既存の資源 owner と同じアプリ ID で、異常終了は exec_reclaim_owned の pcm_reclaim が待たずに止めて返す。実体は drivers/pcm_cs4231.c。v60 = PCI 結線の診断 (票 TASK_HAL_WIRING §1-4): pci_bind_info — idx 番目 (pci_get と同じ列挙順) の結線結果を呼び手のバッファへ**8 バイトちょうど**写す。並びは drivers/pci_bind.h の struct pci_bind_info (bus / dev / fn / result / irq / reason / line_state / pad)。result = NONE(0) / BOUND(1) / DECLINED(2) / QUARANTINED(3)、line_state = OK(0) / STORM_MASKED(1) / QUARANTINED(2) は**読む時点で合成**するので、結線の後で線が隔離されても BOUND のまま line_state だけが変わる。既存 pci_get の 40 バイトは広げない (旧呼び手のバッファ)。戻り 0 = 成功 / OS32_ERR_INVAL = out が NULL・8 バイトが帯境界を跨ぐ・idx が範囲外 (**負のときは 1 バイトも書かない**)。出力が読み取り専用の USER ページなら ring3_fault_kill。シェルの lspci が注記 (bound (ok) irq=N / declined (<reason>) / quarantined (<reason>) / [irq N quarantined]) を出す。v59 = µs 時計 (票 TASK_HAL_WIRING §1-5): sys_time_now — 起動からの経過を µs で返す。64 ビットは KAPI で返せないので**出力引数 2 本**に 同じスナップショットの上下を書く。時間源は tick_count (どちらのシステムクロックでも ちょうど 10ms) + PIT ch0 のラッチ読みで、周期境界は PIC1 の IRR bit0 をラッチの前後で 挟んで判定する。戻り 0 = 成功 / OS32_ERR_AGAIN = 境界で 3 回続けて判定できなかった / OS32_ERR_NOSYS = PIT 未初期化か mode 2 でない。**負のときは 2 本とも書かない**。lo / hi が NULL・4 バイトが帯境界を跨ぐ・2 本の範囲が交差する (差 0〜3) のは OS32_ERR_INVAL。v58 = 実機の PCI 列挙 (票 TASK_LAN_82557 L-A): pci_count — 起動時に列挙して記録したデバイス数 (PCI が無い機械・NP21/W では 0)。pci_get — idx 番目の記録を呼び手のバッファへ**写す** (40 バイト固定、カーネルのポインタは渡さない)。並びは drivers/pci.h の struct pci_dev と同じで、外部プログラムは同じ並びの写しを自分で持つ (IdeInfo と同じ作法)。pci_cfg_read32 — コンフィギュレーション空間の生読み (`pcidump` 用)。**書きは公開しない** — BIOS の割り当てを壊さないため。v57 = ローカル打鍵だけの読み口 (票 TASK_SERIAL_VFAST 往復 4、Codex ④): kbd_trygetchar_local — cooked リングだけを見て、シリアルも注入リングも見ない。rshell が 「この 1 バイトはシリアル由来か」を 1 回の読みで確定できるようにする (2 度読みのあいだに届いたバイトが由来の印を落とす窓を消す)。⚠ **番号は PM が着地時に振り直す** — 同じ日に別レーン (L-A の pci_*) も v57 を取っている。v56 = 実機のシリアルを 115200bps まで上げる (票 TASK_SERIAL_VFAST): serial_init_vfast — FIFO 搭載機 (0136h で判定) で V･FAST モード (013Ah bit7) に入る。0 = 入った / -1 = FIFO 非搭載か表に無い速度で 互換モードに落ちた。serial_get_status — いまのモード (0=互換 / 1=V･FAST)・実効速度・FIFO の有無を 3 つの u32 で返す (カーネルのポインタは渡さない)。起動時の既定 9600 は従来どおり互換モードで、V･FAST は明示的に呼んだときだけ入る。v55 = 終了コードの配線 (票 TASK_EXIT_STATUS): exec_last_result — 直前の exec_run の結果を「種別 + 値」で返す。種別 (EXEC_KIND_*) は畳んだ側が渡すので、exit(-2) と fault と CTRL+STOP を値ではなく種別で見分けられる。起動しなかった場合 (exec_launch の早期 return) もexec_run のすべての return 点で書くので、前回の記録が残らない。記録が無ければ OS32_ERR_INVAL。v54 = 覗くだけのキー取得 (継承バグ「source が ESC 以外も食う」): kbd_peekkey — キューを 1 バイトも動かさずに次のキーを返す (無ければ -1)。script_exec の毎行の ESC 監視が kbd_trygetkey で打鍵を取り出して捨てていたのを直す。取り除くのは ESC と分かってからで、kbd_trygetkey を 1 回呼ぶ。v53 = 排他的作成 (票 H2): sys_open の KAPI_O_EXCL (0x0400)。O_CREAT と組でだけ有効で、名前が何であれ既に あれば OS32_ERR_EXIST、判定できなければその負値。VfsOps.create_excl を 持つ FS (ext2) だけが受け、持たない FS は OS32_ERR_NOSYS。スロットは増えていない (フラグだけ) が sys_open の意味が広がるので 版数を上げる ([ABI3])。v52 = mtime の保存 (票 H3): sys_set_mtime (VfsOps の任意実装フック。ext2 のみ実装、他の FS は OS32_ERR_NOSYS)。v51 = Host Services の基盤 (票 N1): host_open / host_status / host_read / host_write / host_close の 5 本 (非ブロッキング、同時 2 ハンドル、プロトコルを進めるのは 100Hz の link_tick だけ)。v50 = 設定レジストリの基盤 (票 S0-K): db_open_existing (RO / RW、CREATE 無し) / db_prepare_only / db_bind_int / db_bind_text / db_bind_blob / db_bind_null / db_error_code の 7 本。v49 = T9: 起動要求表 launch_req / launch_pending / launch_take / launch_report / launch_poll / launch_cancel / launch_child と sys_yield。v48 = T8: gfx_screen_owner。v47 = K7: kbd_inject / kbd_inject_pending。v46 = con_sink_read / con_sink_stat */

/* ======================================================================== */
/*  SQLite DB API 共有定数・構造体                                           */
/* ======================================================================== */

/* DB 最大接続数 */
#define DB_MAX_CONNECTIONS    8

/* DB結果ステータス */
#define DB_STATUS_DONE     0    /* クエリ完了 (行なし or 最終行到達) */
#define DB_STATUS_ROW      1    /* 行データあり (db_step で次を取得) */
#define DB_STATUS_ERROR   (-1)  /* エラー発生 */

/* DB カラム型 */
#define DB_TYPE_INT        1
#define DB_TYPE_FLOAT      2
#define DB_TYPE_TEXT       3
#define DB_TYPE_BLOB       4
#define DB_TYPE_NULL       5

/* IPC 共有メモリブロックサイズ (DB結果用) */
#define DB_SHM_BLOCK_SIZE  (16 * 1024)

/* 票 TASK_DB_ERRSTR §4 — ブロック 0 の末尾を **診断領域** に切り出す。
 *
 *   [0]                                                        [16KB]
 *   | DB_ResultHeader | 列情報 | 結果データ | 診断文 | 空文字列 |
 *   0                                       ^DIAG_OFFSET       ^EMPTY_OFFSET
 *
 * 共有メモリはアプリの PD に見えているが、カーネルの .rodata / .data と
 * SQLite の帯 (0x200000〜0x2FFFFF) は見えない。だから `const char *` を返す
 * KAPI (`db_last_error` / `db_column_text`) は、返す前にここへ写す。
 *
 * **結果データの上限は必ず DB_SHM_RESULT_LIMIT から引く** ([C4])。
 * DB_SHM_BLOCK_SIZE から直接引くと、結果データが診断文を踏み潰す。 */
#define DB_SHM_DIAG_SIZE    256    /* 診断領域の総量 (診断文 + 空文字列) */
#define DB_SHM_DIAG_OFFSET  (DB_SHM_BLOCK_SIZE - DB_SHM_DIAG_SIZE)
#define DB_SHM_ERRSTR_MAX   (DB_SHM_DIAG_SIZE - 1)   /* 診断文のバッファ長 (NUL 込み) */
#define DB_SHM_EMPTY_OFFSET (DB_SHM_BLOCK_SIZE - 1)  /* 常に NUL の 1 バイト */
#define DB_SHM_RESULT_LIMIT DB_SHM_DIAG_OFFSET       /* 結果データが使える上限 */

/* v50 (票 S0-K §1a) — db_prepare_only / db_bind_* の上限。カーネル側
 * (kapi/kapi_db.c) の検証用スクラッチもこの値で取るので、ここが唯一の
 * 管理元 ([C4])。超過は**切り捨てず拒否**する。 */
#define DB_SQL_MAX_BYTES   1024   /* SQL 文字列 (NUL 込み) */
#define DB_BIND_TEXT_MAX   255    /* db_bind_text の length (NUL を含まない) */
#define DB_BIND_BLOB_MAX   4096   /* db_bind_blob の length */

/* DB_ResultHeader — db_exec/db_step 結果の先頭に配置 */
typedef struct {
    i32 status;         /* DB_STATUS_xxx */
    i32 column_count;   /* 列数 (0 = 結果セットなし) */
    i32 error_offset;   /* エラーメッセージのオフセット (0 = エラーなし) */
} DB_ResultHeader;

/* DB_ColumnInfo — 各カラムの型・サイズ・データ位置 */
typedef struct {
    i32 type;           /* DB_TYPE_xxx */
    i32 length;         /* データサイズ (バイト) */
    i32 data_offset;    /* 共有メモリ先頭からのデータ位置 */
} DB_ColumnInfo;

/* ======================================================================== */
/*  システム共通制限値 (SSoT)                                                */
/* ======================================================================== */

#define OS32_MAX_PATH     256
#define OS32_MAX_ARGS     256
#define OS32_GFX_WIDTH    640
#define OS32_GFX_HEIGHT   400

/* ======================================================================== */
/*  システム共通ステータスコード                                             */
/* ======================================================================== */

/* 実行エンジンのステータスコード */
typedef enum {
    EXEC_SUCCESS = 0,
    EXEC_ERR_GENERAL = -1,
    EXEC_ERR_FAULT = -2,     /* 例外/フォールトによる強制終了 */
    EXEC_ERR_NOT_FOUND = -3, /* 実行ファイルが見つからない */
    EXEC_ERR_NOMEM = -4,     /* メモリ不足 */
    EXEC_ERR_INVALID = -5    /* OS32Xヘッダが不正 */
} exec_status_t;

/* ------------------------------------------------------------------------ */
/*  exec_last_result の「種別」 (票 TASK_EXIT_STATUS §2-1、KAPI v55)          */
/*                                                                          */
/*  **値からは種別を作れない** — exit(-2) と fault はどちらも                */
/*  exec_exit_status = -2 になる。だから種別は「畳んだ側」が渡す:            */
/*    kapi_sys_exit       → EXEC_KIND_EXITED   (子が自分で終わった)          */
/*    exec_fault_recover  → EXEC_KIND_FAULT    (#PF / #GP で畳んだ)          */
/*    CTRL+STOP           → EXEC_KIND_ABORTED  (ring3_abort_check)           */
/*  起動しなかった場合は exec_run が EXEC_ERR_* を写して入れる。             */
/*  シェルはこの種別だけで PATH 走査を止めるかどうかを決める ([C4])。        */
/* ------------------------------------------------------------------------ */
#define EXEC_KIND_NONE       0   /* 記録なし (exec_last_result は INVAL) */
#define EXEC_KIND_EXITED     1   /* 子が終了した。code = 終了コード */
#define EXEC_KIND_FAULT      2   /* 例外で畳んだ */
#define EXEC_KIND_ABORTED    3   /* CTRL+STOP で畳んだ */
#define EXEC_KIND_NOT_FOUND  4   /* 起動しなかった: 実行ファイルが無い */
#define EXEC_KIND_INVALID    5   /* 起動しなかった: OS32X ヘッダが不正 */
#define EXEC_KIND_NOMEM      6   /* 起動しなかった: メモリ / スロット不足 */
#define EXEC_KIND_GENERAL    7   /* 起動しなかった: その他 */

/* ======================================================================== */
/*  KernelAPI テーブルの配置アドレス                                          */
/*  カーネルビルド時のみ有効。外部プログラムは main() 第3引数を使用。         */
/* ======================================================================== */
#ifdef __KERNEL_BUILD__
#include "memmap.h"
#define KAPI_ADDR         (KHEAP_BASE + KHEAP_SIZE)
#endif

/* ======================================================================== */
/*  OS32X バイナリヘッダ                                                     */
/* ======================================================================== */

#define OS32X_MAGIC       0x4F533332UL  /* 'OS32' リトルエンディアン */
#define OS32X_HDR_V1_SIZE 40            /* v1ヘッダのサイズ */
#define OS32X_HDR_V2_SIZE 44            /* v2ヘッダのサイズ (load_addr を末尾に追記) */
#define OS32X_HDR_V3_SIZE 48            /* v3ヘッダのサイズ (kapi_data_off を末尾に追記) */
#include "os32_generations.h"
#define OS32X_HDR_SIZE 60
/* v3 のバイナリが要求する最低 KAPI 版。v3 の照合を持たない旧カーネル
 * (v62 以前) が v3 バイナリを受け入れないよう、生成器が min_api_ver を
 * これ以上に引き上げる (票 TASK_KAPI_DATA_FIELDS)。 */
#define OS32X_HDR_V3_MIN_API 63

/* フラグ定義 */
#define OS32X_FLAG_GFX    0x0001        /* GFXモードを使用 */
#define OS32X_FLAG_RING3  0x0002        /* CPL=3 (リング3) で実行する (v2 M1) */
#define OS32X_FLAG_SHLIB  0x0008        /* 共有ライブラリ (MEM_SHLIB_BASE 常駐、GUI v1.1 K3/C3) */
/* CUI 専用 (mkos32x --cui-only、app.conf 4 列目 `cui`、票 T8-2)。
 * V86 / VDM のように「CPL=3 のプログラムだが KAPI の向こうで画面と BIOS を
 * 丸ごと持っていく」ものに立てる。GUI (con_sink 有効) からの exec_start は
 * OS32_ERR_INVAL で断り、CUI からは従来どおり通す。FORCE_CPL0 とは独立 —
 * v86.bin は CPL=3 なので FORCE_CPL0 では捕まらなかった (受入 F5)。 */
#define OS32X_FLAG_CUI_ONLY 0x0010
/* 起動要求を出せる宣言 (mkos32x --launcher、app.conf 4 列目 `launcher`、票 T9 D3)。
 * GUI 中の CPL=3 アプリは入れ子 exec_run を使えないので、外部プログラムの起動は
 * カーネルの要求表 (launch_req) 経由で WM に頼む。その口を叩けるのはこの宣言を
 * 持つバイナリだけ — 認証ではなく協調的な宣言で、任意のアプリが端末経由の
 * 非同期起動を使えてしまう穴を塞ぐためのもの (票 §5 blocker 2)。 */
#define OS32X_FLAG_LAUNCHER 0x0020

typedef struct {
    u32 magic;            /* 0x00: OS32X_MAGIC */
    u32 header_size;      /* 0x04: ヘッダ全体のサイズ (バイト) */
    u32 version;          /* 0x08: ヘッダバージョン (現在: 1) */
    u32 flags;            /* 0x0C: フラグ */
    u32 entry_offset;     /* 0x10: エントリポイント */
    u32 text_size;        /* 0x14: コード + 初期化済みデータ */
    u32 bss_size;         /* 0x18: BSS領域サイズ */
    u32 heap_size;        /* 0x1C: 要求ヒープサイズ */
    u32 stack_size;       /* 0x20: 要求スタックサイズ */
    u32 min_api_ver;      /* 0x24: 必要な最低KernelAPIバージョン */
    /* ---- v2 (header_size >= OS32X_HDR_V2_SIZE のときだけ有効) ---- */
    u32 load_addr;        /* 0x28: リンク時のロードアドレス (mkos32x が ELF の
                           * .text から焼く)。exec はここが自分の置こうとして
                           * いる番地と一致するかを確かめ、食い違えば
                           * OS32_ERR_INVAL で弾く。0 = 不明 (警告のみ)。
                           * GUI v1.1 K3 でロードアドレスが 0x400000 →
                           * 0x500000 に動いたため、旧バイナリ (version 1 /
                           * header_size 40) が黙って別番地へ飛ぶのを防ぐ。 */
    /* ---- v3 (header_size >= OS32X_HDR_V3_SIZE のときだけ有効) ---- */
    u32 kapi_data_off;    /* 0x2C: ビルドに使った KernelAPI のデータ欄
                           * (sbrk_heap_limit) のオフセット。生成器が ELF の
                           * .os32_kapi_layout (crt0 / os32api の刻印) から
                           * 写す。exec / shlib ローダはカーネルの
                           * KAPI_DATA_FIELDS_OFF と違えば「rebuild required
                           * (KAPI data layout)」で断る (票 TASK_KAPI_DATA_FIELDS)。 */
    u32 kapi_abi_generation;
    u32 memory_layout_generation;
    u32 shlib_protocol;
} OS32Header;             /* 合計: 60バイト (0x3C)。version 1 は先頭 40 バイト、
                           * version 2 は44バイト、version 3 は48バイト。旧版はローダが拒否。 */

/* ======================================================================== */
/*  共有ライブラリ (OS32X_FLAG_SHLIB) のジャンプ表 — GUI v1.1 K3 / C3        */
/*                                                                          */
/*  ライブラリ本体 (OS32X) の先頭 4KB がこのヘッダ。カーネル (K3) が            */
/*  MEM_SHLIB_BASE に読み、.text/.rodata を read-only + USER で全 PD に共有、  */
/*  data_vaddr から data_pages ページをアプリごとの物理ページにする。          */
/*  アプリ側スタブ (C3) は magic / version を照合してから entry[i] を呼ぶ。    */
/*  entry[] は **末尾追記のみ** (KAPI と同じ作法)。ヘッダ 32B の直後から。      */
/* ======================================================================== */
#define OS32_SHLIB_MAGIC      0x42494C53UL  /* 'SLIB' リトルエンディアン */
#define OS32_SHLIB_HDR_SIZE   4096          /* ジャンプ表を含む先頭ページ */
#define OS32_SHLIB_ENTRY_OFF  32            /* entry[0] のヘッダ先頭からのオフセット */
#define OS32_SHLIB_MAX_FUNC   ((OS32_SHLIB_HDR_SIZE - OS32_SHLIB_ENTRY_OFF) / 4)

typedef struct {
    u32 magic;        /* 0x00: OS32_SHLIB_MAGIC */
    u32 version;      /* 0x04: ライブラリの版 (libos32gui は GUI_PROTO_VERSION) */
    u32 nfunc;        /* 0x08: entry[] の有効数 */
    u32 data_vaddr;   /* 0x0C: .data/.bss の先頭仮想アドレス (ページ境界) */
    u32 data_pages;   /* 0x10: アプリごとに複製する物理ページ数 */
    u32 text_pages;   /* 0x14: 共有する read-only ページ数 (先頭ページ含む) */
    u32 _rsvd[2];     /* 0x18: 0 */
    /* 0x20: u32 entry[nfunc] — 関数の絶対アドレス (C89 なので配列は持たない。
     *       ((const u32 *)hdr)[OS32_SHLIB_ENTRY_OFF / 4 + i] で読む) */
} OS32ShlibHeader;    /* 32B */

/* ======================================================================== */
/*  KernelAPI 構造体                                                         */
/*                                                                          */
/*  外部プログラムがカーネル関数を呼ぶためのテーブル。                        */
/*  固定アドレス KAPI_ADDR に配置される。                                    */
/*  新しい関数は末尾に追加すること（バイナリ互換維持）。                      */
/* ======================================================================== */



/* ======================================================================== */
/*  共有構造体定義 (外部プログラムで直接使用可能)                              */
/* ======================================================================== */

/* 矩形 (ダーティレクタングルのX/Wは32の倍数でアライメントされる) */
typedef struct {
    int x, y, w, h;
} GFX_Rect;

/* ハードウェアバックバッファ本体の構造体 (PC-98プレーン構造) */
typedef struct {
    int width;      /* 640 */
    int height;     /* 400 */
    int pitch;      /* 80 bytes per line */
    u8 *planes[4];  /* 0:B, 1:R, 2:G, 3:I */
} GFX_Framebuffer;

/* 画面能力 (GUI HAL 用、docs/tasks/gui/API_CONTRACTS.md G5)。gfx_screen_info() が埋める。
 * バックエンド (9801 プレーン / PEGC 8bpp / アクセラレータ) の違いはここで問い合わせ、
 * GUI とアプリは 400 ライン・16 色を決め打ちしない。末尾追記のみ。 */
#define GFX_FMT_PLANAR4   0   /* 4 プレーン 16 色 (PC-9801 標準) */
#define GFX_FMT_PACKED8   1   /* 1 バイト 1 ピクセル 256 色 (PEGC / アクセラレータ) */
#define GFX_CAP_TEXT_OVERLAY  0x0001  /* テキスト VRAM がグラフィック上に合成される */
#define GFX_CAP_HW_FILL       0x0002  /* gfx_hw_fill_rect が使える */
#define GFX_CAP_HW_BLT        0x0004  /* gfx_hw_blit が使える */
#define GFX_CAP_PAGE_FLIP     0x0008  /* 表裏ページ切替あり */
typedef struct {
    u16 width, height;   /* 640 x 400/480 ... */
    u8  bpp;             /* 4 or 8 */
    u8  format;          /* GFX_FMT_* */
    u32 flags;           /* GFX_CAP_* */
    u16 lease_mask;      /* 16 色: フォーカスアプリに貸せるパレット index のビット集合 (G8) */
    u16 lease_first;     /* 256 色: 貸せる先頭 index */
    u16 lease_count;     /* 256 色: 貸せる項目数 */
    u16 reserved[5];
} GFX_ScreenInfo;

/* GUI カウンタ (gfx_stats() が埋める、docs/tasks/gui/API_CONTRACTS.md G7)。
 * 累積値。NP21/W ではウェイトを再現しないので、性能はこの回数・転送量で
 * 見積もる (契約 P2)。末尾追記のみ。 */
typedef struct {
    u32 present_bytes;   /* VRAM へ転送したバイト数の累計 */
    u32 hw_ops;          /* アクセラレータの塗り/転送回数 */
    u32 io_accesses;     /* I/O ポートアクセス回数 */
    u32 commits;         /* commit 回数 */
} GFX_Stats;

/* キーボード 8251 の診断 (kbd_diag() が埋める、KAPI v62、シェルの `kbdstat`)。
 * 実機 PC-9821Ra266 で打鍵が届かなかった件 (docs/POLICY_DEBUG.md §4-57) の
 * 切り分け用。カウンタは起動からの累計 (飽和しない、u32 で折り返す)。
 * 並びを変えない (24 バイト。drivers/kbd.c の STATIC_ASSERT が見張る)。 */
typedef struct {
    u32 irq_count;       /* IRQ1 ハンドラに入った回数 (空・エラーも含む) */
    u32 empty_count;     /* 0043h の RxRDY = 0 だった IRQ (0041h を読まずに返した) */
    u32 err_count;       /* PE/FE のどれかが立っていた IRQ (読み捨て + ER で解除) */
    u32 flushed;         /* kbd_init が起動時に読み捨てたバイト数 */
    u8  init_st_before;  /* kbd_init がコマンド語を書く前の 0043h */
    u8  init_st_after;   /* 書いた後の 0043h */
    u8  last_st;         /* 直近の IRQ で読んだ 0043h */
    u8  last_code;       /* 直近に受け取ったスキャンコード (0041h) */
    u8  cmd;             /* kbd_init が書いたコマンド語 (0x16) */
    u8  now_st;          /* kbd_diag を呼んだ時点の 0043h (IRQ が 1 回も来ないときの RxRDY 判定用) */
    u16 overrun_count;   /* OE だけが立っていた IRQ (バイトは使い、ER で解除)。0xFFFF で飽和。
                          * bda95fa では reserved[2] (= 0) だった場所。大きさ・並びは同じ */
} KbdDiag;

/* キーボードの受信記録 (kbd_diag_log() が写す、KAPI v67、シェルの `kbdstat -w`)。
 * 票 docs/tasks/gui/TASK_KBD_NAV.md §3: カナ / CAPS が「ロックで make、解除で
 * break」か「押すたびに make だけ」かを実機で見るための道具。IRQ1 が 0041h から
 * **使うバイトを読むたびに** 1 件積む (DATA / OVERRUN。EMPTY / ERROR で読まなかった
 * ・捨てたバイトは積まない — そちらは KbdDiag の empty_count / err_count が数える)。
 * CUI でも GUI でも V86 中でも積む。リングは KBD_DLOG_CAP 件の循環で、溢れたら
 * 古い方から上書きする。seq は起動からの通し番号 (1 から、0 = まだ 1 件も無い)。
 * 呼び手は「最後に読んだ seq」を渡し、それより新しい分を古い順に受け取る。
 * 受け取った先頭の seq が「渡した seq + 1」でなければ、その間は上書きで失われた。
 * 並びを変えない (8 バイト。drivers/kbd.c の STATIC_ASSERT が見張る)。 */
#define KBD_DLOG_CAP        32     /* リングの件数 (2 の冪) */
#define KBD_DLOG_F_OVERRUN  0x01   /* 0043h が OE だけだった (前のバイトを取りこぼした) */
#define KBD_DLOG_F_V86      0x02   /* V86 ゲストへ回した (mods は更新されない) */
#define KBD_DLOG_F_GUI      0x04   /* GUI モード中 (raw リングへ積む側) だった */
#define KBD_DLOG_BREAK      0x80   /* code の bit7 = break (離し)、0 = make */
#define KBD_DLOG_KEY_MASK   0x7F   /* code の下位 7 ビット = キーコード */
/* mods のビット (drivers/kbd.h の SHIFT_* と同じ値。kbd_get_modifiers() も同じ並び) */
#define KBD_DLOG_MOD_SHIFT  0x01
#define KBD_DLOG_MOD_CAPS   0x02
#define KBD_DLOG_MOD_KANA   0x04
#define KBD_DLOG_MOD_GRPH   0x08
#define KBD_DLOG_MOD_CTRL   0x10
typedef struct {
    u32 seq;             /* 通し番号 (1 から) */
    u8  code;            /* 0041h の生の値 (bit7 = break) */
    u8  mods;            /* このバイトを処理した**後**の修飾 (kbd_shift_state) */
    u8  flags;           /* KBD_DLOG_F_* */
    u8  reserved;        /* 0 */
} KbdDiagLogEnt;

/* HDD の幾何 (hdd_geom_info() が埋める、KAPI v64、シェルの `hdprep`)。
 * 票 docs/archive/realhw_v21/TASK_HDD_INSTALL.md 段 1。幾何は 2 種類ある:
 *   BIOS 幾何 (INT 1Bh AH=84h、ローダが 0x7E00 に残した値) — 区画表と IPL の CHS
 *   ATA の申告 (IDENTIFY)                                  — I/O の指定の方式
 * bios_* はローダが問い合わせていれば**生の値**を入れる (bios_valid = 0 でも)。
 * bios_valid = 1 はカーネルの規則 (CF=0・BX=512・CX/DH/DL≠0) を満たすときだけ。
 * addr_mode は I/O の方式 (HDD_AMODE_*)。並びを変えない (32 バイト、
 * kernel/bootinfo.c の STATIC_ASSERT が見張る)。 */
#define HDD_AMODE_NONE     0   /* ドライブが無い */
#define HDD_AMODE_LBA28    1   /* IDENTIFY word 49 bit9 */
#define HDD_AMODE_CHS_CUR  2   /* word 53 bit0 の現在の幾何 */
#define HDD_AMODE_CHS_DEF  3   /* 既定の幾何 (word 1/3/6) — 書き込みの成否を保証しない */
typedef struct {
    u8  bios_queried;    /* ローダが AH=84h を問い合わせた */
    u8  bios_valid;      /* 規則を満たす (区画表の CHS に使える) */
    u8  bios_heads;      /* DH */
    u8  bios_spt;        /* DL */
    u16 bios_cyl;        /* CX */
    u16 bios_seclen;     /* BX */
    u16 ata_def_cyl;     /* word 1 */
    u16 ata_def_heads;   /* word 3 */
    u16 ata_def_spt;     /* word 6 */
    u16 ata_cur_cyl;     /* word 54 */
    u16 ata_cur_heads;   /* word 55 */
    u16 ata_cur_spt;     /* word 56 */
    u16 ata_w49;         /* word 49 (bit9 = LBA) */
    u16 ata_w53;         /* word 53 (bit0 = 54-58 が有効) */
    u32 ata_total;       /* word 60-61 */
    u8  ata_present;     /* IDENTIFY に答えた */
    u8  addr_mode;       /* HDD_AMODE_* */
    u8  bios_da;         /* 対応する DA (0x80 / 0x81、無ければ 0) */
    u8  reserved;
} HddGeom;

/* serial_diag (KAPI v66、票 TASK_SERIAL_HOSTFS §1-v2「ISR の計数」) の出力。
 * 16 バイト。受信の誤りは ISR (と SerialFS の受信器) がリセットする前に数える。
 * FIFO (V･FAST) モードの 0132h は資料と NP21/W でビットの並びが食い違うので
 * FE / PE は目安 (OE は一致)。overflow は受信リング (4KB) 溢れで捨てたバイト。 */
typedef struct {
    u32 oe;          /* オーバーラン */
    u32 fe;          /* フレーミング */
    u32 pe;          /* パリティ */
    u32 overflow;    /* 受信リング溢れ */
} SerialDiag;

/* v86_gdc_capture (KAPI v68、票 docs/tasks/realhw/TASK_PEGC480_REALHW.md §3 段 1)
 * の出力。実機の ROM の INT 18h AH=31h / AH=30h を V86 で呼び、その間に ROM が
 * 出した I/O を記録する管理者用の診断 (`v86 -g`)。
 *
 * mode: V86G_MODE_ROM = 実機の ROM を呼ぶ / V86G_MODE_SELFTEST = 決まった OUT 列を
 * 出す試験ゲストで記録器を確かめる (捕まえたポートは実機へ通さない)。
 *
 * OUT は**畳まずに**起きた順に out[] へ積む (V86G_OUT_MAX 件。溢れたら
 * overflow = 1 で status = V86G_ST_OVERFLOW)。IN は数が読めない (GDC の FIFO /
 * VSYNC 待ちのポーリング) ので、ポートごとの回数・最初と最後の値だけ in[] に持つ。
 * seq は IN も含めた「捕まえた I/O」の通し番号なので、OUT の seq の飛びが
 * その間の IN の回数になる。並びを変えない (kernel/v86_gcap.c の STATIC_ASSERT)。 */
#define V86G_MODE_ROM        0
#define V86G_MODE_SELFTEST   1

#define V86G_OUT_MAX         512
#define V86G_IN_MAX          16

/* status (戻り値も同じ)。負は始められなかった: OS32_ERR_INVAL = GUI 中・mode 不正・
 * out が NULL / OS32_ERR_BUSY = 480 ライン表示中・V86 使用中 / OS32_ERR_NOSPC =
 * 記録の領域 (kmalloc) が取れない。 */
#define V86G_ST_OK           0   /* 記録した (ROM) / 自己試験に合格 */
#define V86G_ST_NO31         1   /* AH=31h に答えが無い (AL/BH が入れた印のまま) */
#define V86G_ST_UNDECIDED    2   /* AH=31h の値がどちらの並びとも決められない (30h は呼ばない) */
#define V86G_ST_REJECTED     3   /* AH=30h の戻り AH が 05h でない (OUT 列は捨てた) */
#define V86G_ST_OVERFLOW     4   /* OUT が V86G_OUT_MAX 件を超えた (OUT 列は出さない) */
#define V86G_ST_ABORT        5   /* INS/OUTS か 32 ビット IN/OUT で打ち切った (abort_kind) */
#define V86G_ST_GUEST        6   /* V86 が想定外の終わり方をした (exit_reason) */
#define V86G_ST_SETUP        7   /* V86 の準備に失敗した */
#define V86G_ST_NOVEC        8   /* IVT[18h] が ROM を指していない (呼ばない) */
#define V86G_ST_SELFTEST     9   /* 自己試験の不合格 (selftest_fail のビット) */

/* layout — AH=31h/30h の bit の並び (票 §3 段 1 の表) */
#define V86G_LAYOUT_NONE     0
#define V86G_LAYOUT_BIT2     1   /* NP21/W bios18.c: AL bit2 = 31kHz、BH bit5-4 = 解像度・bit1-0 = 行数 */
#define V86G_LAYOUT_BIT3     2   /* Bible 3-2: AL D3 = 31kHz、BH D4-D3 = 行数・D2-D1 = 解像度 */

/* restore — 採取の後の戻し方 */
#define V86G_RST_NONE        0   /* AH=30h を呼んでいない (戻すものが無い) */
#define V86G_RST_ROM         1   /* AH=30h (AH=31h で読んだ値) で戻った */
#define V86G_RST_FALLBACK    2   /* ROM で戻らず OS32 の表 (pegc_restore_text_sync) で戻した */

/* abort_kind */
#define V86G_ABORT_NONE      0
#define V86G_ABORT_INSOUTS   1   /* INS / OUTS (6Ch〜6Fh) */
#define V86G_ABORT_IO32      2   /* 66h 付きの IN EAX / OUT EAX */

/* out[].flags / in[].flags */
#define V86G_F_PASSED        0x01   /* 実ポートへ通した (0 = 仮想化・捨てた) */
#define V86G_F_PHASE_SHIFT   4      /* bit7-4 = フェーズ (V86G_PH_*) */
#define V86G_PH_SELFTEST     0
#define V86G_PH_READ31       1      /* AH=31h (今のモードの読み) */
#define V86G_PH_SET480       2      /* AH=30h → 640x480 */
#define V86G_PH_RESTORE      3      /* AH=30h → 元のモード */

typedef struct {
    u32 seq;             /* 捕まえた I/O の通し番号 (IN も数える、0 から) */
    u16 port;
    u16 value;           /* 幅 1 なら下位 8 ビット */
    u16 cs;              /* OUT を出した命令の CS:IP */
    u16 ip;
    u8  width;           /* 1 / 2 */
    u8  flags;           /* V86G_F_* */
    u16 reserved;
} V86GcapOut;            /* 16 バイト */

typedef struct {
    u16 port;
    u8  widths;          /* 見た幅の OR (1 / 2) */
    u8  flags;           /* V86G_F_PASSED */
    u32 count;
    u16 first;           /* 最初に返した値 */
    u16 last;            /* 最後に返した値 */
} V86GcapIn;             /* 12 バイト */

typedef struct {
    u32 status;          /* V86G_ST_* */
    u32 mode;            /* V86G_MODE_* */
    u32 layout;          /* V86G_LAYOUT_* */
    u32 restore;         /* V86G_RST_* */
    u32 exit_reason;     /* 最後の V86 セッションの終了理由 (kernel/v86.h の enum) */
    u32 abort_kind;      /* V86G_ABORT_* */
    u16 abort_cs;
    u16 abort_ip;
    u16 r31_ax;          /* AH=31h の後の AX / BX */
    u16 r31_bx;
    u16 set_ax;          /* AH=30h (480) に渡した AX / BX と戻り */
    u16 set_bx;
    u16 set_ret_ax;
    u16 set_ret_bx;
    u16 rst_ax;          /* AH=30h (戻し) に渡した AX / BX と戻り */
    u16 rst_bx;
    u16 rst_ret_ax;
    u16 rst_ret_bx;
    u32 n_out;           /* out[] の有効件数 */
    u32 overflow;        /* 1 = OUT が溢れた */
    u32 in_total;        /* 捕まえた IN の総数 */
    u32 in_other;        /* in[] に入りきらなかったポートの IN の数 */
    u32 n_in;            /* in[] の有効件数 */
    u32 selftest_fail;   /* 自己試験で落ちた項目のビット (0 = 全部合格) */
    u32 seq_next;        /* 次に振る seq (= 捕まえた I/O の総数) */
    V86GcapIn  in[V86G_IN_MAX];
    V86GcapOut out[V86G_OUT_MAX];
} V86Gcap;

/* sfs_end の戻り値: 閉じたが線が 5 秒で静まらなかった (警告済み) */
#define KAPI_SFS_END_NOT_QUIET  1

/* boot_image_info (KAPI v65、票 TASK_SERIAL_HOSTFS 部品 A-4) の出力。40 バイト。
 * image_crc は**ローダが検査して起動した** vmkernel.lz4 (VK32 v2) の
 * ファイル全体の CRC32 (ヘッダの image_crc 欄の値、zlib.crc32 と同じ計算で
 * image_crc の欄を 0 として求めたもの)。ローダが記録していなければ
 * crc_valid = 0 (image_crc / image_size も 0)。hsync が `.old` を作る前に
 * 「置き換える /boot/vmkernel.lz4 が今動いている版か」を比べるのに使う。
 * commit はカーネルを組んだ git のコミット ID (NUL 終端、"-dirty" / "unknown")。
 * 並びを変えない (kernel/bootinfo.c の STATIC_ASSERT が見張る)。 */
#define BOOT_IMAGE_COMMIT_MAX  24
typedef struct {
    u32  image_crc;      /* VK32 のファイル全体の CRC32 */
    u32  image_size;     /* ローダが読んだ長さ */
    u8   crc_valid;      /* 1 = ローダが記録した */
    u8   source;         /* 1 = FD ローダ、2 = HDD ローダ、0 = 不明 */
    u16  reserved;
    char commit[BOOT_IMAGE_COMMIT_MAX];
    u32  reserved2;
} BootImageInfo;

/* パレットエントリ (各0-15) */
typedef struct {
    u8 r, g, b;
} GFX_Color;

/* サーフェス — オフスクリーン描画バッファ */
typedef struct {
    int w, h;
    int pitch;          /* バイト/ライン (w+7)/8 */
    u8 *planes[4];      /* 4プレーン (パックドビット) */
    int _pool_idx;      /* 静的プール管理用 (-1=外部管理) */
} GFX_Surface;

/* スプライト — マスク付き事前コンパイル済み (透過描画用) */
typedef struct {
    int w, h;
    int pitch;
    u8 *planes[4];      /* スプライトデータ */
    u8 *mask;           /* ANDマスク (透過=0xFF, 不透過=0x00) */
    u8 *bg_buf;         /* 自動背景退避用バッファ (4プレーン連続) */
    int _pool_idx;
} GFX_Sprite;

/* ラスタパレットエントリ (1分割 = 2ライン単位) */
typedef struct {
    u16 line;           /* 開始ライン (0-398, 2ライン単位推奨) */
    u8  pal_idx;        /* パレット番号 (0-15) */
    u8  r, g, b;        /* RGB値 (0-15) */
    u8  _pad[2];        /* アラインメント用 */
} GFX_RasterPalEntry;

#define GFX_RASTER_MAX_ENTRIES 200  /* 最大200エントリ (NP21/W: 1024イベント制限) */

/* ラスタパレットテーブル (外部プログラムが構築→カーネルに渡す) */
typedef struct {
    int count;                                    /* 有効エントリ数 */
    GFX_RasterPalEntry entries[GFX_RASTER_MAX_ENTRIES];
} GFX_RasterPalTable;

/* マウス情報構造体 (mouse_poll 用) */
typedef struct {
    i16  x;          /* 現在のX座標 (画面座標) */
    i16  y;          /* 現在のY座標 (画面座標) */
    i16  dx;         /* X差分 (前回poll以降) */
    i16  dy;         /* Y差分 (前回poll以降) */
    u8   buttons;    /* ボタンビットマスク (MOUSE_BTN_xxx) */
    u8   mode;       /* 0=なし, 1=バス, 2=シームレス */
} MouseInfo;

/* マウスボタンビットマスク */
#define MOUSE_BTN_LEFT   0x01
#define MOUSE_BTN_RIGHT  0x02
#define MOUSE_BTN_MIDDLE 0x04

/* マウスカーソル表示モード */
#define MOUSE_CURSOR_NONE  0  /* カーソル非表示 (生ポーリング専用) */
#define MOUSE_CURSOR_TEXT  1  /* TVRAM属性反転カーソル */
#define MOUSE_CURSOR_GFX   2  /* GFXスプライトカーソル (将来用) */

/* RTC時刻構造体 */
typedef struct {
    u8 year, month, day, wday, hour, min, sec;
} RTC_Time_Ext;

/* VFS 系 KAPI (sys_open/sys_chdir/sys_unlink/sys_mkdir/sys_rmdir/sys_rename
 * /sys_stat 等) が返す負のエラーコード。カーネル内の VFS_ERR_* (fs/vfs.h)
 * はこの別名。各 FS ドライバの内部コード (EXT2_ERR_* 等) はドライバ境界で
 * 必ずこの体系に変換される */
#define OS32_ERR_IO        -1   /* 入出力エラー */
#define OS32_ERR_NOTFOUND  -2   /* パスが存在しない */
#define OS32_ERR_NOMOUNT   -3   /* 該当マウントなし / FS 未対応操作 */
#define OS32_ERR_NOSPC     -4   /* 空き容量・FD・スロット不足 */
#define OS32_ERR_EXIST     -5   /* 既に存在する */
#define OS32_ERR_NOTDIR    -6   /* ディレクトリではない */
#define OS32_ERR_NOTEMPTY  -7   /* ディレクトリが空でない */
#define OS32_ERR_ISDIR     -8   /* ディレクトリである (open/unlink 不可) */
#define OS32_ERR_INVAL     -9   /* 引数不正 / FS をまたぐ rename 等 */
#define OS32_ERR_NOSYS     -10  /* このバックエンド / 機種では未対応 */
/* GUI (KAPI v42、KAPI_SPEC §3-2 の予約どおり)。-14 以降はネットワーク用に空ける。 */
#define OS32_ERR_STALE     -11  /* 破棄済み / generation 不一致のハンドル (契約 T4) */
#define OS32_ERR_VERSION   -12  /* proto_version が WM より新しい (契約 T5) */
#define OS32_ERR_FULL      -13  /* スロット / 資源が満杯 (契約 T2a) */
/* GUI v1.3 K7 (KAPI v47)。KAPI_SPEC §3-2 の予約を 1 つ進め、ネットワークは
 * -15 以降へずらした (ネットワーク側は番号を 1 つも使っていない)。 */
#define OS32_ERR_AGAIN     -14  /* いまは無い / 後でもう一度 (kbd 待ちの resume) */
/* 票 B8 往復 5 (ユーザー決裁 2、2026-09-15)。**番号の追加だけで構造体・スロットは
 * 変えない**ので KAPI 版数は据え置き。KAPI_SPEC §3-2 の予約を 1 つ進め、
 * ネットワークは -16 以降へずらした (ネットワーク側は番号を 1 つも使っていない)。 */
#define OS32_ERR_ROFS      -15  /* 書き込みを受け付けない (FS がエラー状態、再マウントまで) */
/* 票 TASK_VFS_FD_PATH (2026-09-24)。**番号の追加だけで構造体・スロットは変えない**
 * ので KAPI 版数は据え置き。KAPI_SPEC §3-2 の予約を 2 つ進め、ネットワークは
 * -18 以降へずらした (ネットワーク側は番号を 1 つも使っていない)。 */
#define OS32_ERR_NAMETOOLONG -16 /* パスが長すぎる / 深すぎる (切り詰めずに断る) */
#define OS32_ERR_BUSY      -17  /* 使用中 (開いている SQLite DB の rename、使用中の loop イメージ) */

/* ファイル種別 (OS32_FILE_TYPE_*) */
#define OS32_FILE_TYPE_FILE 1
#define OS32_FILE_TYPE_DIR  2

/* ディレクトリエントリ (コールバック用) */
typedef struct {
    char name[OS32_MAX_PATH];
    u32  size;
    u8   type;  /* OS32_FILE_TYPE_FILE / OS32_FILE_TYPE_DIR */
} DirEntry_Ext;

/* DirEntry_Ext コールバック型 */
typedef void (*DirCallback)(const DirEntry_Ext *entry, void *ctx);

/* ======================================================================== */
/*  console シンク (KAPI v46、票 K6C)                                        */
/*                                                                          */
/*  GUI モード中、カーネル / CUI コマンドが console.c の入口へ書いた出力は    */
/*  テキスト VRAM (非表示) に消える。これをカーネル内のリングに **レコード**  */
/*  として溜め、端末アプリが con_sink_read() で吸って Paint する。            */
/*                                                                          */
/*  ワイヤ形式 (先頭 1 バイトが型、残りは型ごと。詰め物・整列は無い):        */
/*    PRINT  : [type=1][color u8][len u8][UTF-8 バイト列 len 個]             */
/*    CLEAR  : [type=2]                                                      */
/*    CURSOR : [type=3][x u8][y u8]                                          */
/*    EXIT   : [type=4][id u8]        — gshell 配下の子が畳まれた (票 T7 E1) */
/*                                                                          */
/*  改行 / CR / TAB は PRINT のバイトとして流れる (端末モデルが解釈する)。   */
/*  スクロールはレコードにしない (行の追加に畳む)。                          */
/* ======================================================================== */
#define CON_SINK_REC_PRINT   1
#define CON_SINK_REC_CLEAR   2
#define CON_SINK_REC_CURSOR  3
#define CON_SINK_REC_EXIT    4

#define CON_SINK_PRINT_MAX   200  /* PRINT 1 本が運ぶ UTF-8 バイト数の上限 */
#define CON_SINK_HDR_PRINT   3    /* type + color + len */
#define CON_SINK_HDR_CLEAR   1    /* type */
#define CON_SINK_HDR_CURSOR  3    /* type + x + y */
#define CON_SINK_HDR_EXIT    2    /* type + id */
/* レコード 1 本の最大バイト数。con_sink_read() の cap はこれ以上でなければ
 * ならない (小さいと先頭レコードが永久に取り出せず読み手が止まるため)。 */
#define CON_SINK_REC_MAX     (CON_SINK_HDR_PRINT + CON_SINK_PRINT_MAX)

/* ======================================================================== */
/*  起動要求表 (KAPI v49、票 T9 D3 / §1a)                                    */
/*                                                                          */
/*  GUI 中の CPL=3 アプリ (端末・sh) は入れ子 exec_run を使えない (子が park  */
/*  できず協調型全体が止まる) ので、外部プログラムの起動と kill を **カーネル  */
/*  の要求表** に載せ、owner 1 (WM) が top-level で取りに来る。               */
/*                                                                          */
/*  表は要求者 ID ごとに 1 本 (ID 2〜5 の 4 本)。欄は「配送状態」(phase /     */
/*  kind) と「子の所有」(child) を分けてあり、取消や要求者の退場の途中でも    */
/*  child は消えない。照合は **token** で行う (要求者 ID は再利用されうる)。  */
/*                                                                          */
/*    launch_req    要求者 (宣言 LAUNCHER) → token                           */
/*    launch_pending 誰でも → PENDING の本数 (WM の should_park の材料)       */
/*    launch_take   owner 1 → token + kind + cmdline / 畳む ID               */
/*    launch_report owner 1 → 起動結果 (rc) を表へ返す                        */
/*    launch_poll   要求者 → status (下の LAUNCH_ST_*)                        */
/*    launch_cancel 要求者 → RUNNING を KILL(child) の PENDING へ             */
/*    launch_child  誰でも → その ID の表が所有する子 (連鎖の次)             */
/* ======================================================================== */

/* cmdline の欄 (NUL 込み)。launch_take の cap はこれ以上でなければならない。 */
#define LAUNCH_CMDLINE_MAX   256

/* launch_take が書く kind */
#define LAUNCH_KIND_NONE     0
#define LAUNCH_KIND_LAUNCH   1    /* buf に cmdline */
#define LAUNCH_KIND_KILL     2    /* arg = 畳む ID */

/* launch_poll が書く status。RUNNING は下位に子 ID、FAILED は下位に -rc。 */
#define LAUNCH_ST_PENDING    0x000
#define LAUNCH_ST_TAKEN      0x001
#define LAUNCH_ST_RUNNING    0x100   /* + child */
#define LAUNCH_ST_DONE       0x200
#define LAUNCH_ST_FAILED     0x300   /* + (-rc) */

/* token は 32bit の全体単調増加カウンタ。0 と負は使わない。ここに達したら
 * launch_req は OS32_ERR_FULL を返す (1 要求 1 token なので事実上到達しない)。 */
#define LAUNCH_TOKEN_MAX     0x7FFFFFFF

/* コンソール属性色 */
#define ATTR_WHITE   0xE1
#define ATTR_CYAN    0xA1
#define ATTR_GREEN   0x81
#define ATTR_YELLOW  0xC1
#define ATTR_RED     0x41
#define ATTR_MAGENTA 0x61

/* ======================================================================== */
/*  ファイル属性と時間 (Stat)                                               */
/* ======================================================================== */

/* UNIX時間に準拠した 32-bit (符号なし) エポック秒 (1970年1月1日〜) */
typedef u32 os_time_t;

/* ファイル種別 (st_mode の S_IFMT ビットマスク)。
 * 値は POSIX の S_IF* と同じ。パス名で開けるのは DIR / REG / CHR の 3 つで、
 * FIFO は `fstat` でしか出てこない (パイプ中の fd 0/1/2、票 TASK_FSTAT_REDIR)。*/
#define OS_S_IFMT   0xF000
#define OS_S_IFIFO  0x1000 /* パイプ (名前を持たない FIFO) */
#define OS_S_IFCHR  0x2000 /* キャラクタデバイス */
#define OS_S_IFDIR  0x4000 /* ディレクトリ */
#define OS_S_IFREG  0x8000 /* 通常ファイル */

/* パーミッションフラグ */
#define OS_S_IRUSR  00400  /* User (システムではエンドユーザー) Read */
#define OS_S_IWUSR  00200  /* User Write */
#define OS_S_IXUSR  00100  /* User eXecute */
#define OS_S_IRWXU  00700  /* User R/W/X mask */

#define OS_S_IRGRP  00040  /* Group (プログラム) Read */
#define OS_S_IWGRP  00020  /* Group Write */
#define OS_S_IXGRP  00010  /* Group eXecute */
#define OS_S_IRWXG  00070  /* Group R/W/X mask */

#define OS_S_IROTH  00004  /* Other (OS/システム) Read */
#define OS_S_IWOTH  00002  /* Other Write */
#define OS_S_IXOTH  00001  /* Other eXecute */
#define OS_S_IRWXO  00007  /* Other R/W/X mask */

typedef struct {
    u32       st_dev;     /* デバイスID (マウントポイント等) */
    u32       st_ino;     /* inode番号 (FS一意の識別子) */
    u16       st_mode;    /* ファイル種別 + パーミッション (16bit) */
    u16       st_nlink;   /* ハードリンク数 (FATでは常に 1) */
    u16       st_uid;     /* 所有ユーザー ID (OS32では固定化) */
    u16       st_gid;     /* 所有プログラム(グループ) ID */
    u32       st_size;    /* ファイルサイズ (バイト) */
    os_time_t st_atime;   /* 最終アクセス日時 (UNIX Epoch) */
    os_time_t st_mtime;   /* 最終更新日時 (UNIX Epoch) */
    os_time_t st_ctime;   /* 状態変更日時・作成日時 (UNIX Epoch) */
} OS32_Stat;

/* ======================================================================== */
/*  ファイル I/O 定数 (Stream API)                                           */
/* ======================================================================== */

/* オープンモード API定数 */
#define KAPI_O_RDONLY    0x00
#define KAPI_O_WRONLY    0x01
#define KAPI_O_RDWR      0x02
#define KAPI_O_CREAT     0x0100
#define KAPI_O_TRUNC     0x0200
/* 排他的作成 (票 H2 §2-1、KAPI v53)。**O_CREAT と組でだけ有効** —
 * 単独で渡すと OS32_ERR_INVAL。名前が既に在れば種別を問わず OS32_ERR_EXIST
 * (ディレクトリでも ISDIR ではない)。在るかどうかを判定できなかったときは
 * その負値をそのまま返す (「読めなかった」を「無い」と読み替えない、票 B8)。
 * 排他性を持てるのは VfsOps.create_excl を実装した FS だけで、持たない FS は
 * OS32_ERR_NOSYS を返す (呼び手は黙って通常の作成へ落ちないこと)。
 * ホスト側が同時に書ける FS (HostDrv) では排他性は成り立たない。 */
#define KAPI_O_EXCL      0x0400

#ifndef O_RDONLY
#define O_RDONLY    KAPI_O_RDONLY
#define O_WRONLY    KAPI_O_WRONLY
#define O_RDWR      KAPI_O_RDWR
#define O_CREAT     KAPI_O_CREAT
#define O_TRUNC     KAPI_O_TRUNC
#define O_EXCL      KAPI_O_EXCL
#endif

/* ======================================================================== */
/*  シリアルの初期化とモード (KAPI v56)                                     */
/*                                                                          */
/*  `serial_init_vfast` の戻り値と `serial_get_status` の `mode`。          */
/*  **カーネル (drivers/serial_plan.h) とユーザランド (userland/shell) の   */
/*  両方が同じ値を見る必要がある** ので、共有の契約ヘッダに置く — 片方に    */
/*  写すと、ずれた日に `serial 115200` の分岐がそっくり狂う ([C4])。        */
/*                                                                          */
/*  **SER_INIT_REFUSED は「何もしなかった」。** 8253 でちょうど出せない     */
/*  速度は適用せず、いまの設定を維持する。ずれた実効値を黙って入れると、    */
/*  FIFO 非搭載機で `serial 115200` を打ったときに 153600bps が入り、       */
/*  ホストが 115200 へ移ったきり**戻すための `serial 9600` も届かなくなる**。 */
/* ======================================================================== */
#define KAPI_SER_MODE_COMPAT   0   /* 8251 + 8253 カウンタ#2 (0030h/0032h) */
#define KAPI_SER_MODE_VFAST    1   /* FIFO + V･FAST (0130h/0132h/013Ah)    */

/* `serial_putchar` の戻り値 (KAPI v57 で void → int に広げた)。
 * **「送れなかった」を黙って捨てない** ([V4]) — rshell の番犬は「応答の EOT を
 * 送り終えた」ことを往復の証拠にしているので、捨てると「応答したつもり」で
 * 解除してしまう。 */
#define KAPI_SER_TX_OK         0
#define KAPI_SER_TX_DROPPED  (-1)

#define KAPI_SER_INIT_VFAST    0   /* V･FAST に入った */
#define KAPI_SER_INIT_COMPAT (-1)  /* 互換モードで初期化した */
#define KAPI_SER_INIT_REFUSED (-2) /* 出せない速度 → 適用しなかった (現状維持) */

/* シーク起点 */
#define SEEK_SET    0
#define SEEK_CUR    1
#define SEEK_END    2

/* FDリダイレクトモード (sys_redirect_fd()用) */
#define FD_REDIR_READ      0   /* 読み込み (stdin用) */
#define FD_REDIR_WRITE     1   /* 書き込み・上書き */
#define FD_REDIR_APPEND    2   /* 書き込み・追記 */

/* パイプバッファの容量 */
#define PIPE_BUF_SIZE   (64 * 1024)

/* ======================================================================== */
/*  FEP モード定数と構造体                                                   */
/*                                                                          */
/*  ユーザ空間 (ime コマンド等) とカーネルの双方が参照するため、             */
/*  kernel/ime.h ではなく共有ヘッダに置く。                                  */
/* ======================================================================== */
#define IME_MODE_OFF       0   /* FEP無効 (直接入力) */
#define IME_MODE_HIRAGANA  1   /* ひらがな入力 */
#define IME_MODE_KATAKANA  2   /* カタカナ入力 */

/* ユーザ辞書エントリ (kapi ime_user_list が void* で返す実体) */
typedef struct {
    char yomi[32];      /* 読み (UTF-8, ヌル終端) */
    char kanji[32];     /* 漢字/表層形 (UTF-8, ヌル終端) */
    int  freq;          /* 変換頻度 */
} IME_UserEntry;

/* 自動生成された APIテーブルを、全ての構造体が定義された後でインクルード */
#include "os32_kapi_generated.h"

/* 外部プログラムのエントリポイント型 */
typedef void (__cdecl *ExecEntry)(int argc, char **argv, KernelAPI *api);

#endif /* OS32_KAPI_SHARED_H */
