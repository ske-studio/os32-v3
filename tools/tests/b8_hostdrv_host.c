/* ========================================================================
 *  b8_hostdrv_host.c — HostDrv の「OPEN の失敗を全部 NOTFOUND に畳む」
 *  経路 (票 B8 / Codex 実装レビュー P1-4) を **実物の fs/hostdrvfs.c** で見る
 *
 *  票:   docs/archive/shell/TASK_FS_TYPE.md §2
 *  実行: python3 -B tools/tests/test_b8_hostdrv.py
 *  記録: tools/tests/b8_tdd.md §5
 *
 *  なぜ純関数だけでは足りないのか
 *  ------------------------------
 *  前の回は判定を純関数 (hdrv_size_result) に切り出してそこだけ試験した。
 *  ところが**その手前の hostdrv_create() の失敗**が依然として全部
 *  VFS_ERR_NOTFOUND に畳まれていたので、
 *    stat は成功 -> 通常ファイルと判定
 *    -> サイズ取得のための OPEN だけ失敗 -> NOTFOUND
 *    -> vfs_open の O_CREAT が hdrv_write_file(..., "", 0) へ進む
 *    -> NP2_FILE_OVERWRITE_IF が**既存ファイルを切り詰める**
 *  という経路が残った。純関数の試験はこれを見ていない。
 *  **実物の hdrv_get_file_size() を通す。**
 *
 *  どうやってホストで動かすか
 *  --------------------------
 *  fs/hostdrvfs.c はハイパーコール (I/O ポート) でエミュレータと話す。
 *  include/io.h の inp / outp は特権命令のインライン asm なのでホストでは
 *  走らない。tools/tests/hostdrv_hostshim/io.h を -I で**先に**置いて
 *  差し替え、コマンド列が完成した時点で**贋の NP21/W** が応答を書く。
 *  贋物は「次の CREATE が返す NTSTATUS」「次の QUERY が返す中身」を
 *  試験から指定できるだけの最小のもの。**fs/hostdrvfs.c は 1 行も写さない。**
 *
 *  [C1] C89 / GNU89。u32 は unsigned long なので**必ず ILP32 で組む**
 *  (Np2* 構造体のオフセットが変わるため)。libc は使わない。
 * ======================================================================== */

#include "types.h"
#include "vfs.h"
#include "hostdrvfs_proto.h"

/* ======================================================================== */
/*  libc の代わり (-nostdlib)                                               */
/* ======================================================================== */

static int g_exit_code;

static void die(int code)
{
    __asm__ volatile("int $0x80" : : "a"(1), "b"(code));
    for (;;) { }
}

static u32 h_strlen(const char *s) { u32 n = 0; while (s[n]) n++; return n; }

static void report(const char *text)
{
    u32 len = h_strlen(text);
    __asm__ volatile("int $0x80" : : "a"(4), "b"(1), "c"(text), "d"(len)
                     : "memory");
}

static void report_i(int v)
{
    char buf[16];
    int i = 15;
    u32 u;
    buf[i] = '\0';
    if (v < 0) u = (u32)(-v); else u = (u32)v;
    if (u == 0) { buf[--i] = '0'; }
    while (u > 0) { buf[--i] = (char)('0' + (u % 10)); u /= 10; }
    if (v < 0) buf[--i] = '-';
    report(&buf[i]);
}

static int g_checks;
static int g_failures;

static void check_at(int cond, const char *what, int line)
{
    g_checks++;
    if (cond) return;
    g_failures++;
    report("  FAIL line ");
    report_i(line);
    report(": ");
    report(what);
    report("\n");
}

#define CHECK(x) check_at((x) ? 1 : 0, #x, __LINE__)

/* ======================================================================== */
/*  kstring / kprintf / kmalloc の境界                                      */
/* ======================================================================== */

void *kmemcpy(void *dst, const void *src, u32 n)
{
    u8 *d = (u8 *)dst; const u8 *s = (const u8 *)src; u32 i;
    for (i = 0; i < n; i++) d[i] = s[i];
    return dst;
}

void *kmemset(void *dst, int val, u32 n)
{
    u8 *d = (u8 *)dst; u32 i;
    for (i = 0; i < n; i++) d[i] = (u8)val;
    return dst;
}

u32 kstrlen(const char *s) { return h_strlen(s); }

int kstrcmp(const char *a, const char *b)
{
    while (*a && *a == *b) { a++; b++; }
    return (int)(u8)*a - (int)(u8)*b;
}

