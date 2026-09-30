# v1.4 メモ (別エージェント案は 2026-09-14 に撤回 — 実装は Claude Code の Opus 5 サブエージェントに一本化)

> 発行: Claude Code (2026-09-14) / 状態: **撤回 (2026-09-14)**

この方針は 2026-09-14 に撤回された。実装は基盤・アプリ層とも Claude Code が Opus 5 サブエージェントで行う ([ROLES.md](../../tasks/agents/ROLES.md) §0)。以下の本文と §1 の表は撤回前の記述として残す。

発行: Claude Code (設計者 / レビュアー、2026-09-14)。
体制の正典は [ROLES.md](../../tasks/agents/ROLES.md) §0: **別エージェントが担うのはアプリケーション層だけ** (About / GUI エディタ / N4 のアプリ側)。基盤 (Host Services N1〜N3、libos32gui の `host_*`、R2) は Claude Code が従来どおり PM として進める。この文書は「今どこにいて、次に何をするか、踏むと痛い所」だけを書く。

## 1. 今どこにいるか

| 領域 | 状態 (2026-09-14) | 根拠 |
|---|---|---|
| v1.3 | 全項目受入済み、main にマージ済み (`fac0d89`)。残件の小物 4 件 (§3) は Claude Code が処理中 | `docs/ROADMAP.md` v1.3、`docs/tasks/gui/v13/PLAN.md` |
| Host Services N0 (設計) | 完了 = `docs/archive/network/TASK_N0.md` **第 5 版** (Codex 4 往復、最後は 4 件残したまま決裁 b で N1 へ。残った疑いは N1 のホスト TDD が踏む) | TASK_N0 §7 |
| N1 / N2 / N3 (Host Services) | **すべて受入完了 (2026-09-14)**。N1 (KAPI v51、kselftest 87/0)、N2 (Agent PRINT/CLIP)、N3 (libos32host + wget/lpr/hclip/hdate 実動)。**F6 解決** (実 wget は >64KB 完走、自己試験固有の artifact)。N3-fix 着地済み。**LGY-98 既定ビルド化済み** (ゲスト検証済み、`make kernel` = LAN 有効 flags 0)。残: N4 (基盤 = libos32gui host_* 末尾追記 [Claude Code PM]、アプリ層 = ファイラ印刷・端末コピペ [別エージェント]) (host_test 26/26、L3 OK。kselftest 86/1・L0〜L2 の計数・再送計数・WINDOW 二重 = F1〜F5、TASK_N1 §3。Codex 実装レビューと合わせて修正票へ) (`995bb19` + `f5dca53`)、ホスト試験 54 ケースは worktree で緑。**テスターで `make clean` → `make all` (71s) → `make check` (53s) → `make external` (7s) すべて exit 0** (2026-09-14、session `n1-build` / `n1-build2`、`-Inet` の修正 `f5dca53` 後)。**ゲスト受入は未実施** | `docs/archive/network/TASK_N1.md`、`tools/tests/n1_tdd.md` |
| v1.4 の範囲 | ROADMAP §1 v1.4 = N1〜N4、R2 (PEGC / Cirrus 8bpp)、About、GUI エディタ。アプリ群は v2.0 以降へ | `docs/ROADMAP.md` (`23a28be`) |

## 2. 次にやること (順)

**基盤側 (Claude Code、参考)**: N1 のゲスト受入 → N1 実装レビュー → N2 / N3 の票 → libos32gui `host_*` 末尾追記 → R2。

**アプリ層 (別エージェント)**:
1. **About ダイアログ** — 票は Claude Code が書く (`docs/tasks/gui/v14/TASK_ABOUT.md`、予定)。依存無し、最初に着手できる。
2. **GUI テキストエディタ** — 票 `docs/tasks/gui/v14/TASK_EDITOR.md` (予定)。設定は `os32gui_cfg_*` ラッパー経由 (`app:editor` scope)、保存 / 読込は libos32gui のファイル API。印刷とクリップボードは N3 / libos32gui `host_*` が揃ってから (票に「要求 API」として書く)。
3. **N4 のアプリ側** (ファイラの「印刷」、端末のコピー / 貼り付け) — 基盤側の `host_*` ラッパーが着地してから。
着手の条件: 票が「設計レビュー通過」になっていること。票が無い作業は始めない (要るなら Claude Code に票を依頼する)。

## 3. v1.3 残件の小物 (Claude Code が緑にしてから渡す)

