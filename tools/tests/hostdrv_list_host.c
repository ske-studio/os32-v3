/* =========================================================================
 *  HOSTDRV_LIST_HOST.C — hdrv_list_dir() の列挙ループを **実物のソースで**
 *  確かめる (票 H1「I/O 失敗を成功にしない」/ 対象「HostDrv のエラー処理」)
 *
 *  実行: python3 -B tools/tests/test_hostdrv_list.py [--target]
 *  記録: tools/tests/h1_tdd.md
 *
 *  fs/hostdrv_list_rules.inc を 1 行も写さずそのまま #include する
 *  (模型ではない)。差し替えるのは `hostdrv_query_dir` に当たる 1 件取得と、
 *  1 件ぶんの組み立てだけ。台本で
 *    (a) 途中で負値を返す (200 件のうち 50 件目で失敗)
 *    (b) 上限を超える件数を返す
 *  を注入し、**部分的な列挙が成功として返らない**ことを見る。
 *
 *  あわせて呼び手 (hsync) 側の意味も見る: rc != 0 なら hsync は
 *  `FAIL: ls ...` で errors に数えて非ゼロ終了する — その結合は
 *  tools/tests/hsync_h1_host.c 側 (case_b4) が持つ。
 *
 *  エミュレータ・実配備・make には一切触れない。
 * ========================================================================= */

#include <stdio.h>
#include <string.h>

#include "os32api.h"      /* OS32_ERR_* / u8,u32 */

#include "../../fs/hostdrv_list_rules.inc"

static int failures;
static int checks;

static void check(int cond, const char *name)
{
    checks++;
    printf("  %s %s\n", cond ? "ok  " : "FAIL", name);
    if (!cond) failures++;
}

/* ------------------------------------------------------------------------ */
/*  贋 hostdrv_query_dir                                                      */
/* ------------------------------------------------------------------------ */

typedef struct {
    int total;        /* この件数を返したあと「列挙終了」(> 0) を返す */
    int fail_at;      /* > 0 … この回数めの step で負値を返す (1 始まり) */
    int skip_every;   /* > 0 … この倍数の emit は流さない ("." 相当) */

    int steps;        /* step が呼ばれた回数 */
    int emits;        /* emit が呼ばれた回数 */
    int delivered;    /* 実際に「コールバックへ流した」件数 */
    int first_seen;   /* 最初の step に first=1 が渡ったか */
    int first_again;  /* 2 回目以降に first=1 が渡ってしまった回数 */
} Script;

static int fk_step(void *ctx, int first)
{
    Script *s = (Script *)ctx;

    s->steps++;
    if (s->steps == 1) {
        s->first_seen = first ? 1 : 0;
    } else if (first) {
        s->first_again++;
    }

    if (s->fail_at > 0 && s->steps == s->fail_at) return -1;   /* I/O エラー */
    if (s->steps > s->total) return 1;                         /* 列挙終了 */
    return 0;                                                  /* 1 件取れた */
}

static void fk_emit(void *ctx)
{
    Script *s = (Script *)ctx;

    s->emits++;
    if (s->skip_every > 0 && (s->emits % s->skip_every) == 0) return;
    s->delivered++;
}

static void script_reset(Script *s, int total, int fail_at, int skip_every)
{
    memset(s, 0, sizeof(*s));
    s->total = total;
    s->fail_at = fail_at;
    s->skip_every = skip_every;
}

/* ------------------------------------------------------------------------ */

int main(void)
{
    Script s;
    int rc;

    printf("=== hdrv_list_dir 列挙ループ (票 H1) ===\n");

    printf("== 正常系 ==\n");
    script_reset(&s, 200, 0, 0);
    rc = hdrv_list_run(&s, fk_step, fk_emit, 1000);
    check(rc == 0, "200 件を最後まで読めたら 0");
    check(s.delivered == 200, "200 件すべて流す");
    check(s.first_seen == 1, "最初の問い合わせに first=1");
    check(s.first_again == 0, "2 回目以降は first=0 (再開規約)");

    script_reset(&s, 0, 0, 0);
    rc = hdrv_list_run(&s, fk_step, fk_emit, 1000);
    check(rc == 0 && s.delivered == 0, "空ディレクトリも 0 (誤検出しない)");

    script_reset(&s, 200, 0, 3);
    rc = hdrv_list_run(&s, fk_step, fk_emit, 1000);
    check(rc == 0, "'.' / '..' 相当を飛ばしても 0");
    check(s.emits == 200 && s.delivered == 200 - 200 / 3,
          "飛ばした分も繰り返しを 1 回消費する (従来の挙動)");

    printf("== (a) 途中で query_dir が負値 ==\n");
    script_reset(&s, 200, 50, 0);
    rc = hdrv_list_run(&s, fk_step, fk_emit, 1000);
    check(rc != 0, "**VFS_OK を返さない** (部分的な列挙を成功にしない)");
    check(rc == HDRV_LIST_ERR_IO, "OS32_ERR_IO を返す");
    check(s.delivered == 49, "流れたのは 49 件だけ (一覧は不完全)");

    script_reset(&s, 200, 1, 0);
    rc = hdrv_list_run(&s, fk_step, fk_emit, 1000);
    check(rc == HDRV_LIST_ERR_IO && s.delivered == 0,
          "1 件目で失敗しても「空ディレクトリ」と言わない");

    script_reset(&s, 200, 200, 0);
    rc = hdrv_list_run(&s, fk_step, fk_emit, 1000);
    check(rc == HDRV_LIST_ERR_IO && s.delivered == 199,
          "最後の 1 件で失敗しても失敗");

    printf("== (b) 件数上限での打ち切り ==\n");
    script_reset(&s, 5000, 0, 0);
    rc = hdrv_list_run(&s, fk_step, fk_emit, 1000);
    check(rc != 0, "**VFS_OK を返さない** (黙って切り詰めない)");
    check(rc == HDRV_LIST_ERR_CAPPED, "OS32_ERR_FULL を返す (I/O とは区別)");
    check(s.delivered == 1000, "上限ぶんだけ流れている");
    check(s.steps == 1000, "上限を超えて問い合わせない");

    script_reset(&s, 1000, 0, 0);
    rc = hdrv_list_run(&s, fk_step, fk_emit, 1000);
    check(rc == HDRV_LIST_ERR_CAPPED,
          "ちょうど上限の件数でも打ち切り扱い (count++ が先)");

    script_reset(&s, 999, 0, 0);
    rc = hdrv_list_run(&s, fk_step, fk_emit, 1000);
    check(rc == 0 && s.delivered == 999, "上限の 1 つ手前は成功 (境界)");

    printf("== 引数の防御 ==\n");
    check(hdrv_list_run(&s, 0, fk_emit, 1000) == HDRV_LIST_ERR_IO,
          "step が NULL なら失敗");
    check(hdrv_list_run(&s, fk_step, 0, 1000) == HDRV_LIST_ERR_IO,
          "emit が NULL なら失敗");
    script_reset(&s, 10, 0, 0);
    check(hdrv_list_run(&s, fk_step, fk_emit, 0) == HDRV_LIST_ERR_IO,
          "上限 0 は失敗 (無限ループにしない)");

    printf("== 2 つのエラーは別物 ==\n");
    check(HDRV_LIST_ERR_IO != HDRV_LIST_ERR_CAPPED,
          "I/O 失敗と件数上限を同じ値にしない");
    check(HDRV_LIST_ERR_IO != 0 && HDRV_LIST_ERR_CAPPED != 0,
          "どちらも 0 (VFS_OK) ではない");

    printf("\n%d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
