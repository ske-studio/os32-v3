#ifndef __cdecl
#define __cdecl __attribute__((cdecl))
#endif
/* ======================================================================== */
/*  HSYNC.C — HostDrv同期コマンド                                           */
/*                                                                          */
/*  /host (HostDrvマウントポイント) の内容を ext2 ルート (/) に同期する。     */
/*                                                                          */
/*  使い方:                                                                 */
/*    hsync              — /host 配下を / に同期 (**sys は除く**)           */
/*    hsync bin           — /host/bin/ → /bin/ のみ同期                    */
/*    hsync -n            — dry-run (読んで比べるだけ、1 バイトも書かない)  */
/*    hsync -v            — 判定理由まで出す                               */
/*    hsync -f            — 同一判定を省いて全上書き (sys は除く)           */
/*    hsync --verify      — 日時を見ず、全件の内容を必ず比較する            */
/*    hsync sys           — /sys を明示指定したときだけ同期する             */
/*    hsync -f sys        — /host/sys/ を強制同期                          */
/*    hsync --root /hd0 boot — /host/boot/ → /hd0/boot/ (同期先の根を替える) */
/*                                                                          */
/*  **同一判定は「サイズ + 内容のバイト比較」** (票 H1、設計書              */
/*  docs/tasks/shell/HSYNC_IMPROVEMENT_PLAN.md §1 / §3.1)。                 */
/*  以前はサイズが同じなら中身を見ずにスキップしていたので、長さを変えずに   */
/*  ヘッダだけ変わった shlib (2026-09-14 の libos32gui.shlib) が更新されず、 */
/*  「配備したのに古いまま」が起きた。                                      */
/*                                                                          */
/*  **日時は「前置フィルタ」** (票 H3 §8、ユーザー決裁 2026-09-15)。        */
/*  H1 の全件バイト比較は変更 0 件でも `hsync sys` に 25.8 秒 / 全体同期に   */
/*  135.4 秒かかった (両側を読むので実 I/O は対象の 2 倍)。日時で候補を絞り、*/
/*  **候補だけをバイト比較**することで読む量を数十分の 1 にする。           */
/*                                                                          */
/*    | サイズか日時が違う   | **内容を比較**して、違えばコピー            | */
/*    | サイズも日時も同じ   | スキップ (unchanged)                       | */
/*    | 日時が不明 / 0       | **内容を比較する**                         | */
/*                                                                          */
/*  **証拠が無いことを同一の根拠にしない** — 日時が取れないときに黙って     */
/*  スキップしない。日時の一致だけでコピーも決めない (決めるのは内容比較)。  */
/*  失うのは「サイズが同じ、かつ日時も同じ、かつ中身が違う」場合だけで、     */
/*  そこが要るときは `--verify` で全件の内容を比較する (= H1 の挙動)。       */
/*                                                                          */
/*  **保存側 (sys_set_mtime、KAPI v52) が無いと成立しない**: 宛先の日時は    */
/*  「hsync がコピーした時刻」なので、コピー元の時刻を宛先へ書かない限り     */
/*  全ファイルが永遠に「変更あり」に見えて 1 件もスキップされない。          */
/*                                                                          */
/*  **既定で sys を外す理由**: /sys には稼働中の常駐シェル (shell.bin)、     */
/*  共有ライブラリ (lib/)、unicode.bin、フォントが入っている。走っている     */
/*  ものを背後から差し替えると、次の exec まで実体と食い違う。入れ替えたい   */
/*  ときは `hsync sys` と明示する (2026-09-09、ホットデプロイ撤去に伴い)。   */
/*  除外は `-f` でも解除しない。                                            */
/*                                                                          */
/*  **置き換えは一時ファイル経由** (票 H2、KAPI v53)。宛先と同じディレクトリ */
/*  の予約名 `.hs~<名前>` へ O_EXCL で作り、書き込み・sync・読戻し検証・      */
/*  mtime まで済ませてから sys_rename で本名に載せ替える。                    */
/*                                                                          */
/*    **公開の前** (書き込み・検証・mtime・置換の途中まで) の失敗            */
/*        … 旧宛先の名前と内容が残る。一時ファイルは片づけられれば片づける。 */
/*          メタデータの失敗でマウントが書き込み禁止 (ROFS) に落ちた後は      */
/*          unlink も通らないので `STALE` と表示し、次の実行が片づける。      */
/*    **公開の後** (宛先エントリの inode が新しい方を指した後) の失敗        */
/*        … 宛先には検証済みの新しい内容が現れる。後始末が落ちたら漏れが      */
/*          残り (e2fsck が回収)、`replace_partial` として報告する            */
/*          (成功には数えない)。                                             */
/*                                                                          */
/*  公開の有無は**宛先の `st_ino`** で判定する。サイズと CRC は証拠にしない   */
/*  (`-f` で新旧が同じ内容だと未公開を公開と誤る)。ジャーナルは無いので       */
/*  「原子的」とは書かない — 電源断では一時ファイルが残り得る。               */
/*                                                                          */
/*  `.hs~` は **hsync の予約接頭辞**。この名前のファイルは hsync が作り、     */
/*  訪れたディレクトリで消す。利用者はこの接頭辞を使わないこと。             */
/*                                                                          */
/*  **古いカーネル** (KAPI v53 未満) では一時ファイル方式が成立しない         */
/*  (O_EXCL が黙って無視され、ext2 の置き換えも旧順序)。既定は 1 件も書かずに */
/*  `kernel_too_old` で断る。`--unsafe-overwrite` を明示したときだけ、以前と  */
/*  同じ直接上書きで進む (**失敗すると旧内容は残らない**)。                   */
/*                                                                          */
/*  **`--root <根>`** (2026-09-26、ユーザー提案): 同期先の根を / から <根>へ  */
/*  替える。FD の新しいカーネルで起動し、シリアル (SerialFS) 越しの /host から */
/*  /hd0 (HDD、FD 起動では自動マウント) を更新するための口。<根> は**マウント */
/*  の根そのもの**で、FD でなく、ext2 であること (root_guard)。保護・予約名・ */
/*  マウントをまたがない規則・名札の照合・/boot/vmkernel.old は <根> からの   */
/*  相対パスで効く。KAPI の版の門 (v53 / 名札の kapi=) が見るのは**走っている*/
/*  カーネル** — 書き込みを行うのはそのカーネルなので、宛先の根では変えない。 */
/* ======================================================================== */

#include "os32api.h"

/* コピー先が /etc/settings.db* かを字句で見る純関数 (票 S0-D / D0)。
 * HostDrv に古い settings.db が残っていても NHD の本体を切り詰めない。
 * 実体は userland/system/hsync_protect.inc、ホスト試験は
 * tools/tests/test_hsync_protect.py。 */
#include "hsync_protect.inc"

/* CRC-32 のストリーム核 (カーネルと共用、票 H1 / 設計書 §4.1)。
 * KAPI は増やさない — カーネル内の crc32_calc を番地で呼ぶのでもなく、
 * 同じ .inc をこちら側でもコンパイルして同じ値を得る。 */
#include "lib/crc32_core.inc"

/* `/boot/vmkernel.old` を作ってよいかの純規則 (票 TASK_SERIAL_HOSTFS §1-v3)。
 * ホスト試験は tools/tests/test_serialfs.py (hsync_bootold)。 */
#include "hsync_bootold.inc"

/* コピーは 64KB 単位。比較はその 64KB を 32KB x 2 に割って使う
 * (設計書 §4.3: ファイル全体を確保しない)。 */
#define FILE_BUF_SIZE  (64 * 1024)
#define CMP_BUF_SIZE   (32 * 1024)
#define MAX_FILES      128
#define MAX_DEPTH      8

/* sys_read / sys_write は int を返すので、2GiB 以上は 1 回の長さで表せない。
 * 黙って切り詰めず、扱えないと言って断る (設計書 §4.3)。 */
#define HS_MAX_FILE_SIZE 0x7FFFFFFFUL

/* ---- 理由コード。**固定文字列**で出す (設計書 §7.2) ------------------- */
/* テスターが文言の雰囲気で判定しないよう、ここ以外で組み立てないこと。 */
#define HR_NEW          "new_file"          /* 宛先が無い */
#define HR_SIZE         "size_changed"      /* サイズが違う */
#define HR_CONTENT      "content_changed"   /* サイズ同じ・内容が違う */
#define HR_FORCED       "forced"            /* -f で比較を省いた */
#define HR_VERIFY       "verify_failed"     /* 読戻しの長さ / CRC が合わない */
#define HR_SOURCE       "source_changed"    /* 読んだ総量が stat と違う */
#define HR_TYPE         "type_conflict"     /* 通常ファイルでない */
#define HR_TYPE_UNKNOWN "type_unknown"      /* 種別が取れない */
#define HR_IO           "io_error"          /* stat / read / write の失敗 */
#define HR_TOO_LARGE    "size_unsupported"  /* 32bit / int で扱えない長さ */
/* ---- 票 H3 (日時) ---- */
#define HR_MTIME_ONLY   "mtime_only"        /* 内容同じ・日時だけ違う */
#define HR_SAME_MTIME   "size_mtime_same"   /* サイズも日時も同じ = 読まずに省略 */
#define HR_SAME_CONTENT "content_same"      /* 読み比べて全内容一致 */
#define HR_META_FAILED  "metadata_failed"   /* 有効な時刻の保存に失敗した */
#define HR_MTIME_UNKNOWN "mtime_unknown"    /* 元の mtime が 0 / 不明 */
#define HR_MTIME_NOSYS  "mtime_unsupported" /* 宛先 FS が set_mtime を持たない */
#define HR_DEFAULT_SYS  "default_sys_exclusion"
#define HR_SETTINGS_DB  "settings_db"
#define HR_TOO_DEEP     "path_too_deep"     /* VFS の要素数上限を越える */
/* ---- 票 H2 (一時ファイル + 検証 + 置換) ---- */
#define HR_REPLACE_PARTIAL "replace_partial"  /* 公開済み・後始末が落ちた */
#define HR_REPLACE_FAILED  "replace_failed"   /* 未公開・旧内容のまま */
#define HR_REPLACE_UNKNOWN "replace_unknown"  /* 公開したか判定できない */
#define HR_TEMP_EXISTS     "temp_exists"      /* 予約名が在って消せない */
#define HR_NAME_TOO_LONG   "name_too_long"    /* 一時名が NAME_CAP に入らない */
#define HR_NO_SPACE        "no_space"         /* 空き不足 */
#define HR_HARDLINK        "hardlink"         /* 宛先の st_nlink > 1 */
#define HR_KERNEL_TOO_OLD  "kernel_too_old"   /* KAPI が v53 未満 */
#define HR_REPLACE_UNSUPPORTED "replace_unsupported" /* 宛先 FS に O_EXCL が無い */
#define HR_DEST_CHANGED    "dest_changed"     /* 判定後に宛先が変わった */
#define HR_SYNC_FAILED     "sync_failed"      /* 置換後の vfs_sync が落ちた */
#define HR_PROT_RESERVED   "protected"        /* 予約名だが保護対象の実体 */
#define HR_RESERVED_NAME   "reserved_name"    /* コピー元に予約名 .hs~ が在る */
/* ---- 票 TASK_SERIAL_HOSTFS (vmkernel の .old) ---- */
#define HR_OLD_NO_INFO     "boot_image_unknown" /* 起動したイメージの記録が無い */
#define HR_OLD_DISK        "boot_image_unreadable" /* 今の vmkernel.lz4 が VK32 v2 として読めない */
#define HR_OLD_DIFF        "not_booted_image"   /* 今の vmkernel.lz4 は起動した版でない */
#define HR_OLD_CORRUPT     "boot_image_corrupt" /* image_crc 欄と中身が合わない */
#define HR_OLD_FAILED      "backup_failed"      /* .old の複製・検証・rename の失敗 */

/* hsync の**予約接頭辞** (票 H2 §2-4、決裁 D3 (a'))。この接頭辞で始まる名前は
 * hsync が作り、hsync が消す。所有の根拠は「作った印」ではなく**予約された
 * 名前空間**に置いてある — `st_nlink` や作成時刻では所有を証明できないので
 * (Codex 往復 1 所見 2)、man ページ (docs/manpages/hsync.1) と
 * docs/06_filesystem.md に「利用者は使わない」と明記したうえで消す。 */
#define HS_TEMP_PREFIX     ".hs~"
/* 長さは**接頭辞の文字列から導く** ([C4]: 同じ値を 2 か所に書かない)。
 * sizeof は終端の '\0' を含むので 1 を引く。 */
#define HS_TEMP_PREFIX_LEN ((int)(sizeof(HS_TEMP_PREFIX) - 1))

/* 一時ファイル方式が成立する最小の KAPI 版 (票 H2 §2-1: O_EXCL)。
 * **build/app.conf の要求版は 52 のまま**なので、v52 のカーネルでも hsync は
 * 起動でき、ここで自分から断れる (§2-3 末尾)。 */
#define HS_MIN_KAPI_H2  53
#define HR_BAD_NAME     "bad_name"          /* 名前に '\' が混じっている */
#define HR_PATH_REJECT  "path_rejected"     /* 正規化できず判定もできない */
#define HR_DST_ON_FD    "dest_on_fd"        /* 同期先がフロッピーのマウント */
#define HR_OTHER_MOUNT  "other_mount"       /* 宛先が別のマウントの根 (またがない) */
/* ---- --root の門 (2026-09-26) ---- */
#define HR_ROOT_NOT_MOUNT "root_not_mount"  /* --root がマウントの根でない */
#define HR_ROOT_NOT_EXT2  "root_not_ext2"   /* --root の FS が ext2 と確かめられない */
#define HR_ROOT_IS_SOURCE "root_is_source"  /* --root が同期元 (/host) 自身かその下 */
#define HR_ROOT_STAT_FAIL "root_stat_failed" /* --root の根を stat できない (I/O 失敗・NOMOUNT) */

/* ext2 のルート inode 番号。**fs/ext2.h の EXT2_ROOT_INO が正典**で、外部
 * プログラムからは引けないので写しを置く (ずれは tools/tests/test_hsync_h2.py
 * が両方を読んで突き合わせる、[C4])。
 *
 * --root の根が ext2 かを**KAPI を足さずに**確かめる印に使う: VFS はマウントの
 * 根の stat を FS へ渡し、ext2 は根の inode (必ず 2) を返す。ほかの FS は
 * 2 を返さない — FAT (fatfs_vfs_stat) と HostDrv / SerialFS は st_ino を 0 の
 * まま、FS が根を stat できないときの VFS の合成 (vfs_synth_root_stat) も 0、
 * ISO9660 は根ディレクトリの LBA (システム領域 16 セクタの後なので 16 以上)。
 * カーネル内の vfs_fstype は KAPI に出ていない。 */
#define HS_EXT2_ROOT_INO 2UL

/* フロッピーのデバイス名の先頭 (drivers/dev.c の "fd0" / "fd1"、kernel.c の
 * FD 起動のルート root_dev = "fd0")。同期先のマウントがこれなら断る */
#define HS_FD_DEV_PREFIX0 'f'
#define HS_FD_DEV_PREFIX1 'd'


/* VFS が 1 パスで扱える要素数。**fs/vfs.h の VFS_MAX_PATH_DEPTH が正典**で、
 * 外部プログラムからはそのヘッダを引けないので写しを置く。ずれの検出は
 * tools/tests/test_hsync_h1.py が両方を読んで突き合わせる ([C4])。
 *
 * fs/vfs.c の正規化は上限を越えた要素を**黙って捨てる** (エラーを返さない)。
 * `/host` を前置すると同期元は指定より 1 要素深くなるので、32 要素ちょうどの
 * dir を渡すと 33 要素になり、VFS が末尾を落として**指定した親ディレクトリ**を
 * 列挙してしまう。宛先側も同じ理由で別のパスに化ける。何も同期していないのに
 * `errors=0` / 終了コード 0 になる経路だったので、越えたら明示エラーにする
 * (Codex 実装レビュー B2)。 */
#define HS_MAX_PATH_DEPTH 32

static KernelAPI *api;
static u8 *file_buf;                 /* 64KB。コピーと読戻しで使う */
static u8 *cmp_a;                    /* file_buf[0 .. 32KB) */
static u8 *cmp_b;                    /* file_buf[32KB .. 64KB) */

/* 統計 (設計書 §7.2 の 6 区分) */
static int g_copied;                 /* dry-run では「予定件数」 */
static int g_unchanged;
static int g_excluded;
static int g_protected;
static int g_metadata_updated;       /* 内容は同じで mtime だけ直したもの */
static int g_cleaned;                /* 片づけた予約名 (.hs~) の数 (票 H2 §2-4) */
static int g_errors;

/* 集計には出さないが、**省略したことを必ず見せる**ための数 (票 H3) */
static int g_mtime_unknown;          /* 元の mtime が 0 = 保存を省略した */
static int g_mtime_nosys;            /* 宛先 FS が set_mtime を持たない */

/* オプション */
static int g_force;
static int g_dry_run;
static int g_verbose;
static int g_verify;                 /* 全件の内容を必ず比較する (票 H3 §8) */
static int g_unsafe;                 /* --unsafe-overwrite が指定された */
static int g_direct;                 /* 実際に直接上書きで進む (旧カーネル) */
static int g_direct_overwrite;       /* 直接上書きで書いた件数 */

/* 既定の /sys 除外は「全体同期のルート直下」だけに効かせる。
 * `hsync usr` の usr/sys を巻き添えにしない (設計書 §3.2)。 */
static int g_root_sync;

/* 更新した先に応じた再起動の案内 (設計書 §7.2)。自動では再起動しない */
static int g_touched_sys;
static int g_touched_boot;
static int g_no_backup;              /* --no-backup (vmkernel.old を作らない) */

/* --root の根 (正規化済み、末尾 '/' 無し)。既定 (/) は空文字列 — 宛先のパスは
 * 常に `g_root + ルートからの相対` で組むので、既定では今までと同じ文字列になる。 */
static char g_root[OS32_MAX_PATH];
static int  g_root_len;

