/* ======================================================================== */
/*  HOSTDRVFS.C — NP21/W HostDrv(NT) VFSドライバ v2                       */
/*                                                                          */
/*  v2改修: session_begin() / setup_xxx() 方式に全面書き換え。              */
/*  NTゲストドライバ (hostdrv.c) の動作を忠実に模倣し、                     */
/*  invoke_clear() によるFileObject/FsContext破壊を解消。                   */
/*                                                                          */
/*  原則:                                                                   */
/*    - session_begin() は CREATE前に1回だけ呼ぶ (全バッファ初期化)         */
/*    - 後続IRPでは g_stack のみ全クリア+再設定                             */
/*    - g_fobj, g_fsctx, g_invoke は CREATE以降触らない                     */
/*    - g_stack.fileObject は毎回再設定                                     */
/*                                                                          */
/*  Phase 1: 読み取り専用 (list_dir, read_file, stat, read_stream)          */
/*                                                                          */
/*  参考: NP21/W generic/hostdrvnt.c, generic/hostdrvntdef.h               */
/*        np2tool/hostdrvnt/hostdrv.c (NTゲストドライバ)                    */
/* ======================================================================== */

#include "hostdrvfs.h"
#include "hostdrvfs_proto.h"
#include "io.h"
#include "kmalloc.h"
#include "kstring.h"
#include "kutf16.h"
#include "kprintf.h"

/* 純規則 (ハイパーコールを叩かないので**ホストでそのまま試験できる**)。
 *   hdrv_stat_fill / hdrv_stat_mtime  … stat の成否判定 (票 H1 / H3)
 *   hdrv_size_result                  … get_file_size の成否判定 (票 B8 ③)
 *   hdrv_create_status_to_vfs         … CREATE の NTSTATUS 変換 (票 B8 P1-4)
 * hostdrv_create() が使うので、**この位置で**取り込む。 */
#include "hostdrv_stat_rules.inc"
STATIC_ASSERT(HDRV_STAT_ATTR_DIRECTORY == NP2_FILE_ATTRIBUTE_DIRECTORY,
              hdrv_stat_attr_dir);

/* 名前の入口検査 (TASK_VFS_FD_PATH 実装レビュー ラリー 2)。Win32 は '\' を
 * 区切りと読み、名前の**末尾の**空白と '.' を落とす ("disk.img " も
 * "disk.img." も "disk.img")。VFS の BUSY / pinned は 1 バイトの fold で名前を
 * 比べるので、この綴りは使用中の実体を別の綴りで消せる。INVAL で断る。
 * **途中の空白は Windows の名前として正当なので通す** — FAT (fs/fatfs_vfs.c)
 * は FatFs が途中の空白で名前を打ち切るので途中も断る。この差が規則の
 * 引数 (VFS_NAME_RULE_WIN32 / _FAT)。規則は fs/vfs_name_rules.inc。 */
#include "vfs_name_rules.inc"

/* ===================================================================== */
/*  内部定数                                                              */
/* ===================================================================== */
#define HOSTDRV_DATA_BUF_SIZE  4096
#define HOSTDRV_NAME_BUF_WORDS  260   /* MAX_PATH */
#define HOSTDRV_MAX_DIR_ENTRIES 1000  /* 列挙上限 (無限ループ防止) */

/* エミュレータ未応答検出用の番兵値 */
#define NP2_STATUS_SENTINEL  0xDEADBEEFUL

/* ===================================================================== */
/*  コンテキスト構造体 (静的グローバル — ページ境界問題を回避)             */
/* ===================================================================== */

/* 通信バッファ群 — エミュレータが直接読み書きするため volatile 必須 */
static volatile Np2InvokeInfo      g_invoke  __attribute__((aligned(4)));
static volatile Np2IoStackLocation g_stack   __attribute__((aligned(4)));
static volatile Np2FileObject      g_fobj    __attribute__((aligned(4)));
static volatile Np2FsContext       g_fsctx   __attribute__((aligned(4)));
static volatile Np2IoStatusBlock   g_iostatus __attribute__((aligned(4)));
static volatile Np2IoSecurityContext g_secctx __attribute__((aligned(4)));
static volatile u16  g_namebuf[HOSTDRV_NAME_BUF_WORDS] __attribute__((aligned(4)));
static volatile u8   g_databuf[HOSTDRV_DATA_BUF_SIZE]  __attribute__((aligned(4)));

/* SectionObjectPointers ダミー (12バイト: DataSectionObject, SharedCacheMap, ImageSectionObject) */
static u32 g_sop[3] __attribute__((aligned(4)));

/* マウント状態 */
static int g_mounted = 0;

/* ===================================================================== */
/*  低レベルI/Oポート通信                                                  */
/* ===================================================================== */

/* ホストドライブ検出: エミュレータ対応なら1 */
int hostdrvfs_detect(void)
{
    int a, c;
    a = inp(HOSTDRV_IO_ADDR);
    c = inp(HOSTDRV_IO_CMD);
    if (a == HOSTDRV_DETECT_ADDR_VAL && c == HOSTDRV_DETECT_CMD_VAL) {
        /* エミュレータ側の状態をリセット (0x00000000 + HDR9801) */
        outp(HOSTDRV_IO_ADDR, 0);
        outp(HOSTDRV_IO_ADDR, 0);
        outp(HOSTDRV_IO_ADDR, 0);
        outp(HOSTDRV_IO_ADDR, 0);
        outp(HOSTDRV_IO_CMD, 'H');
        outp(HOSTDRV_IO_CMD, 'D');
        outp(HOSTDRV_IO_CMD, 'R');
        outp(HOSTDRV_IO_CMD, '9');
        outp(HOSTDRV_IO_CMD, '8');
        outp(HOSTDRV_IO_CMD, '0');
        outp(HOSTDRV_IO_CMD, '1');
        return 1;
    }
    return 0;
}

