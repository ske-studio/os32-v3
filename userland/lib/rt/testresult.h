/* ======================================================================== */
/*  TESTRESULT.H — 試験プログラムの合否を機械が読める形にする約束事           */
/*                                                                          */
/*  票 docs/archive/test/TASK_TEST_RESULT.md §2 / §11 の正典。                 */
/*                                                                          */
/*  このヘッダが持つのは 2 つ。**どちらも 1 回の呼び出しで同時に起きる。**   */
/*                                                                          */
/*    (1) 終了コードを決める (0 / 1 / 2)                                    */
/*    (2) 集計行を **fd 1 へ書く**                                          */
/*                                                                          */
/*  片方だけ直せない形にしてあるのが目的で、ランナー (3 段目) が 2 つの      */
/*  答えを持つ状態にならないようにしてある。                                */
/*                                                                          */
/*  使い方 (これ以外の形を作らない):                                        */
/*                                                                          */
/*      #include "os32api.h"                                                */
/*      #include "rt/testresult.h"                                          */
/*                                                                          */
/*      int main(int argc, char **argv, KernelAPI *api)                     */
/*      {                                                                   */
/*          ...                                                             */
/*          return os32_test_summary(api, "math_test", g_passed, g_total);  */
/*      }                                                                   */
/*                                                                          */
/*  `<名前>` は**固定文字列**で渡す。argv[0] から作ると、リダイレクト先や    */
/*  呼び出し方 (`/usr/bin/math_test.bin` / `math_test`) で行が変わって       */
/*  ランナーが取りこぼす。                                                  */
/*                                                                          */
/*  ■ なぜ fd 1 なのか (票 §11、2026-09-17 にランナーが暴いた)              */
/*                                                                          */
/*  2026-09-17 まで、このヘッダは**行を組み立てて返すだけ**で、どこへ出すか  */
/*  は呼び手任せだった。その結果、第 1 陣 16 本のうち 14 本が `api->kprintf` */
/*  を使っていた。kprintf はカーネルの画面出力なので**リダイレクトを素通り   */
/*  する** — 人間には見えているのに `>` で拾えず、ランナーは 16 本中 14 本を */
/*  「$?=0 なのに集計行が無い」と報告した。                                  */
/*                                                                          */
/*    printf (newlib)      fd 1 へ行く。が、`-nostdlib` の試験では使えない  */
/*    api->kprintf         画面へ直に書く。リダイレクトを通らない            */
/*    api->sys_write(1,…)  libc に依存せず、**リダイレクトも通る**           */
/*                                                                          */
/*  両方を満たすのは sys_write だけなので、**出し口もこのヘッダが持つ**。    */
/*  呼び手に行を渡さないのは、渡した瞬間に出し口が 2 通りに増えるから。      */
/*  (集計行**以外**の出力 — 各項目の [OK] / [FAIL] など — は今までどおり     */
/*   kprintf でも printf でもよい。揃えるのは集計行だけ。)                  */
/* ======================================================================== */

#ifndef __LIBOS32_TESTRESULT_H
#define __LIBOS32_TESTRESULT_H

#include "os32api.h"

/* ------------------------------------------------------------------------ */
/*  終了コード (票 §2-1)                                                     */
/* ------------------------------------------------------------------------ */

#define OS32_TEST_EXIT_PASS   0   /* 全項目合格                             */
#define OS32_TEST_EXIT_FAIL   1   /* 1 件以上不合格                         */
#define OS32_TEST_EXIT_SKIP   2   /* 実行しなかった (前提の欠如・引数不正)  */

/* 3〜125 は個別の票が定義してよい。定義しないなら使わない。 */
#define OS32_TEST_EXIT_LOCAL_MIN   3
#define OS32_TEST_EXIT_LOCAL_MAX   125

/* シェルの `$?` が使う予約値 (userland/shell/shell.h)。試験は返さない。
 * 返すと「落ちた」のか「自分で落ちた」のか区別できなくなる。 */
#define OS32_TEST_EXIT_RESV_SPAWN     126  /* 起こせなかった               */
#define OS32_TEST_EXIT_RESV_NOTFOUND  127  /* 実行ファイルが無い           */
#define OS32_TEST_EXIT_RESV_INTR      130  /* CTRL+STOP / ESC で中断       */
#define OS32_TEST_EXIT_RESV_FAULT     139  /* 例外 (#PF / #GP) で畳んだ    */

/* 集計行 1 行を組み立てるのに要るバイト数 (NUL 込み)。名前と SKIP の理由が
 * 長くなっても切り詰めで済むようにこの大きさにしてある。 */