/* ======== 文字列ユーティリティ ======== */

static int str_len(const char *s)
{
    int n = 0;
    while (*s++) n++;
    return n;
}

/* 連結・コピーは**容量付きのものだけ**を置く。無検査の str_cpy / str_cat は
 * 引数から組み立てた長いパスでバッファを越えるので撤去した (往復 2 の 5)。
 *
 * 容量付き連結。cap は NUL 込みのバッファ長。
 * 戻り値 1 = 入った / 0 = 溢れた (dst は変えない)。
 * `hsync ./././...etc` のように引数から組み立てた文字列が OS32_MAX_PATH を
 * 越えると、正規化や保護判定にたどり着く前にスタックを壊していた
 * (往復 2 の 5)。連結は必ずこちらを通す。 */
static int str_ncat(char *dst, const char *src, int cap)
{
    int len = str_len(dst);
    int add = str_len(src);
    int i;

    if (len + add + 1 > cap) return 0;
    for (i = 0; i < add; i++) dst[len + i] = src[i];
    dst[len + add] = '\0';
    return 1;
}

/* 容量付きコピー。戻り値 1 = 入った / 0 = 溢れた */
static int str_ncpy(char *dst, const char *src, int cap)
{
    dst[0] = '\0';
    return str_ncat(dst, src, cap);
}

static int str_cmp(const char *a, const char *b)
{
    while (*a && *a == *b) { a++; b++; }
    return (unsigned char)*a - (unsigned char)*b;
}

static int str_has_prefix(const char *s, const char *pre)
{
    int i = 0;
    while (pre[i]) {
        if (s[i] != pre[i]) return 0;
        i++;
    }
    return 1;
}

/* hsync の予約名か (票 H2 §2-4)。**`st_nlink` や作成時刻は見ない** —
 * 途中で止まった媒体では nlink が 2 になり得るし、所有の根拠は名前空間の
 * 予約だけに置いてある。 */
static int hs_is_temp_name(const char *name)
{
    return str_has_prefix(name, HS_TEMP_PREFIX);
}

/* 宛先の完全パスから、同期先の根 (--root) を除いた**ルートからの相対**を返す。
 * 保護 (/etc/settings.db*)・名札の照合・/sys と /boot の案内・vmkernel の判定は
 * これで見る — `--root /hd0` の `/hd0/etc/settings.db` も `/etc/settings.db` と
 * 同じに守る。宛先は必ず g_root から組むので前置は一致するが、一致しないものが
 * 来たら**そのまま**返す (完全パスで判定する = 狭めない側)。根そのものは "/"。 */
static const char *hs_rel(const char *dst)
{
    const char *r;

    if (g_root_len == 0 || !str_has_prefix(dst, g_root)) return dst;
    r = dst + g_root_len;
    if (r[0] == '\0') return "/";
    if (r[0] != '/') return dst;              /* /hd0x は /hd0 の下ではない */
    return r;
}

/* ======== ファイルリスト ======== */

/* 名前の保持幅。FileList はスタックに載る (MAX_FILES x NAME_CAP x 深さ) ので
 * 無闇に広げられない。収まらない名前は**切り詰めずにエラー**にする (下記)。 */
#define NAME_CAP 64

typedef struct {
    const char *src_dir;  /* 列挙中のコピー元ディレクトリ (表示用。FS には触らない) */
    char names[MAX_FILES][NAME_CAP];
    u8   types[MAX_FILES];
    int  count;
    int  dropped;      /* MAX_FILES を越えて捨てたエントリ数 */
    int  truncated;    /* NAME_CAP に収まらず取り込めなかったエントリ数 */
    int  bad_name;     /* '\' を含むので取り込まなかったエントリ数 (B1) */
} FileList;

/* 列挙時のサイズは**持ち回さない**。同一判定に使うサイズは比較の直前に
 * stat で取り直す (設計書 §3.1)。ここでは名前と種別だけ私有バッファへ写す
 * (コールバックの中では FS に触らない、POLICY_DEBUG §4-26)。 */
static void ls_cb(const DirEntry_Ext *entry, void *ctx)
{
    FileList *fl = (FileList *)ctx;
    int i;

    /* **予約名は同期対象にしない** (票 H2 §2-3 手順 1)。127 件の枠に入れる
     * より**前**に弾く — 途中で止まった実行が残した `.hs~` が 128 件の枠を
     * 食って通常ファイルを落とすのを防ぐ。掃除は別の枠で行う (§2-4)。
     *
     * **黙って落とさない**。ホスト側の配備元に誤って `.hs~x` が紛れると、
     * その 1 件は同期されないのに `excluded` にも `-v` の行にも出ず、
     * 気づく手がかりが無かった。除外として数えて -v で見せる。
     * 見せるのは**コピー元**の名前 — 直すのはそちらなので。
     * コールバックの中なので FS には触らず、パスも組み立てずに書式で繋ぐ
     * (POLICY_DEBUG §4-26)。 */
    if (hs_is_temp_name(entry->name)) {
        g_excluded++;
        if (g_verbose) {
            const char *dir = fl->src_dir ? fl->src_dir : "";
            int n = str_len(dir);
            api->kprintf(ATTR_YELLOW, "  EXCLUDE %s%s%s reason=%s\n",
                         dir, (n > 0 && dir[n - 1] == '/') ? "" : "/",
                         entry->name, HR_RESERVED_NAME);
        }
        return;
    }

    if (fl->count >= MAX_FILES) { fl->dropped++; return; }

    if (!hsp_name_fits(entry->name, NAME_CAP)) {
        /* 切り詰めた名前でコピーすると**別のファイル**を作って成功と出る
         * (往復 3 の D6)。取り込まずに数えて、呼び手がエラーにする。 */
        fl->truncated++;
        return;
    }
    if (hsp_has_backslash(entry->name)) {
        /* ホスト側のファイル名に '\' は入らないはずだが、そこを信用しない。
         * '\' は OS32 では普通の 1 文字なのに HostDrv の先では区切りに化け、
         * `..\other` のような名前が同期元の外を指す (B1)。組み立てる前に断る。 */
        fl->bad_name++;
        return;
    }
    i = 0;
    while (entry->name[i]) {
        fl->names[fl->count][i] = entry->name[i];
        i++;
    }
    fl->names[fl->count][i] = '\0';
    fl->types[fl->count] = entry->type;
    fl->count++;
}

/* ======== 票 H4: 配備の名札 (manifest) ======== */

/* 配備元に「この配備元がどの版か」を書いた名札を置き、`hsync` がそれを読む
 * (票 H4、docs/archive/shell/TASK_H4.md §2-1〜§2-3、ユーザー決裁 D1)。
 * 防ぎたい事故は **「古い配備元から新しい成果物へ戻してしまう」**:
 * `make deploy` を忘れたまま `hsync` を打つと、ゲストの新しいファイルが
 * ホストの古いもので上書きされ、しかも「同期が成功した」ように見える。
 * 内容の違いは H1 の内容比較で分かるが、**どちらが意図した版か**は分からない。
 *
 * 形式は**行指向の平文** (ゲストに JSON パーサが無い):
 *
 *     format=2
 *     build=9742a6b+dirty
 *     generated=2026-09-16T21:45:19Z
 *     kapi=1208
 *     kapi_version=63
 *     count=198
 *     ---
 *
 * format=2 (票 TASK_KAPI_DATA_FIELDS、KAPI v63) で `kapi=` (配備物の KernelAPI
 * データ欄のオフセット = OS32X ヘッダ v3 の kapi_data_off、10 進) と
 * `kapi_version=` (配備物を作った KAPI 版) を足した。**配置がカーネルと違う**
 * か **配備物の版がカーネルより新しい**なら既定で 1 件も書かずに断る
 * (`--force-kapi` で越える)。名札が無い・壊れている・format=1 (kapi が無い)
 * は「確かめられない」で、**一致とは扱わない** (同じく断る)。
 *     bin/cat.bin 16428 3b7f2a10 1789520013
 *
 * ファイルの行は **パス / サイズ / CRC-32 (8 桁 16 進、小文字) / mtime**、
 * 区切りは空白 1 つ。パスはルートからの相対で、絶対パスと `..` は読まない。
 *
 * **守らないこと** (§2-3-1): コピー群の配備と名札の更新は**原子的ではない**。
 * 全件のコピーが成功した後・名札を書く前にホストが落ちると、配備元は新しい
 * のに名札は古い (または無い) 状態になる。そこへ `--expect-build <新しい ID>`
 * を打つと `build_mismatch` で断られる — ファイルは新しいのに同期できない。
 * **安全側の壊れ方**だが、起こることとして書いておく。復旧は `make deploy`
 * をもう一度打つだけ。
 *
 * **信じすぎないこと** (§2-3): 名札の CRC が宛先と一致していても、それは
 * 宛先が同じである証明にはならない。**内容比較は絶対に省かない**。
 * 永続 CRC キャッシュも作らない。 */

/* `/host` を前置する前の相対パス。名札そのものの位置でもある ([C4]: 同じ
 * 文字列を 2 か所に書かず、連結で導く)。 */
#define HS_MANIFEST_REL  ".deploy/manifest.txt"
#define HS_MANIFEST_PATH "/host/" HS_MANIFEST_REL
#define HS_MAN_FORMAT    "2"
#define HS_MAN_SEP       "---"

/* 名札の表のメモリ上限。**越えたら名札ごと捨てる** (§2-3、受入 M9b)。
 *
 * 実測 (2026-09-16): 配備定義 (`build/core.yaml` + `userland/deploy.yaml` +
 * `apps` / `game` の `deploy.yaml`) が展開するファイルは**約 200 件**。
 * 320 はそこに 6 割の余裕を置いた値で、表は
 * `320 x (64 + 4 + 4 + 4) = 24,320 バイト` の BSS に収まる — 既に確保して
 * いるコピー用バッファ (FILE_BUF_SIZE = 64KB) より小さい。
 *
 * 越えたときに**同期そのものを止めない**のは、名札が事故防止の補助であって
 * 本体の同期は名札なしでも正しく動くから。名札のためにメモリを食って本来の
 * 列挙 (FileList) を妨げるほうが害が大きい (設計レビュー往復 3)。
 *
 * パスの上限は `NAME_CAP` に揃える (§2-3 が「長すぎるパス (NAME_CAP 超)」を
 * 捨てる条件に挙げている)。実測の最長は `usr/bin/sqlite_standalone.bin` の
 * 29 文字なので倍以上の余裕がある。 */
#define HS_MAN_MAX      320
#define HS_MAN_PATH_CAP NAME_CAP

/* 名札の本文はコピー用バッファ (file_buf) を借りて読む。名札を読むのは同期を
 * 始める**前の 1 回だけ**で、そのときコピー用バッファはまだ使っていない。
 * 数十 KB を別取りしないための作法。入りきらなければ名札ごと捨てる。 */
#define HS_MAN_TEXT_CAP (FILE_BUF_SIZE - 1)

typedef struct {
    char path[HS_MAN_PATH_CAP];   /* ルートからの相対 (先頭に '/' を付けない) */
    u32  size;
    u32  crc;                     /* 表示・検査用。**判定には使わない** */
    u32  mtime;
} ManEntry;

static ManEntry g_man[HS_MAN_MAX];
static int g_man_count;
static int g_man_present;            /* 名札のファイルが在った (壊れていても 1) */
static int g_man_valid;              /* 全件の検査を通り、表が使える */
static const char *g_man_bad;        /* 捨てた理由。**固定文字列**で出す */
static char g_man_build[HS_MAN_PATH_CAP];
static char g_man_generated[HS_MAN_PATH_CAP];
static const char *g_expect_build;   /* --expect-build の引数 */
/* 票 TASK_KAPI_DATA_FIELDS: 名札の kapi= / kapi_version= */
static u32 g_man_kapi;
static u32 g_man_kapi_ver;
static int g_force_kapi;             /* --force-kapi が指定された */

/* 集計 (§2-3)。**`manifest_missing` は実装しない** — `hsync` は配備元を正と
 * して列挙するので、名札にあるのに配備元に無いファイルは列挙に現れず、
 * そのままでは数えられない。名札を正とする走査が別に要るので**別票**。 */
static int g_man_extra;
/* 観測窓: 名札の表を実際に引いた回数。「その経路が走ったか」を数で見る。 */
static int g_man_lookups;
/* 観測窓: 内容比較 (compare_files) が走った回数。**名札を信じて省いていない**
 * ことを、文言ではなくこの数で確かめる (受入 M10)。 */
static int g_content_compares;

#define HR_BUILD_MISMATCH   "build_mismatch"
#define HR_MANIFEST_INVALID "manifest_invalid"
#define HR_MANIFEST_ABSENT  "manifest_absent"
#define HR_MAN_EXTRA        "not_in_manifest"
/* 票 TASK_KAPI_DATA_FIELDS (KAPI の門) */
#define HR_KAPI_LAYOUT      "kapi_layout_mismatch"
#define HR_KAPI_NEWER       "kapi_newer_than_kernel"

/* 本文を行に割る。改行は '\0' に書き換える (その場で壊して読む)。
 * 戻り値 0 = もう行が無い。末尾の '\r' は落とす (ホストが CRLF で書いた
 * ときに名札ごと捨てるのは行き過ぎ — 形式の要は空白区切りと欄数)。 */
static char *man_next_line(char **pp)
{
    char *s = *pp;
    char *p;
    int n;

    if (!s || !*s) { *pp = 0; return 0; }
    p = s;
    while (*p && *p != '\n') p++;
    if (*p == '\n') { *p = '\0'; *pp = p + 1; }
    else             { *pp = p; }
    n = str_len(s);
    if (n > 0 && s[n - 1] == '\r') s[n - 1] = '\0';
    return s;
}

/* 10 進の u32。**桁あふれを黙って丸めない**。戻り値 1 = 読めた */
static int man_parse_u32(const char *s, u32 *out)
{
    u32 v = 0;
    int i;

    if (!s[0]) return 0;
    for (i = 0; s[i]; i++) {
        u32 d;
        if (s[i] < '0' || s[i] > '9') return 0;
        d = (u32)(s[i] - '0');
        if (v > 429496729UL) return 0;
        v = v * 10;
        if (v > 0xFFFFFFFFUL - d) return 0;
        v += d;
    }
    *out = v;
    return 1;
}

/* **ちょうど 8 桁の小文字 16 進**。桁数も大小も緩めない (§2-1) */
static int man_parse_crc(const char *s, u32 *out)
{
    u32 v = 0;
    int i;

    for (i = 0; i < 8; i++) {
        int c = s[i];
        int d;
        if (c >= '0' && c <= '9')      d = c - '0';
        else if (c >= 'a' && c <= 'f') d = c - 'a' + 10;
        else return 0;
        v = (v << 4) | (u32)d;
    }
    if (s[8]) return 0;
    *out = v;
    return 1;
}

/* 名札に書けるパスか。**絶対パスと `..` は読まない** (§2-1)。
 * '\' は OS32 では普通の 1 文字なのに HostDrv の先では区切りに化けるので、
 * 列挙側 (B1) と同じ規則で弾く。 */
static int man_path_ok(const char *p)
{
    int i = 0;
    int s = 0;

    if (!p[0] || p[0] == '/') return 0;
    for (;;) {
        char c = p[i];
        if (c == '\0' || c == '/') {
            int n = i - s;
            if (n == 0) return 0;                            /* 空の要素 */
            if (n == 1 && p[s] == '.') return 0;             /* '.' */
            if (n == 2 && p[s] == '.' && p[s + 1] == '.') return 0;  /* '..' */
            if (c == '\0') return 1;
            s = i + 1;
        } else if (c == '\\' || c == ' ' || c == '\t' ||
                   (unsigned char)c < 0x20) {
            return 0;
        }
        i++;
    }
}

/* ファイルの行 1 本。**欄はちょうど 4 つ、区切りは空白 1 つ**。 */
static int man_parse_entry(char *line)
{
    char *f[4];
    int nf = 1;
    char *q = line;
    int i;
    ManEntry *e;

    f[0] = line;
    while (*q) {
        if (*q == ' ') {
            if (nf >= 4) { g_man_bad = "extra field"; return 0; }
            *q = '\0';
            f[nf++] = q + 1;
        }
        q++;
    }
    if (nf != 4) { g_man_bad = "bad field count"; return 0; }

    if (str_len(f[0]) >= HS_MAN_PATH_CAP) {
        g_man_bad = "path too long";
        return 0;
    }
    if (!man_path_ok(f[0])) { g_man_bad = "bad path"; return 0; }

    for (i = 0; i < g_man_count; i++) {
        if (str_cmp(g_man[i].path, f[0]) == 0) {
            g_man_bad = "duplicate path";
            return 0;
        }
    }
    if (g_man_count >= HS_MAN_MAX) {
        /* count の検査を通っていればここへは来ないが、上限は 2 重に守る */
        g_man_bad = "too many entries";
        return 0;
    }

    e = &g_man[g_man_count];
    if (!str_ncpy(e->path, f[0], (int)sizeof(e->path))) {
        g_man_bad = "path too long";
        return 0;
    }
    if (!man_parse_u32(f[1], &e->size))  { g_man_bad = "bad size";  return 0; }
    if (!man_parse_crc(f[2], &e->crc))   { g_man_bad = "bad crc";   return 0; }
    if (!man_parse_u32(f[3], &e->mtime)) { g_man_bad = "bad mtime"; return 0; }
    g_man_count++;
    return 1;
}

/* 本文を解く。**全件を先に検査してから使う** — 1 件でも壊れていたら
 * 名札ごと捨てる (§2-3)。戻り値 1 = 使える。 */
