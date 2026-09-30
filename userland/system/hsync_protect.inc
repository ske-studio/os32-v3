/* ========================================================================
 *  hsync_protect.inc — hsync が /etc/settings.db* を壊さないための字句判定
 *
 *  票: docs/archive/settings/TASK_S0.md §2 (S0-D)、契約は S0_FOUNDATION.md §5 (D0)
 *  ホスト試験: tools/tests/hsync_protect_host.c / tools/tests/test_hsync_protect.py
 *
 *  hsync は HostDrv (/host = C:\os32) の中身を NHD の / へ O_CREAT|O_TRUNC で
 *  写す。HostDrv に古い etc/settings.db が 1 つ残っているだけで、ゲストが書いた
 *  設定 DB が切り詰められる。ここは**コピー先の文字列**を判定するだけの純関数で、
 *  KernelAPI にも FS にも触らない (だからホストでそのまま走らせて試験できる)。
 *
 *  判定は 2 段のうちの「名前規則」。字句正規化 (`./` `..` 連続 `/`、-f の subdir
 *  連結) をしてから、親が /etc で basename が保護対象名なら真 (大文字小文字は
 *  区別しない)。実体規則 (inode 比較) は sys_stat が要るので hsync.c 側にある。
 *
 *  [C1] C89 / GNU89。宣言はブロック先頭、`//` コメント無し。
 * ======================================================================== */

#ifndef HSP_MAX_PATH
#define HSP_MAX_PATH 512          /* OS32_MAX_PATH (256) より広く取る */
#endif
#define HSP_MAX_DEPTH 32

/* 保護対象のベース名。末尾は番兵。
 *
 * `.bak` 以降はリカバリ (`install --recover-settings` / `--revert-settings`)
 * が /etc に作る 7 名 (票 docs/archive/settings/TASK_S3.md §1b の 9 名から本体
 * `settings.db` と `settings.db-journal` を除いたもの、S3-D / 往復 3 の B5)。
 * `.bak` + `.bak-journal` は切替前の元の対の**唯一の写し**、`.recover-state`
 * は phase の印なので、通常同期が 1 つでも掴むと「元へ戻す」経路そのものが
 * 消える。`.new*` / `.failed*` は他人の生成物で、hsync が作ることも消すことも
 * しない。表の件数は hsync.c の HS_MAX_PROT (現在 24、実在名の収集上限) に
 * 余裕を残すこと — 大文字小文字違いの別名の分だけ実在名は表より増える。 */
static const char *hsp_protected_names[] = {
    "settings.db",
    "settings.db-journal",
    "settings.db-wal",
    "settings.db-shm",
    "settings.db.bak",
    "settings.db.bak-journal",
    "settings.db.failed",
    "settings.db.failed-journal",
    "settings.db.new",
    "settings.db.new-journal",
    "settings.db.recover-state",
    0
};

static int hsp_lower(int c)
{
    if (c >= 'A' && c <= 'Z') return c + ('a' - 'A');
    return c;
}

/* 大文字小文字を無視した完全一致 */
static int hsp_ieq(const char *a, const char *b)
{
    while (*a && *b) {
        if (hsp_lower((unsigned char)*a) != hsp_lower((unsigned char)*b))
            return 0;
        a++;
        b++;
    }
    return *a == '\0' && *b == '\0';
}

/* パスに '\' が混じっているか (1 = 混じっている)。
 *
 * OS32 の区切りは '/' だけなので、ここでは '\' は**普通の 1 文字**として
 * 要素名に収まる。ところが HostDrv の `session_set_path` (fs/hostdrvfs.c) は
 * '/' を '\' へ直したうえで他の文字をそのまま通すので、`/host/..\other` は
 * ホスト側で `C:\os32\..\other` になる。NP21/W の `hostdrvNT_getHostPath` は
 * `PathCanonicalizeW` で '..' を畳んだあと**境界を見ない前方一致**でルート検査を
 * するため、`C:\os32-other` が `C:\os32` に前方一致して通ってしまう
 * (Codex 実装レビュー B1、PM が np21w のソースで確認)。
 * つまり '\' を含む要素は OS32 側の '..' 脱出検査を素通りしたうえで
 * ホスト側の同期元の外へ出られる。hsync は '\' を含むパスを一切扱わない。 */