/* ハイパーバイザーコール: invoke_info のアドレスを送信し処理実行 */
static void hostdrv_hypercall(void)
{
    u32 addr = (u32)&g_invoke;
    outp(HOSTDRV_IO_ADDR, (u8)(addr));
    outp(HOSTDRV_IO_ADDR, (u8)(addr >> 8));
    outp(HOSTDRV_IO_ADDR, (u8)(addr >> 16));
    outp(HOSTDRV_IO_ADDR, (u8)(addr >> 24));
    outp(HOSTDRV_IO_CMD, 'H');
    outp(HOSTDRV_IO_CMD, 'D');
    outp(HOSTDRV_IO_CMD, 'R');
    outp(HOSTDRV_IO_CMD, '9');
    outp(HOSTDRV_IO_CMD, '8');
    outp(HOSTDRV_IO_CMD, '0');
    outp(HOSTDRV_IO_CMD, '1');  /* 最後のバイトで同期実行 */
}

/* ===================================================================== */
/*  v2: セッション管理 (invoke_clear() の代替)                             */
/* ===================================================================== */

/* セッション開始: CREATE前に1回だけ呼ぶ。全バッファを初期化する。
 *
 * NTドライバとの差異分析に基づき、以下を正しく設定:
 *   - deviceFlags = 0x30 (DO_BUFFERED_IO | FILE_DEVICE_IS_MOUNTED)
 *   - sectionObjectPointerAddr = ダミーSOPバッファ
 *   - g_fobj.SectionObjectPointer = ダミーSOPバッファ
 *   - g_fobj.Flags = NP2_FO_SYNCHRONOUS_IO
 */
static void session_begin(void)
{
    kmemset((void*)&g_invoke, 0, sizeof(g_invoke));
    kmemset((void*)&g_stack, 0, sizeof(g_stack));
    kmemset((void*)&g_fobj, 0, sizeof(g_fobj));
    kmemset((void*)&g_fsctx, 0, sizeof(g_fsctx));
    kmemset((void*)&g_secctx, 0, sizeof(g_secctx));
    kmemset((void*)&g_iostatus, 0, sizeof(g_iostatus));
    kmemset(g_sop, 0, sizeof(g_sop));

    /* InvokeInfo 固定フィールド */
    g_invoke.stackAddr = (u32)&g_stack;
    g_invoke.statusAddr = (u32)&g_iostatus;
    g_invoke.inBufferAddr = (u32)g_databuf;
    g_invoke.outBufferAddr = (u32)g_databuf;
    g_invoke.sectionObjectPointerAddr = (u32)g_sop;
    g_invoke.deviceFlags = 0x30; /* DO_BUFFERED_IO | FILE_DEVICE_IS_MOUNTED */
    g_invoke.version = 1;

    /* FILE_OBJECT 固定フィールド */
    g_fobj.FsContext = (u32)&g_fsctx;
    g_fobj.SectionObjectPointer = (u32)g_sop;
    g_fobj.Flags = NP2_FO_SYNCHRONOUS_IO;

    /* IO_STACK_LOCATION 固定フィールド */
    g_stack.fileObject = (u32)&g_fobj;
}

/* パスをUTF-16LEに変換してg_namebufとg_fobjに設定
 * 戻り値: VFS_OK / VFS_ERR_NAMETOOLONG (ntpath に収まらない) /
 *         VFS_ERR_INVAL (正しい BMP の UTF-8 でない — 変換が別名を作らない
 *         ように、置換も切り詰めもせずに断る。実装レビュー ラリー 3 B2)。
 * 失敗したら**ホストへ何も送らない** (呼び手はハイパーコールをしない)。 */
static int session_set_path(const char *path)
{
    int words;
    char ntpath[260];
    int i;

    if (kstrlen(path) >= sizeof(ntpath)) return VFS_ERR_NAMETOOLONG;
    kstrncpy(ntpath, path, sizeof(ntpath));
    for (i = 0; ntpath[i]; i++) {
        if (ntpath[i] == '/') ntpath[i] = '\\';
    }
    /* 空パスの場合はルート "\\" */
    if (ntpath[0] == '\0') {
        ntpath[0] = '\\';
        ntpath[1] = '\0';
    }

    words = kutf8_to_utf16le(ntpath, (u16*)g_namebuf, HOSTDRV_NAME_BUF_WORDS);
    if (words < 1) {
        g_fobj.FileName.Length = 0;
        g_fobj.FileName.MaximumLength = 0;
        return VFS_ERR_INVAL;
    }
    /* UNICODE_STRING: Length はNULL終端を含まないバイト数 */
    g_fobj.FileName.Length = (u16)((words - 1) * 2);
    g_fobj.FileName.MaximumLength = (u16)(words * 2);
    g_fobj.FileName.Buffer = (u32)g_namebuf;
    return VFS_OK;
}

