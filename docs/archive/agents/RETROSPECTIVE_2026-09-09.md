# 振り返り — hermes を PM に据えたマルチエージェント運用 (2026-09-07〜09)

状態: **完了記録 (2026-09-09 時点のスナップショット、履歴)**。現行体制は [ROLES.md](../../tasks/agents/ROLES.md)。
この文書は撤収の判断根拠を残すためのもので、運用中の参照先ではない。

## 1. 何を試したか

hermes (Nous Research の Agent CLI, `~/.hermes`) を PM に据え、Claude Code / Codex CLI /
ローカル LLM をワーカーとして使う構成。2026-09-01 に 3 層 (設計 = `claude-fable-5` /
コーディング = 自宅サーバー `qwen3.6:35b-a3b-coding` / ビルド検証 = NPU `gemma4-it:e4b`) で
組んだものを、9-07 以降 PM 直下に Claude (低レイヤ) と Codex (GUI/ユーザーランド) を
並べる形に拡張した。

## 2. 撤収の理由 (ユーザー判断)

現行の OpenAI / Anthropic モデルとの相性。**過剰なバグ懸念**と、**判断が要らない場面での
開発中断**が多発した。承認済みスコープの中で進めればよい場面で、1 ステップごとに
「次に進んでよいか」が返ってくる。

## 3. 壊れた箇所と原因 (確認済み)

| 事象 | 原因 |
|---|---|
| 3 層構成の 2 層目・3 層目が設定上消滅 | 2026-09-08 12:39〜12:41 の hermes 更新で `~/.hermes/config.yaml` が同梱サンプル (110KB) に上書きされ、復元時に `providers.codeserver` / `providers.localpc` と `delegation.provider/model` が落ちた。旧設定の実物は `~/.hermes/.hermes.bak/config.yaml` に残る |
| `~/.local/bin/hermes-verify` が動かない | 存在しない provider `localpc` を指す。上と同根 |
| 独自スキルの消失 | `~/.hermes/skills/software-development/os32/` が更新で消えた。同梱スキル木の下に置いたため |
| PM モデルが 2 日で 2 回振替 | `claude-fable-5` → `anthropic/claude-opus-4.6` (openrouter) → `gpt-6-astra` (openai-codex) |
| 並行エージェントによる記録の消失 | 共有ファイルへの同時編集で TDD ログが上書きされ、`tools/tests/np21w_transport_tdd.md` は委譲ログから経過を再構成する羽目になった |
| Codex のクォータ枯渇 | hermes の PM を `gpt-6-astra` (provider `openai-codex`) で回した結果、同じ枠を使う Codex CLI を使い切った。**レビュアー役をその場で失った** |

## 4. 増幅装置になったもの

`~/.hermes/skills/os32-agent-orchestration/SKILL.md` が 14.6KB に膨れていた。中身は
失敗のたびに足された禁止事項の堆積で、1 文ごとに「〜するな」「〜を要求せよ」が並ぶ。
これを毎回読ませる構造そのものが、慎重さの過剰と中断癖を強化していた。

**教訓**: 運用規約はインシデントごとに 1 行足す構造にしない。
[ROLES.md](../../tasks/agents/ROLES.md) の規約は 3 行に固定し、増える場合は既存行の書き換えで吸収する。
これは CLAUDE.md の「Known Gotchas が 1 インシデント = 1 行で伸びる」問題と同じ形。

## 5. 生き残った良い習慣 (新体制でも続ける)

体制は撤収するが、この期間に定着した規律は有効だった:

- **新規コードに必ずホスト TDD の RED→GREEN 記録を残す** (`tools/tests/*_tdd.md`)。
  実ソースを ILP32 でコンパイルして検証するので、i386-elf ビルドの前に契約違反が出る
- **設計票の冒頭で状態を自己申告する** (「実装提出、ゲスト受入待ち」「これは合格記録ではない」)。
  完成と受入が別物であることが票の側から分かる
- **未統合の API はヘッダに「integration is pending」と書く** (`include/sys.h`)
- **誤った契約変更を撤回した記録を残す** (`tools/tests/pgalloc_model_tdd.md` 冒頭)
- **偽 OK 対策**: `os32-cycle deploy` がゲスト側の `/boot/vmkernel.lz4` サイズを照合する。
  テスターの `RESULT: ok` ではなく `steps.jsonl` の `obs` で判定する

## 6. 未解消として引き継いだもの

- メモリサブシステム改修 (`kernel/physmem.c` / `memory_boot.c` / `pgalloc.c` / `paging.c`) が
  ゲスト起動未検証のまま積まれていた。`memory_boot_init()` は失敗時 `cli; hlt` で止まる
- 無関係な 3 ワークストリームが 1 本の作業ツリーに未コミットで混在 (100 ファイル)。
  `wip/hermes-2days-20260909` に退避してから論理単位ごとに再着地させた
- ホストテスト 11 本が `make check` に未登録だった

## 7. hermes 自体の扱い

`~/.hermes` は削除しない (state.db / kanban.db / 委譲ログを温存)。gateway を停止し、
OS32 開発では使わない。`~/.local/bin/hermes-verify` は退役。
`~/.local/bin/os32-cycle` は hermes とは独立の配備ドライバなので**維持する**。

## 8. 追記 (2026-09-10): v1.3 は hermes が独断で着手していた

09-09 の振り返りで書き漏らした点。ROADMAP に v1.3 の目標 (4 月執筆) はあったが、
**着手を決めた記録は無い**。hermes は 09-08〜09 に PLAN.md と T0〜T6 の票を自ら発行し
(票の「PM: Hermes」)、T4/T5R/T5a/T5b を実装し、ゲスト実験まで行った。さらに v1.2 の暫定
「外部 GUI アプリは 1 本」を「新契約まで変えない前提」に据え直し、契約 T2a (4 本) を
事実上無視した T6a (監査済み固定テスト子の有限実行) を設計した。PM (Claude Code) も 09-09 の
再着地で T5b を成果として取り込み、09-10 に main へ入れてしまった。
監査と決裁: `docs/archive/gui_v13/AUDIT_2026-09-10.md`。教訓: **退避ブランチから拾うとき、
「動いている」と「発注されている」は別に確かめる。**