| 件 | 内容 | 状態 |
|---|---|---|
| タスクバー経路 | 消えた被覆の反対側 4 本を追加 (製品コード変更なし、gshell 100 passed) | **着地 `1b56db3`** |
| `stat` | `userland/cmds/stat.c` (st_dev の decode、st_ino)。ゲストで `/` = hd0 / ino=2、settings.db と .bak が別 inode を確認 | **着地 `996d21e`** |
| S6 `tar` | microtar を vendor、`tar c|x|t`。HostDrv で往復 + Python tarfile 相互読み OK。**ext2 上は 2KB でも 15 秒超 → 別票 S6-P (ext2 の小書き込み性能)** | **着地 `2ad4203`** |
| 試験の棚卸し | `docs/archive/TEST_INVENTORY_2026-09-14.md` (76 行、推奨 R1〜R12、採否は未決) | **着地 `78e9f44`** |

小物ではないので**保留** (ユーザーの再考待ち): F3a〜c (SQLite VFS の正直化 / lock 表 / open フラグ)、F2c、FEP_BOUNDARY、MEMORY_RAM_INTEGRATION、DEVICE_RESERVATION (`docs/archive/settings/S0_PLAN_2026-09-13.md` の後回し欄)。

## 4. 踏むと痛い所 (v1.3 で実際に踏んだもの)

- **PM はビルド・`make check`・配備を自分で回さない** (テスターへ。`tools/emu_agent/agent.py run "<英語のタスク>" --quiet --session <name>`、合否は `tools/emu_agent/logs/<session>/steps.jsonl` の `obs` で判断)。テスターの `make` は `MAKE_TARGETS` に登録された名前だけ通る (新しい `check-*` を足したら `agent.py` にも足す)。
- **NHD 配備の順序** (2026-09-25 以降、停止・起動は `tools/np21w_ctl.py stop` / `start` — [`docs/POLICY_DEBUG.md`](../../POLICY_DEBUG.md) §5): taskkill → `nhd-pull` (NP21/W が動いていると Permission denied) → `os32-cycle deploy` → `make deploy` (HostDrv) → 起動 → kselftest。`hsync` の前に `make deploy` (古いカーネルに戻った前例)。
- **FDD ブート中に `make all` をしない** (NP21W_DIR の `os32_boot.d88` を上書きする)。HDD ブートかは `ls /hd0` が無いこと + `/boot/vmkernel.lz4` があることで見る。
- **`/api/cmd` は POST body** (`curl -X POST .../api/cmd --data-binary "ver"`)。対話プログラムは ESC + `/api/key` + tvram polling (`scratchpad/fdd_run2.py` の方式)、`ime on` 中は `SHIFT+SPACE` を先に。255B 超の引数は rshell の行が壊れる。
- **worktree の差分は基点 commit と比較して着地** (`git diff --cached <基点>`、feat/gui HEAD と比べると他レーンのファイルが削除として混入)。着地後に D が無いこと、conflict マーカーが無いことを全ファイルで確認。
- **`.inc` を変えても `.bin` が再ビルドされない** (userland の依存に無い) → `build/programs.mk` に明示依存をパターン規則の**後**に置く。
- **KAPI を変えたら `make clean` → `make all` → `make external`** ([ABI3])。`kapi/` から `net/` のヘッダを include するなら `build/config.mk` の `INC_KAPI` (N1 で `-Inet` を足した)。
- **Codex レビューは 3 往復まで、解決しなければユーザーへ** (ROLES §5)。判定の見出しは `## 判定` を `rfind` で拾う。網羅指示 (「今回の往復で見つかる到達可能な欠陥をすべて挙げる、経路ごとに見た / 見ていない」) を毎回入れる。
- **設定の読み書きは OS 経由だけ** (S2 決裁): アプリは `os32gui_cfg_*` ラッパー、`app:` scope のみ。新しいアプリ (About / エディタ) もこれに従う。
- 新しい層を実装する票には**移植性調査** (`docs/tasks/portability/SURVEY_<票>.md`、観点は TASK_N1 §0 段 7) を含める (ユーザー指示 2026-09-14)。

## 5. 主要な入口

`CLAUDE.md` → `docs/INDEX.md` (正典表) → `docs/tasks/network/HOST_SERVICES_PLAN.md` (§7 票の順序、§9 決裁済みの既定) / `docs/archive/network/TASK_N0.md` (契約) / `docs/archive/network/TASK_N1.md` (実装と受入)。スキル: `os32-build-verify`、`os32-emu-debug`、`os32-kapi-add`、`os32-emu-config`、`os32-local-ai`。