int kstrncmp(const char *a, const char *b, u32 n)
{
    u32 i;
    for (i = 0; i < n; i++) {
        if (a[i] != b[i]) return (int)(u8)a[i] - (int)(u8)b[i];
        if (!a[i]) return 0;
    }
    return 0;
}

char *kstrncpy(char *dst, const char *src, u32 n)
{
    u32 i = 0;
    if (n == 0) return dst;
    for (; i + 1 < n && src[i]; i++) dst[i] = src[i];
    dst[i] = '\0';
    return dst;
}

char *kstrncat(char *dst, const char *src, u32 n)
{
    u32 d = h_strlen(dst);
    if (d + 1 >= n) return dst;
    kstrncpy(dst + d, src, n - d);
    return dst;
}

void *memcpy(void *dst, const void *src, u32 n) { return kmemcpy(dst, src, n); }
void *memset(void *dst, int val, u32 n) { return kmemset(dst, val, n); }

void kprintf(u8 attr, const char *fmt, ...) { (void)attr; (void)fmt; }

void *kzalloc(u32 size) { (void)size; return (void *)0; }
void kfree(void *p) { (void)p; }

/* hdrv_stat の FILETIME -> Unix 秒 変換が 64bit 除算を使う。-nostdlib かつ
 * この環境に 32bit の libgcc が無いので、素朴な筆算で自前に用意する
 * (ホスト試験専用。実カーネルは i386-elf の libgcc を使う)。 */
unsigned long long __udivdi3(unsigned long long num, unsigned long long den);
unsigned long long __udivdi3(unsigned long long num, unsigned long long den)
{
    unsigned long long q = 0, r = 0;
    int i;
    if (den == 0) return 0;
    for (i = 63; i >= 0; i--) {
        r <<= 1;
        r |= (num >> i) & 1ULL;
        if (r >= den) { r -= den; q |= (1ULL << i); }
    }
    return q;
}

unsigned long long __umoddi3(unsigned long long num, unsigned long long den);
unsigned long long __umoddi3(unsigned long long num, unsigned long long den)
{
    if (den == 0) return 0;
    return num - __udivdi3(num, den) * den;
}

/* VFS 登録の境界 (hostdrvfs_init から呼ばれるだけ) */
void vfs_register_fs(VfsOps *ops) { (void)ops; }

/* ======================================================================== */
/*  実物の UTF-16 変換 と 実物の HostDrv ドライバ                           */
/* ======================================================================== */

#include "../../lib/kutf16.c"
#include "../../fs/hostdrvfs.c"

/* ======================================================================== */
/*  贋の NP21/W (ハイパーコールの受け手)                                    */
/* ======================================================================== */

/* 試験が仕込む応答 */
static u32 t_create_status;     /* 次の IRP_MJ_CREATE が返す NTSTATUS */
static int t_query_fail;        /* 1 = QUERY_INFORMATION を失敗させる */
static u64 t_eof;               /* FileStandardInformation.EndOfFile */
static u8  t_directory;         /* FileStandardInformation.Directory */
static u32 t_attributes;        /* FileBasicInformation.FileAttributes */
static u64 t_last_write;        /* FileBasicInformation.LastWriteTime */
static int t_no_response;       /* 1 = 応答しない (番兵のまま) */

/* 観測 */
static int t_create_calls;
static int t_write_calls;       /* IRP_MJ_WRITE の回数 */
static int t_setinfo_calls;     /* IRP_MJ_SET_INFORMATION の回数 */
static u32 t_last_disposition;  /* 直前の CREATE の disposition */
/* **切り詰めの印**: OVERWRITE_IF の CREATE。NT はこの disposition だけで
 * 既存ファイルを 0 バイトにするので、IRP_MJ_WRITE が 1 度も来なくても
 * (0 バイト書き込みは WRITE を出さない) 中身は消える。 */
static int t_overwrite_creates;

static void t_reset(void)
{
    t_create_status = NP2_STATUS_SUCCESS;
    t_query_fail = 0;
    t_eof = 1234;
    t_directory = 0;
    t_attributes = 0x20;            /* FILE_ATTRIBUTE_ARCHIVE */
    t_last_write = 0;
    t_no_response = 0;
    t_create_calls = 0;
    t_write_calls = 0;
    t_setinfo_calls = 0;
    t_last_disposition = 0;
    t_overwrite_creates = 0;
}