#define OS32_TEST_LINE_MAX  128

/* 集計行の出し先 ([C4] ここが管理元)。票 §11 の決め直し。
 * シェルの `>` / `>>` が差し替えるのはこの番号なので、ここを変えると
 * ランナーが集計行を拾えなくなる。 */
#define OS32_TEST_FD_STDOUT  1

/* ------------------------------------------------------------------------ */
/*  内部 — libc に依存しない小道具                                           */
/*                                                                           */
/*  `os32_test__*` は**ホスト試験 (tools/tests/test_result_conv_host.c) と    */
/*  変異試験のためだけに見えている**。試験プログラムから直接呼ばないこと —    */
/*  行と終了コードを別々に作れてしまい、このヘッダの目的が消える。            */
/*  tools/tests/test_result_conv.py の静的検査がこれを見張っている。          */
/* ------------------------------------------------------------------------ */

/* `dst[at..cap-1]` へ `src` を NUL 手前まで詰める。返り値は新しい末尾。
 * cap は NUL のぶんを含む。入り切らないぶんは黙って捨てる (溢れない)。 */
static __inline__ unsigned int
os32_test__put(char *dst, unsigned int at, unsigned int cap, const char *src)
{
    unsigned int n = at;

    if (!dst || cap == 0) return at;
    if (!src) return n;
    while (*src && n + 1 < cap) {
        dst[n] = *src;
        n++;
        src++;
    }
    return n;
}

/* `dst[at..cap-1]` へ 10 進数を詰める。負値は先頭に '-' を置く。 */
static __inline__ unsigned int
os32_test__put_num(char *dst, unsigned int at, unsigned int cap, int v)
{
    char tmp[12];
    unsigned int n = at;
    unsigned int u;
    int k = 0;

    if (!dst || cap == 0) return at;
    if (v < 0) {
        n = os32_test__put(dst, n, cap, "-");
        u = (unsigned int)(-(long)v);
    } else {
        u = (unsigned int)v;
    }
    if (u == 0) {
        tmp[k] = '0';
        k++;
    }
    while (u > 0) {
        tmp[k] = (char)('0' + (int)(u % 10u));
        u /= 10u;
        k++;
    }
    while (k > 0) {
        k--;
        if (n + 1 < cap) {
            dst[n] = tmp[k];
            n++;
        }
    }
    return n;
}

/* 組み立ての最後。**改行はどんなに切り詰めても落とさない** — 落ちると
 * ランナーが次の出力と 1 行に繋げて読むので、行が丸ごと消えるのと同じに
 * なる。本文のぶんを cap-1 までに抑え、余らせた 1 バイトを改行に使う。 */
static __inline__ void
os32_test__end(char *dst, unsigned int n, unsigned int cap)
{
    if (!dst || cap == 0) return;
    if (cap < 2) {
        dst[0] = '\0';
        return;
    }
    if (n + 1 >= cap) n = cap - 2;
    dst[n] = '\n';
    dst[n + 1] = '\0';
}

/* ------------------------------------------------------------------------ */
/*  内部 — 行を組み立てる (出さない)                                         */
/* ------------------------------------------------------------------------ */

/* `<名前>: PASS <n>/<m>\n` / `<名前>: FAIL <n>/<m>\n` を組み立て、終了コード
 * を返す。**total が 0 以下、または pass が総数と釣り合わないときは FAIL** —
 * 1 項目も走らずに終わった試験を合格にしない。 */
static __inline__ int os32_test__build(char *buf, unsigned int cap,
                                       const char *name, int pass, int total)
{
    unsigned int n = 0;
    int ok;

    ok = (total > 0) && (pass == total);

    if (buf && cap > 0) {
        unsigned int body = (cap > 1) ? (cap - 1) : cap;

        n = os32_test__put(buf, n, body, name ? name : "?");
        n = os32_test__put(buf, n, body, ": ");
        n = os32_test__put(buf, n, body, ok ? "PASS " : "FAIL ");
        n = os32_test__put_num(buf, n, body, pass);
        n = os32_test__put(buf, n, body, "/");
        n = os32_test__put_num(buf, n, body, total);
        os32_test__end(buf, n, cap);
    }
    return ok ? OS32_TEST_EXIT_PASS : OS32_TEST_EXIT_FAIL;
}

/* `<名前>: SKIP <理由>\n` を組み立て、終了コード 2 を返す。
 * 理由は空にしない (空だとランナーが行を読み違える)。 */
