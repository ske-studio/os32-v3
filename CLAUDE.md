# CLAUDE.md — AI コーディングアシスタント向けガイダンス

AI コーディングアシスタント共通の入口。**置くのは指示と参照だけ**で、
番地・KAPI 表・ファイル地図・障害の経緯といった技術情報の本文は置かない。更新先は
[`docs/INDEX.md`](docs/INDEX.md) 冒頭の「情報単位ごとの正典」表が 1 か所に決めている。

## 体制

**現行の体制の正典は [`docs/tasks/agents/ROLES.md`](docs/tasks/agents/ROLES.md) §0** (1 表。ここは要約)。
PM = Claude Code (**`claude-opus-5-5`**、2026-09-29 夕〜)、コーダー = worktree 隔離で既定 **Codex `gpt-6.1-sol`** (2026-10-01〜、
`codex exec -m gpt-6.1-sol -s workspace-write`)、**Codex が重大 (P1 / major) と判定した指摘を含む修正は Fable 5.1**、レビュアー = **Codex だけ** (`codex exec -s read-only`。
モデルを名乗らせ、**astra でなければそのセッションは休ませて Fable 5.1 が代行**)、**ドライバ設計のレビューは必ず Codex と突き合わせる**。
テスター = ローカル AI (`tools/emu_agent/`、スキル `os32-local-ai`)。コーダーの完了条件は `make check-changed` の rc=0、
PM は着地で `make all` + `make check` を 1 回。**PM の推奨は基本的に承認、[D2] と実機の物理操作は個別承認**。
**3 ラリーで決着しない争点はユーザーへ**。
**このリポジトリ os32-v3 が現行 (v3)** (2026-09-30 に os32 から fork)。os32 の v2.x (タグ `v2.1`、`main`) は戻り先 —
新機能は入れず、文書の正典もここ ([`docs/ROADMAP.md`](docs/ROADMAP.md) §0-1)。
**カーネル層 (カーネル本体・VFS/FS・exec/ページング・KAPI・shlib 読み込み) に分かっている不具合が
あるあいだは、新機能より先に直す** — 理由と適用の仕方は
[`docs/POLICY_DEV.md`](docs/POLICY_DEV.md) §1。
承認済みスコープの中では止まらずに進め、止まるのは [D1]〜[D3] の承認・仕様の分岐・スコープ拡大・
**独立レビューが要る地点** の 4 つだけ。レビューは PM が代行せず、ROLES §5 の書式で依頼する。

## Project Overview

OS32 is a 32-bit bare-metal OS for NEC PC-9801/9821 machines, built with an i386-elf GCC
cross-compiler and NASM. The kernel runs in protected mode at physical 0x100000; external
programs load at 0x500000 and run at CPL=3 in their own page directory.

## Build Commands