unsigned int hdrv_shim_inp(unsigned int port)
{
    if (port == HOSTDRV_IO_ADDR) return HOSTDRV_DETECT_ADDR_VAL;
    if (port == HOSTDRV_IO_CMD)  return HOSTDRV_DETECT_CMD_VAL;
    return 0xFF;
}

/* コマンド列 "HDR9801" の 7 バイト目で 1 回だけ「実行」する */
static void hdrv_fake_emulator(void)
{
    u32 major = g_stack.majorFunction;

    if (t_no_response) return;      /* 番兵のまま = 応答なし */

    if (major == NP2_IRP_MJ_CREATE) {
        t_create_calls++;
        t_last_disposition = g_stack.parameters.create.options >> 24;
        if (t_last_disposition == NP2_FILE_OVERWRITE_IF) t_overwrite_creates++;
        g_iostatus.Status = t_create_status;
        return;
    }

    if (major == NP2_IRP_MJ_QUERY_INFORMATION) {
        u32 cls = g_stack.parameters.queryFile.FileInformationClass;
        if (t_query_fail) {
            g_iostatus.Status = NP2_STATUS_INVALID_PARAMETER;
            return;
        }
        if (cls == NP2_FileStandardInformation) {
            Np2FileStandardInfo si;
            kmemset(&si, 0, sizeof(si));
            si.EndOfFile = t_eof;
            si.AllocationSize = t_eof;
            si.NumberOfLinks = 1;
            si.Directory = t_directory;
            kmemcpy((void *)g_databuf, &si, sizeof(si));
            g_iostatus.Information = sizeof(si);
            g_iostatus.Status = NP2_STATUS_SUCCESS;
            return;
        }
        if (cls == NP2_FileBasicInformation) {
            Np2FileBasicInfo bi;
            kmemset(&bi, 0, sizeof(bi));
            bi.FileAttributes = t_attributes;
            bi.LastWriteTime = t_last_write;
            kmemcpy((void *)g_databuf, &bi, sizeof(bi));
            g_iostatus.Information = sizeof(bi);
            g_iostatus.Status = NP2_STATUS_SUCCESS;
            return;
        }
        g_iostatus.Status = NP2_STATUS_INVALID_PARAMETER;
        return;
    }

    if (major == NP2_IRP_MJ_WRITE) {
        t_write_calls++;
        g_iostatus.Information = g_stack.parameters.write.length;
        g_iostatus.Status = NP2_STATUS_SUCCESS;
        return;
    }

    if (major == NP2_IRP_MJ_SET_INFORMATION) {
        t_setinfo_calls++;
        g_iostatus.Status = NP2_STATUS_SUCCESS;
        return;
    }

    /* CLEANUP / CLOSE など */
    g_iostatus.Status = NP2_STATUS_SUCCESS;
}

void hdrv_shim_outp(unsigned int port, unsigned int value)
{
    static int cmd_pos;
    static const char SEQ[7] = { 'H', 'D', 'R', '9', '8', '0', '1' };

    if (port == HOSTDRV_IO_ADDR) { cmd_pos = 0; return; }
    if (port != HOSTDRV_IO_CMD) return;

    if ((char)value == SEQ[cmd_pos]) {
        cmd_pos++;
        if (cmd_pos == 7) {
            cmd_pos = 0;
            hdrv_fake_emulator();
        }
    } else {
        cmd_pos = 0;
    }
}

/* ======================================================================== */
/*  試験本体                                                                */
/* ======================================================================== */