static int man_parse(char *text)
{
    char *p = text;
    char *line;
    int have_format = 0;
    int have_build = 0;
    int have_gen = 0;
    int have_count = 0;
    int have_kapi = 0;
    int have_kapi_ver = 0;
    u32 want = 0;
    u32 n;

    g_man_count = 0;
    g_man_build[0] = '\0';
    g_man_generated[0] = '\0';
    g_man_kapi = 0;
    g_man_kapi_ver = 0;

    for (;;) {
        char *eq;
        const char *key;
        const char *val;

        line = man_next_line(&p);
        if (!line) { g_man_bad = "no separator"; return 0; }
        if (str_cmp(line, HS_MAN_SEP) == 0) break;

        eq = line;
        while (*eq && *eq != '=') eq++;
        if (*eq != '=') { g_man_bad = "bad header line"; return 0; }
        *eq = '\0';
        key = line;
        val = eq + 1;

        if (str_cmp(key, "format") == 0) {
            if (have_format) { g_man_bad = "duplicate key"; return 0; }
            have_format = 1;
            if (str_cmp(val, HS_MAN_FORMAT) != 0) {
                g_man_bad = "format version";
                return 0;
            }
        } else if (str_cmp(key, "build") == 0) {
            int i;
            if (have_build) { g_man_bad = "duplicate key"; return 0; }
            have_build = 1;
            if (!val[0]) { g_man_bad = "empty build"; return 0; }
            for (i = 0; val[i]; i++) {
                if (val[i] == ' ' || val[i] == '\t') {
                    g_man_bad = "build has space";
                    return 0;
                }
            }
            if (!str_ncpy(g_man_build, val, (int)sizeof(g_man_build))) {
                g_man_bad = "build too long";
                return 0;
            }
        } else if (str_cmp(key, "generated") == 0) {
            if (have_gen) { g_man_bad = "duplicate key"; return 0; }
            have_gen = 1;
            if (!str_ncpy(g_man_generated, val,
                          (int)sizeof(g_man_generated))) {
                g_man_bad = "generated too long";
                return 0;
            }
        } else if (str_cmp(key, "count") == 0) {
            if (have_count) { g_man_bad = "duplicate key"; return 0; }
            have_count = 1;
            if (!man_parse_u32(val, &want)) { g_man_bad = "bad count"; return 0; }
        } else if (str_cmp(key, "kapi") == 0) {
            /* 配備物の KAPI データ欄の配置 (票 TASK_KAPI_DATA_FIELDS)。
             * 0 は「分からない」と同じなので不正として捨てる。 */
            if (have_kapi) { g_man_bad = "duplicate key"; return 0; }
            have_kapi = 1;
            if (!man_parse_u32(val, &g_man_kapi) || g_man_kapi == 0) {
                g_man_bad = "bad kapi";
                return 0;
            }
        } else if (str_cmp(key, "kapi_version") == 0) {
            if (have_kapi_ver) { g_man_bad = "duplicate key"; return 0; }
            have_kapi_ver = 1;
            if (!man_parse_u32(val, &g_man_kapi_ver) || g_man_kapi_ver == 0) {
                g_man_bad = "bad kapi_version";
                return 0;
            }
        } else {
            /* format=1 が形式を固定しているので、知らない鍵は「別の形式」。
             * 読めるふりをしない。 */
            g_man_bad = "unknown key";
            return 0;
        }
    }

    if (!have_format || !have_build || !have_gen || !have_count ||
        !have_kapi || !have_kapi_ver) {
        g_man_bad = "missing key";
        return 0;
    }
    /* **表に収まらない名札は捨てる** (§2-3、受入 M9b)。切り詰めて使うと
     * 「名札に無い」が嘘になり、manifest_extra が意味を失う。 */
    if (want > (u32)HS_MAN_MAX) { g_man_bad = "too many entries"; return 0; }

    for (n = 0; n < want; n++) {
        line = man_next_line(&p);
        if (!line) { g_man_bad = "count mismatch"; return 0; }
        if (!man_parse_entry(line)) return 0;
    }
    /* count より行が多い = 数えたものと書いてあるものが違う */
    if (man_next_line(&p)) { g_man_bad = "count mismatch"; return 0; }
    return 1;
}

/* 名札を読む。**無ければ今までどおり動く** (後方互換、受入 M5)。 */
static void man_load(void)
{
    int fd;
    int total = 0;
    char *text;

    g_man_present = 0;
    g_man_valid = 0;
    g_man_count = 0;
    g_man_bad = 0;

    fd = api->sys_open(HS_MANIFEST_PATH, KAPI_O_RDONLY);
    if (fd < 0) return;
    g_man_present = 1;

    text = (char *)file_buf;
    for (;;) {
        int n;
        if (total >= HS_MAN_TEXT_CAP) {
            /* 上限ちょうどで終わったのか、まだ続くのかを 1 バイトで見分ける */
            char probe;
            n = api->sys_read(fd, &probe, 1);
            if (n < 0)      { g_man_bad = "read error"; break; }
            if (n > 0)      { g_man_bad = "too large";  break; }
            break;
        }
        n = api->sys_read(fd, text + total, (u32)(HS_MAN_TEXT_CAP - total));
        if (n < 0) { g_man_bad = "read error"; break; }
        if (n == 0) break;
        total += n;
    }
    api->sys_close(fd);
    if (g_man_bad) return;

    text[total] = '\0';
    g_man_valid = man_parse(text) ? 1 : 0;
    if (!g_man_valid) g_man_count = 0;      /* 半端な表を残さない */
}

/* 起動時の表示と `--expect-build` の門。
 * 戻り値 0 = 同期を続けてよい / 1 = **1 件も書かずに断る**。 */
static int man_gate(void)
{
    if (g_man_present && g_man_valid) {
        api->kprintf(ATTR_CYAN, "DEPLOY build=%s count=%d generated=%s kapi=%d/v%d\n",
                     g_man_build, g_man_count, g_man_generated,
                     (int)g_man_kapi, (int)g_man_kapi_ver);
    } else if (g_man_present) {
        /* 名札の不備で作業が止まるのは本末転倒 — 既定は表示だけ (§2-3) */
        api->kprintf(ATTR_YELLOW, "DEPLOY manifest invalid: %s\n",
                     g_man_bad ? g_man_bad : "unknown");
    }

    if (!g_expect_build) return 0;      /* 既定は表示だけして同期を続ける */

    if (g_man_present && g_man_valid) {
        /* 判定は**文字列の完全一致**。大小を比べない、前方一致もしない
         * (§2-3)。名札は順序を表さないので「新しい / 古い」も決めない。 */
        if (str_cmp(g_man_build, g_expect_build) == 0) return 0;
        api->kprintf(ATTR_RED,
                     "Error: DEPLOY reason=%s expect=%s actual=%s\n",
                     HR_BUILD_MISMATCH, g_expect_build, g_man_build);
        api->kprintf(ATTR_RED,
                     "  配備元の世代が指定と違う。**1 件も書かない**。\n"
                     "  `make deploy` を打ち直すか、意図した巻き戻しなら\n"
                     "  --expect-build を外すこと\n");
        return 1;
    }

    /* ---- ここから先は「**確かめられない**」 --------------------------
     * 名札が壊れている場合と**無い**場合を、ここでは同じに扱う (PM 決裁
     * 2026-09-16)。**読めないことを「一致」と扱わない** (往復 1 所見 2) —
     * 壊れた名札で保護が素通りすると、H4 が防ぐはずの事故がそのまま起きる。
     *
     * 上の `build_mismatch` と分かれているのが要点:
     * **「確かめた結果おかしい」と「確かめられない」は別**。
     * 前者は絞り込みでも断る (protection の本体)。後者はここで扱う。 */
    if (!g_root_sync) {
        /* **範囲を絞った同期では表示だけにして続ける** (往復 2 所見 1)。
         * 名札はルートの世代を表すもので、**絞った範囲の正しさは保証しない**。
         * この理屈は「壊れている」と「無い」の両方に等しく当たるので、
         * 名札の有無で扱いを変えない。ここで止めると「名札の不備で作業が
         * 止まる」形になり、本末転倒。 */
        api->kprintf(ATTR_YELLOW,
                     "NOTE: 配備元の世代を確かめられないが、範囲を絞った同期"
                     "なので続ける (断るのは全体同期のときだけ)\n");
        return 0;
    }
    api->kprintf(ATTR_RED, "Error: DEPLOY reason=%s (%s)\n",
                 g_man_present ? HR_MANIFEST_INVALID : HR_MANIFEST_ABSENT,
                 g_man_present ? (g_man_bad ? g_man_bad : "unknown")
                               : "no manifest");
    api->kprintf(ATTR_RED,
                 "  --expect-build を指定した以上、世代を確かめられないなら\n"
                 "  進まない。**1 件も書かない**\n");
    return 1;
}

/* KAPI の門 (票 TASK_KAPI_DATA_FIELDS)。**全体同期でも絞り込みでも**見る
 * — `hsync sys` こそ常駐シェルと共有ライブラリを差し替える一番危ない形。
 *
 * 比べるのは名札の kapi= (配備物のデータ欄の配置) と**このカーネルの配置**。
 * 動いている hsync 自身がこのカーネルに受け入れられた (exec が OS32X ヘッダ
 * v3 の kapi_data_off を照合済み) ので、コンパイル時の KAPI_DATA_FIELDS_OFF
 * がカーネルの配置に等しい。版は api->version。
 *
 * 断るのは:
 *   - 配置が違う                    → kapi_layout_mismatch
 *   - 配備物の版 > カーネルの版     → kapi_newer_than_kernel (v64 以降は
 *     「カーネルを先、ユーザーランドを後」— 決裁 2026-09-24)。
 *     **ただし `/boot` だけに絞った同期 (`hsync boot`) はこの 1 条件から外す**:
 *     カーネルを先に運ぶ唯一の HostDrv 経路で、ここで断ると v64 の
 *     カーネルがいつまでも入らない (Codex 実装レビュー R1 blocker 2)。
 *     /boot にはユーザーランドが無いので、版の前後はこの同期に関係しない。
 *     配置違い・名札の欠落/不正は /boot でも断る (配備元の一式が
 *     このカーネル系列のものか確かめられない)。
 *   - 名札が無い / 壊れている / kapi が無い (format=1) → 確かめられない。
 *     **一致とは扱わない**。
 * `--force-kapi` で越える (理由を表示して続ける)。`-f` / `--force` は
 * 同一判定の省略で別の意味なので、この門は開けない。
 * 戻り値 0 = 続けてよい / 1 = **1 件も書かずに断る**。 */
static int man_kapi_check(u32 kernel_off, u32 kernel_ver, int boot_only,
                          const char **why)
{
    if (!g_man_present) { *why = HR_MANIFEST_ABSENT;  return 1; }
    if (!g_man_valid)   { *why = HR_MANIFEST_INVALID; return 1; }
    if (g_man_kapi != kernel_off) { *why = HR_KAPI_LAYOUT; return 1; }
    if (!boot_only && g_man_kapi_ver > kernel_ver) {
        *why = HR_KAPI_NEWER;
        return 1;
    }
    *why = 0;
    return 0;
}

/* target = 正規化した絞り込み先 (`/boot` など)、全体同期なら NULL。
 * `/boot` とその下だけが「カーネルを先に運ぶ」同期。 */
static int man_kapi_gate(const char *target)
{
    const char *why = 0;
    u32 koff = (u32)KAPI_DATA_FIELDS_OFF;
    u32 kver = (u32)api->version;
    int boot_only = (target != 0 &&
                     (str_cmp(target, "/boot") == 0 ||
                      str_has_prefix(target, "/boot/")));

    if (man_kapi_check(koff, kver, boot_only, &why) == 0) {
        if (boot_only && g_man_valid && g_man_kapi_ver > kver) {
            api->kprintf(ATTR_YELLOW,
                         "NOTE: /boot だけの同期なので配備物の版 v%d > "
                         "カーネル v%d でも続ける (カーネルを先)。\n"
                         "  再起動して ver で版を確かめてから hsync / hsync sys\n",
                         (int)g_man_kapi_ver, (int)kver);
        }
        return 0;
    }

    if (g_force_kapi) {
        api->kprintf(ATTR_YELLOW,
                     "NOTE: KAPI (%s) を --force-kapi で越える "
                     "(kernel kapi=%d/v%d)\n", why, (int)koff, (int)kver);
        return 0;
    }
    api->kprintf(ATTR_RED,
                 "Error: KAPI reason=%s host=%d/v%d kernel=%d/v%d\n", why,
                 (int)g_man_kapi, (int)g_man_kapi_ver, (int)koff, (int)kver);
    api->kprintf(ATTR_RED,
                 "  配備元のユーザーランドがこのカーネルと合うか確かめられない。"
                 "**1 件も書かない**。\n"
                 "  配置違いは全部作り直し (make clean && make all) + NHD 一式か FD。\n"
                 "  版が新しいならカーネルを先に: hsync boot → 再起動 → ver → hsync。\n"
                 "  承知の上なら --force-kapi\n");
    return 1;
}

/* 宛先のパス (`/bin/a.bin`) が名札にあるか。無ければ -1。
 * --root のときも名札のパスは**根からの相対**なので、根を除いてから引く。 */
static int man_find(const char *dst_path)
{
    const char *rel = hs_rel(dst_path);
    int i;

    if (rel[0] == '/') rel++;
    for (i = 0; i < g_man_count; i++)
        if (str_cmp(g_man[i].path, rel) == 0) return i;
    return -1;
}

/* 1 件写した。**名札に無いものを数える** (§2-3、受入 M9)。
 * 数えるだけで拒否はしない — 名札は事故の手がかりであって関門ではない。 */
static void man_note_copy(const char *dst_path)
{
    const char *rel;

    if (!g_man_valid) return;           /* 名札が無ければ数えようがない */
    g_man_lookups++;
    if (man_find(dst_path) >= 0) return;

    /* **名札は自分自身を載せられない** (中身が決まる前に自分の CRC は出せ
     * ない)。名札の写しを「名札に無いもの」と数えると、まっさらなゲストでは
     * 必ず manifest_extra=1 になり、本当の食い違いが埋もれる。 */
    rel = hs_rel(dst_path);
    if (rel[0] == '/') rel++;
    if (str_cmp(rel, HS_MANIFEST_REL) == 0) return;

    g_man_extra++;
    if (g_verbose)
        api->kprintf(ATTR_YELLOW, "  EXTRA %s reason=%s\n",
                     dst_path, HR_MAN_EXTRA);
}

/* ======== 低レベル I/O (短い read / write を詰める) ======== */

/* want バイト読めるまで sys_read を繰り返す。
 * 戻り値 >= 0 … 実際に読めたバイト数 (want 未満は EOF)
 *        <  0 … I/O エラー
 * 両側の short read の**分割が違っても**、これを通せば比較のオフセットと
 * 有効長が揃う (設計書 §3.1)。 */
static int read_fill(int fd, u8 *buf, int want)
{
    int got = 0;

    while (got < want) {
        int rd = api->sys_read(fd, buf + got, (u32)(want - got));
        if (rd < 0) return rd;              /* I/O エラー */
        if (rd == 0) break;                 /* EOF */
        if (rd > want - got) return -1;     /* 契約違反 (溢れている) */
        got += rd;
    }
    return got;
}

/* len バイト書けるまで sys_write を繰り返す。
 * 0 進捗・負値・要求超過は失敗 (設計書 §6 の規則を H1 でも守る)。
 * 戻り値 len = 成功 / 負値 = 失敗。**失敗は sys_write が返した番号をそのまま
 * 返す** (票 H2 §2-3 手順 3: 空き不足を `no_space` と呼び分けるため。
 * 0 進捗と契約違反は番号が無いので OS32_ERR_IO にする)。 */
static int write_all(int fd, const u8 *buf, int len)
{
    int done = 0;

    while (done < len) {
        int wr = api->sys_write(fd, buf + done, (u32)(len - done));
        if (wr < 0) return wr;                  /* 番号を保つ */
        if (wr == 0) return OS32_ERR_IO;        /* 0 進捗も失敗にする */
        if (wr > len - done) return OS32_ERR_IO;/* 契約違反 */
        done += wr;
    }
    return done;
}

static int buf_equal(const u8 *a, const u8 *b, int n)
{
    int i;
    for (i = 0; i < n; i++) if (a[i] != b[i]) return 0;
    return 1;
}

/* ======== 同一判定: サイズ + 内容のバイト比較 ======== */

/* 戻り値 0 = 全内容一致 / 1 = 不一致 / -1 = I/O 失敗・予定サイズに届かない EOF
 *
 * **エラーを「同一」扱いにしない** (設計書 §3.1)。読めなかった部分を
 * 「同じだった」と見なすと、まさに今回の「更新されない」が戻る。
 * CRC ではなくバイト比較なのは、どちらにせよ両方を読む必要があるなら
 * 衝突が無く最初の相違で打ち切れるから (設計書 §1)。 */
static int compare_files(const char *pa, const char *pb, u32 size)
{
    int fa, fb;
    int result = 0;
    u32 remaining = size;

    /* 観測窓 (票 H4 受入 M10)。名札の CRC を信じて比較を省いていないことを、
     * 文言ではなく**この数**で確かめられるようにしておく。 */
    g_content_compares++;

    fa = api->sys_open(pa, KAPI_O_RDONLY);
    if (fa < 0) return -1;
    fb = api->sys_open(pb, KAPI_O_RDONLY);
    if (fb < 0) { api->sys_close(fa); return -1; }

    while (remaining > 0) {
        int want = (remaining > (u32)CMP_BUF_SIZE)
                       ? CMP_BUF_SIZE : (int)remaining;
        int ga, gb;

        ga = read_fill(fa, cmp_a, want);
        if (ga < 0) { result = -1; break; }
        gb = read_fill(fb, cmp_b, want);
        if (gb < 0) { result = -1; break; }
        if (ga != want || gb != want) { result = -1; break; }  /* 早期 EOF */
        if (!buf_equal(cmp_a, cmp_b, want)) { result = 1; break; }
        remaining -= (u32)want;
    }

    if (result == 0) {
        /* 予定より長い = stat のあとで変わった。「同一」とは言えないので
         * コピー側へ回す (握りつぶして unchanged にしない)。 */
        int ea = read_fill(fa, cmp_a, 1);
        int eb = read_fill(fb, cmp_b, 1);
        if (ea < 0 || eb < 0) result = -1;
        else if (ea != 0 || eb != 0) result = 1;
    }

    api->sys_close(fa);
    api->sys_close(fb);
    return result;
}

