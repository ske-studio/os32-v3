/* ========================================================================
 *  hostdrv_list_rules.inc — hdrv_list_dir() の列挙ループと「成功と言ってよいか」
 *
 *  票: H1 (docs/tasks/shell/HSYNC_IMPROVEMENT_PLAN.md §8 の H1 行 —
 *      「I/O 失敗を成功にしない」/ 対象「HostDrv のエラー処理」)
 *      Codex 実装レビュー 往復 3 からの派生 (コーダー報告 -> PM が到達性を確認)
 *  ホスト試験: tools/tests/hostdrv_list_host.c
 *
 *  直した点:
 *    1. `hostdrv_query_dir` が負値を返したら **VFS_OK を返さない**。
 *       以前は `break` して末尾で無条件に VFS_OK だったので、200 件のうち
 *       50 件目で失敗すると「50 件だけの成功した列挙」になっていた。
 *       hsync は rc == 0 を見て 50 件を同期し errors=0 / 終了コード 0 で
 *       終わる — 票が潰そうとしている失敗の形そのもの。
 *    2. 件数上限での打ち切りも明示エラーにする (設計書 §7.3
 *       「128 件・名前長・深さの制限は継続して**明示エラー**」)。
 *       黙って切り詰めた一覧を「全部読めた」と言わない。
 *
 *  ★ 2 は挙動の変更 (ls / filer / cp などの list_dir 利用者に及ぶ)。
 *    切り離せるよう **1 行に閉じてある** — `hdrv_list_run()` の
 *    「[H1-CAP]」と書いた行を `return 0;` に戻せば 1 だけが残る。
 *
 *  ハイパーコールにもカーネルの他のヘッダにも触らない。1 件の取得と
 *  エントリの組み立ては呼び手が関数ポインタで渡すので、ホストで贋の
 *  query_dir を挿してそのまま走らせて試験できる。
 *
 *  [C1] C89 / GNU89。宣言はブロック先頭、`//` コメント無し。
 * ======================================================================== */

#ifndef FS_HOSTDRV_LIST_RULES_INC
#define FS_HOSTDRV_LIST_RULES_INC

/* 列挙が途中で切れた = 一覧は不完全。部分的な結果を成功にしない */
#define HDRV_LIST_ERR_IO      OS32_ERR_IO
/* 件数上限で打ち切った = 呼び手の知らないところで一覧が欠ける */
#define HDRV_LIST_ERR_CAPPED  OS32_ERR_FULL

/* 1 件進める。戻り値 > 0 = 列挙終了 / 0 = 1 件取れた / < 0 = エラー。
 * first は「最初の問い合わせか」(NT の QUERY_DIRECTORY の再開規約)。 */
typedef int (*HdrvListStepFn)(void *ctx, int first);

/* step が 0 を返したときに呼ばれる。取れた 1 件を呼び手がコールバックへ流す。
 * 名前が化けた / "." ".." だった場合に**流さない**のは呼び手の判断で、
 * どちらにせよ 1 回ぶんの繰り返しを消費する (従来の挙動と同じ)。 */
typedef void (*HdrvListEmitFn)(void *ctx);

/* 列挙ループ本体。
 * 戻り値 0 = 最後まで届いた / HDRV_LIST_ERR_IO / HDRV_LIST_ERR_CAPPED。
 * 呼び手はこの戻り値にかかわらず後始末 (hostdrv_cleanup_close) を通すこと。 */
static int hdrv_list_run(void *ctx, HdrvListStepFn step, HdrvListEmitFn emit,
                         int max_entries)
{
    int count = 0;
    int first = 1;

    if (!step || !emit || max_entries <= 0) return HDRV_LIST_ERR_IO;

    for (;;) {
        int rc;

        if (count++ >= max_entries) {
            /* [H1-CAP] 修正 2。ここを `return 0;` に戻すと修正 1 だけが残る */
            return HDRV_LIST_ERR_CAPPED;
        }

        rc = step(ctx, first);
        first = 0;

        if (rc > 0) return 0;                  /* 列挙終了 (最後まで届いた) */
        if (rc < 0) return HDRV_LIST_ERR_IO;   /* 部分的な列挙を成功にしない */

        emit(ctx);
    }
}

#endif /* FS_HOSTDRV_LIST_RULES_INC */