/* [E1] hdrv_get_file_size を**実物で**通す — Directory と ISDIR (③) */
static void case_get_file_size_directory(void)
{
    u32 sz;
    int rc;

    report("  [E1] hdrv_get_file_size: Directory -> ISDIR\n");

    /* 通常ファイル: サイズを返す */
    t_reset();
    sz = 0;
    t_eof = 4096;
    t_directory = 0;
    rc = hdrv_get_file_size((void *)0, "/f", &sz);
    CHECK(rc == VFS_OK);
    CHECK(sz == 4096);
    CHECK(t_create_calls == 1);

    /* **ディレクトリ**: NT は OPEN も QUERY も成功させ、サイズも返す。
     * それでも「通常ファイルのサイズ」と名乗ってはいけない。 */
    t_reset();
    sz = 0;
    t_eof = 4096;
    t_directory = 1;
    rc = hdrv_get_file_size((void *)0, "/d", &sz);
    CHECK(rc == VFS_ERR_ISDIR);
    CHECK(rc != VFS_OK);
    CHECK(rc != VFS_ERR_NOTFOUND);

    /* Directory が非 0 ならディレクトリ (NP21/W は 0/1 を入れる) */
    t_reset();
    t_directory = 2;
    CHECK(hdrv_get_file_size((void *)0, "/d", &sz) == VFS_ERR_ISDIR);

    /* サイズ問い合わせが失敗: 「サイズ 0 の通常ファイル」と名乗らない */
    t_reset();
    sz = 0xBEEF;
    t_query_fail = 1;
    rc = hdrv_get_file_size((void *)0, "/f", &sz);
    CHECK(rc == VFS_ERR_IO);
    CHECK(rc != VFS_ERR_NOTFOUND);
}

/* [E2] **P1-4 本体**: OPEN の失敗を「不存在」と読み替えない */
static void case_open_failure_not_notfound(void)
{
    u32 sz;
    int rc;

    report("  [E2] hdrv_get_file_size: OPEN の失敗を NOTFOUND に畳まない\n");

    /* 本当に無い: NOTFOUND で正しい (O_CREAT はここから作ってよい) */
    t_reset();
    t_create_status = NP2_STATUS_OBJECT_NAME_NOT_FOUND;
    CHECK(hdrv_get_file_size((void *)0, "/nope", &sz) == VFS_ERR_NOTFOUND);

    t_reset();
    t_create_status = NP2_STATUS_OBJECT_PATH_NOT_FOUND;
    CHECK(hdrv_get_file_size((void *)0, "/no/where", &sz) == VFS_ERR_NOTFOUND);

    /* **以下はどれも「無い」ではない。NOTFOUND にしたら O_CREAT が
     * 既存ファイルを切り詰める。** */
    t_reset();
    t_create_status = NP2_STATUS_SHARING_VIOLATION;
    rc = hdrv_get_file_size((void *)0, "/busy", &sz);
    CHECK(rc < 0);
    CHECK(rc != VFS_ERR_NOTFOUND);

    t_reset();
    t_create_status = NP2_STATUS_ACCESS_DENIED;
    rc = hdrv_get_file_size((void *)0, "/ro", &sz);
    CHECK(rc != VFS_ERR_NOTFOUND);

    t_reset();
    t_create_status = NP2_STATUS_TOO_MANY_OPENED_FILES;
    rc = hdrv_get_file_size((void *)0, "/f", &sz);
    CHECK(rc != VFS_ERR_NOTFOUND);

    t_reset();
    t_create_status = NP2_STATUS_MEDIA_WRITE_PROTECTED;
    CHECK(hdrv_get_file_size((void *)0, "/f", &sz) != VFS_ERR_NOTFOUND);

    t_reset();
    t_create_status = NP2_STATUS_INVALID_PARAMETER;
    CHECK(hdrv_get_file_size((void *)0, "/f", &sz) != VFS_ERR_NOTFOUND);

    /* 名前に使えない文字。NP21/W は **わざと** NOT_FOUND と別にしている
     * (ワイルドカード付き copy のため) ので、こちらも別に扱う。 */
    t_reset();
    t_create_status = NP2_STATUS_OBJECT_NAME_INVALID;
    rc = hdrv_get_file_size((void *)0, "/f", &sz);
    CHECK(rc == VFS_ERR_INVAL);
    CHECK(rc != VFS_ERR_NOTFOUND);

    /* ディレクトリを開こうとして断られた */
    t_reset();
    t_create_status = NP2_STATUS_FILE_IS_A_DIRECTORY;
    CHECK(hdrv_get_file_size((void *)0, "/d", &sz) == VFS_ERR_ISDIR);

    /* **エミュレータが応答しない** (番兵のまま) */
    t_reset();
    t_no_response = 1;
    rc = hdrv_get_file_size((void *)0, "/f", &sz);
    CHECK(rc == VFS_ERR_IO);
    CHECK(rc != VFS_ERR_NOTFOUND);
}