/* ======== コピー + 読戻し検証 ======== */

/* コピー元を読みながら CRC-32 と総バイト数を作り、開いてある fd へ書く。
 * 戻り値 0 = 成功 / -1 = 失敗 (*reason に固定文字列)。
 * 空き不足は `no_space` と呼び分ける (票 H2 §2-3 手順 3)。 */
static int copy_body(const char *src, int fd, u32 *out_crc, u32 *out_total,
                     const char **reason)
{
    int fs;
    u32 crc = CRC32_INIT;
    u32 total = 0;
    int failed = 0;

    *reason = HR_IO;
    fs = api->sys_open(src, KAPI_O_RDONLY);
    if (fs < 0) return -1;

    while (1) {
        int got = read_fill(fs, file_buf, FILE_BUF_SIZE);
        int wr;
        if (got < 0) { failed = 1; break; }
        if (got == 0) break;                         /* EOF */
        wr = write_all(fd, file_buf, got);
        if (wr != got) {
            if (wr == OS32_ERR_NOSPC || wr == OS32_ERR_FULL)
                *reason = HR_NO_SPACE;
            failed = 1;
            break;
        }
        crc = crc32_core_update(crc, file_buf, (u32)got);
        total += (u32)got;
        if (got < FILE_BUF_SIZE) break;              /* read_fill は EOF でのみ短い */
    }

    api->sys_close(fs);
    if (failed) return -1;
    *out_crc = crc;
    *out_total = total;
    return 0;
}

/* 書いたファイルを開き直して長さと CRC を照合する (設計書 §4.2)。
 * キャッシュを経由するので媒体からの物理再読の保証ではない
 * (最終受入は再起動後の内容で見る)。戻り値 0 = 一致 / -1 = 不一致・失敗。 */
static int verify_readback(const char *path, u32 crc, u32 total)
{
    int fd;
    u32 crc2 = CRC32_INIT;
    u32 total2 = 0;
    int failed = 0;

    fd = api->sys_open(path, KAPI_O_RDONLY);
    if (fd < 0) return -1;
    while (1) {
        int got = read_fill(fd, file_buf, FILE_BUF_SIZE);
        if (got < 0) { failed = 1; break; }
        if (got == 0) break;
        crc2 = crc32_core_update(crc2, file_buf, (u32)got);
        total2 += (u32)got;
        if (total2 > total) break;                   /* 余分 = 不一致 */
        if (got < FILE_BUF_SIZE) break;
    }
    api->sys_close(fd);
    if (failed) return -1;

    /* 最後の XOR は 1 ストリームにつき 1 回だけ (チャンクごとに畳まない) */
    if (total2 != total || crc32_core_final(crc2) != crc32_core_final(crc))
        return -1;
    return 0;
}

/* ---- 直接上書き (**古いカーネル向けの退避経路だけ**) -------------------
 *
 * 宛先を O_TRUNC で直接開く。ここで落ちたとき**旧宛先を復元する保証は無い**。
 * 票 H2 の既定は下の replace_file (一時ファイル + 検証 + 置換) で、この関数へ
 * 来るのは KAPI v53 未満のカーネル上で `--unsafe-overwrite` を明示したときだけ。
 *
 * 戻り値 0 = 成功 / -1 = 失敗 (*reason に固定文字列)。 */
static int copy_verify(const char *src, const char *dst, u32 expect,
                       const char **reason)
{
    int fd;
    u32 crc = 0;
    u32 total = 0;

    *reason = HR_IO;
    fd = api->sys_open(dst, KAPI_O_WRONLY | KAPI_O_CREAT | KAPI_O_TRUNC);
    if (fd < 0) return -1;
    if (copy_body(src, fd, &crc, &total, reason) != 0) {
        api->sys_close(fd);
        return -1;
    }
    api->sys_close(fd);

    if (total != expect) { *reason = HR_SOURCE; return -1; }

    /* 書いたものをディスクへ出す。ここが落ちたら「届いていない」 */
    if (api->vfs_sync() != 0) { *reason = HR_VERIFY; return -1; }
    if (verify_readback(dst, crc, total) != 0) { *reason = HR_VERIFY; return -1; }
    return 0;
}

/* ======== 保護対象の判定 ======== */

/* 現に存在する /etc/settings.db* の実体。同期を始める前に 1 度だけ集める
 * (ファイルごとに何度も stat すると 16MHz の実機では効く)。
 *
 * 表の小文字名を決め打ちで stat するだけでは足りない: ext2 は大文字小文字を
 * 区別するので `/etc/SETTINGS.DB` が本体でも拾えず、そこへの hardlink を
 * `hsync -f bin` が上書きしてしまう (往復 1 の B5)。/etc を sys_ls で列挙し、
 * 大文字小文字を無視して一致する**実在名**を全部 stat する。
 * コールバックの中では FS に触らない (private バッファに写すだけ、§4-26)。
 *
 * 上限は**表の名前数 + 大文字小文字違いの別名の余裕**。越えたら守れないので
 * 同期を拒否する (往復 2 の 3) = 上限が表より詰まっていると、リカバリ途中の
 * /etc (票 TASK_S3 §1b の 9 名 + wal/shm = 11 名) で通常同期が止まる。
 * 表が 5 名 → 11 名になったので (S3-D)、余裕を同じだけ保つよう 16 → 24。
 * `hsp_protected_names` に名前を足すときはここも見直すこと。 */
#define HS_MAX_PROT 24
static u32 g_prot_dev[HS_MAX_PROT];
static u32 g_prot_ino[HS_MAX_PROT];
static int g_prot_count;
static char g_prot_name[HS_MAX_PROT][64];
static int g_prot_name_count;
/* 一覧が HS_MAX_PROT を越えた。守れないので同期を拒否する (往復 2 の 3) */
static int g_prot_overflow;
/* 判定できない stat 失敗を踏んだ。同期を中止する印 */
static int g_abort;

static void prot_scan_cb(const DirEntry_Ext *entry, void *ctx)
{
    int i;

    (void)ctx;
    if (!hsp_is_protected_basename(entry->name)) return;
    if (g_prot_name_count >= HS_MAX_PROT) {
        /* 黙って捨てると 17 件目の実体を hardlink 経由で上書きできてしまう */
        g_prot_overflow++;
        return;
    }
    i = 0;
    while (entry->name[i] && i < 63) {
        g_prot_name[g_prot_name_count][i] = entry->name[i];
        i++;
    }
    g_prot_name[g_prot_name_count][i] = '\0';
    g_prot_name_count++;
}

/* 0 = ok / -1 = 判定できないので同期を中止 */
static int scan_protected_entities(void)
{
    OS32_Stat st;
    char buf[OS32_MAX_PATH];
    int i;
    int rc;
    char etc[OS32_MAX_PATH];

    g_prot_count = 0;
    g_prot_name_count = 0;
    g_prot_overflow = 0;

    /* 守るのは**同期先の根**の /etc (--root /hd0 なら /hd0/etc)。書くのは
     * 根の下だけで、hardlink はデバイスをまたがないので、ほかの /etc は
     * 実体規則に要らない。 */
    if (!str_ncpy(etc, g_root, (int)sizeof(etc)) ||
        !str_ncat(etc, "/etc", (int)sizeof(etc))) {
        api->kprintf(ATTR_RED, "Error: path too long (%s/etc)。中止する\n", g_root);
        return -1;
    }
    rc = api->sys_ls(etc, prot_scan_cb, 0);
    if (rc != 0 && rc != OS32_ERR_NOTFOUND) {
        api->kprintf(ATTR_RED, "Error: %s を読めない (%d)。中止する\n", etc, rc);
        return -1;
    }
    if (g_prot_overflow) {
        api->kprintf(ATTR_RED,
                     "Error: /etc の保護対象が %d 件を越えた (+%d)。中止する\n",
                     HS_MAX_PROT, g_prot_overflow);
        return -1;
    }

    for (i = 0; i < g_prot_name_count; i++) {
        if (!str_ncpy(buf, etc, (int)sizeof(buf)) ||
            !str_ncat(buf, "/", (int)sizeof(buf)) ||
            !str_ncat(buf, g_prot_name[i], (int)sizeof(buf))) {
            api->kprintf(ATTR_RED, "Error: path too long (%s/%s)。中止する\n",
                         etc, g_prot_name[i]);
            return -1;
        }
        rc = api->sys_stat(buf, &st);
        if (rc == 0) {
            g_prot_dev[g_prot_count] = st.st_dev;
            g_prot_ino[g_prot_count] = st.st_ino;
            g_prot_count++;
        } else if (rc != OS32_ERR_NOTFOUND) {
            /* 読めない = 守れない。書いてから気づくより中止する。 */
            api->kprintf(ATTR_RED, "Error: stat %s 失敗 (%d)。中止する\n",
                         buf, rc);
            return -1;
        }
    }
    return 0;
}

/* 実体規則: dst_path が現に /etc/settings.db* のどれかと同じ実体 (NHD 上の
 * hardlink) なら真。名前規則 (hsp_path_protected) をすり抜ける別名を塞ぐ。
 * 宛先の stat が「不存在」以外で失べば g_abort を立てる (往復 1 の B5) —
 * 読めないまま open すると O_TRUNC で切り詰めてしまう。 */
static int is_same_as_protected(const char *dst_path)
{
    OS32_Stat here;
    int i;
    int rc;

    if (g_prot_count == 0) return 0;                  /* 守る実体が無い */
    rc = api->sys_stat(dst_path, &here);
    if (rc != 0) {
        if (rc != OS32_ERR_NOTFOUND) {
            api->kprintf(ATTR_RED, "Error: stat %s 失敗 (%d)。中止する\n",
                         dst_path, rc);
            g_abort = 1;
        }
        return 0;
    }
    for (i = 0; i < g_prot_count; i++) {
        if (here.st_dev == g_prot_dev[i] && here.st_ino == g_prot_ino[i])
            return 1;
    }
    return 0;
}

/* コピー / mkdir の**直前**に通す 1 か所の判定。
 *    1 = 保護対象 (書かない。失敗ではない)
 *    0 = 対象外 (進んでよい)
 *   -1 = 判定できない (正規化に失敗。書かないが**エラーとして数える**)
 *
 * 判定できないものを「保護」に畳むと、`PROTECTED` が「守った」と「読めなかった」の
 * 両方を指すことになり、何も同期していないのに errors=0 で終わる
 * (Codex 実装レビュー B2)。語を分けるためにここで 3 値にする。 */
static int dst_protected(const char *dst_path)
{
    /* 名前規則は**根からの相対**で見る (--root /hd0 の /hd0/etc/settings.db も
     * /etc/settings.db として守る)。実体規則は完全パスで stat する。 */
    int cls = hsp_path_classify(hs_rel(dst_path));

    if (cls != 0) return cls;                  /* 1 = 保護 / -1 = 判定不能 */
    return is_same_as_protected(dst_path);
}

/* ======== 1 ファイルの判定と処理 (設計書 §3.1 の判定順) ======== */

static void note_target(const char *dst)
{
    const char *rel = hs_rel(dst);
    if (str_has_prefix(rel, "/sys/"))  g_touched_sys = 1;
    if (str_has_prefix(rel, "/boot/")) g_touched_boot = 1;
}

/* 票 TASK_VFS_FD_PATH で増えたエラーの名前 (番号だけでは読めないもの) */
static const char *err_tag(int err)
{
    switch (err) {
    case OS32_ERR_STALE:       return " (STALE)";
    case OS32_ERR_NAMETOOLONG: return " (NAMETOOLONG)";
    case OS32_ERR_BUSY:        return " (BUSY)";
    default:                   return "";
    }
}

static void fail_file(const char *dst, const char *reason, int err)
{
    api->kprintf(ATTR_RED, "  FAIL %s reason=%s err=%d%s\n", dst, reason, err,
                 err_tag(err));
    g_errors++;
}

/* ======== 一時ファイル方式の道具 (票 H2 §2-3 / §2-4) ======== */

/* 定義は下の「コピー元 mtime を宛先へ」節。置き換えは**一時ファイルへ**
 * 設定してから rename するので、置換本体より前に名前だけ要る。 */
static int apply_mtime(const char *target, const char *label, u32 src_mtime);

/* 宛先と同じディレクトリの予約名 `.hs~<名前>` を組み立てる。
 * 戻り値 1 = 組めた / 0 = 名前が長すぎる。
 *
 * 長さの上限は `EXT2_NAME_LEN` ではなく **hsync 自身の列挙幅 `NAME_CAP`**
 * (票 H2 §2-3 手順 1 / Codex 往復 2 所見 7)。一時名が NAME_CAP に収まらないと
 * §2-4 の掃除の列挙で拾えなくなり、消せない予約名が残り続ける。
 * **直接上書きへは落とさない** — 落とすと H2 の保証がその 1 件だけ消える。 */
static int build_temp_path(const char *dst_path, char *out, int cap)
{
    int i, last = -1;
    const char *base;

    for (i = 0; dst_path[i]; i++) if (dst_path[i] == '/') last = i;
    base = (last >= 0) ? dst_path + last + 1 : dst_path;

    if (str_len(base) + HS_TEMP_PREFIX_LEN >= NAME_CAP) return 0;

    out[0] = '\0';
    if (last >= 0) {
        if (last + 2 > cap) return 0;
        for (i = 0; i <= last; i++) out[i] = dst_path[i];
        out[last + 1] = '\0';
    }
    if (!str_ncat(out, HS_TEMP_PREFIX, cap)) return 0;
    if (!str_ncat(out, base, cap)) return 0;
    return 1;
}

/* この実行が作った一時ファイルを片づける。
 * 消せなければ `STALE` を表示する (票 H2 §2-3 末尾 / A14c)。
 * メタデータの失敗でマウントが書き込み禁止 (ROFS) に落ちた後は unlink も
 * 通らないので、「必ず消える」とは言わない — 次の実行が予約名として消す。
 *
 * **errors に数えるかは呼び手が決める** — 1 つの失敗は 1 と数えるため:
 *   - 手順 7 (一時ファイルへの mtime 設定が落ちた) は**数えない**。ext2 は
 *     メタデータの I/O 失敗でマウントを ROFS に落とすので、続く unlink の
 *     失敗は同じ 1 つの失敗の続きであって別件ではない。apply_mtime が
 *     既に 1 件数えている。
 *   - 公開の前の失敗と手順 8 の rename 失敗では**数える**。そちらは unlink が
 *     落ちる理由が元の失敗と独立に在り得る (A14c は実際に別々の注入)。
 *
 * 戻り値 0 = 消えた / もう無い、-1 = 残った (STALE を表示済み)。 */
static int drop_temp(const char *tmp)
{
    OS32_Stat st;
    int rc = api->sys_stat(tmp, &st);

    if (rc == OS32_ERR_NOTFOUND) return 0;    /* もう無い (置換で本名になった等) */
    if (rc == 0) {
        rc = api->sys_unlink(tmp);
        if (rc == 0) return 0;
    }
    api->kprintf(ATTR_RED,
                 "  STALE %s (一時ファイルを消せない err=%d。"
                 "次の実行が予約名として片づける)\n", tmp, rc);
    return -1;
}

/* **公開の前**の失敗。旧宛先は名前も内容もそのまま残っている。 */
static void fail_before_publish(const char *dst, const char *tmp,
                                const char *reason, int err)
{
    api->kprintf(ATTR_RED,
                 "  FAIL %s reason=%s err=%d (公開の前なので旧宛先はそのまま)\n",
                 dst, reason, err);
    g_errors++;
    if (drop_temp(tmp) != 0) g_errors++;   /* 後始末の失敗は独立した 1 件 */
}

/* 手順 2 で予約名が既に在ったとき。**`st_nlink` は見ない** (途中で止まった
 * 媒体では 2 になり得る、票 H2 §2-2-4)。通常ファイルで、保護対象の実体でない
 * ものだけ消す。戻り値 0 = 消した / -1 = 消さなかった。 */
static int remove_stale_temp(const char *tmp)
{
    OS32_Stat st;

    if (api->sys_stat(tmp, &st) != 0) return -1;
    if ((st.st_mode & OS_S_IFMT) != OS_S_IFREG) return -1;  /* ディレクトリ等 */
    if (dst_protected(tmp) != 0) return -1;                 /* 保護 > 予約 (R5) */
    return api->sys_unlink(tmp) == 0 ? 0 : -1;
}

/* ---- 置き換え本体 (票 H2 §2-3 の手順 1〜9) -----------------------------
 *
 * 戻り値 0 = 置き換えた (呼び手が copied に数える) /
 *       -1 = 失敗 (表示と errors はこの中で済ませてある)。
 *
 * `*published` は**媒体の上で宛先が新しい内容に入れ替わったか**を返す。
 * 手順 8 の rename が通った時点で 1 になり、手順 9 の `vfs_sync` が落ちて
 * -1 を返すときも 1 のまま残る。rename が非ゼロを返した回でも、宛先の
 * `st_ino` が一時ファイルのものと一致する (= `replace_partial`) なら 1。
 * `replace_failed` と `replace_unknown` では 0 のまま — 前者は旧内容のまま、
 * 後者は公開したか分からないので、案内を出す根拠がない。失敗なのに置換は済んでいる場面があるので、
 * 呼び手はこれを見て**再起動の案内だけは出す** — 置換が媒体に載っているのに
 * 「/sys を更新した -> シェル再起動が必要」が消えるのは誤報になる。
 * copied に数えないのは今までどおり (errors にも入っている)。 */
