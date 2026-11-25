# ユーザーが決めた事と、配置の未決 (D-1〜D-11)

> 上位: [TASK_RESTRUCTURE.md](TASK_RESTRUCTURE.md)・[RULES.md](RULES.md) (入口と、どの段も先に読む決まり)

| # | 決定 | この票での効き方 |
|---|---|---|
| U1 | **機能の実装は停止のまま。置き場の変更と CI の作り直し (フルスクラッチ) が終わってから再開する** | この票の P0〜P7 が今の本線の仕事。A0 (着手) は済み。P6 は今の workflow の手直しではなく、新しい workflow を一から書いて並べて走らせ、P7 で切り替える |
| U2 | **全件の検査 (変異込み) は main に入れる時**、が元の決まり。「push ごとに全部」は 2026-11-18 の特務の PM の対処で広がった物で、ユーザーの決定ではない (ROLES §0「検査」の行にもそう書いてある) | 新しい CI は「main に入れる SHA に全件」「途中の push は変更に関係する検査」の形で作る (P6-6・P6-7 は影で測るだけでなく、この形の設計の根拠にする)。今の workflow は切り替えまで触らない。「push ごとに全部」と書いている文書・スキルは下の一覧 — 下の一覧の文言は 2026-11-25 に全部直した (決まりは「main に入れる SHA に全件」、今の workflow が途中の push でも回すのは切り替えまでの作り)。P7-3 では「今の workflow は…」の但し書きを消す |
| U3 | 検査の統廃合はユーザーが行ごとに選ぶ (POLICY_CI G14)。**表には推奨を付け、判断の要らない行は分けて出す** | P5 の表に「推奨」と「判断の要否」の列 (`P5.md`) |
| U4 | **この票が先。** 実機の IRQ10 の回帰ほかは記録して後で対処 | 記録は [DEFERRED_TESTS](../DEFERRED_TESTS.md) §2 の `SND-IRQ10-RA266`・`SND-IRQ0-RA266`・`SND-S9-SIDE`・`SND-HAL-RA266-IF0`。枝 `wip/snd-irq10-hwb` は入れずに残し、実装の再開の時に新しい置き場へ載せ直す (P3 の対応表で `tools/move_files.py` を枝にも掛ける) |
| U5 | 書き手は astra、**読み手は sol** (ROLES §0 のまま)。調べの段は sol が書き astra が読む | 各段の「書き手 / 読み手」の列 |
| U8 | **この票は一時的に、書き手も読み手もすべて astra** (ユーザー指示 2026-11-25「このタスクは一時的にすべてastraにするように」)。U5 と段の表の「書き手 / 読み手」の列より優先する | 依頼・レビューのモデルは gpt-6.2-astra。読み手は書き手と別のセッションで、書き手の報告を渡さない (ROLES §0 の独立はセッションで保つ — PM の解釈)。戻す時はユーザーの指示で |
| U6 | **P3・P4 の間、ほかの枝 (本線・os32powerpc) は止める** | [RULES.md](RULES.md) の 5。PM が始める前に os32powerpc へ知らせる |
| U7 | 仕様 §19 の未決 8 項は、P0 の結果を根拠に P1 (配置)・P5 (統廃合)・P6 (CI) の承認の時に 1 つずつ | 承認の節目 A1・A5・A6 |

「push ごとに全部を変異込み」と書いていた所 (2026-11-25 に直した。P7-3 で但し書きを消す): `docs/POLICY_CI.md` G1・R4、`docs/tasks/agents/ROLES.md` §0「検査」、`docs/tasks/agents/PARALLEL_BRANCHES.md` §3、`docs/tasks/agents/PORT_FLOW.md` の段 8、`docs/tasks/agents/HANDOVER_2026-10-25.md` §1 の 2 行、`docs/08_build.md` (§8-1 の CI の段落・木の図・`make check` の表・§8-6)、`docs/POLICY_DEV.md` §6 の 1 行、`docs/design/tools/ci/gate.md` (F1・§2)・`sep_identity.md` §4、`CLAUDE.md` の Build Commands、スキル `os32-ci-read`・`os32-delegate`・`os32-test-basis`・`os32-arch-sep`。

P0 で出た追加の未決 (2026-11-25、P0 の取り直しのレビュー): **D-9** 第三者の物の置き場 (`lib/os32_opna/upstream`・`lib/softfloat`・`userland/lib/font/vendor`)、**D-10** `sdk/` の include 以外の公開物 (リンカスクリプト・`mkos32x.py`・Rust のターゲットの定義)、**D-11** 残骸の削除 (どの像の入力でもないヘッダ — 移動と別の種類の変更なので RULES の 1 により別の段)。

配置の未決 (P1-1 が案を出し、P1 の承認で決まる): **D-1** `sys/kern/` の下位 (`mm`・`exec`・`init` を残すか平たくするか)、**D-2** GeForce (`drivers/video/geforce` 29・`hwdev/gf` 143・`kdebug/gfinit_*`) を `sys/dev/` の 1 か所に寄せる範囲、**D-3** `kdebug/` の行き先 (起動時自己試験 `kselftest` は検査か製品か)、**D-4** `sound/hal`・`input/hal`・`include/*_contract.h` のうち `sys/hal/` に移す物 (仕様 §4: 契約と共通処理だけ)、**D-5** `lib/` のうちカーネルにだけ入る物 (`kstring` など) の置き場、**D-6** `sys/conf/` に置く物 (`build/images/`・`build/features/` の像と機能の定義か)、**D-7** `sample/`・`packages/`・`images/`・`game/` の扱い、**D-8** `userland/tests/` (配備する試験のプログラム) を `tests/` に移すか。
