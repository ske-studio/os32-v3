# A0 ranged pgalloc — 検証記録

## 範囲と契約

変更対象は `kernel/pgalloc.c` / `.h` とこの専用テストだけ。
`pgalloc_alloc_n_range(int n, u32 lo, u32 hi)` はページ整列済みの
半開区間 `[lo, hi)` 全体が管理域内であることを要求し、範囲を切り詰めない。
失敗は 0、bitmap/統計は不変、範囲外フォールバックなし。
単一 CPU の IRQ に対し探索から全ページ・統計の予約完了まで排他し、元の IF を復元する。
SMP/NMI、初期化との並行実行、既存 API 自体の同期改善は対象外。

## 実行済み RED → GREEN

コマンド: `python3 tools/tests/test_pgalloc_range.py`

1. 実装前に real-source 基本テストを作成。
   ILP32 GNU89 コンパイル成功後、`ASSERT FAIL: pgalloc_alloc_n_range != 0`、終了 1。
   未実装シンボルの weak 参照により、リンクエラーではなく assertion RED を確認。
2. 最小の範囲内 first-fit を追加。`PASS basic`、target GNU89 コンパイル成功。
3. IRQ 監視テストを追加。基本テストは成功し、bitmap 読み出し時の
   `ASSERT FAIL: !(test_flags & TEST_IF)`、終了 1。
4. 既存 `io.h` の `irq_save` / `irq_restore` を新 API にのみ追加。
   IF=1/0 の成功・枯渇経路が GREEN、target コンパイルも成功。
5. 境界・断片化・網羅モデル・既存 API の回帰検査を追加し、実装の追加変更なしで全通過。

最終実出力:

```text
HOST ILP32 GNU89 COMPILE PASS
PASS basic
PASS irq_atomic IF=1/0 success/exhaustion
PASS invalid/overflow/management boundaries IF=1/0
PASS fragmentation/exhaustion/no APP_BAND fallback/reuse
PASS exhaustive 8-page occupancy x n=1..9
PASS generic alloc/free/mark/init regression
TARGET i386-elf GNU89 -Werror COMPILE PASS
```

## 検査方法と限界

- ホスト GCC の `-m32 -nostdlib -static` で実 `pgalloc.c` を直接取り込む。
  `u32` と `int` がともに 4 bytes であることを assertion。
  特権 IRQ 操作とログだけを置き換え、allocator はコピー/再実装しない。
- `-finstrument-functions` で実 bitmap read/write ヘルパーの IF=0 を検査。
  read 回数は要求範囲のページ数以下。IRQ 復元直前と戻り後に
  bitmap 全体・使用ページ数を期待値と比較し、部分予約や失敗時変更を検出。
- 8 ページの全占有パターンと n=1..9 を独立した連続空きモデルと比較。
  bitmap ワード境界をまたぐ配置、IF=0/1、断片化、末端ぴったりの割当てを検査。
- ゼロ/負/INT_MIN/INT_MAX/乗算すると桁あふれする n、逆順/空/非整列範囲、
  管理域上下逸脱、u32 上端、低メモリ、ページ端数、hotdeploy 除外境界を検査。
- APP_BAND 外の要求が枯渇しても、空きの APP_BAND に逃げないことを検査。
  既存 alloc/free/mark/init の first-fit、二重解放、非整列拒否、統計、再利用も検査。
- 実 `io.h` を使う `i386-elf-gcc -std=gnu89 -O2 -Werror` の単体コンパイル成功。
  `git diff --check` (対象ファイル限定) 成功。
- exec の reserve/claim 式は読み取りのみ。paging/exec/shlib の呼出し側は未移行。
  この A0 は完全な backing 安全性の解決ではない。
- エミュレータ、配備、カーネル全体のビルド、実機 IRQ 動作確認は依頼範囲外として未実施。