/* ===================================================================== */
/*  v2: IRP別セットアップ関数                                              */
/*  各関数は g_stack を全クリアして必要フィールドのみ再設定する。           */
/*  g_fobj / g_fsctx / g_invoke には一切触れない。                         */
/* ===================================================================== */

/* g_stack を全クリアして CREATE 用に設定
 * 戻り値: VFS_OK / session_set_path の失敗 (そのときはハイパーコールしない) */
static int setup_create(const char *path, u32 disposition,
                        u32 options_flags, u32 desired_access)
{
    int rc;

    kmemset((void*)&g_stack, 0, sizeof(g_stack));
    g_stack.majorFunction = NP2_IRP_MJ_CREATE;
    g_stack.fileObject = (u32)&g_fobj;  /* 再設定必須 */
    /* options: 上位8bit = disposition, 下位24bit = options */
    g_stack.parameters.create.options =
        (disposition << 24) | (options_flags & 0x00FFFFFFUL);
    g_stack.parameters.create.shareAccess = 0x07; /* 全共有 */

    /* SecurityContext: エミュレータは +8 から DesiredAccess を読む */
    kmemset((void*)&g_secctx, 0, sizeof(g_secctx));
    g_secctx.DesiredAccess = desired_access;
    g_stack.parameters.create.securityContext = (u32)&g_secctx;

    /* IOステータス: 番兵値 */
    g_iostatus.Status = NP2_STATUS_SENTINEL;
    g_iostatus.Information = 0;

    /* パス設定 */
    rc = session_set_path(path);
    return rc;
}

/* g_stack を全クリアして READ 用に再設定 */
static void setup_read(u32 length, u64 offset)
{
    kmemset((void*)&g_stack, 0, sizeof(g_stack));
    g_stack.majorFunction = NP2_IRP_MJ_READ;
    g_stack.fileObject = (u32)&g_fobj;  /* ★ 再設定必須 */
    g_stack.parameters.read.length = length;
    g_stack.parameters.read.byteOffset = offset;

    g_iostatus.Status = NP2_STATUS_SENTINEL;
    g_iostatus.Information = 0;
}

/* g_stack を全クリアして WRITE 用に再設定 */
static void setup_write(u32 length, u64 offset)
{
    kmemset((void*)&g_stack, 0, sizeof(g_stack));
    g_stack.majorFunction = NP2_IRP_MJ_WRITE;
    g_stack.fileObject = (u32)&g_fobj;
    g_stack.parameters.write.length = length;
    g_stack.parameters.write.byteOffset = offset;

    g_invoke.inBufferAddr = (u32)g_databuf;
    g_iostatus.Status = NP2_STATUS_SENTINEL;
    g_iostatus.Information = 0;
}

/* g_stack を全クリアして SET_INFORMATION 用に再設定 */
static void setup_set_info(u32 info_class, u32 buf_len)
{
    kmemset((void*)&g_stack, 0, sizeof(g_stack));
    g_stack.majorFunction = NP2_IRP_MJ_SET_INFORMATION;
    g_stack.fileObject = (u32)&g_fobj;
    g_stack.parameters.setFile.Length = buf_len;
    g_stack.parameters.setFile.FileInformationClass = info_class;

    g_invoke.inBufferAddr = (u32)g_databuf;
    g_iostatus.Status = NP2_STATUS_SENTINEL;
    g_iostatus.Information = 0;
}

/* g_stack を全クリアして QUERY_INFORMATION 用に再設定 */
static void setup_query_info(u32 info_class, u32 buf_len)
{
    kmemset((void*)&g_stack, 0, sizeof(g_stack));
    g_stack.majorFunction = NP2_IRP_MJ_QUERY_INFORMATION;
    g_stack.fileObject = (u32)&g_fobj;
    g_stack.parameters.queryFile.Length = buf_len;
    g_stack.parameters.queryFile.FileInformationClass = info_class;

    g_invoke.outBufferAddr = (u32)g_databuf;
    g_iostatus.Status = NP2_STATUS_SENTINEL;
    g_iostatus.Information = 0;
}

/* g_stack を全クリアして DIRECTORY_CONTROL 用に再設定 */
static void setup_query_dir(int first)
{
    kmemset((void*)&g_stack, 0, sizeof(g_stack));
    g_stack.majorFunction = NP2_IRP_MJ_DIRECTORY_CONTROL;
    g_stack.minorFunction = NP2_IRP_MN_QUERY_DIRECTORY;
    g_stack.flags = NP2_SL_RETURN_SINGLE_ENTRY;
    if (first) {
        g_stack.flags |= NP2_SL_RESTART_SCAN;
    }
    g_stack.fileObject = (u32)&g_fobj;
    g_stack.parameters.queryDirectory.Length = HOSTDRV_DATA_BUF_SIZE;
    g_stack.parameters.queryDirectory.FileName = 0;
    g_stack.parameters.queryDirectory.FileInformationClass =
        NP2_FileBothDirectoryInformation;
    g_stack.parameters.queryDirectory.FileIndex = 0;

    g_invoke.outBufferAddr = (u32)g_databuf;
    g_iostatus.Status = NP2_STATUS_SENTINEL;
    g_iostatus.Information = 0;
}