static int replace_file(const char *src_path, const char *dst_path, u32 size,
                        const OS32_Stat *ss0, const OS32_Stat *ds0,
                        int dst_exists, int *published)
{
    char tmp[OS32_MAX_PATH];
    OS32_Stat ss1, ds1, ts;
    const char *reason = HR_IO;
    u32 crc = 0, total = 0;
    u32 tmp_ino = 0, old_ino = 0;
    int fd, rc, mrc, prot;

    *published = 0;

    /* 手順 1: 一時名 */
    if (!build_temp_path(dst_path, tmp, (int)sizeof(tmp))) {
        fail_file(dst_path, HR_NAME_TOO_LONG, 0);
        return -1;
    }

    /* 手順 2: 排他的作成。EXIST は §2-4 の決裁どおり 1 回だけ作り直す */
    fd = api->sys_open(tmp, KAPI_O_WRONLY | KAPI_O_CREAT | KAPI_O_EXCL);
    if (fd == OS32_ERR_EXIST) {
        if (remove_stale_temp(tmp) != 0) {
            fail_file(dst_path, HR_TEMP_EXISTS, 0);
            return -1;
        }
        fd = api->sys_open(tmp, KAPI_O_WRONLY | KAPI_O_CREAT | KAPI_O_EXCL);
    }
    if (fd < 0) {
        /* **NOSYS で直接上書きへ黙って落ちない** (票 H2 §2-3 手順 2)。
         * 宛先 FS が排他的作成を持たないなら、この票の保証は出せない。 */
        if (fd == OS32_ERR_NOSYS)
            fail_file(dst_path, HR_REPLACE_UNSUPPORTED, fd);
        else if (fd == OS32_ERR_EXIST)
            fail_file(dst_path, HR_TEMP_EXISTS, fd);
        else if (fd == OS32_ERR_NOSPC || fd == OS32_ERR_FULL)
            fail_file(dst_path, HR_NO_SPACE, fd);
        else
            fail_file(dst_path, HR_IO, fd);
        return -1;
    }

    /* 手順 3: 書き込み + CRC */
    rc = copy_body(src_path, fd, &crc, &total, &reason);
    api->sys_close(fd);
    if (rc != 0) { fail_before_publish(dst_path, tmp, reason, 0); return -1; }
    if (total != size) {
        fail_before_publish(dst_path, tmp, HR_SOURCE, 0);
        return -1;
    }

    /* 手順 4: 同期 → 読戻し検証 */
    if (api->vfs_sync() != 0) {
        fail_before_publish(dst_path, tmp, HR_VERIFY, 0);
        return -1;
    }
    if (verify_readback(tmp, crc, total) != 0) {
        fail_before_publish(dst_path, tmp, HR_VERIFY, 0);
        return -1;
    }

    /* 手順 5: 再確認 (コピー元・宛先・保護判定) と ino の控え */
    rc = api->sys_stat(src_path, &ss1);
    if (rc != 0) { fail_before_publish(dst_path, tmp, HR_IO, rc); return -1; }
    if (ss1.st_size != ss0->st_size || ss1.st_mtime != ss0->st_mtime) {
        fail_before_publish(dst_path, tmp, HR_SOURCE, 0);
        return -1;
    }

    rc = api->sys_stat(dst_path, &ds1);
    if (dst_exists) {
        if (rc != 0) {
            fail_before_publish(dst_path, tmp, HR_DEST_CHANGED, rc);
            return -1;
        }
        if ((ds1.st_mode & OS_S_IFMT) != OS_S_IFREG ||
            ds1.st_size != ds0->st_size || ds1.st_mtime != ds0->st_mtime) {
            fail_before_publish(dst_path, tmp, HR_DEST_CHANGED, 0);
            return -1;
        }
        old_ino = ds1.st_ino;
    } else if (rc != OS32_ERR_NOTFOUND) {
        /* 判定時に無かったものが在る / 読めない。**置き換えない** */
        fail_before_publish(dst_path, tmp, HR_DEST_CHANGED, rc);
        return -1;
    }

    /* **保護対象の判定もここで取り直す** (票 H2 §2-3 手順 5 / A17b) */
    prot = dst_protected(dst_path);
    if (prot < 0) {
        fail_before_publish(dst_path, tmp, HR_PATH_REJECT, 0);
        return -1;
    }
    if (prot > 0) {
        fail_before_publish(dst_path, tmp, HR_SETTINGS_DB, 0);
        return -1;
    }
    /* `is_same_as_protected` は「不存在以外の stat 失敗」で g_abort を立てて
     * **0 を返す**。守れないと分かった状態で置き換えへ進まない (往復 1 の B5)。 */
    if (g_abort) {
        fail_before_publish(dst_path, tmp, HR_IO, 0);
        return -1;
    }

    rc = api->sys_stat(tmp, &ts);
    if (rc != 0) { fail_before_publish(dst_path, tmp, HR_IO, rc); return -1; }
    tmp_ino = ts.st_ino;

    /* 手順 6: hardlink (呼び手が一時ファイルを作る前にも見ている) */
    if (dst_exists && ds1.st_nlink > 1) {
        fail_before_publish(dst_path, tmp, HR_HARDLINK, (int)ds1.st_nlink);
        return -1;
    }

    /* 手順 7: mtime は**一時ファイルへ**。ここでの失敗は「公開の前の失敗」
     * なので中断し、**copied に数えない** (Codex 往復 1 所見 4: ext2 は
     * メタデータの I/O 失敗でマウントを書き込み禁止にするので、続く rename は
     * 必ず ROFS になり、宛先は旧内容のまま)。 */
    mrc = apply_mtime(tmp, dst_path, ss1.st_mtime);
    if (mrc < 0) {
        /* **数え直さない**。ext2 は mtime の I/O 失敗でマウントを ROFS に
         * 落とすので、ここで unlink が落ちるのは同じ 1 つの失敗の続きで、
         * apply_mtime が既に 1 件数えている (1 failure = 1 error)。
         * 表示 (metadata_failed と STALE) は両方出す。 */
        (void)drop_temp(tmp);
        return -1;
    }

    /* 手順 8: 置換 */
    rc = api->sys_rename(tmp, dst_path);
    if (rc != 0) {
        /* **公開の有無は宛先の st_ino で決める** (票 H2 §2-3 手順 8)。
         * サイズと CRC は証拠にしない — `-f` で新旧が同じ内容のときに
         * 未公開を公開と誤るし、CRC 不一致は「旧内容のまま」の証明にもならない。 */
        const char *why = HR_REPLACE_UNKNOWN;
        const char *note = " (公開したか判定できない)";
        OS32_Stat now;
        int srr = api->sys_stat(dst_path, &now);
        int unpublished = 0;    /* 1 = 旧内容のまま (why = replace_failed) */

        if (srr == 0) {
            if (now.st_ino == tmp_ino) {
                why = HR_REPLACE_PARTIAL;
                note = " (公開済み: 宛先は検証済みの新しい内容。後始末が落ちた)";
                /* **公開済みなので案内は出す** (PM 決裁 2026-09-16、手順 9 と
                 * 同じ理屈)。媒体の上で内容は入れ替わっているのに
                 * 「/sys を更新した -> シェル再起動が必要」が消えるのは誤報。
                 * `replace_failed` (旧内容のまま) と `replace_unknown`
                 * (どちらか分からない) では**立てない** — 案内を出す根拠がない。 */
                *published = 1;
            } else if (dst_exists && now.st_ino == old_ino) {
                why = HR_REPLACE_FAILED;
                note = " (未公開: 宛先は旧内容のまま)";
                unpublished = 1;
            }
        } else if (!dst_exists && srr == OS32_ERR_NOTFOUND) {
            why = HR_REPLACE_FAILED;
            note = " (未公開: 宛先は作られていない)";
            unpublished = 1;
        }
        /* BUSY = VFS が**何もせずに**断った: 開いている SQLite DB (FEP を
         * 有効にした後の /db/fep.db など) とそのジャーナル・祖先は、接続が
         * 閉じるまで付け替えさせない (票 TASK_VFS_FD_PATH のユーザー決裁 ①)。
         * 設計どおりの拒否なので、何が起きたかと次の手を言う。 */
        if (rc == OS32_ERR_BUSY && unpublished) {
            note = dst_exists
                ? " (使用中で置き換えられなかった: 宛先は旧内容のまま。"
                  "開いている DB (FEP 辞書など) は閉じるまで置き換えない。"
                  "再起動して FEP を有効にする前に hsync する)"
                : " (使用中で作れなかった: 開いている DB のジャーナル名か"
                  "その祖先。DB を閉じてから hsync する)";
        }
        api->kprintf(ATTR_RED, "  FAIL %s reason=%s err=%d%s%s\n",
                     dst_path, why, rc, err_tag(rc), note);
        g_errors++;
        if (drop_temp(tmp) != 0) g_errors++; /* 後始末の失敗は独立した 1 件 */
        return -1;
    }

    /* ここから先、宛先の名前は**検証済みの新しい実体**を指している。
     * 以降の失敗で旧内容へ戻そうとはしない (票 H2 の規則 2)。 */
    *published = 1;

    /* 手順 9: 同期 */
    if (api->vfs_sync() != 0) {
        api->kprintf(ATTR_RED,
                     "  FAIL %s reason=%s (置換は済んでいる可能性がある)\n",
                     dst_path, HR_SYNC_FAILED);
        g_errors++;
        return -1;
    }
    return 0;
}

/* ======== コピー元 mtime を宛先へ (票 H3 / 設計書 §5.2) ======== */

/* **データを書き終えてから**呼ぶこと。通常の書き込みは mtime を
 * ゲストの現在時刻で上書きするので、先に設定すると消える。
 * 宛先の ctime はゲスト側の変更時刻でよい (カーネルがそうする)。
 *
 *   戻り値  1 … 設定した
 *           0 … 設定しなかったが**失敗ではない**
 *                (src_mtime が 0 = 不明 / 宛先 FS が持っていない = NOSYS)
 *          -1 … 有効な時刻の保存を試みて失敗した
 *                (呼び手が metadata_failed + errors に数える)
 *
 * dry-run では 1 バイトも書かないので呼ばない (呼び手側で分岐する)。
 *
 * 票 H2: 置き換え経路では**一時ファイル**に設定するので、設定先 (target) と
 * 表示する名前 (label) を分ける。rename は inode の mtime を変えないので、
 * 本名に現れた時点で日時が揃っている。 */
static int apply_mtime(const char *target, const char *label, u32 src_mtime)
{
    int rc;

    if (src_mtime == 0) {
        /* 証拠が無いものを書かない。**省略したことは必ず見せる** */
        g_mtime_unknown++;
        if (g_verbose)
            api->kprintf(ATTR_YELLOW, "  NOTIME %s reason=%s\n",
                         label, HR_MTIME_UNKNOWN);
        return 0;
    }

    rc = api->sys_set_mtime(target, src_mtime);
    if (rc == 0) return 1;

    if (rc == OS32_ERR_NOSYS) {
        /* 「できなかった」ではなく「この FS には無い」。内容の同期は続ける */
        g_mtime_nosys++;
        if (g_verbose)
            api->kprintf(ATTR_YELLOW, "  NOTIME %s reason=%s\n",
                         label, HR_MTIME_NOSYS);
        return 0;
    }

    /* 有効な時刻を書こうとして落ちた。内容コピーの成功だけで
     * 全成功と表示しない (設計書 §5.2)。 */
    fail_file(label, HR_META_FAILED, rc);
    return -1;
}

/* ======== /boot/vmkernel.old (票 TASK_SERIAL_HOSTFS §1-v3) ======== */

#define HS_KERNEL_PATH  "/boot/vmkernel.lz4"
#define HS_KERNEL_OLD   "/boot/vmkernel.old"

/* path の VK32 の CRC (image_crc 欄を 0 として) と、欄に書いてある値。0 / -1 */
static int kernel_disk_crc(const char *path, u32 *out, u32 *stored)
{
    int fd, got, first = 1;
    u32 state = CRC32_INIT, pos = 0, off = 0, k;

    *stored = 0;
    fd = api->sys_open(path, KAPI_O_RDONLY);
    if (fd < 0) return -1;
    for (;;) {
        got = read_fill(fd, file_buf, FILE_BUF_SIZE);
        if (got < 0) { api->sys_close(fd); return -1; }
        if (first) {
            if (hbo_crc_offset(file_buf, (u32)got, &off) != 0) {
                api->sys_close(fd);
                return -1;
            }
            first = 0;
        }
        if (got == 0) break;
        /* 欄に書いてある値を拾う (欄がチャンクをまたいでも 1 バイトずつ) */
        for (k = 0; k < 4; k++) {
            if (off + k >= pos && off + k < pos + (u32)got)
                *stored |= (u32)file_buf[off + k - pos] << (8 * k);
        }
        state = hbo_crc_update(state, file_buf, (u32)got, pos, off);
        pos += (u32)got;
        if (got < FILE_BUF_SIZE) break;
    }
    api->sys_close(fd);
    *out = crc32_core_final(state);
    return 0;
}

/* 置き換える前の門。0 = 進んでよい / -1 = 断った (表示と errors は済み)。
 * **断るのは置き換えの前** — 旧宛先はそのまま残る。 */
static int kernel_backup(const char *dst_path, const OS32_Stat *ds)
{
    BootImageInfo bi;
    char tmp[OS32_MAX_PATH];
    char old[OS32_MAX_PATH];          /* <根>/boot/vmkernel.old */
    const char *reason = HR_IO;
    u32 disk_crc = 0, stored = 0, crc = 0, total = 0;
    int have = 0, disk_ok, dec, fd, rc;

    /* .old は置き換える vmkernel と**同じ根**に作る (--root /hd0 なら
     * /hd0/boot/vmkernel.old)。起動した版との照合 (boot_image_info) は
     * 走っているカーネルの記録なので、FD で起動して --root /hd0 を更新する
     * ときは HDD の vmkernel と一致せず断る (not_booted_image) — 下の案内。 */
    if (!str_ncpy(old, g_root, (int)sizeof(old)) ||
        !str_ncat(old, HS_KERNEL_OLD, (int)sizeof(old))) {
        /* 組めなかった名前も根付きで出す (--root /hd0 なら /hd0/boot/vmkernel.old) */
        api->kprintf(ATTR_RED, "  FAIL %s%s reason=%s err=0\n", g_root,
                     HS_KERNEL_OLD, HR_NAME_TOO_LONG);
        g_errors++;
        return -1;
    }

    if (!g_no_backup && api->version >= 65 &&
        api->boot_image_info(&bi) == 0 && bi.crc_valid)
        have = 1;
    disk_ok = (!g_no_backup && have) ?
              (kernel_disk_crc(dst_path, &disk_crc, &stored) == 0) : 0;
    dec = hbo_decide(g_no_backup, have, have ? bi.image_crc : 0,
                     disk_ok, disk_crc, stored);

    if (dec == HBO_NO_BACKUP) {
        api->kprintf(ATTR_YELLOW, "  NOTE %s: --no-backup なので %s は作らない\n",
                     dst_path, old);
        return 0;
    }
    if (dec != HBO_MAKE) {
        const char *why = (dec == HBO_REFUSE_INFO) ? HR_OLD_NO_INFO :
                          (dec == HBO_REFUSE_DISK) ? HR_OLD_DISK :
                          (dec == HBO_REFUSE_CORRUPT) ? HR_OLD_CORRUPT : HR_OLD_DIFF;
        api->kprintf(ATTR_RED,
                     "  FAIL %s reason=%s (booted crc=%08X disk crc=%08X%s)\n"
                     "       起動した版と確かめられないので %s を作れず、置き換えない。\n"
                     "       (FD 起動・前回の更新の後に未起動など。承知の上なら --no-backup)\n",
                     dst_path, why, have ? bi.image_crc : 0, disk_crc,
                     have ? "" : ", no boot record", old);
        if (g_root_len > 0)
            api->kprintf(ATTR_RED,
                         "       --root %s: 起動したのは別の媒体なので、%s の今の版が\n"
                         "       起動した版かは確かめられない。旧版を残さずに進むなら --no-backup\n",
                         g_root, dst_path);
        g_errors++;
        return -1;
    }
    if (g_dry_run) {
        api->kprintf(ATTR_CYAN, "  PLAN %s reason=backup (booted crc=%08X)\n",
                     old, disk_crc);
        return 0;
    }

    /* 複製 → 検証 → rename。本名 (.old) が存在しない時間を作らない */
    if (!build_temp_path(old, tmp, (int)sizeof(tmp))) {
        fail_file(old, HR_NAME_TOO_LONG, 0);
        return -1;
    }
    fd = api->sys_open(tmp, KAPI_O_WRONLY | KAPI_O_CREAT | KAPI_O_EXCL);
    if (fd == OS32_ERR_EXIST && remove_stale_temp(tmp) == 0)
        fd = api->sys_open(tmp, KAPI_O_WRONLY | KAPI_O_CREAT | KAPI_O_EXCL);
    if (fd < 0) {
        fail_file(old, HR_OLD_FAILED, fd);
        return -1;
    }
    rc = copy_body(dst_path, fd, &crc, &total, &reason);
    api->sys_close(fd);
    if (rc != 0 || total != ds->st_size || api->vfs_sync() != 0 ||
        verify_readback(tmp, crc, total) != 0) {
        fail_before_publish(old, tmp, HR_OLD_FAILED, 0);
        return -1;
    }
    /* **公開する一時ファイル自身を**起動記録と突き合わせる (Codex 2)。
     * 判定から複製までの間に本名が差し替わっていても、起動した版でない
     * ものを .old として公開しない。既存の .old はここまで手付かず。 */
    {
        u32 tcrc = 0, tstored = 0;
        int tok = (kernel_disk_crc(tmp, &tcrc, &tstored) == 0);
        if (hbo_decide(0, 1, bi.image_crc, tok, tcrc, tstored) != HBO_MAKE) {
            fail_before_publish(old, tmp, HR_OLD_FAILED, 0);
            return -1;
        }
    }
    rc = api->sys_rename(tmp, old);
    if (rc != 0 || api->vfs_sync() != 0) {
        fail_before_publish(old, tmp, HR_OLD_FAILED, rc);
        return -1;
    }
    api->kprintf(ATTR_GREEN, "  BACKUP %s -> %s size=%d crc=%08X\n",
                 dst_path, old, (int)total, disk_crc);
    return 0;
}