static int hsp_has_backslash(const char *s)
{
    int i;

    if (!s) return 0;
    for (i = 0; s[i]; i++)
        if (s[i] == '\\') return 1;
    return 0;
}

/* 正規形のパス ("/a/b" 形式、末尾 '/' 無し) の要素数。"/" と "" は 0。
 *
 * `hsp_normalize` の HSP_MAX_DEPTH は**この関数を通す前**の入力に掛かる上限で、
 * `/host` を前置したあとの深さは別に見る必要がある。fs/vfs.c の正規化は
 * VFS_MAX_PATH_DEPTH を越えた要素を**黙って捨てる** (エラーを返さない) ので、
 * 越えたパスは「指定した親ディレクトリ」を指す別のパスに化ける
 * (Codex 実装レビュー B2)。呼び手は必ず上限と突き合わせること。 */
static int hsp_depth(const char *norm)
{
    int n = 0;
    int i = 0;

    if (!norm) return 0;
    while (norm[i]) {
        if (norm[i] == '/' && norm[i + 1] != '\0' && norm[i + 1] != '/') n++;
        i++;
    }
    return n;
}

/* 字句正規化。out には先頭 '/' 付きの正規パスが入る。
 * 戻り値 1 = 成功、0 = 失敗 (溢れ / '..' が root を越えた / '\' を含む)。 */
static int hsp_normalize(const char *in, char *out, int cap)
{
    int starts[HSP_MAX_DEPTH];
    int depth = 0;
    int len = 0;
    int i = 0;

    if (!in || !out || cap < 2) return 0;
    /* '\' は OS32 では区切りでないので `..\x` が 1 要素として通ってしまう。
     * ホスト側では区切りに化けるので、ここで丸ごと断る (B1)。 */
    if (hsp_has_backslash(in)) return 0;
    out[0] = '\0';

    while (in[i]) {
        int seg_begin;
        int seg_len;
        int k;

        while (in[i] == '/') i++;
        if (!in[i]) break;

        seg_begin = i;
        while (in[i] && in[i] != '/') i++;
        seg_len = i - seg_begin;

        if (seg_len == 1 && in[seg_begin] == '.') continue;
        if (seg_len == 2 && in[seg_begin] == '.' && in[seg_begin + 1] == '.') {
            if (depth == 0) return 0;       /* root の外へ出る */
            depth--;
            len = starts[depth];
            out[len] = '\0';
            continue;
        }
        if (depth >= HSP_MAX_DEPTH) return 0;
        if (len + 1 + seg_len + 1 > cap) return 0;

        starts[depth] = len;
        depth++;
        out[len++] = '/';
        for (k = 0; k < seg_len; k++) out[len++] = in[seg_begin + k];
        out[len] = '\0';
    }

    if (len == 0) {
        out[0] = '/';
        out[1] = '\0';
    }
    return 1;
}

/* ベース名 1 個が保護対象名かを大文字小文字を無視して見る。
 * `/etc` を列挙して実在名を拾うときの一致判定 (往復 1 の B5) — 小文字の表と
 * 突き合わせるだけでは `/etc/SETTINGS.DB` を取りこぼす。 */
static int hsp_is_protected_basename(const char *name)
{
    int i;

    if (!name) return 0;
    for (i = 0; hsp_protected_names[i]; i++)
        if (hsp_ieq(name, hsp_protected_names[i])) return 1;
    return 0;
}