/* g_stack を全クリアして CLEANUP 用に再設定 */
static void setup_cleanup(void)
{
    kmemset((void*)&g_stack, 0, sizeof(g_stack));
    g_stack.majorFunction = NP2_IRP_MJ_CLEANUP;
    g_stack.fileObject = (u32)&g_fobj;
    g_iostatus.Status = NP2_STATUS_SENTINEL;
    g_iostatus.Information = 0;
}

/* g_stack を全クリアして CLOSE 用に再設定 */
static void setup_close(void)
{
    kmemset((void*)&g_stack, 0, sizeof(g_stack));
    g_stack.majorFunction = NP2_IRP_MJ_CLOSE;
    g_stack.fileObject = (u32)&g_fobj;
    g_iostatus.Status = NP2_STATUS_SENTINEL;
    g_iostatus.Information = 0;
}

/* ===================================================================== */
/*  IRP操作ラッパー                                                       */
/* ===================================================================== */

/* IRP_MJ_CREATE: ファイル/ディレクトリを開く
 * 戻り値: 0=成功, <0=エラー
 *
 * 注意: session_begin() を呼んだ後に使用すること。
 */
/* 戻り値: 0 = 成功 / 負値 = **その失敗に対応する VFS_ERR_***。
 *
 * 以前は失敗を一律 -1 にし、呼び手がそれを VFS_ERR_NOTFOUND に畳んでいた
 * (票 B8 / Codex 実装レビュー P1-4)。そのため「実在する通常ファイルだが
 * OPEN だけが一度失敗した」場合に open の O_CREAT 経路が走り、
 * hdrv_write_file の NP2_FILE_OVERWRITE_IF が**既存ファイルを切り詰めて**
 * いた。**「無い」と言ってよいのは NT が「無い」と言ったときだけ。**
 * 対応表と根拠 (NP21/W のどの状態がいつ返るか) は
 * fs/hostdrv_stat_rules.inc の hdrv_create_status_to_vfs にある。 */
static int hostdrv_create(const char *path, u32 disposition,
                          u32 options_flags, u32 desired_access)
{
    /* 名前を受け取る入口は rename の宛先を除いて全部ここを通る。
     * ホストへ何も送らないうちに断る (上の vfs_name_rules.inc の説明) */
    int rc = vfs_name_rule_check(path, VFS_NAME_RULE_WIN32);
    if (rc != VFS_OK) return rc;

    rc = setup_create(path, disposition, options_flags, desired_access);
    if (rc != VFS_OK) return rc;    /* ホストへは何も送っていない */
    hostdrv_hypercall();

    if (g_iostatus.Status == NP2_STATUS_SENTINEL) {
        return VFS_ERR_IO;          /* エミュレータが応答しなかった */
    }
    return hdrv_create_status_to_vfs((unsigned long)g_iostatus.Status);
}

/* IRP_MJ_READ: ファイル読み込み (チャンク分割)
 * 戻り値: 読み込んだバイト数, <0=エラー
 *
 * g_fobj / g_fsctx は session_begin+CREATE で確立済み。
 * g_stack のみ全クリア+再設定する (v2方式)。
 */
static int hostdrv_read(void *buf, u32 size, u64 offset)
{
    u32 chunk;
    u32 total = 0;

    while (total < size) {
        chunk = size - total;
        if (chunk > HOSTDRV_DATA_BUF_SIZE)
            chunk = HOSTDRV_DATA_BUF_SIZE;

        setup_read(chunk, offset + total);
        g_invoke.outBufferAddr = (u32)g_databuf;

        hostdrv_hypercall();

        if (g_iostatus.Status == NP2_STATUS_SENTINEL) {
            if (total > 0) break;
            return -1;
        }
        if (g_iostatus.Status == NP2_STATUS_END_OF_FILE) {
            break;
        }
        if (g_iostatus.Status != NP2_STATUS_SUCCESS) {
            if (total > 0) break;
            return -1;
        }
        if (g_iostatus.Information == 0) break;

        kmemcpy((void*)buf + total, (const void*)g_databuf, g_iostatus.Information);
        total += g_iostatus.Information;

        if (g_iostatus.Information < chunk) break;
    }
    return (int)total;
}

/* IRP_MJ_WRITE: ファイル書き込み (チャンク分割) */
static int hostdrv_write(const void *buf, u32 size, u64 offset)
{
    u32 chunk;
    u32 total = 0;

    while (total < size) {
        chunk = size - total;
        if (chunk > HOSTDRV_DATA_BUF_SIZE)
            chunk = HOSTDRV_DATA_BUF_SIZE;

        setup_write(chunk, offset + total);
        kmemcpy((void*)g_databuf, (const u8 *)buf + total, chunk);

        hostdrv_hypercall();

        if (g_iostatus.Status == NP2_STATUS_SENTINEL) {
            if (total > 0) break;
            return -1;
        }
        if (g_iostatus.Status != NP2_STATUS_SUCCESS) {
            if (total > 0) break;
            return -1;
        }
        if (g_iostatus.Information == 0) break;

        total += g_iostatus.Information;
        if (g_iostatus.Information < chunk) break;
    }
    return (int)total;
}

/* IRP_MJ_SET_INFORMATION */
static int hostdrv_set_info(u32 info_class, const void *data, u32 len)
{
    setup_set_info(info_class, len);
    kmemset((void*)g_databuf, 0, HOSTDRV_DATA_BUF_SIZE);
    if (data && len <= HOSTDRV_DATA_BUF_SIZE) {
        kmemcpy((void*)g_databuf, data, len);
    }

    hostdrv_hypercall();

    if (g_iostatus.Status == NP2_STATUS_SENTINEL) return -1;
    if (g_iostatus.Status != NP2_STATUS_SUCCESS) {
        return -1;
    }
    return 0;
}