static void sync_file(const char *src_path, const char *dst_path)
{
    OS32_Stat ss;
    OS32_Stat ds;
    const char *reason = HR_NEW;
    const char *vreason = HR_IO;
    u32 size;
    int rc;
    int cmp;
    int mrc;
    int need = 1;
    int meta_only = 0;       /* 内容は同じで mtime だけ違う (票 H3) */
    int dst_exists = 0;      /* 判定時に宛先が在ったか (票 H2 手順 5 / 8) */
    int published = 0;       /* 置換が媒体に載ったか (票 H2 手順 8 / 9) */

    /* コピー元: 列挙結果を信用せず**直前に取り直す** (設計書 §3.1)。
     * 列挙とコピーの間にホスト側が差し替えているかもしれない。 */
    rc = api->sys_stat(src_path, &ss);
    if (rc != 0) { fail_file(dst_path, HR_IO, rc); return; }
    if ((ss.st_mode & OS_S_IFMT) == 0) {
        /* 種別が取れない。特殊ファイルを通常ファイルとして読まない */
        fail_file(dst_path, HR_TYPE_UNKNOWN, 0);
        return;
    }
    if ((ss.st_mode & OS_S_IFMT) != OS_S_IFREG) {
        fail_file(dst_path, HR_TYPE, 0);
        return;
    }
    if (ss.st_size > HS_MAX_FILE_SIZE) {
        fail_file(dst_path, HR_TOO_LARGE, 0);
        return;
    }
    size = ss.st_size;

    /* 宛先 */
    rc = api->sys_stat(dst_path, &ds);
    if (rc == 0) dst_exists = 1;
    if (rc == OS32_ERR_NOTFOUND) {
        reason = HR_NEW;
    } else if (rc != 0) {
        fail_file(dst_path, HR_IO, rc);
        return;
    } else if ((ds.st_mode & OS_S_IFMT) != OS_S_IFREG) {
        /* ディレクトリ等。勝手に削除・切り詰めしない */
        fail_file(dst_path, HR_TYPE, 0);
        return;
    } else if (g_force) {
        /* 比較だけ省く。保護・型検査・コピー後検証は省略しない */
        reason = HR_FORCED;
    } else if (ds.st_size != size) {
        /* サイズが違う = 内容も必ず違う。読み比べる意味が無い */
        reason = HR_SIZE;
    } else {
        /* ---- ここからが票 H3 §8 の判定順 ----------------------------
         *
         * サイズは同じ。**日時を前置フィルタに使う**が、「証拠が無い」
         * ことを同一の根拠にはしない:
         *
         *   - どちらかの mtime が 0 (不明)        → 内容を比較する
         *   - mtime が違う                        → 内容を比較する
         *   - `--verify`                          → 内容を比較する
         *   - どちらも有効で一致                  → 読まずにスキップ
         *
         * **日時の一致だけでコピーを決めない。**候補に挙がったものは
         * 必ずバイト比較を通し、コピーするかは内容が決める (H1 の A08 の
         * 精神 — CRC が同値を返しても既定のバイト比較が違いを見つける)。 */
        int mtime_known = (ss.st_mtime != 0 && ds.st_mtime != 0);
        int mtime_same  = mtime_known && (ss.st_mtime == ds.st_mtime);

        if (!g_verify && mtime_same) {
            /* サイズも日時も同じ。**1 バイトも読まない** — ここが速さの源 */
            g_unchanged++;
            if (g_verbose)
                api->kprintf(ATTR_WHITE, "  SAME %s size=%d reason=%s\n",
                             dst_path, (int)size, HR_SAME_MTIME);
            return;
        }

        cmp = compare_files(src_path, dst_path, size);
        if (cmp < 0) { fail_file(dst_path, HR_IO, 0); return; }
        if (cmp > 0) {
            reason = HR_CONTENT;
        } else {
            need = 0;
            /* 内容は同じ。日時だけ違うなら**本体を書き直さず mtime だけ
             * 更新する** (設計書 §5.2)。
             *
             * 条件は「宛先の mtime がコピー元と**一致していると言えない**」。
             * コピー元が不明 (0) のときもここに入るが、apply_mtime が
             * 0 を書きに行かず「省略した」と数える — **省略したことを
             * 黙らせない**のが要点 (票 H3)。 */
            meta_only = !(ss.st_mtime != 0 && ss.st_mtime == ds.st_mtime);
        }
    }

    if (!need) {
        if (meta_only) {
            if (g_dry_run) {
                /* dry-run は mtime も書かない (設計書 §3.2) */
                g_metadata_updated++;
                api->kprintf(ATTR_CYAN, "  PLAN %s reason=%s size=%d\n",
                             dst_path, HR_MTIME_ONLY, (int)size);
                return;
            }
            mrc = apply_mtime(dst_path, dst_path, ss.st_mtime);
            if (mrc < 0) return;            /* metadata_failed (errors 済み) */
            if (mrc > 0) {
                /* **内容は同じ** = 稼働中の版とディスクの食い違いは
                 * 生まれない。note_target は呼ばない (再起動の案内を
                 * 出すと「入れ替わった」と読めてしまう)。 */
                api->kprintf(ATTR_GREEN, "  MTIME %s reason=%s size=%d\n",
                             dst_path, HR_MTIME_ONLY, (int)size);
                g_metadata_updated++;
                return;
            }
            /* 設定を省略した (NOSYS / 不明)。内容は同じなので unchanged */
        }
        g_unchanged++;
        if (g_verbose)
            api->kprintf(ATTR_WHITE, "  SAME %s size=%d reason=%s\n",
                         dst_path, (int)size, HR_SAME_CONTENT);
        return;
    }

    /* 票 TASK_SERIAL_HOSTFS: カーネルを置き換える前に、起動した版なら
     * .old に残す。確かめられなければ**置き換えない** (--no-backup で進む)。
     * dry-run では判定だけ出す (書かない)。 */
    if (dst_exists && str_cmp(hs_rel(dst_path), HS_KERNEL_PATH) == 0) {
        if (kernel_backup(dst_path, &ds) != 0) return;
    }

    if (g_dry_run) {
        /* 読んで比べるだけ。mkdir・一時ファイル・明示 sync・mtime はしない */
        g_copied++;
        man_note_copy(dst_path);            /* 票 H4: 予定も数える */
        api->kprintf(ATTR_CYAN, "  PLAN %s reason=%s size=%d\n",
                     dst_path, reason, (int)size);
        return;
    }

    if (g_direct) {
        /* ---- 古いカーネル向けの直接上書き (票 H2 §2-3 末尾) ----
         * `--unsafe-overwrite` を明示したときだけここへ来る。
         * **失敗すると旧内容は残らない。** */
        if (copy_verify(src_path, dst_path, size, &vreason) != 0) {
            api->kprintf(ATTR_RED,
                         "  FAIL %s reason=%s (直接上書きなので旧内容は残らない)\n",
                         dst_path, vreason);
            g_errors++;
            return;
        }
        api->kprintf(ATTR_GREEN, "  UPDATE %s reason=%s size=%d\n",
                     dst_path, reason, (int)size);
        g_copied++;
        g_direct_overwrite++;
        man_note_copy(dst_path);            /* 票 H4 */
        note_target(dst_path);
        /* **データを書き終えてから**コピー元の mtime を宛先へ (設計書 §5.2)。
         * ここで落ちてもコピー自体は成功しているので copied は戻さないが、
         * metadata_failed は errors に入る = 終了コードは非ゼロになる。 */
        (void)apply_mtime(dst_path, dst_path, ss.st_mtime);
        return;
    }

    /* ---- 票 H2 の既定: 一時ファイル + 検証 + 置換 ----
     * 手順 6 の hardlink 判定は**一時ファイルを作る前にも**行う
     * (無駄な書き込みを避ける)。別名まで更新する仕様は持ち込まない。 */
    if (dst_exists && ds.st_nlink > 1) {
        fail_file(dst_path, HR_HARDLINK, (int)ds.st_nlink);
        return;
    }

    if (replace_file(src_path, dst_path, size, &ss, &ds, dst_exists,
                     &published) != 0) {
        /* 表示と errors は replace_file の中で済んでいる。ただし
         * **置換だけは媒体に載っている**場合 (手順 9 の sync_failed と、
         * 手順 8 の replace_partial) は再起動の案内を出す — 出さないと
         * 「/sys は入れ替わっていない」と読める誤報になる。
         * copied には数えない。 */
        if (published) note_target(dst_path);
        return;
    }

    /* mtime は一時ファイルに設定済み (rename は inode の mtime を変えない) */
    api->kprintf(ATTR_GREEN, "  UPDATE %s reason=%s size=%d\n",
                 dst_path, reason, (int)size);
    g_copied++;
    man_note_copy(dst_path);                /* 票 H4 */
    note_target(dst_path);
}

/* ======== 予約名 `.hs~` の掃除 (票 H2 §2-4、決裁 D3 (a')) ======== */

/* 掃除の列挙は**同期の列挙と枠を分ける** (Codex 往復 2 所見 7)。候補は
 * `.hs~` で始まる名前だけなので小さくてよい。越えたら `truncated` と同じ扱いで
 * **「全部は見ていない」と表示する** — 掃除の完了を主張しない。 */
#define MAX_TEMPS 32

typedef struct {
    char names[MAX_TEMPS][NAME_CAP];
    int  count;
    int  dropped;      /* 掃除の枠 MAX_TEMPS を越えて見送った数 */
    int  too_long;     /* NAME_CAP に収まらず拾えなかった数 (枠とは別の理由) */
} TempList;

static void temp_cb(const DirEntry_Ext *entry, void *ctx)
{
    TempList *tl = (TempList *)ctx;
    int i;

    if (!hs_is_temp_name(entry->name)) return;
    /* ディレクトリは消さない (§2-4)。特殊ファイルは下の stat で弾く */
    if (entry->type == OS32_FILE_TYPE_DIR) return;
    /* **枠と長さは別の理由**。列挙幅に入らないだけのものを「掃除の枠を
     * 越えた」と呼ぶと、MAX_TEMPS を広げれば直ると読めてしまう (直らない)。
     * 結論の「全部は見ていない」はどちらも同じなので、そこは変えない。 */
    if (!hsp_name_fits(entry->name, NAME_CAP)) { tl->too_long++; return; }
    if (tl->count >= MAX_TEMPS) { tl->dropped++; return; }

    i = 0;
    while (entry->name[i]) { tl->names[tl->count][i] = entry->name[i]; i++; }
    tl->names[tl->count][i] = '\0';
    tl->count++;
}

/* hsync が**実際に訪れた**ディレクトリの予約名を片づける。
 * 既定除外の /sys へは再帰しないので触らない。保護対象の実体 (hardlink) は
 * **予約より保護を優先して消さない** (R5)。`st_nlink` は見ない。 */
static void clean_temps(const char *dst_dir)
{
    TempList tl;
    char dir[OS32_MAX_PATH];
    char path[OS32_MAX_PATH];
    OS32_Stat st;
    int i, rc;

    if (!str_ncpy(dir, dst_dir[0] ? dst_dir : "/", (int)sizeof(dir))) return;

    tl.count = 0;
    tl.dropped = 0;
    tl.too_long = 0;
    rc = api->sys_ls(dir, temp_cb, &tl);
    if (rc != 0) {
        /* 宛先ディレクトリがまだ無いのは普通 (新規階層)。それ以外は
         * 「掃除できなかった」ことだけ見せる — 同期は続ける。 */
        if (rc != OS32_ERR_NOTFOUND)
            api->kprintf(ATTR_YELLOW,
                         "  NOTE: %s の予約名を列挙できない (err=%d)。"
                         "掃除は行っていない\n", dir, rc);
        return;
    }
    if (tl.dropped)
        api->kprintf(ATTR_YELLOW,
                     "  NOTE: %s の予約名が掃除の枠 %d を越えた (+%d)。"
                     "**全部は見ていない**\n", dir, MAX_TEMPS, tl.dropped);
    if (tl.too_long)
        api->kprintf(ATTR_YELLOW,
                     "  NOTE: %s に %d 文字を越える予約名が %d 件 "
                     "(掃除の枠ではなく名前の長さ)。**全部は見ていない**\n",
                     dir, NAME_CAP - 1, tl.too_long);

    for (i = 0; i < tl.count; i++) {
        if (!str_ncpy(path, dir, (int)sizeof(path)) ||
            ((str_len(path) == 0 || path[str_len(path) - 1] != '/') &&
             !str_ncat(path, "/", (int)sizeof(path))) ||
            !str_ncat(path, tl.names[i], (int)sizeof(path))) {
            api->kprintf(ATTR_RED, "  FAIL: path too long: %s/%s\n",
                         dir, tl.names[i]);
            g_errors++;
            continue;
        }

        {
            int prot = dst_protected(path);
            if (prot < 0) {
                api->kprintf(ATTR_RED, "  FAIL %s reason=%s\n",
                             path, HR_PATH_REJECT);
                g_errors++;
                continue;
            }
            if (prot > 0) {
                /* 予約名だが保護対象の実体を指している。**消さない** (R5) */
                api->kprintf(ATTR_YELLOW, "  PROTECTED %s reason=%s\n",
                             path, HR_PROT_RESERVED);
                g_protected++;
                continue;
            }
        }
        if (g_abort) return;

        rc = api->sys_stat(path, &st);
        if (rc == OS32_ERR_NOTFOUND) continue;
        if (rc != 0) { fail_file(path, HR_IO, rc); continue; }
        /* 通常ファイルだけ。`st_nlink` は見ない (§2-2-4 の復旧表) */
        if ((st.st_mode & OS_S_IFMT) != OS_S_IFREG) continue;

        if (g_dry_run) {
            api->kprintf(ATTR_CYAN, "  PLAN-CLEAN %s\n", path);
            g_cleaned++;
            continue;
        }
        rc = api->sys_unlink(path);
        if (rc != 0) { fail_file(path, HR_IO, rc); continue; }
        api->kprintf(ATTR_GREEN, "  CLEAN %s\n", path);
        g_cleaned++;
    }
}

/* 宛先をディレクトリとして使えるかを確かめる (Codex 実装レビュー B3)。
 *   0 = ディレクトリ、または不存在 (進んでよい)
 *  -1 = 通常ファイル等の型衝突 / stat 不能 (errors に数え済み)
 *
 * fs/ext2_dir.c の ext2_find_entry は**種別を問わず**名前があれば EXIST を
 * 返すので、sys_mkdir の OS32_ERR_EXIST だけでは「同じ名前の通常ファイル」を
 * 見分けられない。無条件に受理して再帰すると、`/host/usr/empty` が
 * ディレクトリ・`/usr/empty` が通常ファイルのまま errors=0 / 終了コード 0 で
 * 終わっていた。通常ファイル側には type_conflict を入れたのに、
 * ディレクトリ側に無かった。 */
static int dst_dir_type_ok(const char *dst_path)
{
    OS32_Stat ds;
    int rc = api->sys_stat(dst_path, &ds);

    if (rc == OS32_ERR_NOTFOUND) return 0;
    if (rc != 0) { fail_file(dst_path, HR_IO, rc); return -1; }
    if ((ds.st_mode & OS_S_IFMT) == OS_S_IFDIR) return 0;
    /* 勝手に消さない。型が食い違ったまま「完了」と言わない。 */
    fail_file(dst_path, HR_TYPE, 0);
    return -1;
}

/* 明示 dir の同期を**始める前**に、起点の型を 1 度だけ確かめる
 * (Codex 実装レビュー 往復 2 の B3 残件)。
 *
 * 列挙ループの中の型検査は**子項目**にしか掛からない。コピー元が空の
 * ディレクトリだと子が 1 つも無いので一度も呼ばれず、`/host/usr/empty` が
 * ディレクトリ・`/usr/empty` が通常ファイルのまま errors=0 / 終了コード 0 で
 * 終わっていた。-n でも -f でも同じ。語と集計は子項目側とそろえる。
 *
 * 戻り値 0 = 始めてよい / -1 = 型衝突等 (errors に数え済み)。 */
static int start_point_ok(const char *src_path, const char *dst_path)
{
    OS32_Stat ss;
    int rc;

    /* コピー元: dir として渡された以上ディレクトリであること。
     * sys_ls 任せにしない — HostDrv の hdrv_list_dir は DIRECTORY_FILE で
     * 開くので通常ファイルなら失敗するが、返るのは VFS_ERR_NOTFOUND で
     * 「存在しない」と区別が付かないし、通常ファイルに空の列挙を成功として
     * 返す FS が 1 つでもあれば同じ穴がそのまま開く。 */
    rc = api->sys_stat(src_path, &ss);
    if (rc != 0) { fail_file(src_path, HR_IO, rc); return -1; }
    if ((ss.st_mode & OS_S_IFMT) == 0) {
        fail_file(src_path, HR_TYPE_UNKNOWN, 0);
        return -1;
    }
    if ((ss.st_mode & OS_S_IFMT) != OS_S_IFDIR) {
        fail_file(src_path, HR_TYPE, 0);
        return -1;
    }

    /* 宛先: 在るならディレクトリであること (子項目と同じ検査) */
    return dst_dir_type_ok(dst_path);
}

/* ======== ディレクトリ再帰同期 ======== */

