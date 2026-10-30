# CLAUDE.md — AI コーディングアシスタント向けガイダンス

指示と参照の入口。更新先は [docs/INDEX.md](docs/INDEX.md) を参照。

## 体制

正典は [ROLES §0](docs/tasks/agents/ROLES.md)。
3 役: PM (分解・統合・受入・権限)、実装者 (worktree 隔離)、独立レビュアー (読み取りだけ、書き手と別のモデル)。
**PM の推奨は基本的に承認、[D2] と実機の物理操作は個別承認**。**3 ラリーで決着しない争点はユーザーへ**。
承認済みスコープの中では止まらずに進め、止まるのは [D1]〜[D4] の承認・仕様の分岐・スコープ拡大・独立レビューが要る地点。
**このリポジトリ os32-v3 が現行** (os32 の v2.x は戻り先、[VERSIONS](docs/decisions/VERSIONS.md) §2)。
**カーネル層に分かっている不具合があるあいだは新機能より先に直す**、**カーネルの機能は整数演算が第一、浮動小数点が要るときは実装前にユーザーに確認** ([POLICY_DEV](docs/POLICY_DEV.md) §1・§2)。

実装者の完了条件は `make all` + 足した・触った試験を `MUTATE=0` + `tools/checks/run.py --lint` の rc=0。複数段の作業は ROLES §0 に従う。

## Build Commands

ビルド・配備・検査の正典は [docs/08_build.md](docs/08_build.md) §8-4。
`make all` / `make kernel` / `make programs` でビルドする。
`make check-<name>` / `check-changed` / `check` — 指定の検査 / 変更に当たる検査 (段は各検査の `CHECK` 宣言、`tools/checks/run.py`) / 全部を変異込み (節目に PM が 1 回)。
PM の作業のスキル: **`os32-design-rally`** (設計の往復)・**`os32-delegate`** (実装の依頼)・**`os32-land`** (取り込み・検査・push)・**`os32-realhw-update`** (実機の更新はノートのセッションに頼む)・**`run-os32`** (NP21/W のゲストを叩く)。
KAPI 変更後は `make clean` → `make all` ([ABI])。
`make deploy` は HostDrv、`make deploy-kernel` / `deploy-boot` は NP21/W 停止後 ([D1])。

## Project Constraints

正典: [docs/CONSTRAINTS.md](docs/CONSTRAINTS.md)。

- **[C1]** 内部は GNU11、公開 SDK ヘッダは C89/GNU89、SQLite は GNU89。暗黙の宣言・int、VLA 等を禁止。
- **[C2]** カーネルの文字列操作は `lib/kstring.h` を使う。
- **[C4]** 番地・サイズ・パスは定数の 3 層管理に従う。
- **[C5]** 物理番地とポインタの変換は `P2V` / `V2P` 等に集約する。
- **[ABI]** `sdk/kapi.json` を正とし、末尾追加・版上げ・clean ビルドを行う。ラッパーは生成する。
- **[HW1]** EGC / GRCG / GDC の描画コマンドを使わず HAL を通す。
- **[HW2]** DMA バッファは 64KB 境界をまたがない。
- **[HW3]** 新しいハードウェアはメーカー資料と BSD/MIT 実装を参照し、GPL を写さず、禁止されたリバースエンジニアリングをしない。
- **[V1]** HostDrv の配備だけでは検証にならない。
- **[V3]** リモート実行の curl タイムアウトは最低 15 秒、長時間は 60 秒以上。
- **[V4]** 失敗・手順飛ばし・未確認をそのまま報告する。
- **[D1]** NP21/W を停止してから NHD に配備する。
- **[D2]** 不可逆な操作は事前に承認を得る。
- **[D3]** 秘密情報の内容を出力しない。
- **[D4]** `docs/archive/` はユーザーの指示があるときだけ参照し、作業の根拠にしない。

## 触る前に読む 1 か所

| 何をするか | 読む場所 |
|---|---|
| 知らないサブシステムに触る | [`docs/WORK_GUIDE.md`](docs/WORK_GUIDE.md) (作業別の入口)・[`docs/DEVELOPMENT.md`](docs/DEVELOPMENT.md) §2 (ファイル地図) |
| **落とし穴を先に知る** (検証・配備 / 実機と NP21/W の差 / カーネル・FS / GUI・入力) | [`docs/POLICY_DEBUG.md`](docs/POLICY_DEBUG.md) §4 冒頭の「領域別の早見」 |
| 番地・帯域 | [`docs/02_memory.md`](docs/02_memory.md) §2-1 (説明の正典。地図は `make docs-gen` → `build/out/MEMMAP.md`、定義は `include/memmap.h`) |
| ビルド・配備・検証を選ぶ | スキル **`os32-build-verify`**、[`docs/08_build.md`](docs/08_build.md) §8-4 |
| KAPI の追加・変更 | スキル **`os32-kapi-add`**、[`docs/KAPI_SPEC.md`](docs/KAPI_SPEC.md) §3-1 ([ABI]) |
| エミュレータ上の障害 / ini の変更 | スキル **`os32-emu-debug`** / **`os32-emu-config`** (ini は PM だけ) |
| 実行モデル (CPL=3、資源回収) / 描画 | [`docs/09_exec.md`](docs/09_exec.md) / [`docs/05_drivers.md`](docs/05_drivers.md) §5-5 |

**External programs** — The CRT calls `main` by name; see the [SDK application contract](sdk/README.md#アプリを書くときの約束).
In-tree programs live under `userland/`. The `apps/` and `game/` submodules are deferred under
[H-7](docs/tasks/v3/T2H_EXTERNAL_GATE.md#再開前に揃える-8-項目); rebuild after KAPI or SDK library changes once reopening is approved.

## Documentation

ソースツリーの図は [`docs/08_build.md`](docs/08_build.md) §8-3 が正典。

| Document | Content |
|---|---|
| [`docs/INDEX.md`](docs/INDEX.md) | 索引。冒頭の「情報単位ごとの正典」表が**更新先を 1 か所に決める**。§1〜§10 の技術仕様と `docs/tasks/` の領域別設計もここから辿る |
| [`docs/CONSTRAINTS.md`](docs/CONSTRAINTS.md) | 制約規則の正典 ([C1]〜[D4] の理由と詳細) |
| [`docs/WORK_GUIDE.md`](docs/WORK_GUIDE.md) / [`docs/DEVELOPMENT.md`](docs/DEVELOPMENT.md) | 作業別の入口 (索引) / ファイル地図 (§2) — 未知のサブシステムはここから |
| [`docs/KAPI_SPEC.md`](docs/KAPI_SPEC.md) | KernelAPI 仕様と追加手順 (§3-1) |
| [`docs/POLICY_DEV.md`](docs/POLICY_DEV.md) / [`POLICY_DEBUG.md`](docs/POLICY_DEBUG.md) | 開発規約 / デバッグ (反映確認 §2、教訓集 §4、道具箱 §5) |
| [`docs/ROADMAP.md`](docs/ROADMAP.md) | リリース計画 (v1.x GUI シェル〜) |
| `docs/hw/` | PC-9800 ハード資料のミラー (`tools/sync_hwdocs.sh`)。**著作権物・gitignore・コミット禁止**。Bible と矛盾したら UNDOCUMENTED を採る |
| `~/np21w-src/docs/` (WSL 側のフォーク) | NP21/W ai-debug フォーク。WSL 側が正で、`make build && make deploy` で Windows にミラーされる |