/* [E3] 他の入口も同じ畳み込みをしていないこと */
static void case_other_entries(void)
{
    static u8 buf[64];
    OS32_Stat st;
    int rc;

    report("  [E3] read_file / stat / read_stream / unlink / rename も同じ\n");

    t_reset();
    t_create_status = NP2_STATUS_SHARING_VIOLATION;
    rc = hdrv_read_file((void *)0, "/f", buf, sizeof(buf));
    CHECK(rc != VFS_ERR_NOTFOUND);

    t_reset();
    t_create_status = NP2_STATUS_SHARING_VIOLATION;
    rc = hdrv_stat((void *)0, "/f", &st);
    CHECK(rc != VFS_ERR_NOTFOUND);

    t_reset();
    t_create_status = NP2_STATUS_SHARING_VIOLATION;
    rc = hdrv_read_stream((void *)0, "/f", buf, sizeof(buf), 0);
    CHECK(rc != VFS_ERR_NOTFOUND);

    t_reset();
    t_create_status = NP2_STATUS_SHARING_VIOLATION;
    rc = hdrv_unlink((void *)0, "/f");
    CHECK(rc != VFS_ERR_NOTFOUND);

    t_reset();
    t_create_status = NP2_STATUS_SHARING_VIOLATION;
    rc = hdrv_rename((void *)0, "/a", "/b");
    CHECK(rc != VFS_ERR_NOTFOUND);

    /* 本当に無いときは従来どおり NOTFOUND (回帰) */
    t_reset();
    t_create_status = NP2_STATUS_OBJECT_NAME_NOT_FOUND;
    CHECK(hdrv_read_file((void *)0, "/x", buf, sizeof(buf)) == VFS_ERR_NOTFOUND);
    t_reset();
    t_create_status = NP2_STATUS_OBJECT_NAME_NOT_FOUND;
    CHECK(hdrv_stat((void *)0, "/x", &st) == VFS_ERR_NOTFOUND);
    t_reset();
    t_create_status = NP2_STATUS_OBJECT_NAME_NOT_FOUND;
    CHECK(hdrv_unlink((void *)0, "/x") == VFS_ERR_NOTFOUND);
}

/* [E4] **被害そのもの**: vfs_open(O_CREAT) の受け手が切り詰めないこと。
 * ここでは hdrv_write_file が呼ばれない = OVERWRITE_IF の CREATE が
 * 発行されないことを、贋エミュレータ側の回数で見る。 */
static void case_no_truncate_on_open_failure(void)
{
    u32 sz;
    int rc;

    report("  [E4] OPEN 失敗が切り詰めの CREATE(OVERWRITE_IF) を誘発しない\n");

    /* まず「切り詰めが起きるとどう見えるか」を確かめておく (対照)。
     * vfs_open の O_CREAT 分岐が呼ぶのはこれ。**0 バイト書き込みなので
     * IRP_MJ_WRITE は 1 度も出ない** — 中身を消すのは OVERWRITE_IF の
     * CREATE そのもの。だから「書き込み回数 0」では安全の証拠にならず、
     * **OVERWRITE_IF の CREATE が出ていないこと**を見る必要がある。 */
    t_reset();
    CHECK(hdrv_write_file((void *)0, "/f", "", 0) >= 0);
    CHECK(t_create_calls == 1);
    CHECK(t_last_disposition == NP2_FILE_OVERWRITE_IF);
    CHECK(t_overwrite_creates == 1);
    CHECK(t_write_calls == 0);
    CHECK(t_setinfo_calls == 1);        /* EndOfFile を 0 にする */

    /* サイズ取得の OPEN だけが失敗 -> **NOTFOUND ではない**ので、
     * fs/vfs_fd.c の O_CREAT 分岐 (NOTFOUND 限定) には入れない。
     * ここでは get_file_size の戻り値がその条件を満たさないことと、
     * get_file_size 自身が何も壊さないことを見る。 */
    t_reset();
    t_create_status = NP2_STATUS_SHARING_VIOLATION;
    rc = hdrv_get_file_size((void *)0, "/f", &sz);
    CHECK(rc != VFS_ERR_NOTFOUND);
    CHECK(t_overwrite_creates == 0);    /* 切り詰めていない */
    CHECK(t_write_calls == 0);
    CHECK(t_setinfo_calls == 0);

    t_reset();
    t_no_response = 1;
    rc = hdrv_get_file_size((void *)0, "/f", &sz);
    CHECK(rc != VFS_ERR_NOTFOUND);
    CHECK(t_overwrite_creates == 0);
    CHECK(t_setinfo_calls == 0);

    /* ディレクトリでも切り詰めへ進ませない */
    t_reset();
    t_directory = 1;
    rc = hdrv_get_file_size((void *)0, "/d", &sz);
    CHECK(rc == VFS_ERR_ISDIR);
    CHECK(t_overwrite_creates == 0);
}

