# CLAUDE.md — AI コーディングアシスタント向けガイダンス

AI コーディングアシスタント共通の入口。**置くのは指示と参照だけ**で、
番地・KAPI 表・ファイル地図・障害の経緯といった技術情報の本文は置かない。更新先は
[`docs/INDEX.md`](docs/INDEX.md) 冒頭の「情報単位ごとの正典」表が 1 か所に決めている。

## 体制

**体制の正典は [`docs/tasks/agents/ROLES.md`](docs/tasks/agents/ROLES.md) §0** (ここは要約、モデル名と起動の形はそこ)。
3 役: **PM** (Claude Code Opus 5.5 — 分解・統合・受入・権限)、**実装者** (Codex、worktree 隔離)、**独立レビュアー** (読み取りだけ、書き手と別のモデル)。
実装者の完了条件は**依頼文に列挙された検査 + 新しく足した試験**の rc=0。全体検査 (`make all` + `make check`) は
PM が**取り込みのまとまりごとに統合状態で 1 回** — 修正のやり直しの中で全体検査を回さない。触るファイルが衝突しない段は並列で出せるだけ出す。試験・検査の実行は全体で同時に 2 本まで (`check_slot.sh`)。
票に書くのは 1 段 10 行以内 (決定・未確認・持越し・証拠の所在)。延ばした試験は [`docs/tasks/DEFERRED_TESTS.md`](docs/tasks/DEFERRED_TESTS.md) の 1 か所に書く。
**PM の推奨は基本的に承認、[D2] と実機の物理操作は個別承認**。
**3 ラリーで決着しない争点はユーザーへ**。
**このリポジトリ os32-v3 が現行 (v3)** (2026-09-30 に os32 から fork)。os32 の v2.x (タグ `v2.1`、`main`) は戻り先 —
新機能は入れず、文書の正典もここ ([`docs/ROADMAP.md`](docs/ROADMAP.md) §0-1)。
**カーネル層 (カーネル本体・VFS/FS・exec/ページング・KAPI・shlib 読み込み) に分かっている不具合が
あるあいだは、新機能より先に直す** — 理由と適用の仕方は
[`docs/POLICY_DEV.md`](docs/POLICY_DEV.md) §1。
承認済みスコープの中では止まらずに進め、止まるのは [D1]〜[D3] の承認・仕様の分岐・スコープ拡大・
**独立レビューが要る地点** の 4 つだけ。レビューは PM が代行せず、ROLES §5 の書式で依頼する。

## Build Commands

