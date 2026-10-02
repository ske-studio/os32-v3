# TASK_PCM_CS4231_RESULTS — 完了した段の記録

> 状態: **完了記録 (2026-10-02 切り出し)**。元票: [TASK_PCM_CS4231.md](../../tasks/v3/TASK_PCM_CS4231.md)。節番号・記述時点は原文のまま。本文中の節参照は元票を指す。未確認事項の受入完了を意味しない。

#### 進捗 (2026-09-23、worktree `wt/pcm`、基点 17e3f46)

**実装済み (手元ビルドのみ。NP21/W と実機は未実施)**:

- `drivers/pcm_cs4231.h` — ポート・間接レジスタ・ビット・寸法・状態・op を
  1 か所に ([C4])。典拠は DS139PP2 の頁番号と `io_sound.md` の節で注記。
- `drivers/pcm_cs4231_math.c` — 純粋部。連続性 (位置だけ)・補充の余裕・
  drain の 3 段階・ステージング・`pcm_start` の配り方・レート → I8・
  close の期限式・percent → 減衰、そして**列を `const` の表**で持つ。
- `drivers/pcm_cs4231.c` — レジスタアクセス (Index と Data は 1 つの
  `irq_save`)、列の実行器、状態機械、KAPI の実体、`pcm_init` / `pcm_tick` /
  `pcm_reclaim`。
- KAPI v61 の 5 本 (slot 224〜228)。`pcm_status` は生成される出力保護つき、
  `pcm_write` は wrapper の body で `ring3_user_range_ok`。
- 結線: `kernel/kernel.c` (`pci_bind_all` の後に `pcm_init`)、
  `kernel/isr_handlers.c` (`snd_tick` の隣に `pcm_tick`)、
  `exec/exec.c` (`snd_owner_exit` の隣に `pcm_reclaim`)、
  `kernel/kselftest.c` (起動後に CLOSED と「知らないレートを装置に触らず断る」)。
- ホスト試験 `make check-pcm-cs4231-host` — 21 ケース + 変異 24 本が全部 RED。
  記録は `tools/tests/pcm_cs4231_tdd.md`。
- CPL=3 の `userland/tests/pcm_test.c` (5 秒 / `short` / `nodev`)、
  `build/app.conf` と `userland/deploy.yaml` に登録 ([V2])。