/* IRP_MJ_QUERY_INFORMATION
 * 戻り値: 0=成功, <0=エラー (結果はg_databufに格納)
 */
static int hostdrv_query_info(u32 info_class, u32 buf_len)
{
    setup_query_info(info_class, buf_len);
    kmemset((void*)g_databuf, 0, buf_len);

    hostdrv_hypercall();

    if (g_iostatus.Status == NP2_STATUS_SENTINEL) return -1;
    if (g_iostatus.Status != NP2_STATUS_SUCCESS &&
        g_iostatus.Status != NP2_STATUS_BUFFER_OVERFLOW) {
        return -1;
    }
    return 0;
}

/* IRP_MJ_DIRECTORY_CONTROL (IRP_MN_QUERY_DIRECTORY)
 * 戻り値: 0=成功, 1=終了, <0=エラー
 */
static int hostdrv_query_dir(int first)
{
    setup_query_dir(first);
    kmemset((void*)g_databuf, 0, HOSTDRV_DATA_BUF_SIZE);

    hostdrv_hypercall();

    if (g_iostatus.Status == NP2_STATUS_SENTINEL) {
        return -1;
    }
    if (g_iostatus.Status == NP2_STATUS_NO_MORE_FILES ||
        g_iostatus.Status == NP2_STATUS_OBJECT_NAME_NOT_FOUND) {
        return 1;  /* 列挙終了 */
    }
    if (g_iostatus.Status != NP2_STATUS_SUCCESS) {
        return -1;
    }
    return 0;
}

/* IRP_MJ_CLEANUP + IRP_MJ_CLOSE のペア */
static void hostdrv_cleanup_close(void)
{
    setup_cleanup();
    hostdrv_hypercall();
    setup_close();
    hostdrv_hypercall();
}

/* ===================================================================== */
/*  VfsOps 実装                                                           */
/* ===================================================================== */

/* mount: ホストドライブ検出・マウント */
static void *hdrv_mount(int dev_id)
{
    (void)dev_id;

    if (!hostdrvfs_detect()) {
        return (void *)0;
    }
    g_mounted = 1;
    kprintf(0x0A, "[HDRV] HostDrv(NT) mounted on /host\n");
    return (void *)1;  /* コンテキストは静的グローバル */
}

/* umount */
static void hdrv_umount(void *ctx)
{
    (void)ctx;
    g_mounted = 0;
}

/* is_mounted */
static int hdrv_is_mounted(void *ctx)
{
    (void)ctx;
    return g_mounted;
}

/* 列挙ループと「成功と言ってよいか」は純関数へ切り出してある (票 H1)。
 * 途中で切れた列挙 / 件数上限での打ち切りを VFS_OK で返さない。
 * ホスト試験は tools/tests/hostdrv_list_host.c。 */
#include "hostdrv_list_rules.inc"

/* hdrv_list_run に渡す入れ物。ハイパーコール側の状態は g_databuf なので、
 * ここにはコールバックと、1 件ぶんの組み立てに要るものだけを置く。 */
typedef struct {
    vfs_dir_cb cb;
    void      *user_ctx;
} HdrvListCtx;

/* 1 件進める (IRP_MN_QUERY_DIRECTORY) */
static int hdrv_list_step(void *ctx, int first)
{
    (void)ctx;
    return hostdrv_query_dir(first);
}

/* 取れた 1 件を VfsDirEntry にして流す。名前が化けたもの / "." / ".." は
 * 流さないが、繰り返しは 1 回ぶん消費する (従来どおり)。 */
static void hdrv_list_emit(void *ctx)
{
    HdrvListCtx *lc = (HdrvListCtx *)ctx;
    Np2FileBothDirInfo *info;
    VfsDirEntry entry;
    char namebuf[260];
    int namelen;

    info = (Np2FileBothDirInfo *)g_databuf;

    /* ファイル名をUTF-8に変換 */
    namelen = kutf16le_to_utf8((const u16 *)info->FileName,
                               info->FileNameLength,
                               namebuf, sizeof(namebuf));
    if (namelen <= 0) return;

    /* "." と ".." はスキップ */
    if (namebuf[0] == '.' &&
        (namebuf[1] == '\0' ||
         (namebuf[1] == '.' && namebuf[2] == '\0'))) {
        return;
    }

    /* VfsDirEntryに変換 */
    kstrncpy(entry.name, namebuf, VFS_MAX_PATH);
    entry.size = (u32)info->EndOfFile;  /* 下位32bitのみ */
    entry.type = (info->FileAttributes & NP2_FILE_ATTRIBUTE_DIRECTORY)
                 ? VFS_TYPE_DIR : VFS_TYPE_FILE;

    lc->cb(&entry, lc->user_ctx);
}

