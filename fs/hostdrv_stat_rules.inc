/* ========================================================================
 *  hostdrv_stat_rules.inc — hdrv_stat() の「成功と言ってよいか」の純規則
 *
 *  票: H1 (docs/tasks/shell/HSYNC_IMPROVEMENT_PLAN.md §2 / §4.3)
 *  ホスト試験: tools/tests/hsync_h1_host.c (A11)
 *
 *  直した点:
 *    1. Basic / Standard 情報の問い合わせに失敗しても最後に成功を返す経路が
 *       あった。取れなかった値は kmemset 由来のゼロのままなので、
 *       **実在する空ファイル / 種別不明** と見分けが付かず、hsync の同一判定
 *       (サイズ 0 同士) をすり抜ける。取れなければ I/O エラーを返す。
 *    2. NT の EndOfFile は 64bit。u32 の st_size へ**黙って切り詰めない**。
 *       4GiB を越えるものは表現できないと言って断る (設計書 §4.3)。
 *
 *  票 H3 で LastWriteTime -> st_mtime の変換を足した (下の
 *  hdrv_filetime_to_unix)。**判定は純関数のまま**なので、境界 (1970 年より
 *  前 / 未提供 / os_time_t の範囲外) はホストで直接叩いて確かめられる
 *  (tools/tests/hsync_h3_host.c の A16)。
 *
 *  ハイパーコールにもカーネルの他のヘッダにも触らない純関数なので、
 *  ホストでそのまま走らせて試験できる。
 *
 *  [C1] C89 / GNU89。宣言はブロック先頭、`//` コメント無し。
 * ======================================================================== */

#ifndef FS_HOSTDRV_STAT_RULES_INC
#define FS_HOSTDRV_STAT_RULES_INC

/* FILE_ATTRIBUTE_DIRECTORY。hostdrvfs.c 側で NP2_FILE_ATTRIBUTE_DIRECTORY
 * と一致することを STATIC_ASSERT で固定している ([C4])。 */
#ifndef HDRV_STAT_ATTR_DIRECTORY
#define HDRV_STAT_ATTR_DIRECTORY 0x00000010UL
#endif

/* st_size (u32) で表せる上限。これを越える EndOfFile は切り詰めずに断る。 */
#ifndef HDRV_STAT_MAX_SIZE
#define HDRV_STAT_MAX_SIZE 0xFFFFFFFFULL
#endif

/* ---- FILETIME -> Unix 秒 (票 H3 / 設計書 §5.1) -------------------------
 *
 * NT 系 FILETIME は **1601-01-01 00:00:00 UTC 起点・100ns 単位**の 64bit。
 * Unix 秒は 1970-01-01 00:00:00 UTC 起点。差は 369 年 = 134774 日 =
 * 11644473600 秒 = 116444736000000000 (100ns)。
 * 根拠: https://learn.microsoft.com/en-us/windows/win32/api/minwinbase/ns-minwinbase-filetime
 *
 * **UTC のまま返す。JST の 9 時間を足さない** — ローカル時刻化は表示側の仕事。
 */
#ifndef HDRV_FT_EPOCH_DIFF_100NS
#define HDRV_FT_EPOCH_DIFF_100NS 116444736000000000ULL
#endif
#ifndef HDRV_FT_PER_SEC
#define HDRV_FT_PER_SEC 10000000ULL
#endif
/* os_time_t は u32 (sdk/include/os32/os32_kapi_shared.h)。これを越える秒は
 * 回り込むので、切り詰めずに「判定できない」にする。 */
#ifndef HDRV_STAT_MAX_TIME
#define HDRV_STAT_MAX_TIME 0xFFFFFFFFULL
#endif

/* この .inc は「hdrv_stat の純規則」の置き場で、取り込む側が全部を使うとは
 * 限らない (票 H1 のホスト試験は時刻の規則を呼ばない)。-Wunused-function で
 * 落とさないよう印を付ける。GNU89 なので attribute は使える ([C1])。 */
#ifndef HDRV_STAT_MAYBE_UNUSED
#if defined(__GNUC__)
#define HDRV_STAT_MAYBE_UNUSED __attribute__((unused))
#else
#define HDRV_STAT_MAYBE_UNUSED
#endif
#endif