/* [E5] NTSTATUS -> VFS の対応表そのもの (純規則) */
static void case_status_table(void)
{
    report("  [E5] hdrv_create_status_to_vfs\n");

    CHECK(hdrv_create_status_to_vfs(NP2_STATUS_SUCCESS) == 0);

    /* **「無い」はこの 2 つだけ** */
    CHECK(hdrv_create_status_to_vfs(NP2_STATUS_OBJECT_NAME_NOT_FOUND)
          == OS32_ERR_NOTFOUND);
    CHECK(hdrv_create_status_to_vfs(NP2_STATUS_OBJECT_PATH_NOT_FOUND)
          == OS32_ERR_NOTFOUND);

    CHECK(hdrv_create_status_to_vfs(NP2_STATUS_OBJECT_NAME_COLLISION)
          == OS32_ERR_EXIST);
    CHECK(hdrv_create_status_to_vfs(NP2_STATUS_FILE_IS_A_DIRECTORY)
          == OS32_ERR_ISDIR);
    CHECK(hdrv_create_status_to_vfs(NP2_STATUS_NOT_A_DIRECTORY)
          == OS32_ERR_NOTDIR);
    CHECK(hdrv_create_status_to_vfs(NP2_STATUS_DIRECTORY_NOT_EMPTY)
          == OS32_ERR_NOTEMPTY);
    CHECK(hdrv_create_status_to_vfs(NP2_STATUS_OBJECT_NAME_INVALID)
          == OS32_ERR_INVAL);

    /* **知らない失敗は必ず I/O。「無い」に落とさない** */
    CHECK(hdrv_create_status_to_vfs(NP2_STATUS_ACCESS_DENIED) == OS32_ERR_IO);
    CHECK(hdrv_create_status_to_vfs(NP2_STATUS_SHARING_VIOLATION) == OS32_ERR_IO);
    CHECK(hdrv_create_status_to_vfs(NP2_STATUS_MEDIA_WRITE_PROTECTED)
          == OS32_ERR_IO);
    CHECK(hdrv_create_status_to_vfs(NP2_STATUS_TOO_MANY_OPENED_FILES)
          == OS32_ERR_IO);
    CHECK(hdrv_create_status_to_vfs(NP2_STATUS_CANNOT_DELETE) == OS32_ERR_IO);
    CHECK(hdrv_create_status_to_vfs(NP2_STATUS_INVALID_PARAMETER) == OS32_ERR_IO);
    CHECK(hdrv_create_status_to_vfs(NP2_STATUS_SENTINEL) == OS32_ERR_IO);
    CHECK(hdrv_create_status_to_vfs(0xC0000999UL) == OS32_ERR_IO);
}

/* [E6] 名前の厳密化 (TASK_VFS_FD_PATH 実装レビュー ラリー 3 B2)。
 * 以前は UTF-16 への変換が途中で切れた列で打ち切り・冗長な符号化を受理・
 * BMP 外を U+FFFD に置き換えたので、"disk.img\xC2" が "disk.img" として
 * ホストに届き、VFS の BUSY / pinned の比較をすり抜けた。**実物の
 * hostdrv_create / hdrv_rename** で、断る名前ではハイパーコールが 1 度も
 * 出ない (ホストへ何も送らない) ことと、正当な日本語は正しい UTF-16 で
 * 届くことを見る。 */