/* list_dir: ディレクトリ列挙 */
static int hdrv_list_dir(void *ctx, const char *path,
                         vfs_dir_cb cb, void *user_ctx)
{
    int rc;
    HdrvListCtx lc;
    (void)ctx;

    /* セッション開始 + ディレクトリを開く */
    session_begin();
    rc = hostdrv_create(path, NP2_FILE_OPEN,
                        NP2_FILE_DIRECTORY_FILE |
                        NP2_FILE_SYNCHRONOUS_IO_NONALERT,
                        NP2_FILE_READ_DATA);
    if (rc < 0) {
        return rc;              /* **畳まない** (票 B8 / P1-4) */
    }

    lc.cb = cb;
    lc.user_ctx = user_ctx;
    rc = hdrv_list_run(&lc, hdrv_list_step, hdrv_list_emit,
                       HOSTDRV_MAX_DIR_ENTRIES);

    /* ディレクトリを閉じる。**失敗しても必ず通す** */
    hostdrv_cleanup_close();

    /* 途中で切れた列挙を「全部読めた」と言わない (票 H1) */
    return rc;
}

/* read_file: 一括ファイル読み込み */
static int hdrv_read_file(void *ctx, const char *path,
                          void *buf, u32 max_size)
{
    int rc;
    int bytes;
    (void)ctx;

    session_begin();
    rc = hostdrv_create(path, NP2_FILE_OPEN,
                        NP2_FILE_NON_DIRECTORY_FILE |
                        NP2_FILE_SYNCHRONOUS_IO_NONALERT,
                        NP2_FILE_READ_DATA);
    if (rc < 0) return rc;   /* **畳まない** (票 B8 / P1-4) */

    bytes = hostdrv_read(buf, max_size, 0);

    hostdrv_cleanup_close();

    return bytes;
}

/* get_file_size
 *
 * **ディレクトリには答えない** (票 B8 の ③)。ここは NON_DIRECTORY_FILE を
 * 付けずに開くのでディレクトリでも成功し、NT はディレクトリにもサイズを
 * 返す。以前はそれを VFS_OK で返していたので、種別が分からなくなった経路で
 * 「サイズが取れた = 通常ファイル」の根拠に使われていた。
 *
 * 種別は **今までも引いていた FileStandardInformation の Directory** で見る。
 * hdrv_stat のように FileBasicInformation を追加で引かないのは、
 * **問い合わせを増やさない**ため — get_file_size は open のたびに走り、
 * hsync は何千回も呼ぶ。NP21/W 側はどちらも同じ GetFileAttributesEx から
 * 埋めている (np21w-src/src/generic/hostdrvnt.c)。
 * 判定そのものは fs/hostdrv_stat_rules.inc の純関数 hdrv_size_result。 */
static int hdrv_get_file_size(void *ctx, const char *path, u32 *size)
{
    int rc;
    int is_dir = 0;
    Np2FileStandardInfo *info;
    (void)ctx;

    session_begin();
    rc = hostdrv_create(path, NP2_FILE_OPEN,
                        NP2_FILE_SYNCHRONOUS_IO_NONALERT,
                        NP2_FILE_READ_DATA);
    if (rc < 0) {
        return rc;              /* **畳まない** (票 B8 / P1-4) */
    }

    rc = hostdrv_query_info(NP2_FileStandardInformation,
                            sizeof(Np2FileStandardInfo));

    if (rc == 0) {
        info = (Np2FileStandardInfo *)g_databuf;
        *size = (u32)info->EndOfFile;
        is_dir = info->Directory ? 1 : 0;
    }

    hostdrv_cleanup_close();

    /* 判定は純関数へ (fs/hostdrv_stat_rules.inc)。ホストで直接叩ける
     * = 変異で落ちる試験が書ける (tools/tests/b8_open_host.c の 段 C)。
     * **問い合わせは 1 回のまま** — 種別は同じ応答の Directory で分かる。 */
    return hdrv_size_result(rc, is_dir);
}

/* read_stream: シーク対応読み込み */
static int hdrv_read_stream(void *ctx, const char *path,
                            void *buf, u32 size, u32 offset)
{
    int rc;
    int bytes;
    (void)ctx;

    session_begin();
    rc = hostdrv_create(path, NP2_FILE_OPEN,
                        NP2_FILE_NON_DIRECTORY_FILE |
                        NP2_FILE_SYNCHRONOUS_IO_NONALERT,
                        NP2_FILE_READ_DATA);
    if (rc < 0) {
        return rc;              /* **畳まない** (票 B8 / P1-4) */
    }

    bytes = hostdrv_read(buf, size, (u64)offset);

    hostdrv_cleanup_close();

    return bytes;
}

/* stat: ファイル情報取得 */
/* 「成功と言ってよいか」の判定は純関数へ切り出してある (票 H1)。
 * 失敗したまま VFS_OK を返してゼロサイズを「実在する空ファイル」に
 * 化けさせない / 64bit EndOfFile を u32 へ黙って切り詰めない。
 * ホスト試験は tools/tests/hsync_h1_host.c (A11)。
 *
 * 票 H3 で LastWriteTime -> st_mtime を足した。FILETIME (1601 起点・100ns)
 * から Unix 秒への変換も同じ .inc の純関数 (hdrv_filetime_to_unix) が持つ。
 * ホスト試験は tools/tests/hsync_h3_host.c (A16)。 */