/* FILETIME を Unix 秒へ。**変換も比較も 64bit 中間値で行う** (u32 へ落として
 * から比べると回り込む)。秒未満は切り捨てる。
 *
 *   戻り値 1 … 変換できた (*out に秒)
 *   戻り値 0 … 判定できない (*out = 0)。次の 3 つ:
 *                (a) 未提供値 (0)         — ホストが値を返さなかった
 *                (b) 1970-01-01 より前   — os_time_t (符号なし) で表せない
 *                (c) os_time_t の範囲外  — 2106 年以降
 *
 * **ちょうど 1970-01-01 00:00:00 UTC は 1 を返して *out = 0 になる。**
 * 現行 ABI に時刻の有効性ビットが無いので、呼び手 (hsync) から見ると
 * 未提供と区別が付かない = どちらも「同一判定の根拠に使えない」。
 * 区別は将来の拡張 stat で定義する (設計書 §10 の未確定 (2))。 */
static HDRV_STAT_MAYBE_UNUSED int
hdrv_filetime_to_unix(unsigned long long ft, u32 *out)
{
    unsigned long long secs;

    if (!out) return 0;
    *out = 0;
    if (ft == 0ULL) return 0;                        /* (a) 未提供 */
    if (ft < HDRV_FT_EPOCH_DIFF_100NS) return 0;     /* (b) 1970 年より前 */
    secs = (ft - HDRV_FT_EPOCH_DIFF_100NS) / HDRV_FT_PER_SEC;
    if (secs > HDRV_STAT_MAX_TIME) return 0;         /* (c) 範囲外 */
    *out = (u32)secs;
    return 1;
}

/* 問い合わせ結果から OS32_Stat を組む。
 *
 *   basic_rc / std_rc … hostdrv_query_info() の戻り値 (0 = 成功)
 *   attributes        … Np2FileBasicInfo.FileAttributes
 *   end_of_file       … Np2FileStandardInfo.EndOfFile (64bit のまま渡す)
 *
 * 戻り値 0 = 成功 (buf を埋めた) / OS32_ERR_IO = 成功と言えない。
 * buf のゼロ初期化は呼び手の責任 (失敗時は buf を信用しないこと)。 */
static HDRV_STAT_MAYBE_UNUSED int
hdrv_stat_fill(int basic_rc, u32 attributes,
               int std_rc, unsigned long long end_of_file,
               OS32_Stat *buf)
{
    if (!buf) return OS32_ERR_IO;

    /* 種別が分からない。ゼロの st_mode を「通常ファイル」と読ませない */
    if (basic_rc != 0) return OS32_ERR_IO;
    /* サイズが分からない。ゼロを「空ファイル」と読ませない */
    if (std_rc != 0) return OS32_ERR_IO;
    /* 64bit -> u32 の切り詰めは検出して断る (設計書 §4.3) */
    if (end_of_file > HDRV_STAT_MAX_SIZE) return OS32_ERR_IO;

    if (attributes & HDRV_STAT_ATTR_DIRECTORY) {
        buf->st_mode = (u16)(OS_S_IFDIR | 0755);
    } else {
        buf->st_mode = (u16)(OS_S_IFREG | 0644);
    }
    buf->st_size = (u32)end_of_file;
    return 0;
}

/* hdrv_get_file_size() の「サイズと名乗ってよいか」の純規則 (票 B8 の ③)。
 *
 *   std_rc       … FileStandardInformation の hostdrv_query_info() の戻り値
 *                  (0 = 成功)
 *   is_directory … Np2FileStandardInfo.Directory (0 / 非 0)
 *
 * 戻り値 0 = サイズを返してよい / OS32_ERR_ISDIR = ディレクトリ /
 *        OS32_ERR_IO = 成功と言えない。
 *
 * **ディレクトリに答えない**のが肝。hdrv_get_file_size は
 * NON_DIRECTORY_FILE を付けずに開くのでディレクトリでも成功し、NT は
 * ディレクトリにもサイズを返す。以前はそれを VFS_OK で返していたので、
 * 種別が分からなくなった経路で「サイズが取れた = 通常ファイル」の根拠に
 * 使われ、`cat <ディレクトリ>` が通っていた。
 *
 * 種別は **FileStandardInformation の Directory** で見る。hdrv_stat と違って
 * FileBasicInformation を引かないのは、**問い合わせを増やさない**ため —
 * get_file_size は open のたびに走り、hsync は何千回も呼ぶ。NP21/W 側は
 * どちらも同じ GetFileAttributesEx から埋めており、失敗したときは
 * 結果そのものがエラーになる (std_rc != 0) ので取りこぼさない
 * (np21w-src/src/generic/hostdrvnt.c の IRP_MJ_QUERY_INFORMATION)。 */
static HDRV_STAT_MAYBE_UNUSED int
hdrv_size_result(int std_rc, int is_directory)
{
    if (std_rc != 0) return OS32_ERR_IO;        /* 種別もサイズも分からない */
    if (is_directory) return OS32_ERR_ISDIR;
    return 0;
}