static void sync_directory(const char *src_dir, const char *dst_dir, int depth)
{
    FileList fl;
    int i;
    int rc;

    if (depth > MAX_DEPTH) {
        /* 打ち切りを黙って成功にしない (未同期のまま「完了」と出ていた) */
        api->kprintf(ATTR_RED, "  FAIL: depth > %d: %s\n", MAX_DEPTH, src_dir);
        g_errors++;
        return;
    }

    fl.src_dir = src_dir;
    fl.count = 0;
    fl.dropped = 0;
    fl.truncated = 0;
    fl.bad_name = 0;
    rc = api->sys_ls(src_dir, ls_cb, &fl);
    if (rc != 0) {
        api->kprintf(ATTR_RED, "  FAIL: ls %s (err=%d%s)\n", src_dir, rc,
                     err_tag(rc));
        g_errors++;
        return;
    }
    if (fl.dropped) {
        api->kprintf(ATTR_RED, "  FAIL: %s のエントリが %d 件を越えた (+%d)\n",
                     src_dir, MAX_FILES, fl.dropped);
        g_errors++;
    }
    if (fl.truncated) {
        api->kprintf(ATTR_RED,
                     "  FAIL: %s に %d 文字を越える名前が %d 件 (コピーしない)\n",
                     src_dir, NAME_CAP - 1, fl.truncated);
        g_errors++;
    }
    if (fl.bad_name) {
        /* '\' を含む名前。OS32 側の '..' 検査を素通りしてホスト側で
         * 同期元の外を指し得る (B1)。組み立てずに数えてエラーにする。 */
        api->kprintf(ATTR_RED,
                     "  FAIL: %s に '\\' を含む名前が %d 件 reason=%s\n",
                     src_dir, fl.bad_name, HR_BAD_NAME);
        g_errors++;
    }

    /* 予約名 `.hs~` の掃除は**この実行が一時ファイルを作る前**に行う
     * (票 H2 §2-4)。前の実行が電源断などで残したものを片づけてから同期に
     * 入るので、手順 2 の EXIST 経路に落ちる回数が減る。 */
    if (!g_direct) clean_temps(dst_dir);
    if (g_abort) return;

    for (i = 0; i < fl.count; i++) {
        char src_path[OS32_MAX_PATH];
        char dst_path[OS32_MAX_PATH];

        /* 判定できない stat 失敗を踏んだら、それ以上は書かない */
        if (g_abort) return;

        /* "." と ".." をスキップ */
        if (fl.names[i][0] == '.') {
            if (fl.names[i][1] == '\0') continue;
            if (fl.names[i][1] == '.' && fl.names[i][2] == '\0') continue;
        }

        /* パス構築。dst_dir は全体同期のとき "" なので、空文字列で
         * [-1] を読まないように長さを先に見る。連結は必ず容量付きで行い、
         * 溢れたら**判定より前に**エラーにする (往復 2 の 5)。
         * ここは純粋な文字列操作で、FS には触らない。 */
        if (!str_ncpy(src_path, src_dir, (int)sizeof(src_path)) ||
            ((str_len(src_path) == 0 ||
              src_path[str_len(src_path) - 1] != '/') &&
             !str_ncat(src_path, "/", (int)sizeof(src_path))) ||
            !str_ncat(src_path, fl.names[i], (int)sizeof(src_path))) {
            api->kprintf(ATTR_RED, "  FAIL: path too long: %s/%s\n",
                         src_dir, fl.names[i]);
            g_errors++;
            continue;
        }

        if (!str_ncpy(dst_path, dst_dir, (int)sizeof(dst_path)) ||
            ((str_len(dst_path) == 0 ||
              dst_path[str_len(dst_path) - 1] != '/') &&
             !str_ncat(dst_path, "/", (int)sizeof(dst_path))) ||
            !str_ncat(dst_path, fl.names[i], (int)sizeof(dst_path))) {
            api->kprintf(ATTR_RED, "  FAIL: path too long: %s/%s\n",
                         dst_dir, fl.names[i]);
            g_errors++;
            continue;
        }

        /* ---- 要素数の検査 (B2) ----
         * fs/vfs.c は上限を越えた要素を黙って捨てるので、越えたパスは
         * 「1 つ上のディレクトリ」を指す**別のパス**として成立してしまう。
         * 内容を開く前に断ち切る。 */
        if (hsp_depth(src_path) > HS_MAX_PATH_DEPTH ||
            hsp_depth(dst_path) > HS_MAX_PATH_DEPTH) {
            api->kprintf(ATTR_RED,
                         "  FAIL %s reason=%s (VFS の上限 %d 要素)\n",
                         dst_path, HR_TOO_DEEP, HS_MAX_PATH_DEPTH);
            g_errors++;
            continue;
        }

        /* ---- マウントをまたがない (rsync -x と同じ、Codex 2026-09-25 P2) ----
         * 宛先の子がマウント点 (vfs_devname が完全一致で名前を返す = 別の
         * マウントの根) なら、その下へは入らない — 掃除も mkdir もコピーもしない。
         * 開始点の判定 (dst_on_floppy) だけでは、/ = hd0 のとき同期元の
         * /host/fd0/x が /fd0 (FD の自動マウント) へ mkdir されて書かれた。
         * FD に限らず全部のマウント (/host、/cd0、/hd1 …) に当てる。
         * 失敗ではなく除外として数え、-v が無くても 1 行出す (配備元にその名前が
         * あるのに黙って入らないと、なぜ入らないかの手がかりが無いので)。
         * 開始点そのもの (`hsync sys` で /sys が別マウントの場合) は明示の指定
         * なので対象にしない — 見るのは子だけ。
         * 既定の sys 除外より**前**に見る — /sys が別マウントのとき、理由が
         * default_sys_exclusion (-v でしか出ない) に隠れないように (Codex 3 回目)。 */
        {
            const char *mdev = api->vfs_devname(dst_path);
            if (mdev && mdev[0]) {
                g_excluded++;
                api->kprintf(ATTR_YELLOW, "  EXCLUDE %s reason=%s (dev %s)\n",
                             dst_path, HR_OTHER_MOUNT, mdev);
                continue;
            }
        }

        /* ---- 範囲判定 (内容を開く処理や mkdir より**先**、設計書 §3.1) ----
         * ルート直下の sys は既定で飛ばす (稼働中のシェル・共有ライブラリ)。
         * **全体同期のときだけ**。`hsync usr` の usr/sys は対象に含める。
         * 入れ替えたいときは `hsync sys` と明示する。-f でも解除しない。 */
        if (depth == 0 && g_root_sync && str_cmp(fl.names[i], "sys") == 0) {
            g_excluded++;
            if (g_verbose)
                api->kprintf(ATTR_YELLOW, "  EXCLUDE %s reason=%s\n",
                             dst_path, HR_DEFAULT_SYS);
            continue;
        }

        /* ---- 保護判定 (同じく内容を開く前) ----
         * /etc/settings.db* は通常配備で作らない・上書きしない (票 S0-D)。
         * ディレクトリ経路も同じ規則で見る (etc/settings.db/ の残骸を作らない)。 */
        {
            int prot = dst_protected(dst_path);
            if (prot < 0) {
                /* 判定できないものを PROTECTED と呼ばない (B2)。
                 * 書かないのは同じだが errors に数えて非ゼロ終了させる。 */
                api->kprintf(ATTR_RED, "  FAIL %s reason=%s\n",
                             dst_path, HR_PATH_REJECT);
                g_errors++;
                continue;
            }
            if (prot > 0) {
                api->kprintf(ATTR_YELLOW, "  PROTECTED %s reason=%s\n",
                             dst_path, HR_SETTINGS_DB);
                g_protected++;
                continue;
            }
        }
        if (g_abort) return;

        if (fl.types[i] == OS32_FILE_TYPE_DIR) {
            /* ディレクトリ: 作成して再帰。mkdir の失敗を無視すると
             * 中身のコピーが全部落ちて「完了」と出る (往復 2 の 4)。
             * dry-run では mkdir しない — 無い宛先の下は「全部新規」として
             * 読み比べだけ続ける。ただし**型検査だけは dry-run でも行う**
             * (書き込みはしない、B3)。 */
            if (g_dry_run) {
                if (dst_dir_type_ok(dst_path) != 0) continue;
            } else {
                int mrc = api->sys_mkdir(dst_path);
                if (mrc == OS32_ERR_EXIST) {
                    /* EXIST は「同名の何か」がある印でしかない。
                     * ディレクトリであることを確かめてから入る (B3)。 */
                    if (dst_dir_type_ok(dst_path) != 0) continue;
                } else if (mrc != 0) {
                    api->kprintf(ATTR_RED, "  FAIL: mkdir %s (err=%d%s)\n",
                                 dst_path, mrc, err_tag(mrc));
                    g_errors++;
                    continue;
                }
            }
            sync_directory(src_path, dst_path, depth + 1);
        } else {
            sync_file(src_path, dst_path);
        }
    }
}

/* ======== 同期先がフロッピーか (Codex 2026-09-25 P2) ========
 * hsync は MINIMAL (= 起動 FD の中身) に入っている。FD から起動して引数なしで
 * 打つと宛先 / は FD 自身になり、ファイル本体は FAT に O_EXCL が無いので
 * replace_unsupported で落ちるが、その前の sys_mkdir は通って走査も続くので、
 * FD を空のディレクトリで埋め得る。**掃除・mkdir・名札の読みより前に**断る。
 *
 * 判定は KAPI を足さずに vfs_devname (マウント点の**完全一致**でデバイス名を
 * 返す、マウント点でなければ "") で行う: 宛先の正規化済みパスから親へ 1 段ずつ
 * 遡り、最初に名前の返るマウント点 (= 最長一致のマウント) のデバイス名を見る。
 * "/" まで来れば必ずルートのマウント。dst は "/" か "/a/b" (正規化済み)。
 * 戻り値: 1 = FD の上、0 = それ以外。devout にデバイス名を返す。 */
static int dst_on_floppy(const char *dst, const char **devout)
{
    char buf[OS32_MAX_PATH];
    const char *dev;
    int n;

    if (!dst[0] || !str_ncpy(buf, dst, (int)sizeof(buf)))
        str_ncpy(buf, "/", (int)sizeof(buf));
    for (;;) {
        dev = api->vfs_devname(buf);
        if (dev && dev[0]) break;
        if (buf[0] == '/' && buf[1] == '\0') { dev = ""; break; }
        n = str_len(buf);
        while (n > 1 && buf[n - 1] != '/') n--;   /* 最後の要素を落とす */
        if (n <= 1) { buf[0] = '/'; buf[1] = '\0'; }
        else buf[n - 1] = '\0';                  /* 末尾の '/' も落とす */
    }
    *devout = dev;
    return dev[0] == HS_FD_DEV_PREFIX0 && dev[1] == HS_FD_DEV_PREFIX1;
}

/* ======== --root の門 (2026-09-26) ========
 * 同期先の根として受け入れるのは、**マウントの根そのもの**で、FD でなく、
 * ext2 のものだけ。どれか 1 つでも確かめられなければ 1 件も書かずに断る
 * (掃除・mkdir・名札の読みより前、dry-run でも同じ)。
 *
 *   - 同期元 (/host) 自身とその下                 → root_is_source
 *   - vfs_devname(根) が空 = マウントの根でない    → root_not_mount
 *     (HDD 起動の /hd0 は自動マウントされない = ここで断る。ルート / の下の
 *      ただのディレクトリへ書き分ける用途は持たない)
 *   - デバイス名が fd で始まる                     → dest_on_fd
 *   - 根の stat が失敗 (rc != 0: I/O 失敗・NOMOUNT) → root_stat_failed err=rc
 *     (FS の種類の話ではないので root_not_ext2 と分ける)
 *   - 根の stat がディレクトリ + inode 2 でない     → root_not_ext2
 *     (/cd0 = ISO9660、FAT の区画、SerialFS など。HS_EXT2_ROOT_INO の注)
 * 戻り値 0 = 受け入れる / 1 = 断った (表示は済み)。 */
static int root_guard(const char *root)
{
    OS32_Stat st;
    const char *dev;
    const char *why = 0;
    int rc = 0;

    if (str_cmp(root, "/host") == 0 || str_has_prefix(root, "/host/")) {
        why = HR_ROOT_IS_SOURCE;
        dev = "";
    } else {
        dev = api->vfs_devname(root);
        if (!dev) dev = "";
        if (!dev[0]) {
            why = HR_ROOT_NOT_MOUNT;
        } else if (dev[0] == HS_FD_DEV_PREFIX0 && dev[1] == HS_FD_DEV_PREFIX1) {
            why = HR_DST_ON_FD;
        } else {
            rc = api->sys_stat(root, &st);
            if (rc != 0)
                why = HR_ROOT_STAT_FAIL;
            else if ((st.st_mode & OS_S_IFMT) != OS_S_IFDIR ||
                     st.st_ino != HS_EXT2_ROOT_INO)
                why = HR_ROOT_NOT_EXT2;
        }
    }
    if (!why) return 0;

    if (rc != 0)
        api->kprintf(ATTR_RED, "Error: --root %s は同期先にできない reason=%s err=%d%s (dev %s)\n",
                     root, why, rc, err_tag(rc), dev);
    else
        api->kprintf(ATTR_RED, "Error: --root %s は同期先にできない reason=%s%s%s%s\n",
                     root, why, dev[0] ? " (dev " : "", dev, dev[0] ? ")" : "");
    api->kprintf(ATTR_RED,
                 "  --root に渡せるのはマウントの根そのもので、FD でない ext2 だけ\n"
                 "  (例: FD 起動のとき HDD は /hd0)。**1 件も書かない**\n");
    return 1;
}

/* ======== メイン ======== */

static void usage(void)
{
    api->kprintf(ATTR_WHITE, "hsync — HostDrv sync (/host -> / or --root)\n");
    api->kprintf(ATTR_WHITE, "Usage: hsync [-f] [-n] [-v] [--verify] [--root <根>] [dir]\n");
    api->kprintf(ATTR_WHITE, "  -f, --force     同一判定を省いて上書き (保護・検証は省かない)\n");
    api->kprintf(ATTR_WHITE, "      --verify    日時を見ず、全件の内容を必ず比較する (遅い)\n");
    api->kprintf(ATTR_WHITE, "  -n, --dry-run   読んで比べるだけ。1 バイトも書かない (mtime も)\n");
    api->kprintf(ATTR_WHITE, "  -v, --verbose   スキップ理由と比較結果も出す\n");
    api->kprintf(ATTR_WHITE, "      --unsafe-overwrite  KAPI v53 未満のカーネルで**直接上書き**する\n");
    api->kprintf(ATTR_WHITE, "                  (旧内容は残らない。既定は kernel_too_old で断る)\n");
    api->kprintf(ATTR_WHITE, "      --expect-build <ID>  配備元の世代が ID と違えば 1 件も書かずに断る\n");
    api->kprintf(ATTR_WHITE, "                  (%s を読む。完全一致で見る)\n",
                 HS_MANIFEST_PATH);
    api->kprintf(ATTR_WHITE, "      --force-kapi 配備物の KAPI 配置・版を確かめられなくても続ける\n");
    api->kprintf(ATTR_WHITE, "                  (既定は名札の kapi= がカーネルと違う/新しい/無いなら断る)\n");
    api->kprintf(ATTR_WHITE, "      --no-backup /boot/vmkernel.lz4 を置き換える前に vmkernel.old を作らない\n");
    api->kprintf(ATTR_WHITE, "      --root <根>  同期先の根を / から替える (マウントの根・FD でない ext2 だけ)\n");
    api->kprintf(ATTR_WHITE, "                  例: FD 起動で hsync --root /hd0 boot = /host/boot -> /hd0/boot\n");
    api->kprintf(ATTR_WHITE, "  -h, --help      この表示\n");
    api->kprintf(ATTR_WHITE, "  dir             同期対象は 1 つだけ (例: bin, sys, usr/bin)\n");
    api->kprintf(ATTR_WHITE, "  既定: サイズか日時が違うものだけ内容を比較し、違えばコピーする\n");
    api->kprintf(ATTR_WHITE, "        日時が不明 (0) なら必ず内容を比較する\n");
    api->kprintf(ATTR_WHITE, "        見逃すのは「サイズも日時も同じで中身が違う」場合だけ\n");
    api->kprintf(ATTR_WHITE, "  全体同期ではルート直下の sys を除外する (-f でも解除しない)\n");
    api->kprintf(ATTR_WHITE, "  置き換えは一時ファイル `%s<名前>` 経由。この接頭辞は hsync の予約\n",
                 HS_TEMP_PREFIX);
}