static int hdrv_stat(void *ctx, const char *path, OS32_Stat *buf)
{
    int rc;
    int basic_rc;
    int std_rc;
    u32 attributes = 0;
    u64 last_write = 0;
    u64 end_of_file = 0;
    Np2FileBasicInfo *basic;
    Np2FileStandardInfo *std_info;
    (void)ctx;

    kmemset(buf, 0, sizeof(OS32_Stat));

    session_begin();
    rc = hostdrv_create(path, NP2_FILE_OPEN,
                        NP2_FILE_SYNCHRONOUS_IO_NONALERT,
                        NP2_FILE_READ_DATA);
    if (rc < 0) return rc;   /* **畳まない** (票 B8 / P1-4) */

    /* FileBasicInformation 取得 (種別)。g_databuf は次の問い合わせで
     * 上書きされるので、その場で値へ退避する。 */
    basic_rc = hostdrv_query_info(NP2_FileBasicInformation,
                                  sizeof(Np2FileBasicInfo));
    if (basic_rc == 0) {
        basic = (Np2FileBasicInfo *)g_databuf;
        attributes = basic->FileAttributes;
        /* **ここで値として退避する** — 次の FileStandardInformation で
         * g_databuf が丸ごと上書きされる (票 H1 と同じ作法、票 H3)。 */
        last_write = basic->LastWriteTime;
    }

    /* FileStandardInformation 取得 (サイズ)。64bit のまま判定へ渡す */
    std_rc = hostdrv_query_info(NP2_FileStandardInformation,
                                sizeof(Np2FileStandardInfo));
    if (std_rc == 0) {
        std_info = (Np2FileStandardInfo *)g_databuf;
        end_of_file = std_info->EndOfFile;
    }

    hostdrv_cleanup_close();

    rc = hdrv_stat_fill(basic_rc, attributes, std_rc,
                        (unsigned long long)end_of_file, buf);
    if (rc != 0) {
        /* 途中まで埋まった値を呼び手に読ませない */
        kmemset(buf, 0, sizeof(OS32_Stat));
        return VFS_ERR_IO;
    }
    /* 更新日時 (票 H3)。判定できない値は 0 = 不明のまま返す。
     * atime / ctime は埋めない (同一判定に使わない。Windows の CreationTime を
     * ctime へ入れない、設計書 §5.1)。 */
    buf->st_mtime = hdrv_stat_mtime(basic_rc, (unsigned long long)last_write);
    return VFS_OK;
}

/* ===================================================================== */
/*  VFS書き込み・変更系実装 (Phase 3)                                     */
/* ===================================================================== */
static int hdrv_write_file(void *ctx, const char *path,
                           const void *data, u32 size)
{
    int rc;
    int bytes;
    Np2FileEndOfFileInfo eof_info;
    (void)ctx;

    session_begin();
    rc = hostdrv_create(path, NP2_FILE_OVERWRITE_IF,
                        NP2_FILE_NON_DIRECTORY_FILE | NP2_FILE_SYNCHRONOUS_IO_NONALERT,
                        NP2_FILE_READ_DATA | NP2_FILE_WRITE_DATA);
    if (rc < 0) return rc;   /* **畳まない** (票 B8 / P1-4) */

    bytes = hostdrv_write(data, size, 0);

    /* EOF を設定して切り詰める (OVERWRITE_IFなので本来不要かもしれないが念のため) */
    eof_info.EndOfFile = (u64)((bytes >= 0) ? bytes : 0);
    hostdrv_set_info(NP2_FileEndOfFileInformation, &eof_info, sizeof(eof_info));

    hostdrv_cleanup_close();

    return bytes;
}

static int hdrv_mkdir(void *ctx, const char *path)
{
    int rc;
    (void)ctx;

    session_begin();
    rc = hostdrv_create(path, NP2_FILE_CREATE,
                        NP2_FILE_DIRECTORY_FILE | NP2_FILE_SYNCHRONOUS_IO_NONALERT,
                        NP2_FILE_READ_DATA | NP2_FILE_WRITE_DATA);
    if (rc < 0) return rc;   /* **畳まない** (票 B8 / P1-4) */

    hostdrv_cleanup_close();

    return VFS_OK;
}

static int hdrv_rmdir(void *ctx, const char *path)
{
    int rc;
    Np2FileDispositionInfo disp;
    (void)ctx;

    session_begin();
    rc = hostdrv_create(path, NP2_FILE_OPEN,
                        NP2_FILE_DIRECTORY_FILE | NP2_FILE_SYNCHRONOUS_IO_NONALERT,
                        NP2_DELETE);
    if (rc < 0) return rc;   /* **畳まない** (票 B8 / P1-4) */

    disp.DeleteFileOnClose = 1;
    rc = hostdrv_set_info(NP2_FileDispositionInformation, &disp, sizeof(disp));

    hostdrv_cleanup_close();

    return (rc == 0) ? VFS_OK : VFS_ERR_IO;
}

static int hdrv_unlink(void *ctx, const char *path)
{
    int rc;
    Np2FileDispositionInfo disp;
    (void)ctx;

    session_begin();
    rc = hostdrv_create(path, NP2_FILE_OPEN,
                        NP2_FILE_NON_DIRECTORY_FILE | NP2_FILE_SYNCHRONOUS_IO_NONALERT,
                        NP2_DELETE);
    if (rc < 0) return rc;   /* **畳まない** (票 B8 / P1-4) */

    disp.DeleteFileOnClose = 1;
    rc = hostdrv_set_info(NP2_FileDispositionInformation, &disp, sizeof(disp));

    hostdrv_cleanup_close();

    return (rc == 0) ? VFS_OK : VFS_ERR_IO;
}