Full target list and compiler flags: [`docs/08_build.md`](docs/08_build.md#ビルドターゲット) §8-4 / §8-2.
Which build and which verification a change actually needs: skill **`os32-build-verify`**.

```bash
make all / kernel / programs              # build (SDK and images come with `all`)
make external                             # apps/ + game/ — after any KAPI or SDK library change
make check-<name> / check-fast / check     # the checks named in your request / everything without mutants / everything with mutants (PM, once per landing batch) — docs/08_build.md §8-4
make clean                                # required after a KAPI struct change ([ABI3])
make deploy                               # HostDrv (C:\os32) — no reboot, not verification ([V1])
make deploy-kernel / deploy-boot          # NHD / boot area — stop NP21/W first ([D1])
```

Which of the three deploy paths a change needs: [`docs/08_build.md`](docs/08_build.md#配備3経路) §8-4.

Guest (NP21/W): drive it through skill **`run-os32`** and `tools/np21w_mcp/`; stop / start only with
`tools/np21w_ctl.py` ([D1], [`docs/POLICY_DEBUG.md`](docs/POLICY_DEBUG.md) §5).

## Project Constraints

規則の正典は [`docs/CONSTRAINTS.md`](docs/CONSTRAINTS.md) — 理由と詳細はそこにある。ここは規則行だけで、
ずれは `make check` (`tools/check_constraints.py`) が ID で検出する。
[D1]〜[D3] の一部の操作には `.claude/settings.json` の `ask` / `deny` も設定している。
これは包括的な保護ではない。NP21/W の停止確認や別コマンド経由の操作にも正典の規則を適用する。

- **[C1]** C11 (GNU11) for internal code — kernel, boot, userland and SDK implementation build with `-std=gnu11`; public SDK headers stay C89/GNU89-compatible and SQLite keeps GNU89. No implicit declarations / implicit int / VLA; no new anonymous structs/unions, `restrict`, atomics, TLS or thread APIs.
- **[C2]** In the kernel use `kstrncpy` / `kstrncat` / `kstrlen` / `kstrcmp` (`lib/kstring.h`), never libc.
- **[C3]** Functions exposed to external programs need `__cdecl` wrappers in `kapi/`.
- **[C4]** No hardcoded constants — follow the three-layer constant scheme.
- **[C5]** Physical addresses become pointers through `P2V` / `P2V_IO` (`P2V_CONST` / `P2V_IO_CONST` only for constant initializers); pointers passed to device/PTE/CR3 use `V2P`. App/lease virtual addresses use AS/SURFACE translation. Exceptions are recorded in `tools/check_p2v_allow.txt`.
- **[HW1]** Never use EGC / GRCG / GDC drawing commands. The CPU writes straight to VRAM at `0xA8000`.
- **[HW2]** DMA buffers must not straddle a 64KB boundary.
- **[ABI1]** `sdk/kapi.json` is the single source of truth. Never hand-edit the generated files.
- **[ABI2]** Append KAPI entries only. Never reorder or delete an existing slot.
- **[ABI3]** After a KAPI change, bump the version and run `make clean` → `make all`.
- **[V1]** `make deploy` (HostDrv) alone is not verification — PATH prefers the NHD's `/usr/bin`.
- **[V2]** Register every launchable binary in its own layer's `deploy.yaml`.
- **[V3]** Do not shorten the curl timeout for remote execution (15s minimum, 60s+ for long runs).
- **[V4]** Report failures and skipped steps as they happened. Never present something unverified as a pass.
- **[D1]** Never deploy to the NHD while NP21/W is running. Stop → deploy → start.
- **[D2]** Get approval before irreversible operations (overwriting the NHD or image masters,
  `rm -rf`, `git reset --hard`, editing `*.ini`).
- **[D3]** Never put the contents of `.env`, API keys or passwords in output.

## 触る前に読む 1 か所

| 何をするか | 読む場所 |
|---|---|
| 知らないサブシステムに触る | [`docs/DEVELOPMENT.md`](docs/DEVELOPMENT.md) §1 (作業別の入口)・§2 (ファイル地図) |
| **落とし穴を先に知る** (検証・配備 / 実機と NP21/W の差 / カーネル・FS / GUI・入力) | [`docs/POLICY_DEBUG.md`](docs/POLICY_DEBUG.md) §4 冒頭の「領域別の早見」 |
| 番地・帯域 | [`docs/02_memory.md`](docs/02_memory.md) §2-1 (説明の正典。地図は `make docs-gen` → `build/out/MEMMAP.md`、定義は `include/memmap.h`) |
| ビルド・配備・検証を選ぶ | スキル **`os32-build-verify`**、[`docs/08_build.md`](docs/08_build.md) §8-4 |
| KAPI の追加・変更 | スキル **`os32-kapi-add`**、[`docs/KAPI_SPEC.md`](docs/KAPI_SPEC.md) §3-1 ([ABI1]〜[ABI3]) |
| エミュレータ上の障害 / ini の変更 | スキル **`os32-emu-debug`** / **`os32-emu-config`** (ini は PM だけ) |
| 実行モデル (CPL=3、資源回収) / 描画 | [`docs/09_exec.md`](docs/09_exec.md) / [`docs/05_drivers.md`](docs/05_drivers.md) §5-5 |

**External programs** — OS32X flat ELF binaries (`sdk/link/app.ld`, `sdk/crt/crt0.asm`); `main()` must be the
**first function** in the source file. In-tree sources live under `userland/` (`cmds/` (25 commands), `shell/`,
`system/`, `tests/`, `rust/`, `lib/`); `apps/` and `game/` are submodules built by `make external` — rebuild
them after any KAPI **or SDK library** change.

## Documentation

ソースツリーの図は [`docs/08_build.md`](docs/08_build.md) §8-3 が正典。

| Document | Content |
|---|---|
| [`docs/INDEX.md`](docs/INDEX.md) | 索引。冒頭の「情報単位ごとの正典」表が**更新先を 1 か所に決める**。§1〜§10 の技術仕様と `docs/tasks/` の領域別設計もここから辿る |
| [`docs/CONSTRAINTS.md`](docs/CONSTRAINTS.md) | 制約規則の正典 ([C1]〜[D3] の理由と詳細) |
| [`docs/DEVELOPMENT.md`](docs/DEVELOPMENT.md) | 作業別の参照先 (§1) とファイル地図 (§2) — 未知のサブシステムはここから |
| [`docs/KAPI_SPEC.md`](docs/KAPI_SPEC.md) | KernelAPI 仕様と追加手順 (§3-1) |
| [`docs/POLICY_DEV.md`](docs/POLICY_DEV.md) / [`POLICY_DEBUG.md`](docs/POLICY_DEBUG.md) | 開発規約 / デバッグ (反映確認 §2、教訓集 §4、道具箱 §5) |
| [`docs/ROADMAP.md`](docs/ROADMAP.md) | リリース計画 (v1.x GUI シェル〜) |
| `docs/hw/` | PC-9800 ハード資料のミラー (`tools/sync_hwdocs.sh`)。**著作権物・gitignore・コミット禁止**。Bible と矛盾したら UNDOCUMENTED を採る |
| `~/np21w-src/docs/` (WSL 側のフォーク) | NP21/W ai-debug フォーク。WSL 側が正で、`make build && make deploy` で Windows にミラーされる |