static void case_strict_names(void)
{
    static const char *const bad[] = {
        "/disk.img\xc2",            /* 途中で切れた列 */
        "/\xc1\xa4isk.img",         /* 冗長な符号化 = "disk.img" */
        "/\xf0\x9f\x98\x80.img",   /* BMP 外 */
        "/\xf0\x9f\x98\x81.img",
        "/a\x80",                   /* 単独の継続バイト */
        "/\xed\xa0\x80",           /* サロゲート */
        "/disk.img::$DATA",         /* 代替データストリーム */
        "/a*", "/a?", "/a\"", "/a<", "/a>", "/a|",
    };
    static u8 buf[16];
    OS32_Stat st;
    u32 i, sz;
    static const u16 want[] = { '\\', 0x65E5, 0x672C, '.', 't', 'x', 't', 0 };

    report("  [E6] 不正な UTF-8 / BMP 外 / Win32 の禁止文字はホストへ送らない\n");
    for (i = 0; i < sizeof(bad) / sizeof(bad[0]); i++) {
        t_reset();
        CHECK(hdrv_unlink((void *)0, bad[i]) == VFS_ERR_INVAL);
        CHECK(hdrv_stat((void *)0, bad[i], &st) == VFS_ERR_INVAL);
        CHECK(hdrv_read_file((void *)0, bad[i], buf, sizeof(buf)) == VFS_ERR_INVAL);
        CHECK(hdrv_get_file_size((void *)0, bad[i], &sz) == VFS_ERR_INVAL);
        CHECK(hdrv_write_file((void *)0, bad[i], "X", 1) == VFS_ERR_INVAL);
        CHECK(hdrv_rmdir((void *)0, bad[i]) == VFS_ERR_INVAL);
        CHECK(hdrv_rename((void *)0, bad[i], "/b") == VFS_ERR_INVAL);
        CHECK(hdrv_rename((void *)0, "/a", bad[i]) == VFS_ERR_INVAL);
        CHECK(t_create_calls == 0);
        CHECK(t_setinfo_calls == 0);
        CHECK(t_write_calls == 0);
        if (t_create_calls != 0) {
            report("    ^ bad["); report_i((int)i); report("]\n");
        }
    }

    /* 二重の防御: 規則を通ってしまった名前でも、変換の段 (session_set_path /
     * setup_create) が失敗を返し、hostdrv_create はハイパーコールしない */
    CHECK(session_set_path("/disk.img\xc2") == VFS_ERR_INVAL);
    CHECK(session_set_path("/\xc1\xa4isk.img") == VFS_ERR_INVAL);
    CHECK(session_set_path("/\xf0\x9f\x98\x80.img") == VFS_ERR_INVAL);
    CHECK(setup_create("/a\x80", NP2_FILE_OPEN, 0, 0) == VFS_ERR_INVAL);
    CHECK(session_set_path("/ok") == VFS_OK);
    /* 規則は通るが ntpath (260) に収まらない名前: 切り詰めずに断り、
     * ホストへ何も送らない (以前は kstrncpy が黙って落とした) */
    {
        static char longp[300];
        for (i = 0; i < 299; i++) longp[i] = (i % 9 == 0) ? '/' : 'a';
        longp[299] = '\0';
        t_reset();
        CHECK(hdrv_stat((void *)0, longp, &st) == VFS_ERR_NAMETOOLONG);
        CHECK(hdrv_unlink((void *)0, longp) == VFS_ERR_NAMETOOLONG);
        CHECK(hdrv_rename((void *)0, "/a", longp) == VFS_ERR_NAMETOOLONG);
        CHECK(t_create_calls == 0 && t_setinfo_calls == 0);
    }

    /* 正当な日本語 (3 バイトの UTF-8) は正しい UTF-16 でホストへ届く */
    t_reset();
    CHECK(hdrv_stat((void *)0, "/\xe6\x97\xa5\xe6\x9c\xac.txt", &st) == 0);
    CHECK(t_create_calls >= 1);
    CHECK(g_fobj.FileName.Length == 14);
    for (i = 0; i < sizeof(want) / sizeof(want[0]); i++)
        CHECK(g_namebuf[i] == want[i]);
    t_reset();
    CHECK(hdrv_rename((void *)0, "/a", "/\xe8\xaa\x9e.txt") == 0);
    CHECK(t_create_calls == 1 && t_setinfo_calls == 1);
}

static void run(void)
{
    report("=== 票 B8 / P1-4: HostDrv の実物を通す ===\n");
    case_get_file_size_directory();
    case_open_failure_not_notfound();
    case_other_entries();
    case_no_truncate_on_open_failure();
    case_status_table();
    case_strict_names();

    report("\n");
    report_i(g_checks);
    report(" checks, ");
    report_i(g_failures);
    report(" failures\n");
    g_exit_code = g_failures ? 1 : 0;
}

__asm__(
    ".text\n"
    ".globl _start\n"
    "_start:\n"
    "    xor %ebp, %ebp\n"
    "    and $-16, %esp\n"
    "    call b8_hostdrv_main\n"
    "    hlt\n");

void b8_hostdrv_main(void);
void b8_hostdrv_main(void)
{
    run();
    die(g_exit_code);
}