static int hdrv_rename(void *ctx, const char *old_path, const char *new_path)
{
    int rc;
    Np2FileRenameInfo rename_info;
    char ntpath[260];
    int i, words;
    (void)ctx;

    /* 宛先は hostdrv_create を通らないので、元を開く**前に**ここで見る */
    rc = vfs_name_rule_check(new_path, VFS_NAME_RULE_WIN32);
    if (rc != VFS_OK) return rc;

    /* 宛先の UTF-16 化も元を開く**前に**。変換できない宛先 (不正な
     * UTF-8・BMP 外・収まらない) は置換も切り詰めもせずに断る — 別名に
     * 化けた宛先へ付け替えさせない (実装レビュー ラリー 3 B2) */
    if (kstrlen(new_path) >= sizeof(ntpath)) return VFS_ERR_NAMETOOLONG;
    kstrncpy(ntpath, new_path, sizeof(ntpath));
    for (i = 0; ntpath[i]; i++) {
        if (ntpath[i] == '/') ntpath[i] = '\\';
    }

    kmemset(&rename_info, 0, sizeof(rename_info));
    rename_info.ReplaceIfExists = 1;
    rename_info.RootDirectory = 0;
    words = kutf8_to_utf16le(ntpath, (u16*)rename_info.FileName, 260);
    if (words < 1) return VFS_ERR_INVAL;
    rename_info.FileNameLength = (u32)((words - 1) * 2);

    session_begin();
    rc = hostdrv_create(old_path, NP2_FILE_OPEN,
                        NP2_FILE_SYNCHRONOUS_IO_NONALERT,
                        NP2_DELETE);
    if (rc < 0) return rc;   /* **畳まない** (票 B8 / P1-4) */

    rc = hostdrv_set_info(NP2_FileRenameInformation, &rename_info,
                          sizeof(rename_info) - 520 + rename_info.FileNameLength);

    hostdrv_cleanup_close();

    return (rc == 0) ? VFS_OK : VFS_ERR_IO;
}

static int hdrv_write_stream(void *ctx, const char *path,
                             const void *buf, u32 size, u32 offset)
{
    int rc;
    int bytes;
    (void)ctx;

    session_begin();
    rc = hostdrv_create(path, NP2_FILE_OPEN_IF,
                        NP2_FILE_NON_DIRECTORY_FILE | NP2_FILE_SYNCHRONOUS_IO_NONALERT,
                        NP2_FILE_READ_DATA | NP2_FILE_WRITE_DATA);
    if (rc < 0) return rc;   /* **畳まない** (票 B8 / P1-4) */

    bytes = hostdrv_write(buf, size, (u64)offset);

    hostdrv_cleanup_close();

    return bytes;
}

static int hdrv_sync(void *ctx)
{ (void)ctx; return VFS_OK; }

static u32 hdrv_total_blocks(void *ctx)
{ (void)ctx; return 0; }

static u32 hdrv_free_blocks(void *ctx)
{ (void)ctx; return 0; }

static u32 hdrv_block_size(void *ctx)
{ (void)ctx; return 512; }

/* 名前の比較規則 (BUSY / pinned、Codex 実装レビュー ラリー 1 の B4)。
 * ホストの Windows は英字の大文字小文字を区別しない。ASCII だけを畳む
 * (非 ASCII の畳み方はホスト側の変換に依るので、ここでは区別したまま) */
static u8 hdrv_name_fold(u8 c)
{
    if (c >= 'a' && c <= 'z') return (u8)(c - 'a' + 'A');
    return c;
}

/* ===================================================================== */
/*  VfsOps テーブル                                                       */
/* ===================================================================== */
static VfsOps g_hostdrvfs_ops = {
    "hostdrv",
    hdrv_mount,
    hdrv_umount,
    hdrv_is_mounted,
    hdrv_list_dir,
    hdrv_mkdir,
    hdrv_rmdir,
    hdrv_read_file,
    hdrv_write_file,
    hdrv_unlink,
    hdrv_rename,
    hdrv_get_file_size,
    hdrv_read_stream,
    hdrv_write_stream,
    hdrv_sync,
    hdrv_total_blocks,
    hdrv_free_blocks,
    hdrv_block_size,
    hdrv_stat,
    /* set_mtime は持たない (票 H3)。vfs_set_mtime が OS32_ERR_NOSYS を
     * 返す = 失敗ではなく「この FS には無い」。**明示的に 0 を置く** —
     * -Wmissing-field-initializers が「書き忘れ」と区別できないため。 */
    0,
    /* create_excl (O_EXCL) も持たない (票 H2 §2-1)。**ホスト側が同時に書ける
     * FS では排他性が成り立たない**ので、持てないものは持たないと言う。
     * vfs_open が O_EXCL に OS32_ERR_NOSYS を返し、呼び手 (hsync) は
     * 直接上書きへ落ちずに replace_unsupported で断る。 */
    0,
    /* inode で動く口も持たない (票 TASK_VFS_FD_PATH)。FD はパスで動く */
    0,
    /* ホスト (Windows) の名前は大文字小文字を区別しない (BUSY / pinned の名前比較用) */
    hdrv_name_fold
};

VfsOps *hostdrvfs_get_ops(void)
{
    return &g_hostdrvfs_ops;
}

void hostdrvfs_init(void)
{
    vfs_register_fs(&g_hostdrvfs_ops);
}