Full target list and compiler flags: [`docs/08_build.md`](docs/08_build.md#ビルドターゲット) §8-4 / §8-2.
Which build and which verification a change actually needs: skill **`os32-build-verify`**.

```bash
make all / kernel / programs              # build (SDK and images come with `all`)
make external                             # apps/ + game/ — after any KAPI or SDK library change
make check-fast / check-changed / check   # no mutants / mutants only for what you changed / all mutants (before merging) — times: docs/08_build.md §8-4
make clean                                # required after a KAPI struct change ([ABI3])
make deploy                               # HostDrv (C:\os32) — no reboot, not verification ([V1])
# userland delivery: make deploy (host -> C:\os32) then `hsync` in the guest
#   hsync skips /sys (running shell + shlibs) unless you name it: `hsync sys`
make deploy-kernel / deploy-boot          # NHD / boot area — stop NP21/W first ([D1])
```

Which of the three deploy paths a change needs: [`docs/08_build.md`](docs/08_build.md#配備3経路) §8-4.

### Talking to the guest (NP21/W)

The debug HTTP server is built into `np21x64w.exe` (the ai-debug fork), enabled with `aidebug=true` /
`aidbport=8025` in `np21x64w.ini` — no relay process. Endpoints `/api/cmd` `/api/key` `/api/mouse`
`/api/tvram` `/api/screenshot`; the curl recipes, the `ax/ay` formula and the URL-encoding trap are in
[`docs/POLICY_DEBUG.md`](docs/POLICY_DEBUG.md) §5. Registers, memory, disassembly, breakpoints and
tracing go through `tools/np21w_mcp/`. Chasing a failure on the emulator: skill **`os32-emu-debug`**.
Stop / start NP21/W only with `tools/np21w_ctl.py` (`stop`, `start --ini <name> [--fd <name>] --wait-ready`,
`status`, `fdd`, `cd`) — it stops through `/api/quit`, force-kills only the process whose exe path matches, and
waits for the image locks to clear before starting; a hand-typed `taskkill` → `Start-Process` stalls the boot
([`docs/POLICY_DEBUG.md`](docs/POLICY_DEBUG.md) §5, §4-60).

## Project Constraints

規則の正典は [`docs/CONSTRAINTS.md`](docs/CONSTRAINTS.md) — 理由と詳細はそこにある。ここは規則行だけで、
ずれは `make check` (`tools/check_constraints.py`) が ID で検出する。
[D1]〜[D3] の一部の操作には `.claude/settings.json` の `ask` / `deny` も設定している。
これは包括的な保護ではない。NP21/W の停止確認や別コマンド経由の操作にも正典の規則を適用する。

- **[C1]** C11 (GNU11) for internal code — kernel, boot, userland and SDK implementation build with `-std=gnu11`; public SDK headers stay C89/GNU89-compatible and SQLite keeps GNU89. No implicit declarations / implicit int / VLA; no new anonymous structs/unions, `restrict`, atomics, TLS or thread APIs.
- **[C2]** In the kernel use `kstrncpy` / `kstrncat` / `kstrlen` / `kstrcmp` (`lib/kstring.h`), never libc.
- **[C3]** Functions exposed to external programs need `__cdecl` wrappers in `kapi/`.
- **[C4]** No hardcoded constants — follow the three-layer constant scheme.
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

## Architecture

**Memory layout** — definitions in `include/memmap.h`, the explanation in
[`docs/02_memory.md`](docs/02_memory.md) §2-1. Bands only:

| Band | Contents |
|---|---|
| `0x00000–0x9FFFF` | Conventional — font cache, Unicode table (0x4A000), GFX backbuffer (0x6A000), V86 test magic (0x8C000), autoplay mailbox (0x90000). Handed whole to the V86 guest |
| `0xA0000–0xFFFFF` | VRAM (text + graphics planes) and BIOS ROM |
| `0x100000–0x2FFFFF` | Kernel (binary + heap + KAPI + 224KB SHM), then SQLite from 0x200000; the 16KB kernel stack sits at the **top of the SQLite band** with its guard below it, and the DMA pool is a fixed 64KB hole at 0x2E8000 in the reserve below it. **The detailed map is generated — `docs/02_memory.md` §2-1 is the only copy** (`tools/gen_memmap.py`, checked by `make check`) |
| `0x300000–0x4FFFFF` | Resident shell (two heaps: newlib sbrk, exec_heap at 0x380000), then the shared-library band — `libos32gui.shlib` `.text` is shared across PDs, `.data`/`.bss` per app |
| `0x500000–` | External programs: code+bss → sbrk → guard → exec_heap → stack |
| arena top | The PEGC 8bpp backbuffer (300KB) — allocated at boot from the pool at the top of the CPL=0 arena (owner boot → gshell, a ledger SURFACE); `sys_usable_mem_end()` is frozen just below it (`ledger_arena_top`). Cirrus uses its linear window instead |
| `0xFE000000–0xFEFFFFFF` | Device-window band (v3 layout, landed in v2.1): the Cirrus linear window lives here; the PT for the first 4MB of the band is static. Decide device windows from the physical map, never the RAM ceiling |

**Subsystem map** — which file does what and which spec section covers it:
[`docs/DEVELOPMENT.md`](docs/DEVELOPMENT.md) §2; the task-to-entry-point table is §1.
Three facts that matter on almost every change:

- External programs run at CPL=3 with their own page directory, so a bad pointer kills only the
  app (`fault_kill_count`). The resident shell and `mkos32x --cpl0` binaries are the exceptions.
- SQLite lives in the kernel at 0x200000 with one fixed 384KB MEMSYS5 pool shared by every
  connection (the FEP dictionary included). Close connections at the end of `*_init()`.
- `exec_exit()` reclaims FDs / redirects / pipe buffers **by owner** (exec nest level).
  Kernel-resident FDs need `vfs_fd_set_protect(fd, 1)`; returning to a parent uses
  `exec_heap_restore_state()`, never `exec_heap_init_at()`.

**KernelAPI** — adding or changing one: skill **`os32-kapi-add`**; the procedure's canon is
[`docs/KAPI_SPEC.md`](docs/KAPI_SPEC.md) §3-1 and the rules are [ABI1]–[ABI3]. The struct,
`__cdecl` wrappers, init table and Rust bindings are all generated from `sdk/kapi.json`.

**External programs** — OS32X flat ELF binaries linked with `sdk/link/app.ld` and entered through
`sdk/crt/crt0.asm`; `main()` must be the **first function** in the source file. In-tree sources are
under `userland/`: `shell/` (resident at 0x300000), `cmds/` (25 commands), `system/`, `tests/`,
`rust/` (no_std Cargo workspace), `lib/` (`libos32*`, statically linked). Standard apps and the
board-game RPG are submodules (`apps/`, `game/`) built by `make external` — rebuild them after any
KAPI **or SDK library** change ([`docs/08_build.md`](docs/08_build.md) §8-4).

**Graphics** — CPU writes to VRAM at `0xA8000` ([HW1]): draw into the backbuffer, then
`gfx_present()` flips the page. Modes and the gfx / libos32gfx split:
[`docs/05_drivers.md`](docs/05_drivers.md) §5-5.

## ⚠️ Known Gotchas

1 行ずつ。症状・経緯・検証はリンク先 (§ 番号だけのものは [`docs/POLICY_DEBUG.md`](docs/POLICY_DEBUG.md))。

**検証・配備**
- 配備の成否は文言で判断しない — ゲストの `ls -l /boot/vmkernel.lz4` と手元のサイズ、kselftest (`kselftest_pass` / `_fail`) は**新しい** `kernel.map` の番地で読む。NHD の作業イメージは `build/nhd/os32.nhd`。→ §2、§4-17、§4-29
- `deploy.yaml` に無いバイナリは NHD で古いまま残り rshell を止める ([V2])。`hsync` は HostDrv の内容で上書きするので NHD 配備の後は先に `make deploy`、`/sys` は `hsync sys` + リセット (GUI の窓が例外 0 件で出ない = shlib が古い)。→ §4-12、§4-33、§4-36、§4-42
- 保存の試験はバイト列で突き合わせ、冪等も繰り返して見る。通知のある API は入力経由と API 経由の両方を見る。→ §4-46、§4-44
- 変異試験は実物のソースを書き換えない (写しの木、`tools/tests/mutpar.py`)。検査の 3 段と所要時間は [`docs/08_build.md`](docs/08_build.md) §8-4。→ §4-40、§4-41

**実機と NP21/W の差** (ここは「エミュレータで確認済み」が通用しない)
- 実機の画面モード設定は NP21/W の BIOS 値ではなく**実機 ROM の OUT 列 (`v86 -g`) に合わせる** (Ra266 の PEGC 640x480)。→ §4-62
- シリアルは 8253 の整数分周 (既定 9600、38400 は 1.9968MHz で 41600 に化ける)、PIT は判定したクロックで割る。NP21/W は通信速度も 2.4576MHz 系も模擬しない。→ §4-49、§4-50、§4-54
- FDC: 時間上限は機構の最悪値から、0x94 は FRY を立てる、1MB 超への DMA は 0439h bit2。FD は 2HD 1232KB と 1.44MB の生イメージ (IPL は 512 バイトしか読まれない)。→ §4-47、§4-48、§4-51、§4-53
- キーボード 8251 のコマンド語は 0x16 (0x14 は再送要求)。0035h は全体で書かない (BUZ は 07h = 停止)。→ §4-57、§4-59
- `io_wait()` を万単位で連打しない (待ちは `nop`)。ISR が書く状態は `volatile` で読む。→ §4-55、§4-56
- 「画面に出ている」は `/api/screenshot` の見た目で確かめる (`kprintf` の属性は入口で変換)。→ §4-52
- NP21/W の停止・起動は `tools/np21w_ctl.py` だけで (手打ちの taskkill → 起動は媒体のロックで止まる)。→ §4-60

**カーネル・FS**
- kstring / kmalloc / kprintf の基本部品を触ったら kselftest のケースを足す。→ §2
- ext2: `ext2_g_aux` を free/alloc をまたいで持たない、`ext2_read_file` は端数ブロックを `to_copy` だけ写す、メタデータの I/O エラー後は書き込みを全部断る (-15 → ホストの e2fsck)、OS32 で読めることは正しい ext2 の証拠でない。→ §4-24、§4-32、§4-35、§4-58
- VFS: エラーは `OS32_ERR_*` (FS の境界で翻訳)、`mount(dev_id)` は `(dev_type << 8) | unit`、`sys_ls` のコールバックから専用バッファ無しで FS を触らない。→ [`docs/06_filesystem.md`](docs/06_filesystem.md) §6-1、§4-30、§4-26
- デバイス窓は物理地図 (`pgalloc_range_has_ram`) で判定する (RAM の上端 `sys_get_mem_kb` ではない)。→ §4-34
- CPL=3 の KAPI は IF=1 で走る (`int80_stub` の出口は IF=0)。KAPI の検査は `ring3_guard_active(ring3_in_syscall, ring3_wm_depth)` で (WM はアプリの syscall の中で走る)。→ §4-19、§4-61
- シェルは 2 ヒープ (`kernel/paging.c` は 0x380000–0x3FFFFF を present に保つ)。exit の資源回収は所有者タグ。→ §4-15、§4-16
- SQLite のプール枯渇は `db_query` の `-2` — `db_last_error()` を必ず出す。→ §4-13
- 日本語は 1 文字 3 バイト・2 桁、切り詰めは UTF-8 の境界で。外部プログラムの漢字は既知の対で JIS 表を確かめてから `utf8_set_jis_table_ready(1)`。フォントは `tools/gen_font16.py`。→ §4-27、§4-11、§4-10
- 物理 0x90000 は自動プレイのメールボックス — 配置を変えたら `game/tools/autoplay/driver.py` も同じコミットで。→ [`docs/02_memory.md`](docs/02_memory.md) §2-1
- ブートローダ: PM 遷移は `loader_fat.asm` に内蔵、`boot_fat.asm` は `.8086`、HDD IPL の INT 1Bh は 16 回 (上限の理由は未確認)。→ [`docs/10_notes.md`](docs/10_notes.md) §10-2、§10-3

**GUI・入力・道具**
- GUI の内部: `libos32gfx_attach()` (`gfx_init` ではない)、Cirrus の窓は 1 回だけ写像、テキストカーソルは CSRFORM の DC ビットだけ。→ §4-18、§4-20〜§4-22
- `mui_pump_input()` はキーキューを食う (自前でキーを読むアプリは `mui_pump_input_ch()`)。→ §4-14
- GUI を NP21/W で叩く: rshell を ESC で抜けてから、`SHIFT+SPACE` は `--data-urlencode`、`/api/mouse` は `ax/ay`、Start メニューの行は `start_row()`、配備は `system.cfg` を書き換える。「遅い」はまず `gfx_counters` と `/api/status` の `eip`。→ §4-23、§4-25、§4-31
- Host Services は常駐の `tools/host_agent.py` が要る (`-100` = `HOST_ELINK`)。設定は ini ではなく `/api/net` に聞く。→ §4-45

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

`make docs-win` はこのリポジトリの `docs/` + `README.md` + `CLAUDE.md` を
`C:\WATCOM\docs\os32\` に書き出す (読み取り専用の出力。編集はここ側で行う)。

スキル: **`os32-build-verify`** (ビルド・配備・検証の選択)、**`os32-emu-debug`** (エミュレータ上の障害調査)、
**`os32-kapi-add`** (KernelAPI の追加・変更)、**`os32-emu-config`** (NP21/W ini の限定変更 — [D2] の承認対象)、
**`os32-local-ai`** (ビルド・試験・配備をローカル AI に実行させる)、
**`os32-local-review`** (主レビュアーが枯渇したときの補助レビュー — `tools/review_local.py`)。