static __inline__ int os32_test__build_skip(char *buf, unsigned int cap,
                                            const char *name,
                                            const char *reason)
{
    unsigned int n = 0;

    if (buf && cap > 0) {
        unsigned int body = (cap > 1) ? (cap - 1) : cap;

        n = os32_test__put(buf, n, body, name ? name : "?");
        n = os32_test__put(buf, n, body, ": SKIP ");
        n = os32_test__put(buf, n, body,
                           (reason && *reason) ? reason : "no reason given");
        os32_test__end(buf, n, cap);
    }
    return OS32_TEST_EXIT_SKIP;
}

/* ------------------------------------------------------------------------ */
/*  内部 — 行を fd 1 へ出す                                                  */
/* ------------------------------------------------------------------------ */

/* 書き切れたら 1、書き切れなければ 0。
 *
 * **短い書き込みを「書けた」ことにしない。** sys_write が返すのは
 * **書けたバイト数**で (fs/vfs_fd.c: vfs_write_fd)、リダイレクト先の空きや
 * パイプの都合で要求より少なくなりうる。1 回の返り値をそのまま成功と読むと
 * 集計行が途中で切れたままランナーには 0 が返り、票 §11 で踏んだ
 * 「$?=0 なのに集計行が無い」が形を変えて戻ってくる。書けたぶんだけ進めて
 * 残りを書き続け、**1 バイトも進まなくなったら失敗として持ち帰る**
 * (進まないものを回し続けると試験が固まり、ランナーの見張りに掛かる)。 */
static __inline__ int os32_test__emit(KernelAPI *api, const char *line)
{
    unsigned int len = 0;
    unsigned int done = 0;
    int n;

    if (!api || !api->sys_write || !line) return 0;
    while (line[len]) len++;
    while (done < len) {
        n = api->sys_write(OS32_TEST_FD_STDOUT, line + done,
                           (u32)(len - done));
        if (n <= 0) return 0;
        done += (unsigned int)n;
        if (done > len) return 0;   /* 要求より多く書いたと言われた */
    }
    return 1;
}

/* 組み立てた行を出し、**出せたかどうかを終了コードに噛み合わせる**。
 *
 * 出せなかったら `code` が何であれ FAIL (1) にする。理由: 集計行が fd 1 に
 * 無いのに 0 を返すのは、票 §11 が暴いた穴そのもの。合格を名乗れるのは
 * **答えがランナーに届いたときだけ**とする ([V4])。FAIL にしておけば、
 * ランナーは「$?=1 なのに集計行が無い」= 食い違いとして必ず名指しする。 */
static __inline__ int os32_test__finish(KernelAPI *api, const char *line,
                                        int code)
{
    if (!os32_test__emit(api, line)) return OS32_TEST_EXIT_FAIL;
    return code;
}

/* ------------------------------------------------------------------------ */
/*  公開                                                                     */
/* ------------------------------------------------------------------------ */

/* 予約値か (票 §2-1 の表)。ランナーと試験の両方がこの 1 本を見る。 */
static __inline__ int os32_test_exit_reserved(int status)
{
    return status == OS32_TEST_EXIT_RESV_SPAWN ||
           status == OS32_TEST_EXIT_RESV_NOTFOUND ||
           status == OS32_TEST_EXIT_RESV_INTR ||
           status == OS32_TEST_EXIT_RESV_FAULT;
}

/* 合否の集計から**集計行を fd 1 へ出し、終了コードを返す**。
 *
 *   api   … KernelAPI。sys_write が無ければ出せないので FAIL。
 *   name  … 固定文字列。argv[0] から作らない。
 *   pass  … 合格数、total … 総数。
 *
 * 呼び手は行を受け取らない。受け取れると出し口を選べてしまい、それが
 * 票 §11 の原因だった。 */
static __inline__ int os32_test_summary(KernelAPI *api, const char *name,
                                        int pass, int total)
{
    char line[OS32_TEST_LINE_MAX];
    int  code;

    code = os32_test__build(line, sizeof(line), name, pass, total);
    return os32_test__finish(api, line, code);
}

/* 前提が無くて実行しなかったとき。`<名前>: SKIP <理由>` を fd 1 へ出して 2。 */
static __inline__ int os32_test_summary_skip(KernelAPI *api, const char *name,
                                             const char *reason)
{
    char line[OS32_TEST_LINE_MAX];
    int  code;

    code = os32_test__build_skip(line, sizeof(line), name, reason);
    return os32_test__finish(api, line, code);
}

#endif /* __LIBOS32_TESTRESULT_H */