int __cdecl main(int argc, char **argv, KernelAPI *_api)
{
    const char *subdir;
    const char *root_arg;
    char norm[HSP_MAX_PATH];
    char rnorm[HSP_MAX_PATH];
    char src[OS32_MAX_PATH];
    char dst[OS32_MAX_PATH];
    int i;
    int rc;

    api = _api;
    subdir = NULL;
    root_arg = NULL;
    g_root[0] = '\0';
    g_root_len = 0;
    g_copied = 0;
    g_unchanged = 0;
    g_excluded = 0;
    g_protected = 0;
    g_metadata_updated = 0;
    g_cleaned = 0;
    g_errors = 0;
    g_mtime_unknown = 0;
    g_mtime_nosys = 0;
    g_force = 0;
    g_force_kapi = 0;
    g_dry_run = 0;
    g_verbose = 0;
    g_verify = 0;
    g_unsafe = 0;
    g_direct = 0;
    g_direct_overwrite = 0;
    g_root_sync = 1;
    g_touched_sys = 0;
    g_touched_boot = 0;
    g_no_backup = 0;
    g_abort = 0;
    file_buf = 0;
    /* 票 H4 */
    g_expect_build = 0;
    g_man_present = 0;
    g_man_valid = 0;
    g_man_count = 0;
    g_man_bad = 0;
    g_man_extra = 0;
    g_man_lookups = 0;
    g_content_compares = 0;

    /* 引数パース。未知オプションと複数 dir は**エラー**にする。
     * 以前は「最後の引数で上書き」だったので `hsync bin sys` が黙って
     * sys だけを同期し、`hsync --dry-run` が dir 名として通っていた。 */
    for (i = 1; i < argc; i++) {
        const char *a = argv[i];

        if (a[0] == '-') {
            if (str_cmp(a, "-f") == 0 || str_cmp(a, "--force") == 0) {
                g_force = 1;
            } else if (str_cmp(a, "--force-kapi") == 0) {
                /* 票 TASK_KAPI_DATA_FIELDS: KAPI の門を明示して越える。
                 * `-f` とは別の旗 (短縮形も作らない)。 */
                g_force_kapi = 1;
            } else if (str_cmp(a, "--verify") == 0) {
                /* 票 H3 §8: 日時によるスキップをしない = H1 の現挙動。
                 * 確実さが要るときだけ払う費用 (短い別名は付けない — 誤って
                 * -v と打ち間違えたときに黙って遅くなるのを避ける)。 */
                g_verify = 1;
            } else if (str_cmp(a, "--unsafe-overwrite") == 0) {
                /* 票 H2 §2-3 末尾: 古いカーネルでの直接上書きを明示する。
                 * **`-f` では解除されない** — 別の意味の旗なので短縮形も
                 * 用意しない。 */
                g_unsafe = 1;
            } else if (str_cmp(a, "--expect-build") == 0) {
                /* 票 H4 §2-3: 配備元の世代を**文字列の完全一致**で確かめる。
                 * 違えば 1 件も書かずに断る。名札が読めないときも
                 * (全体同期なら) 断る — **読めないことを一致と扱わない**。 */
                if (i + 1 >= argc || !argv[i + 1][0]) {
                    api->kprintf(ATTR_RED,
                                 "Error: --expect-build には配備元の世代 "
                                 "(build) が要る\n");
                    return 1;
                }
                g_expect_build = argv[++i];
            } else if (str_cmp(a, "--root") == 0) {
                /* 同期先の根 (2026-09-26)。値の検査は正規化の後 (root_guard) */
                if (i + 1 >= argc || !argv[i + 1][0]) {
                    api->kprintf(ATTR_RED,
                                 "Error: --root には同期先の根 (例 /hd0) が要る\n");
                    return 1;
                }
                if (root_arg) {
                    api->kprintf(ATTR_RED, "Error: --root は 1 つだけ\n");
                    return 1;
                }
                root_arg = argv[++i];
            } else if (str_cmp(a, "--no-backup") == 0) {
                /* 票 TASK_SERIAL_HOSTFS: /boot/vmkernel.lz4 を置き換える前の
                 * .old を作らない (起動した版と確かめられないときに進む口) */
                g_no_backup = 1;
            } else if (str_cmp(a, "-n") == 0 ||
                       str_cmp(a, "--dry-run") == 0) {
                g_dry_run = 1;
            } else if (str_cmp(a, "-v") == 0 ||
                       str_cmp(a, "--verbose") == 0) {
                g_verbose = 1;
            } else if (str_cmp(a, "-h") == 0 || str_cmp(a, "--help") == 0) {
                usage();
                return 0;
            } else {
                api->kprintf(ATTR_RED, "Error: unknown option: %s\n", a);
                usage();
                return 1;
            }
        } else {
            if (subdir) {
                api->kprintf(ATTR_RED,
                             "Error: dir は 1 つだけ (%s と %s)\n", subdir, a);
                return 1;
            }
            subdir = a;
        }
    }

    /* ---- カーネルの版の門 (票 H2 §2-3 末尾) ----------------------------
     *
     * 一時ファイル方式は v53 の O_EXCL と、ext2 の新しい置き換え順序が
     * そろって初めて成立する。v53 未満では O_EXCL が**黙って無視され**、
     * 置き換えも旧順序 (宛先を先に消す) なので、H2 の保証は何ひとつ出せない。
     * **既定は 1 件も書かずに断る。** 直接上書きが要るなら明示させる —
     * 「更新の道が無くなる」は成り立たない (カーネルは停止中の NHD 配備
     * 経路で入れ替えられる、Codex 往復 1 所見 5)。`-f` では解除しない。
     *
     * 判定は dry-run でも同じにする: 断る条件を実行の種類で変えると、
     * `-n` が通ったのに本番が止まる、という分かりにくい形になる。 */
    if (api->version < HS_MIN_KAPI_H2) {
        if (!g_unsafe) {
            api->kprintf(ATTR_RED,
                         "Error: kernel KAPI v%d < v%d reason=%s\n",
                         (int)api->version, HS_MIN_KAPI_H2, HR_KERNEL_TOO_OLD);
            api->kprintf(ATTR_RED,
                         "  一時ファイル方式 (票 H2) が成立しないので 1 件も書かない。\n"
                         "  新しいカーネルを配備するか、旧来の直接上書きでよければ\n"
                         "  --unsafe-overwrite を明示すること (**失敗すると旧内容は残らない**)。\n");
            return 1;
        }
        g_direct = 1;
        api->kprintf(ATTR_YELLOW,
                     "WARN kernel KAPI v%d < %d: direct overwrite (no H2)\n",
                     (int)api->version, HS_MIN_KAPI_H2);
    } else if (g_unsafe) {
        api->kprintf(ATTR_YELLOW,
                     "NOTE: kernel KAPI v%d >= %d なので --unsafe-overwrite は無視する "
                     "(一時ファイル方式で進む)\n",
                     (int)api->version, HS_MIN_KAPI_H2);
    }

    /* 対象パスを**正規化してから** /host 配下と宛先を決める。
     * '..' による同期元脱出、切り詰め、自己コピーをここで断る。 */
    if (subdir) {
        if (hsp_has_backslash(subdir)) {
            /* '\' は OS32 の区切りではないので `..\other` が 1 要素として
             * '..' 検査を素通りするが、HostDrv の先では区切りに化けて
             * 同期元の外を指す (B1)。専用の文言で断る。 */
            api->kprintf(ATTR_RED,
                         "Error: dir に '\\' は使えない (reason=%s): %s\n",
                         HR_BAD_NAME, subdir);
            return 1;
        }
        if (!hsp_normalize(subdir, norm, (int)sizeof(norm))) {
            api->kprintf(ATTR_RED,
                         "Error: dir が不正 (長すぎる / root の外へ出る): %s\n",
                         subdir);
            return 1;
        }
        if (str_cmp(norm, "/") == 0) {
            subdir = NULL;                     /* `hsync .` は全体同期と同じ */
        } else if (str_cmp(norm, "/host") == 0 ||
                   str_has_prefix(norm, "/host/")) {
            /* 同期元そのもの = 同一実体への自己コピー */
            api->kprintf(ATTR_RED,
                         "Error: %s は同期元 (/host) 自身。自己コピーは行わない\n",
                         norm);
            return 1;
        }
    }
    g_root_sync = (subdir == NULL);

    /* ---- --root (2026-09-26): 同期先の根を替える ----
     * 絶対パスだけ受ける (cwd に依らない)。正規化してから門を通す。
     * `--root /` は既定と同じ (門は通す = ルートが ext2 でなければ断る)。 */
    if (root_arg) {
        if (root_arg[0] != '/' || !hsp_normalize(root_arg, rnorm, (int)sizeof(rnorm)) ||
            str_len(rnorm) >= (int)sizeof(g_root)) {
            api->kprintf(ATTR_RED,
                         "Error: --root は / で始まる正規のパス (長すぎない、'\\' を含まない): %s\n",
                         root_arg);
            return 1;
        }
        if (root_guard(rnorm) != 0) return 1;
        if (str_cmp(rnorm, "/") != 0) {
            (void)str_ncpy(g_root, rnorm, (int)sizeof(g_root));
            g_root_len = str_len(g_root);
        }
    }

    /* 同期先がフロッピーなら 1 件も触らずに断る (dry-run でも同じ判定)。
     * 見るのは**根を前置した**宛先 (--root /hd0 bin なら /hd0/bin のマウント) */
    {
        const char *dev;
        char where[OS32_MAX_PATH];
        if (!str_ncpy(where, g_root, (int)sizeof(where)) ||
            !str_ncat(where, subdir ? norm : (g_root_len ? "" : "/"),
                      (int)sizeof(where))) {
            api->kprintf(ATTR_RED, "Error: path too long: %s%s\n", g_root,
                         subdir ? norm : "");
            return 1;
        }
        if (dst_on_floppy(where, &dev)) {
            api->kprintf(ATTR_RED,
                         "Error: 同期先 %s はフロッピー (%s) の上 reason=%s\n",
                         where, dev, HR_DST_ON_FD);
            api->kprintf(ATTR_RED,
                         "  hsync は HDD へ入れるもの。install / cdinst で HDD に入れ、"
                         "HDD から起動して実行すること\n"
                         "  (残りを取るなら `hsync` の後に `hsync sys` + リセット)\n"
                         "  FD で起動して HDD を更新するなら --root /hd0\n");
            return 1;
        }
    }

    /* バッファ確保。64KB を比較用 32KB x 2 に割って使う (設計書 §4.3) */
    file_buf = (u8 *)api->mem_alloc(FILE_BUF_SIZE);
    if (!file_buf) {
        api->kprintf(ATTR_RED, "Error: out of memory\n");
        return 1;
    }
    cmp_a = file_buf;
    cmp_b = file_buf + CMP_BUF_SIZE;

    /* /host がマウントされているか確認 */
    if (!api->sys_is_mounted("/host")) {
        api->kprintf(ATTR_RED, "Error: /host is not mounted\n");
        api->mem_free(file_buf);
        return 1;
    }

    /* ---- 票 H4: 配備の名札 ------------------------------------------
     * 読むのはここ 1 回だけ (file_buf を借りる)。表示は他のどの行よりも
     * 先に出す = 受入 4-2 の「1 行目に DEPLOY build=… が出る」。
     * 断るときは**保護対象の走査にも入らず**、1 件も書かずに戻る。 */
    man_load();
    if (man_gate() != 0) {
        api->mem_free(file_buf);
        return 1;
    }
    /* 票 TASK_KAPI_DATA_FIELDS: 配備物の KAPI 配置と版 (1 件も書く前) */
    if (man_kapi_gate(subdir ? norm : 0) != 0) {
        api->mem_free(file_buf);
        return 1;
    }

    /* 守るべき実体を 1 度だけ集める (票 S0-D の実体規則)。
     * 集められなければ守れないので同期そのものを行わない。 */
    if (scan_protected_entities() != 0) {
        api->mem_free(file_buf);
        return 1;
    }

    /* 同期パス構築。`hsync -f etc` のように subdir で保護対象 (やその子) を
     * 直接指されても書かない。連結は容量付きで行い、溢れたら判定より前に
     * エラーにする (往復 2 の 2 / 5)。 */
    if (subdir) {
        if (!str_ncpy(src, "/host", (int)sizeof(src)) ||
            !str_ncat(src, norm, (int)sizeof(src)) ||
            !str_ncpy(dst, g_root, (int)sizeof(dst)) ||
            !str_ncat(dst, norm, (int)sizeof(dst))) {
            api->kprintf(ATTR_RED, "Error: path too long: %s\n", subdir);
            api->mem_free(file_buf);
            return 1;
        }
        /* **`/host` を前置したあとの**要素数で上限を見る (B2)。
         * hsp_normalize は入力側にしか上限を掛けないので、32 要素ちょうどの
         * dir はここまで通ってくる。宛先側も同じ理由で見る。 */
        if (hsp_depth(src) > HS_MAX_PATH_DEPTH ||
            hsp_depth(dst) > HS_MAX_PATH_DEPTH) {
            api->kprintf(ATTR_RED,
                         "Error: dir が深すぎる reason=%s (VFS の上限 %d 要素、"
                         "/host を足すと %d 要素): %s\n",
                         HR_TOO_DEEP, HS_MAX_PATH_DEPTH, hsp_depth(src), norm);
            api->mem_free(file_buf);
            return 1;
        }
        {
            int prot = dst_protected(dst);
            if (g_abort) {
                api->mem_free(file_buf);
                return 1;
            }
            if (prot < 0) {
                /* 判定できないものを PROTECTED と呼ばない (B2) */
                api->kprintf(ATTR_RED, "Error: %s reason=%s\n",
                             dst, HR_PATH_REJECT);
                api->mem_free(file_buf);
                return 1;
            }
            if (prot > 0) {
                api->kprintf(ATTR_YELLOW, "  PROTECTED %s reason=%s\n",
                             dst, HR_SETTINGS_DB);
                api->mem_free(file_buf);
                return 0;             /* 除外は失敗ではない */
            }
        }
        api->kprintf(ATTR_CYAN, "hsync: %s -> %s\n", src, dst);
    } else {
        if (!str_ncpy(src, "/host", (int)sizeof(src)) ||
            !str_ncpy(dst, g_root, (int)sizeof(dst))) {
            api->kprintf(ATTR_RED, "Error: path too long\n");
            api->mem_free(file_buf);
            return 1;
        }
        api->kprintf(ATTR_CYAN, "hsync: /host -> %s\n", g_root_len ? g_root : "/");
        /* 既定の除外を**先頭で明示する** (設計書 §7.2) */
        api->kprintf(ATTR_YELLOW,
                     "  note: ルート直下の sys は既定で除外 "
                     "(入れ替えるなら `hsync sys`)。-f でも解除しない\n");
    }

    if (g_force) {
        api->kprintf(ATTR_YELLOW,
                     "  (force mode: 同一判定のみ省略。保護と読戻し検証は行う)\n");
    }
    if (g_verify) {
        api->kprintf(ATTR_YELLOW,
                     "  (verify mode: 日時で省略せず、全件の内容を比較する)\n");
    }
    if (g_dry_run) {
        api->kprintf(ATTR_YELLOW,
                     "  (dry-run: 読み取りと比較だけ。mkdir・書き込み・sync・mtime はしない)\n");
    }

    /* 同期実行。明示 dir は**起点の型を先に確かめてから**始める (B3)。
     * 全体同期の起点 (/host -> /) はマウントの前提としてディレクトリ。
     * 始めなかった場合も下の集計行と終了コードはそのまま通る
     * (errors に入っているので FAILED: / 非ゼロになる)。 */
    if (!subdir || start_point_ok(src, dst) == 0) {
        sync_directory(src, dst, 0);
    }

    if (g_abort) {
        api->kprintf(ATTR_RED,
                     "\nAborted: 保護判定に必要な stat が失敗した\n");
        api->mem_free(file_buf);
        return 1;
    }

    /* ファイルシステム同期。落ちたら書いたものが届いていない。
     * dry-run は明示 sync をしない (設計書 §3.2)。 */
    if (!g_dry_run) {
        rc = api->vfs_sync();
        if (rc != 0) {
            api->kprintf(ATTR_RED, "  FAIL: vfs_sync (err=%d)\n", rc);
            g_errors++;
        }
    }

    /* 結果表示。失敗があれば頭を FAILED: にする (成功表示へ進めない) */
    api->kprintf(g_errors ? ATTR_RED : ATTR_WHITE,
                 "\n%s copied=%d unchanged=%d excluded=%d protected=%d "
                 "metadata_updated=%d cleaned=%d errors=%d%s\n",
                 hsp_final_label(g_errors),
                 g_copied, g_unchanged, g_excluded, g_protected,
                 g_metadata_updated, g_cleaned, g_errors,
                 g_dry_run ? " (dry-run: copied は予定件数)" : "");
    /* 票 H4 §2-3: **`manifest_extra` だけ**を数える。`manifest_missing`
     * (名札にあるのに配備元に無い) は名札を正とする走査が要るので別票。 */
    if (g_man_valid)
        api->kprintf(g_man_extra ? ATTR_YELLOW : ATTR_WHITE,
                     "manifest_extra=%d%s\n", g_man_extra,
                     g_dry_run ? " (dry-run: 予定件数)" : "");
    if (g_verbose)
        api->kprintf(ATTR_WHITE, "content_compares=%d\n", g_content_compares);
    if (g_direct_overwrite)
        api->kprintf(ATTR_YELLOW,
                     "direct_overwrite=%d "
                     "(KAPI v%d < %d: 一時ファイル方式を使っていない。"
                     "**失敗した回の旧内容は残らない**)\n",
                     g_direct_overwrite, (int)api->version, HS_MIN_KAPI_H2);
    if (g_dry_run && g_metadata_updated)
        api->kprintf(ATTR_CYAN,
                     "  (dry-run: metadata_updated も予定件数。"
                     "mtime は 1 件も書いていない)\n");

    /* **省略したことを必ず見せる** (票 H3)。集計の 6 区分とは別に数える —
     * 失敗ではないが、「時刻を保存したつもり」で終わらせない。 */
    if (g_mtime_unknown)
        api->kprintf(ATTR_YELLOW,
                     "NOTE: 元の mtime が不明 (0) で時刻の保存を省略: %d 件 "
                     "reason=%s (内容の同期は行った)\n",
                     g_mtime_unknown, HR_MTIME_UNKNOWN);
    if (g_mtime_nosys)
        api->kprintf(ATTR_YELLOW,
                     "NOTE: 宛先 FS が mtime の設定に対応していない: %d 件 "
                     "reason=%s (内容の同期は行った)\n",
                     g_mtime_nosys, HR_MTIME_NOSYS);

    /* 「ディスクへ同期した」と「稼働中の版が入れ替わった」は別のこと
     * (設計書 §7.2)。再起動はここでは行わない。 */
    /* `g_copied` だけを条件にすると、**置換は済んだのに sync が落ちた**回
     * (票 H2 手順 9) で案内が丸ごと消える。note_target は媒体の上で内容が
     * 入れ替わったときだけ立つので、そちらも条件に入れる。 */
    if (!g_dry_run && (g_copied > 0 || g_touched_sys || g_touched_boot)) {
        api->kprintf(ATTR_YELLOW,
                     "NOTE: ディスク上を更新しただけ。稼働中の版は切り替わっていない\n");
        /* --root のときは走っている版とは別の媒体を更新している。シェルの
         * 再起動もリセットも FD (起動した媒体) を読み直すだけなので、
         * 案内は「HDD から起動し直す」 */
        if (g_touched_sys && g_root_len > 0)
            api->kprintf(ATTR_YELLOW,
                         "NOTE: %s/sys を更新した -> HDD (%s) から起動し直すと使われる "
                         "(いまの媒体のシェル再起動では切り替わらない)\n",
                         g_root, g_root);
        else if (g_touched_sys)
            api->kprintf(ATTR_YELLOW,
                         "NOTE: /sys を更新した -> シェル再起動が必要 "
                         "(shlib は起動時ロード)\n");
        if (g_touched_boot && g_root_len > 0)
            api->kprintf(ATTR_YELLOW,
                         "NOTE: %s/boot を更新した -> HDD (%s) から起動し直すと使われる "
                         "(いまの媒体のリセットでは切り替わらない)\n",
                         g_root, g_root);
        else if (g_touched_boot)
            api->kprintf(ATTR_YELLOW,
                         "NOTE: /boot を更新した -> 再起動が必要 "
                         "(カーネルはブート時ロード)\n");
    }

    api->mem_free(file_buf);
    /* 失敗は終了コードに載せる (crt0_c が main の戻り値を sys_exit へ渡す) */
    return g_errors ? 1 : 0;
}