/* 正規化済みパスに名前規則を当てる (親 /etc + 保護対象名) */
static int hsp_name_protected(const char *norm)
{
    char dir[HSP_MAX_PATH];
    const char *base;
    int last = -1;
    int i = 0;

    while (norm[i]) {
        if (norm[i] == '/') last = i;
        i++;
    }
    if (last < 0) return 0;
    base = norm + last + 1;

    if (last == 0) {
        dir[0] = '/';
        dir[1] = '\0';
    } else {
        if (last >= HSP_MAX_PATH) return 0;
        for (i = 0; i < last; i++) dir[i] = norm[i];
        dir[last] = '\0';
    }
    if (!hsp_ieq(dir, "/etc")) return 0;

    return hsp_is_protected_basename(base);
}

/* 最終行のラベル。失敗があるのに "Done:" と出すと、終了コードを見ない目には
 * 成功に読める ("失敗時に成功表示へ進めない"、FOUNDATION §5)。
 * 件数はそのまま出すので、変えるのは頭の 1 語だけ。 */
static const char *hsp_final_label(int errors)
{
    return errors ? "FAILED:" : "Done:";
}

/* 名前が cap バイト (NUL 込み) のバッファに収まるか。
 * 戻り値 1 = 収まる / 0 = 収まらない。
 * 切り詰めた名前で写すと**別のファイル**を作って「成功」と出るので、
 * 呼び手は 0 のときエラーにすること (往復 3 の D6)。 */
static int hsp_name_fits(const char *name, int cap)
{
    int i = 0;

    if (!name || cap < 1) return 0;
    while (name[i]) {
        i++;
        if (i > cap - 1) return 0;
    }
    return 1;
}

/* 正規化済みパスの**祖先**に保護対象が居るか (path 自身は見ない)。
 * `/etc/settings.db/sub` のような宛先は、最終要素だけ見ても素通りする
 * (親が `/etc/settings.db` なので名前規則に当たらない、往復 2 の 2)。 */
static int hsp_ancestor_protected(const char *norm)
{
    char prefix[HSP_MAX_PATH];
    int i;

    for (i = 1; norm[i]; i++) {
        if (norm[i] != '/') continue;
        if (i >= HSP_MAX_PATH) return 1;    /* 判定できない = 保護側 */
        {
            int k;
            for (k = 0; k < i; k++) prefix[k] = norm[k];
            prefix[i] = '\0';
        }
        if (hsp_name_protected(prefix)) return 1;
    }
    return 0;
}

/* 入口: 任意のコピー先 / mkdir 先の文字列を 3 通りに分ける。
 *    1 = 保護対象 (自身または祖先が /etc/settings.db*)
 *    0 = 対象外
 *   -1 = **判定できない** (正規化に失敗 — 異常に長い / root を越える / '\' 混入)
 *
 * 判定できないものを「保護」に畳むと、`PROTECTED` という語が
 * 「守った」と「読めなかった」の両方を指すことになり、何も同期していないのに
 * `errors=0` で終わる経路ができる (Codex 実装レビュー B2)。
 * 書かないという扱いは同じでも、呼び手が**エラーとして数えられる**ように
 * 区別して返す。 */
static int hsp_path_classify(const char *path)
{
    char norm[HSP_MAX_PATH];

    if (!hsp_normalize(path, norm, (int)sizeof(norm))) return -1;
    if (hsp_name_protected(norm)) return 1;
    return hsp_ancestor_protected(norm) ? 1 : 0;
}

/* 真偽だけで足りる呼び手は `hsp_path_classify(path) != 0` と書く
 * (判定できないものは**保護側**に倒す fail-closed)。以前あった
 * `hsp_path_protected()` の包みは、hsync 本体が 3 値を使うようになって
 * 誰も呼ばなくなったのでここから外した (Codex 実装レビュー B2)。
 * 試験側の読みやすさのための包みは tools/tests/hsync_protect_host.c にある。 */
