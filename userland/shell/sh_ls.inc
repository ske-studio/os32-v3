/* ========================================================================
 *  sh_ls.inc — SHELL_AS_APP の sys_ls 写し取り (Codex 往復 6 の B2)
 *
 *  main.c が #ifdef SHELL_AS_APP の中で #include する。拡張子が .c でないの
 *  は build/programs.mk の SHELL_SRC が userland/shell 下の .c を wildcard で
 *  拾うためで、sh_launch.inc / sh_pipe.inc / sh_redraw.inc と同じ理由。
 *
 *  なぜ要るか: CPL=3 のアプリが `sys_ls` の**コールバックの中から KAPI を
 *  呼ぶと落ちる** (int 0x80 の再入。実機で `find /etc -name filetypes` が
 *  [Process crashed]、コールバックで KAPI を呼ばない `du /etc` は正常)。
 *  カーネル側の対処は別票。ここでは S 側で避ける — コールバックは名前と
 *  種別を写すだけにし、`sys_ls` が戻ってから表示や mem_alloc を行う。
 *  `docs/POLICY_DEBUG.md` §4-26 (コールバックから FS を触らない) の延長。
 *
 *  写し取りは 1 本きり。`sys_ls` を呼ぶ直前に sh_ls_reset() し、戻ってから
 *  sh_ls_count_get() / sh_ls_fill() で読む — 入れ子で使わないこと。
 * ======================================================================== */

/* 1 ディレクトリぶんの上限。ui.c の TAB_MAX_MATCHES や filer の
 * FL_MAX_ENTRIES と同じ「一覧は上限つき」の作法。溢れた分は数だけ数えて
 * 呼び手が「(... N more)」を出す。 */
#define SH_LS_MAX       128
/* R3: DirEntry_Ext.name と同じ幅。128B だった頃は 127B で無言に切っていて、
 * 一覧にも glob にも**存在しない短縮名**が出た。同じ幅なら切れない。 */
#define SH_LS_NAME_MAX  OS32_MAX_PATH

typedef struct {
    char name[SH_LS_NAME_MAX];
    u32  size;
    u8   type;
} ShLsEntry;

static ShLsEntry sh_ls_buf[SH_LS_MAX];
static int sh_ls_count;      /* 写し取った数 (<= SH_LS_MAX) */
static int sh_ls_total;      /* ふるいを通った数 (溢れた分も数える) */

/* R4: 写す前のふるい。**純関数だけ**を渡すこと (コールバックの中なので
 * KAPI は呼べない)。glob は「一致したものだけ」を写したい — 列挙順の先頭
 * 128 件で切ると、不一致が先に並ぶディレクトリで一致を取り逃がす。 */
static int (*sh_ls_filter)(const char *name);

void sh_ls_reset(void)
{
    sh_ls_count = 0;
    sh_ls_total = 0;
    sh_ls_filter = (int (*)(const char *))0;
}

void sh_ls_set_filter(int (*f)(const char *name))
{
    sh_ls_filter = f;
}

/* sys_ls のコールバック。**KAPI を 1 つも呼ばない**のがこの関数の仕事。 */
void sh_ls_collect_cb(const DirEntry_Ext *entry, void *ctx)
{
    int i;
    (void)ctx;

    if (sh_ls_filter && !sh_ls_filter(entry->name)) return;

    sh_ls_total++;
    if (sh_ls_count >= SH_LS_MAX) return;

    for (i = 0; i + 1 < SH_LS_NAME_MAX && entry->name[i]; i++)
        sh_ls_buf[sh_ls_count].name[i] = entry->name[i];
    sh_ls_buf[sh_ls_count].name[i] = '\0';
    sh_ls_buf[sh_ls_count].size = entry->size;
    sh_ls_buf[sh_ls_count].type = entry->type;
    sh_ls_count++;
}

int sh_ls_count_get(void) { return sh_ls_count; }
int sh_ls_dropped(void)   { return sh_ls_total - sh_ls_count; }

/* 写しから DirEntry_Ext を組み立てる。呼び手は既存のコールバックへそのまま
 * 流せる (表示の書式を二重に持たないため)。 */
void sh_ls_fill(int i, DirEntry_Ext *out)
{
    int k;

    if (i < 0 || i >= sh_ls_count || !out) return;
    for (k = 0; k + 1 < SH_LS_NAME_MAX && sh_ls_buf[i].name[k]; k++)
        out->name[k] = sh_ls_buf[i].name[k];
    out->name[k] = '\0';
    out->size = sh_ls_buf[i].size;
    out->type = sh_ls_buf[i].type;
}