/* ---- IRP_MJ_CREATE の NTSTATUS -> VFS エラー (票 B8 / P1-4) -------------
 *
 * 以前は hostdrv_create() の失敗を呼び手が**全部 VFS_ERR_NOTFOUND に畳んで**
 * いた。そのため「実在する通常ファイルだが、サイズ取得のための OPEN だけが
 * 一度失敗した」場合に open の O_CREAT 経路が走り、hdrv_write_file の
 * NP2_FILE_OVERWRITE_IF が**既存ファイルを切り詰めていた**。
 *
 * NP21/W の IRP_MJ_CREATE が返し得る状態は
 * `np21w-src/src/generic/hostdrvnt.c` の hostdrvNT_IRP_MJ_CREATE にある。
 * **「無い」を意味するのは 2 つだけ**:
 *   STATUS_OBJECT_NAME_NOT_FOUND … 名前が無い
 *   STATUS_OBJECT_PATH_NOT_FOUND … 途中のディレクトリが無い
 * 同じ関数はほかに OBJECT_NAME_INVALID (無効な文字。NOT_FOUND を返すと
 * ワイルドカード copy が壊れるので別にしてある、と NP21/W 側にコメントあり) /
 * OBJECT_NAME_COLLISION / FILE_IS_A_DIRECTORY / NOT_A_DIRECTORY /
 * SHARING_VIOLATION / ACCESS_DENIED / MEDIA_WRITE_PROTECTED /
 * TOO_MANY_OPENED_FILES / CANNOT_DELETE / DIRECTORY_NOT_EMPTY /
 * INVALID_PARAMETER を返す。**これらは「無い」ではない。**
 *
 * エミュレータが応答しなかったとき (番兵 0xDEADBEEF) も既定の IO に落ちる。
 * **既定が OS32_ERR_IO であること**が安全性の要 — 知らない状態を
 * 「無い」と言わない。 */
#ifndef HDRV_NT_SUCCESS
#define HDRV_NT_SUCCESS                 0x00000000UL
#define HDRV_NT_OBJECT_NAME_INVALID     0xC0000033UL
#define HDRV_NT_OBJECT_NAME_NOT_FOUND   0xC0000034UL
#define HDRV_NT_OBJECT_NAME_COLLISION   0xC0000035UL
#define HDRV_NT_OBJECT_PATH_NOT_FOUND   0xC000003AUL
#define HDRV_NT_FILE_IS_A_DIRECTORY     0xC00000BAUL
#define HDRV_NT_NOT_A_DIRECTORY         0xC0000103UL
#define HDRV_NT_DIRECTORY_NOT_EMPTY     0xC0000101UL
#endif

static HDRV_STAT_MAYBE_UNUSED int
hdrv_create_status_to_vfs(unsigned long status)
{
    if (status == HDRV_NT_SUCCESS) return 0;

    /* **ここだけが「無い」** */
    if (status == HDRV_NT_OBJECT_NAME_NOT_FOUND ||
        status == HDRV_NT_OBJECT_PATH_NOT_FOUND) return OS32_ERR_NOTFOUND;

    /* 意味がはっきりしているものはその意味で返す */
    if (status == HDRV_NT_OBJECT_NAME_COLLISION) return OS32_ERR_EXIST;
    if (status == HDRV_NT_FILE_IS_A_DIRECTORY)   return OS32_ERR_ISDIR;
    if (status == HDRV_NT_NOT_A_DIRECTORY)       return OS32_ERR_NOTDIR;
    if (status == HDRV_NT_DIRECTORY_NOT_EMPTY)   return OS32_ERR_NOTEMPTY;
    if (status == HDRV_NT_OBJECT_NAME_INVALID)   return OS32_ERR_INVAL;

    /* 知らない失敗は**必ず I/O エラー**。「無い」に落とさない。
     * 応答が無かったとき (番兵) もここに来る。 */
    return OS32_ERR_IO;
}

/* Basic 情報から st_mtime を決める (票 H3)。
 * Basic が取れていなければ 0 = 不明。`atime` / `ctime` はここでは埋めない —
 * 同一判定に使わないし、Windows の `CreationTime` は inode の状態変更時刻
 * (`ctime`) ではないので、そのまま入れると意味の違うものを載せることになる
 * (設計書 §5.1)。 */
static HDRV_STAT_MAYBE_UNUSED u32
hdrv_stat_mtime(int basic_rc, unsigned long long last_write_time)
{
    u32 secs = 0;

    if (basic_rc != 0) return 0;
    (void)hdrv_filetime_to_unix(last_write_time, &secs);
    return secs;
}

#endif /* FS_HOSTDRV_STAT_RULES_INC */
