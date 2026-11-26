# userland/lib/rt/testresult.h — 設計
> 状態: **現行 (2026-11-18)** — 出所: TASK_TEST_RESULT の契約を今のソースと照らして要約
ソース: `userland/lib/rt/testresult.h`

ゲストの試験プログラムが合否を機械に読める形で返すための約束事。管理元はこのヘッダ 1 本で、
読む側はゲスト一括実行のランナー `tools/guest_tests.py` (定数を同じ値で持つ)。
試験は `test_result_conv.py (旧版 ee31b3c3405f)` (実物の試験プログラムを贋の KernelAPI で走らせ、出た行と `main` の戻り値を同じ場面で見る)。

## 1. 終了コード

| 値 | 定数 | 意味 |
|---|---|---|
| 0 | `OS32_TEST_EXIT_PASS` | 全項目合格 |
| 1 | `OS32_TEST_EXIT_FAIL` | 1 件以上不合格 |
| 2 | `OS32_TEST_EXIT_SKIP` | 実行しなかった (前提の欠如・引数不正)。不合格と区別する |
| 3〜125 | `OS32_TEST_EXIT_LOCAL_MIN`〜`MAX` | 個々の試験が定義してよい。定義しないなら使わない |
| 126 / 127 / 130 / 139 | `OS32_TEST_EXIT_RESV_*` | **予約。試験は返さない** — シェルの `$?` が「起こせなかった / 無い / 中断 / 例外」に使う値 (`userland/shell/shell.h` の `SH_STATUS_*`) |

`os32_test_exit_reserved(status)` が予約値の判定の 1 本。
`main` は `int main(int argc, char **argv, KernelAPI *api)` で宣言し、`void main` にしない。

## 2. 集計行

最終行に 1 行、どちらかの形:

    <名前>: PASS <合格数>/<総数>
    <名前>: FAIL <合格数>/<総数>
    <名前>: SKIP <理由>

- `<名前>` は**固定文字列** (`argv[0]` から作らない。呼び方やリダイレクト先で行が変わらないように)。
- `total > 0` かつ `pass == total` のときだけ PASS。総数 0 (1 項目も走らなかった) は FAIL。
- SKIP の理由は空にしない (空なら `no reason given`)。
- 行は `OS32_TEST_LINE_MAX` (128、NUL 込み) に収める。長い名前・理由は切り詰めるが、**末尾の改行は落とさない**。

## 3. 公開の口と出し口

公開は 2 本だけ。どちらも**行を組み立てて fd 1 へ書き、終了コードを返す**のを 1 回の呼び出しで行う
(終了コードと集計行の片方だけを直せない形)。呼び手は行のバッファを受け取らない。

    return os32_test_summary(api, "<名前>", passed, total);   /* 0 か 1 */
    return os32_test_summary_skip(api, "<名前>", "<理由>");     /* 2 */

- 出し口は `api->sys_write(OS32_TEST_FD_STDOUT = 1, …)`。`kprintf` は画面へ直に書いてリダイレクトを通らず、
  `printf` は newlib が要るため、両方を満たすのはこれだけ。集計行以外の出力 (`[OK]` など) は `kprintf` でも `printf` でもよい。
- 短い書き込みは書けたぶんだけ進めて残りを書き続ける。1 バイトも進まない・`sys_write` が無い・
  要求より多く書いたと返る場合は「出せなかった」とし、**終了コードを何であれ FAIL (1) にする**
  (集計行が届いていないのに 0 を返さない。ランナーは「`$?`=1 なのに集計行が無い」を食い違いとして名指しする)。
- `os32_test__*` は内部用 (ホスト試験と変異のために見えている)。試験プログラムから直接呼ばない。
