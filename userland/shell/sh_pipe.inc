/* ========================================================================
 *  sh_pipe.inc — SHELL_AS_APP のパイプバッファ (Codex 往復 5 の blocker)
 *
 *  main.c が #ifdef SHELL_AS_APP の中で #include する。拡張子が .c でないの
 *  は build/programs.mk の SHELL_SRC が userland/shell 下の .c を wildcard で
 *  拾うためで、sh_launch.inc / sh_redraw.inc と同じ理由。
 *
 *  なぜ sys_pipe_* をそのまま使えないか:
 *    `sys_pipe_get_buf()` が返すのは fs/pipe_buffer.c が kmalloc した
 *    **カーネル帯**のポインタ。それを `sys_redirect_fd_buf()` へ渡すと、
 *    kapi/kapi_generated.c のポインタ引数マスクに拾われて exec/exec.c の
 *    `ring3_ptr_ok()` に落ち、`ring3_fault_kill()` で sh ごと畳まれる
 *    (`sh> echo a | cat` で強制終了)。常駐は CPL=0 なので通っていた経路を
 *    CPL=3 に持ち込んだときだけ壊れる。
 *
 *  代わりに sh 自身の .bss から配る。アプリ帯のポインタは `ring3_ptr_ok` を
 *  通り、内蔵コマンドは sh の文脈で走るのでカーネルの書き込み先も正しい。
 *  `sys_redirect_get_buf_len` / `reset_all_redirects` は従来どおり。
 *
 *  外部コマンドが混じるパイプは、要求表経由で WM が起こす**別アプリ**に
 *  なるので sh の FD に掛けたリダイレクトが届かない。main.c の
 *  `sh_stage_is_builtin()` が段ごとに見て 1 行断る。
 * ======================================================================== */

/* execute_command のパイプラインが同時に持つ本数。main.c の `num_alloc`
 * (交互使用なので最大 2) と対。増やすときは必ず両方を見ること。
 * .bss は SH_PIPE_SLOTS * PIPE_BUF_SIZE (= 128KB) 常駐する。 */
#define SH_PIPE_SLOTS  2

static u8 sh_pipe_pool[SH_PIPE_SLOTS][PIPE_BUF_SIZE];
static u8 sh_pipe_used[SH_PIPE_SLOTS];

int sh_pipe_alloc(void)
{
    int i;
    for (i = 0; i < SH_PIPE_SLOTS; i++) {
        if (!sh_pipe_used[i]) {
            sh_pipe_used[i] = 1;
            return i;
        }
    }
    return -1;
}

u8 *sh_pipe_get_buf(int slot)
{
    if (slot < 0 || slot >= SH_PIPE_SLOTS) return (u8 *)0;
    if (!sh_pipe_used[slot]) return (u8 *)0;
    return sh_pipe_pool[slot];
}

void sh_pipe_free(int slot)
{
    if (slot >= 0 && slot < SH_PIPE_SLOTS) sh_pipe_used[slot] = 0;
}
