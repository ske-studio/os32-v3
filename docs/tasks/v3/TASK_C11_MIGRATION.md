# TASK_C11_MIGRATION — T0: C89 (gnu89) → C11 (gnu11) への移行 (言語モードと検査の移行)

> 状態: **受入待ち (2026-09-30)** — 段 2〜6 と文書 (A7・A10) はブランチ `wt/t0-c11` に実装済み、`check-c-dialect` / `check-c-dialect-host` を `make check` の列に追加。残件: A8 の差分確認 (Codex レビュー)、A9 のゲスト回帰 (PM)。apps/game は組まない (ユーザー決定、A5 は `check-c-dialect` の (c) で代える)。
>
> それまでの状態: **計画 (2026-09-30)** — §8 の判断 3 点は同日にユーザー承認 (推奨どおり)、[C1] の改訂案 (§9) は文面確定、CONSTRAINTS.md の改訂は fork 後の T0 で。v3 の最初の票 T0 ([TASK_MEMMAP_V3](TASK_MEMMAP_V3.md) §6、[V3_PLAN_DRAFT](V3_PLAN_DRAFT.md) §3 P0)。Codex (gpt-6-astra、読み取りのみ) の調査提案 (`x18/c11.md`、2026-09-30、基点 `d995e078`) を元に起票。**ユーザー判断が要る点 3 つ (§8) と [C1] の改訂文面 (§9) は未決** — 決定後に [CONSTRAINTS.md](../../CONSTRAINTS.md) を直し、設計票へ進む。コードは未着手。
>
> 発行: PM (Claude Code) の指示によりコーダー `claude-fable-5-1` (feat/gui `e8931cac`)。読んだもの: Codex 提案 `x18/c11.md`、V3_PLAN_DRAFT §3 P0 (根拠は X1 で改訂済み)・§6-4、[RUST_VS_C11](RUST_VS_C11.md) §1-1、[CONSTRAINTS](../../CONSTRAINTS.md) [C1]、`tools/check_constraints.py`、[POLICY_DEV](../../POLICY_DEV.md) §2、`build/config.mk` / `boot.mk` / `kernel.mk` / `programs.mk` の旗、`include/types.h`、`kernel/tss.c`、`kernel/shm.c`。**事実は `file:line` で示す。Codex の実測は §3・§5 の表の「実測」列に写した (再実行はしていない)。**

---

## 0. 結論 (1 段落)

T0 は **「言語モードと検査の移行」に限定**する。旗を gnu89 → gnu11 に替え、`STATIC_ASSERT` のマクロ本体を `_Static_assert` にし、規約 ([C1]) と検査 (`make check` に言語モードの検査を新設) を同じ移行単位で更新する。**既存コードの全面書き換えはしない** (宣言位置・`/* */` コメント・`u32` 等の既存型はそのまま)。RUST_VS_C11 §1-1 の「旗 5 行 + マクロ 1 行」は**不足** — SQLite の旗が `CFLAGS_COMMON` を継承しているので旗の分離が要り、gnu89 のままでも検出される既存の欠陥 (暗黙宣言 5 件、TSS の非定数式 1 件) を先に直し、ホスト試験 89 ファイルの旗と変異試験の方針も更新対象になる (§2、§3)。相対コストは依然 **小** だが「1 行」ではない。

---

## 1. 目的と範囲

### 1-1. 目的

- v3 の入れ物 (T1〜T7) を C11 で書けるようにする。**C11 を先にする理由は「コンパイラの規格変更と配置の変更を別々に受け入れ、障害の原因を分けるため」** (V3_PLAN_DRAFT §3 P0、Codex X1 で改訂済み)。「`_Static_assert` が無いと再配置を検査なしでやる」は根拠にしない (既存の `STATIC_ASSERT`・リンカ `ASSERT`・`gen_memmap.py --check`・起動時検査で足りている)。
- 規約 [C1] を「C11 (gnu11) を内部実装の基準とする」に改訂し、**言語モードを `make check` で検査する** (今は ID の整合しか見ていない — §2)。

### 1-2. 範囲 (対象と言語モード)

| 対象 | 言語モード | 備考 |
|---|---|---|
| 本体 (`kernel fs exec drivers gfx net lib include kapi arch platform`) | **gnu11** | `CFLAGS_COMMON` (`build/config.mk:112`) |
| ブート (`boot/` の C) | **gnu11** | `CFLAGS_BOOT` (`build/boot.mk:15`) |
| userland (`userland/`、`sdk/crt`) と SDK の**実装** (`userland/lib/`) | **gnu11** | `USER_CFLAGS` は `CFLAGS_COMMON` を継承 (`config.mk:152`) |
| **公開 SDK ヘッダ** (`sdk/include/os32/*.h`、`include/os32_kapi_shared.h`) | **C89 互換のまま** (gnu89 と gnu11 の両方から使えること) | ユーザー決定済み (RUST_VS_C11 §5 C3 = (a)、TASK_MEMMAP_V3 D36)。apps/game (gnu89) が読む |
| **SQLite 系** (`lib/sqlite3/sqlite3.c` `os32_sqlite_vfs.c` `os32_sqlite_test.c`、userland の SQLite 単体) | **gnu89 の専用規則** | amalgamation は C89。`CFLAGS_SQLITE` の分離が要る (§2) |
| vendor (`lib/zlib/`、`fs/fatfs/`、`lib/microtar` 等) | gnu11 (旗を継承) | K&R 定義は vendor 例外 (§5)。`__STDC_VERSION__` 分岐を確認 |
| apps / game / `sdk/example/hello` (submodule・サンプル) | **gnu89 のまま** (推奨、§8 の 3) | SDK の後方互換の検証例として残す |
| Rust (`userland/rust/`、`userland/gshell/`、`lib/os32_lz4/`) | 対象外 | T0 は C の言語モードだけ |

### 1-3. しないこと

- 宣言位置の移動、`/* */` → `//` の変換、`u32` → `uint32_t` の全置換、匿名構造体/共用体・`restrict`・`_Atomic`・TLS・`<threads.h>` の導入 (§4)。
- KAPI の型・スロット・`kapi.json` の変更、ABI 版の更新 (§6 の 6)。
- 再配置 (T1〜T3) と同時に行うこと (PLAN §1「1 と 2 を同時に動かさない」は保つ)。
- apps/game の submodule の旗変更 (§8 の 3 で「変える」と決まれば別コミット・別票)。

---

## 2. 前提文書の訂正 (Codex が見つけた事実)

RUST_VS_C11 §1-1 と V3_PLAN_DRAFT §3 P0 の記述は、この票で次のとおり訂正する (両文書の該当箇所には注記を入れた。本文は書き換えない)。

| # | 前提文書の記述 | 現在のコードで確認した内容 | 出典 |
|---|---|---|---|
| F1 | `-std=gnu89` は `build/*.mk` の 5 か所 (RUST_VS_C11 §1-1) | `build/*.mk` の直接指定は確かに 5 か所 (`config.mk:112`、`boot.mk:15`、`kernel.mk:110`、`kernel.mk:175`、`programs.mk:368`)。**ただし他にもある**: ホスト試験 `tools/tests/*.py` に **89 ファイル・167 出現**、`apps/Makefile:37`、`game/Makefile:51`、`sdk/example/hello/Makefile:28` | Codex 実測、手元で `grep -l gnu89 tools/tests/*.py \| wc -l` = 89 を確認 |
| F2 | `build/kernel.mk:175` は「SQLite 本体の専用規則」(RUST_VS_C11 §1-1) | **`kernel.mk:175` は `os32_sqlite_test.c` 用**。SQLite 本体 (`sqlite3.o`、`kernel.mk:169`) と VFS (`os32_sqlite_vfs.o`、`:172`) は **`CFLAGS_SQLITE` を使い、それは `CFLAGS_COMMON` を継承する** (`config.mk:158`)。→ `CFLAGS_COMMON` を gnu11 にすると SQLite 本体も gnu11 になる。「SQLite の 2 か所は据え置き」だけでは足りず、**言語指定を共通旗から分離**して SQLite 側に gnu89 を明示する必要がある | `build/kernel.mk:166-175`、`build/config.mk:158` |
| F3 | `kernel/shm.c` の 2 本は定数式でなく黙って無効 (RUST_VS_C11 §1-1、PLAN §1) | **2026-09-17 に修正済み** (`kernel/shm.c:25` 以降のコメント、決裁 D1)。現在は `MEM_SHM_GUI_OFFSET` を使う定数式。T0 では**変えない** | `kernel/shm.c:20-35` |
| F4 | `STATIC_ASSERT` は 102 か所 (RUST_VS_C11 §1-1) | コメント・文字列を除く `STATIC_ASSERT(` は 102 件だが**マクロ定義 1 件 (`include/types.h:37`) を含む。呼出しは 101 件** | Codex 実測 |
| F5 | [C1] は `make check` (`tools/check_constraints.py`) が照合する (CONSTRAINTS 冒頭、V3_PLAN_DRAFT §4 D9) | **`check_constraints.py` は ID の参照整合だけ** (正典の `### [ID]` と `CLAUDE.md` / `SOUL.md` の ID の一致、16 規則)。`//`・宣言位置・C99 機能の言語検査は**実装していない**。言語モードの検査は T0 で新設する (§3 の `check-c-dialect`) | `tools/check_constraints.py:1-40` |
| F6 | V3_PLAN_DRAFT §3 P0「SHM の定数不一致は `shm.c:23` で止まる」 | 正しい (F3 の修正後の式)。行番号は `shm.c:23` = `STATIC_ASSERT(SHM_TOTAL_SIZE == MEM_SHM_SIZE, …)` | `kernel/shm.c:23` |

---

## 3. 作業の分類表 (機械的に可能 / 半機械的 / 手作業 / 変えない)

Codex §2 の表をそのまま載せ、OS32 側の所在を足した。

| 作業 | 分類 | 提案・機械化方法 | 所在 |
|---|---|---|---|
| 本体・ブートの旗変更 | **機械的に可能** | `config.mk` の共通旗、`boot.mk` の旗を完全一致置換。**置換前の一致数を検査**してから差分を作る (下の Python) | `build/config.mk:112`、`build/boot.mk:15` |
| NE2K ホスト試験の旗 | **機械的に可能** | `kernel.mk:110` の `gcc -std=gnu89` を対象指定して置換 | `build/kernel.mk:110` |
| SQLite 例外の維持 | **半機械的** | `CFLAGS_COMMON` を「機械・ABI オプション」と「言語指定」に分け、通常側に `gnu11`、SQLite 側 (`CFLAGS_SQLITE`、`kernel.mk:175`、`programs.mk:368`) に `gnu89` を明示。**`-std` を複数並べて最後の指定で打ち消す構成は避ける** | `build/config.mk:112,158`、`build/kernel.mk:175`、`build/programs.mk:368` |
| ホスト試験の旗変更 | **半機械的** | `tools/tests/*.py` のコンパイラ引数の文字列を機械置換 (89 ファイル・167 出現)。**SDK 互換試験・SQLite・否定 fixture は用途を確認して除外**。`-Wdeclaration-after-statement` (87 ファイル・149 出現) は移行対象の試験から外す | `tools/tests/*.py` |
| `STATIC_ASSERT` 本体変更 | **機械的に可能** | マクロ本体のみ `_Static_assert(cond, #name)` に。**101 呼出しは維持** (呼出しを `_Static_assert(...)` へ直接変換しない — 第 2 引数の文字列化が要り、複数行・入れ子を単純 sed で扱えない) | `include/types.h:36-39` |
| TSS の式 | **手作業** | `(u32)(&((struct tss_entry *)0)->esp0) == 4` を `<stddef.h>` の **`offsetof(struct tss_entry, esp0) == 4`** に。旧式は `_Static_assert` でも「整数定数式ではない」(`-Wpedantic` で警告、`-Werror=vla` で失敗) | `kernel/tss.c:17` |
| SHM の「旧 2 本」 | **変えない** | 修正済み (§2 F3)。現行式を維持し、文書の記述を訂正する | `kernel/shm.c:25-35` |
| 暗黙宣言 5 件 | **半機械的** | コンパイラ (`-Werror=implicit-function-declaration`) で列挙し、正しいヘッダ・プロトタイプを人が確認して追加。`console.c:460` `console_hw_cursor_enable`、`kapi_generated.c:1123` `sys_time`、同 `:1199 :1205 :1211` `gfx_get_framebuffer` / `gfx_add_dirty_rect` / `gfx_present_dirty`。**生成物 (`kapi_generated.c`) は手編集せず** ([ABI1])、正典ヘッダか `sdk/kapi.json` の include 情報・生成器から直す。gnu89 でも同じ箇所が出る = 移行で新しく発生する欠陥ではなく、厳格化で止める既存の欠陥 | `kernel/console.c:460`、`kapi/kapi_generated.c:1123,1199,1205,1211` |
| 宣言位置の制約解除 | **変えない** | 既存宣言は移動しない (スコープ・初期化時点が変わる)。移行対象の試験から `-Wdeclaration-after-statement` を外すだけ | — |
| `//` 解禁 | **変えない** | 既存コメントは変換しない (本体側の実際の `//` は 0 件)。規約を更新し、**SDK 公開ヘッダ側だけ従来制約を検査** | — |
| 言語規約の検査追加 | **手作業** | ID 検査 (`check_constraints.py`) とは別に **`check-c-dialect`** (仮) を新設 — 実際の旗・禁止機能・SDK 互換性を検査 (§6 の 4)。`check_constraints.py` の ID 検査は維持 | `tools/` (新規)、`build/sdk.mk` の `check` 列 |
| 規約・計画文書更新 | **手作業** | 正典と参照側を同時更新: [CONSTRAINTS](../../CONSTRAINTS.md) [C1]、[POLICY_DEV](../../POLICY_DEV.md) §2、[CLAUDE.md](../../../CLAUDE.md) の規則行、**`AGENTS.md:22` (GNU89 固定の記述)**、`docs/08_build.md:94` の旗の表、RUST_VS_C11 §1-1・PLAN §1 の SHM / SQLite の記述。[C2]〜[C4] は変更しない | 文書 6 か所 + |
| apps / game / `sdk/example` の旗 | **変えない** (推奨、§8 の 3) | gnu89 のまま再ビルドし、SDK 公開ヘッダの後方互換を検証する例として使う | `apps/Makefile:37`、`game/Makefile:51`、`sdk/example/hello/Makefile:28` |

旗の置換案 (実装時の案、Codex 提案のまま。**各 old が期待数だけ存在することを確認してから差分を作る**):

```python
# 実装時の案。各 old が期待数だけ存在することを確認してから差分を作る。
replacements = {
    "build/config.mk":
        ("CFLAGS_COMMON = -std=gnu89", "CFLAGS_COMMON = -std=gnu11"),
    "build/boot.mk":
        ("CFLAGS_BOOT = -std=gnu89", "CFLAGS_BOOT = -std=gnu11"),
    "build/kernel.mk":
        ("gcc -std=gnu89 -Wall -Wextra -DNE2K_HOST_TEST",
         "gcc -std=gnu11 -Wall -Wextra -DNE2K_HOST_TEST"),
}
```

(`config.mk` は F2 の分離を先にするので、実際の置換対象は「言語指定を分けた後の変数」になる。)

`STATIC_ASSERT` はこれだけで十分:

```c
#define STATIC_ASSERT(cond, name) _Static_assert(cond, #name)
```

### 3-1. Codex の構文検査の実測 (2026-09-30、`-fsyntax-only`、本番レシピの再現ではない)

条件: `-std=gnu89` または `-std=gnu11`、`-m32 -march=i386 -ffreestanding -fno-pie -fno-stack-protector -fcommon -fsigned-char -fno-short-enums -O2 -Wall -fsyntax-only -Werror=implicit-function-declaration -Werror=implicit-int -Werror=vla -Wold-style-definition`。カーネルは `__KERNEL_BUILD__` + 通常 LAN 設定、userland は `__OS32_USERLAND__`。gnu11 では `-DSTATIC_ASSERT(cond,name)=_Static_assert(cond,#name)` を引数で仮適用 (ファイルは編集していない)。コンパイラは `/home/hight/opt/cross/bin/i386-elf-gcc` **GCC 13.2.0**。

| 範囲 | gnu89 | gnu11 |
|---|---|---|
| `C_KERNEL` の C ソース + `lib/kstring_c.c`、131 翻訳単位 | 3 単位失敗、エラー 6・警告 10 | 2 単位失敗、エラー 5・警告 9 |
| userland + `sdk/crt`、SQLite 単体試験を除く 167 翻訳単位 | 全通過、警告 5 | 全通過、警告 4 |
| ブート用 C、共有する `pc98pt.c` を含む 5 翻訳単位 | 未実施 | 全通過、診断 0 |
| ビルド対象 zlib 5 翻訳単位 | 全通過、旧式定義の警告 35 | 全通過、同じ警告 35 |
| SDK の KAPI 共有ヘッダ + GUI 共有ヘッダを取り込む TU | 通過 | 通過 |

失敗の内訳は §3 の「暗黙宣言 5 件」と「TSS の式」。暗黙 int は 0 件。本体側 281 ファイル (`lib/sqlite3/` 除外) の実際の `//` コメントは **0 件** (単純 grep では 5 行に当たるが文字列・コメント内)、`inline` は **33 行すべて `static inline`**、`arch/` `platform/` も 24 行すべて `static inline`。

---

## 4. C11 機能の採用範囲 (Codex §3、推奨列付き — **決定済み (2026-09-30、ユーザー承認: 推奨どおり)**、§8)

| 機能 | 推奨方針 | 既存コードの変換 | 決定 |
|---|---|---|---|
| ブロック途中の宣言、`for` 内宣言 | 内部実装で**許可** | **変えない**。宣言を移動するとスコープ・初期化時点も変わる | **決定 (2026-09-30、推奨どおり)** |
| `//` | 内部実装で**許可** | **変えない**。コメント書換えに実益がない | **決定 (2026-09-30、推奨どおり)** |
| `_Static_assert` | **採用** (`STATIC_ASSERT` のマクロ本体経由) | マクロ本体だけ (§3) | **決定 (2026-09-30、推奨どおり)** |
| `<stdbool.h>` | 内部の**純粋な真偽値**に許可 | **手作業**。既存 `int` の一括置換は禁止。エラー値・ビット集合・ABI と区別する | **決定 (2026-09-30、推奨どおり)** |
| `<stdint.h>` | 外部形式や移植コードで許可し、**既存 OS32 型 (`u8/u16/u32/i32`) は維持** | **変えない**。`u32` 全置換は T0 に含めない | **決定 (2026-09-30、推奨どおり)** |
| 匿名構造体・共用体 | T0 では**新規導入を見送る** | **変えない**。メンバー名の衝突、外部アクセス、生成器への影響を伴う | **決定 (2026-09-30、推奨どおり)** |
| 複合リテラル | 内部実装で**寿命が明確な場合**に許可 | **手作業**。代入列からの一括変換は、寿命・副作用・未指定メンバーの意味を変え得る | **決定 (2026-09-30、推奨どおり)** |
| 指示付き初期化子 | 内部のテーブルに**採用可** | **半機械的**。型情報からフィールド名を対応付け、人が差分確認。候補: `fatfs_ops` (`fs/fatfs_vfs.c:745`)、`ext2_ops` (`fs/ext2_vfs.c:484`)、`ext2_ino_ops` — **T0 の必須作業ではない** (暗黙のゼロ初期化・条件付きメンバー・配列添字・明示的な「未対応 = 0」を保つ) | **決定 (2026-09-30、推奨どおり)** |
| VLA | **禁止を維持** | **機械検査**。対象コードに `-Werror=vla` | **決定 (2026-09-30、推奨どおり)** |
| `restrict` | T0 では**導入しない** | エイリアス契約の証明が必要 | **決定 (2026-09-30、推奨どおり)** |
| `_Atomic`、TLS、`<threads.h>` | T0 では**導入しない** | 386 命令制限 (RUST_VS_C11 §7)、IRQ、ランタイム対応を別設計にする | **決定 (2026-09-30、推奨どおり)** |

固定幅型の共存の境界 (Codex 提案、§8 の 1 の推奨の中身):

- OS32 の既存 API・共有構造体: `u8/u16/u32/i32` を維持。
- 新規の独立パーサー・外部形式: 必要に応じて `uint32_t` 等を使用。
- 同じ API 内で同じ意味の型名を混在させない。
- SDK・KAPI・Rust FFI の型は T0 で変更しない。

このクロス環境では `u32` と `uint32_t` は同じ型 (`unsigned long`、`__builtin_types_compatible_p` で確認) だが、一般には「同じ 32 ビット幅」と「互換な C 型」は別。ポインタ引数・書式指定・ホスト試験まで含めて一括置換しない。

---

## 5. gnu89 → gnu11 の意味の差とリスク (Codex §4)

| 論点 | 判断 | OS32 での実例 |
|---|---|---|
| `inline` | 非 `static` の `inline` / `extern inline` は外部定義の扱いが変わる (gnu89 と C99 以降で逆)。**確認範囲はすべて `static inline` なので問題なし**。全体へ `-fgnu89-inline` を足す必要はない | 本体 33 行 + `arch/` `platform/` 24 行、全部 `static inline` |
| 暗黙宣言・暗黙 int | C11 では不適合だが、**GCC 13.2 で常に既定エラーになるわけではない** (既定エラー化は GCC 14 から)。明示的な `-Werror=implicit-function-declaration` / `-Werror=implicit-int` を旗に入れる | 暗黙宣言 5 件 (§3)、暗黙 int 0 件 |
| K&R 定義 | C11 でも残る旧式機能。**OS32 自作コードでは禁止**し、zlib の既存 35 件は **vendor 例外**として保持 | `lib/zlib/inflate.c` 等、5 翻訳単位で 35 警告 |
| 引数なしの `f()` | C11 でも `f(void)` と同義ではない。`-Wstrict-prototypes` を**追加調査**に使い、機械置換はしない | 未集計 |
| VLA | gnu89 でも拡張として通る。**言語モードにかかわらず `-Werror=vla` が必要** | `tss.c:17` の旧式が `-Werror=vla` で失敗する (= 検査が効く証拠) |
| 文字列リテラル | C11 でも通常の文字列は `char` 配列 (C++ のように `const char[]` にはならない)。書換えは未定義動作。`-Wwrite-strings` の導入は**別変更** | — |
| enum・整数昇格 | 標準変更だけを理由に全面的に型を書き換えない。既存の `-fsigned-char` / `-fno-short-enums` を維持 | `config.mk:113` |
| 大きな整数定数 | 接尾辞なしの十進定数は型が変わり得る (C90: `unsigned long` / C99 以降: `long long`)。実例は意味を確認し、32 ビット符号なし定数なら `U` 等で意図を明示 | `userland/lib/ui/microui.c:231` `2166136261` (FNV-1a の初期値)。gnu89 では「ISO C90 でのみ unsigned」の警告、C11 では消える。格納先は `unsigned` の `mu_Id` なので**この検出だけで動作不良とは判定しない** |
| 条件コンパイル | `__STDC_VERSION__ = 201112L` により FatFs、zlib、ツールチェーンヘッダの分岐が変わる。**前処理結果と型・配置を確認する** | `fs/fatfs/ff.h:46` (`DWORD` が `unsigned long` → `uint32_t`。このクロス環境では `uint32_t` も `unsigned long`、双方 4 バイト幅を確認)、`lib/zlib/zconf.h:201-205` |
| `_Static_assert` の限界 | gnu11 の `_Static_assert` は**必ず非定数式を拒否するとはいえない** (`tss.c:17` 旧式は gnu11 で通り、`-Wpedantic` で警告)。非定数式の排除は `-Werror=vla` と `offsetof` 化で担保する | `kernel/tss.c:17` |

根拠: `inline` の差は [GCC 5 porting](https://gcc.gnu.org/gcc-5/porting_to.html)、暗黙宣言等の既定エラー化は [GCC 14 porting](https://gcc.gnu.org/gcc-14/porting_to.html)、K&R 定義・文字列・整数定数の規則は [N1570](https://www.open-std.org/jtc1/sc22/wg14/www/docs/n1570.pdf) §6.9.1、§6.4.5、§6.4.4.1。

---

## 6. 実施順序 1〜7 と検査 (Codex §5)

| # | 段 | 内容 | 検査 |
|---|---|---|---|
| 1 | **対象を固定する** | §1-2 の表のとおり: 本体・ブート・userland・SDK 実装は gnu11、公開 SDK ヘッダは C89 互換、SQLite 系は gnu89。**再配置・型の全面刷新を同時に行わない** | §8 の決定を票に記録 |
| 2 | **現 gnu89 のまま既存問題を直す (5 + 1 件)** | 暗黙宣言 5 件 (`console.c:460`、`kapi_generated.c:1123/1199/1205/1211` — 生成物は正典ヘッダか `kapi.json` の include 情報・生成器から)、TSS の `offsetof` 化 (`tss.c:17`)。**旗はまだ変えない** (原因を分ける) | gnu89 + `-Werror=implicit-function-declaration -Werror=implicit-int -Werror=vla` で本体 131 TU が全通過。`make all` + `make check-changed` |
| 3 | **旗・マクロ・規約・ホスト試験を 1 つの移行単位で** | `CFLAGS_COMMON` の言語指定を分離して gnu11、`CFLAGS_BOOT` gnu11、`kernel.mk:110` gnu11、SQLite 3 か所に gnu89 を明示、`STATIC_ASSERT` 本体、`tools/tests/*.py` の旗 (除外リスト付き) と `-Wdeclaration-after-statement` の撤去、文書 ([CONSTRAINTS] [C1]、POLICY_DEV §2、CLAUDE.md、**`AGENTS.md:22`**、`08_build.md:94`、RUST_VS_C11 §1-1・PLAN §1 の訂正) | `make clean` → `make all` (全 `.o` の再生成を保証)、`make check` |
| 4 | **`make check` に言語モードの検査を新設** (`check-c-dialect`、仮) | (a) 全体に gnu11、SQLite に gnu89 が**実際に**適用されていること (旗の文字列でなく、`-v` / `-###` かコンパイル結果で); (b) 暗黙宣言・暗黙 int・VLA の拒否; (c) **SDK 公開ヘッダを gnu89 と gnu11 の両方で取り込めること** (SDK の gnu89 試験だけでは拡張の混入を見逃すので `-Wc90-c99-compat` 等と承認済み GNU 拡張の扱いを併用); (d) `//` や禁止トークンの検査は**文字列・コメントを区別**する; (e) `check_constraints.py` の ID 検査は維持 | 検査器のホスト試験 (`check-c-dialect-host`、TDD 記録 `tools/tests/*_tdd.md`) と `check` 列への追加、`docs/TESTS.md` の再生成 |
| 5 | **変異試験の更新** | C11 で許可する宣言・コメントを「拒否すべき変異」に**しない**。代わりに **VLA、暗黙宣言、偽の静的アサーション (`_Static_assert(1, …)` で条件を消す)、SDK 公開ヘッダへの C11 専用構文の混入**を否定試験にする。コンパイル拒否を期待する検査器試験と、コンパイル失敗を試験不成立とする通常の実行変異試験は区別する。変異は既存方針どおり写しの木で (`tools/tests/mutpar.py`) | `make check` の変異段 (`check-par`) |
| 6 | **生成器・ABI の検査** | 型・スロットを変えない T0 では Rust 生成器の変更や ABI 版更新は**原則不要** (C89 形式の生成コードは gnu11 でも使える)。再生成結果、`check-kapi-layout-host`、`check-kapi-version`、`check-gui-proto` を確認。**将来 `bool` / `uint*_t` を KAPI に入れるなら別票** — 現 `sdk/kapi_rust_gen.py:19-40` は未知型を `u32` に落とすので無条件で対応済みとは扱えない (D35 の「未知型は生成失敗に」と同じ論点) | 上記 3 検査 rc=0、生成物の差分 0 |
| 7 | **完全再ビルド・外部ビルド・ゲスト回帰** | 言語旗の変更だけでは既存 `.o` が再生成されない可能性があるため `make clean` → `make all`。~~`make external`~~ (**apps/game は組まない** — ユーザー決定 2026-09-30。後方互換は `check-c-dialect` の (c) の公開 SDK ヘッダ・配布ライブラリヘッダ・`sdk/example/hello` の gnu89 検査で代える、A5)。NP21/W 停止 → `make deploy-kernel` → 起動 ([D1]): 起動・kselftest、FS、SQLite (`db_*`)、GUI、共有ライブラリ、apps/game。**ブート旗も変えるので HDD / FD のブート経路も確認**。`tools/check_map.yaml:31` は `build/*.mk` を `full` にしているので旗変更の `check-changed` は全変異側になる | `make all` + `make check` rc=0、ゲストの新成果物の反映確認 (POLICY_DEBUG §2) |

---

## 7. 受入条件

| # | 条件 | 確認方法 |
|---|---|---|
| A1 | 本体・ブート・userland・SDK 実装が **gnu11** で、SQLite 系 (`sqlite3.o` / `os32_sqlite_vfs.o` / `os32_sqlite_test.o` / userland 単体) が **gnu89** でコンパイルされている | `check-c-dialect` (実際の旗を確認)、`make -n` の出力 |
| A2 | `-Werror=implicit-function-declaration -Werror=implicit-int -Werror=vla` が本体・ブート・userland の旗に入り、`make clean` → `make all` が通る | `make clean && make all` rc=0 |
| A3 | `STATIC_ASSERT` が `_Static_assert` 経由で、**101 呼出しがそのまま通る**。`tss.c:17` は `offsetof` | `grep -c STATIC_ASSERT(` の件数不変、`make all` |
| A4 | 暗黙宣言 5 件が消えている (`kapi_generated.c` は再生成で) | 段 2 の gnu89 検査、`check-kapi-out` |
| A5 | 公開 SDK ヘッダ (`sdk/include/os32/*.h`、`include/os32_kapi_shared.h`) が gnu89 と gnu11 の両方で取り込める | `check-c-dialect` の (c): 公開 SDK ヘッダを gnu89 (`-Wc90-c99-compat` ほか `-Werror`) と gnu11 で取り込む、SDK が配るライブラリヘッダを gnu89 / gnu11 で取り込む、in-tree の gnu89 の例 `sdk/example/hello` を gnu89 のまま in-tree の SDK ヘッダでコンパイルする。**apps/game は組まない** (ユーザー決定 2026-09-30、`make external` は T0 の検証に使わない) |
| A6 | `make check` に `check-c-dialect` が入り、否定試験 (VLA / 暗黙宣言 / 偽 `_Static_assert` / SDK への C11 構文) が拒否される | `check-c-dialect-host` の TDD 記録、変異段 |
| A7 | `check_constraints.py` の ID 検査が通り、[C1] の改訂が CONSTRAINTS / CLAUDE.md / POLICY_DEV §2 / AGENTS.md / 08_build.md に反映されている | `make check-constraints`、grep で `gnu89` の残りが SQLite 例外・apps/game・注記だけ |
| A8 | 既存コードの宣言位置・コメント・型名を**変えていない** (差分は旗・マクロ・5 + 1 件の修正・検査器・文書だけ) | 差分の目視 (Codex レビュー) |
| A9 | ゲスト回帰: 起動・kselftest 全 pass、FS、SQLite、GUI、shlib、apps/game。HDD / FD ブート | NP21/W (`os32-build-verify`)。実機は T0 では要求しない (旗だけで命令列は `-march=i386` のまま) |
| A10 | `docs/TESTS.md` の再生成、`make check-docs-links check-docs-orphans check-docs-status` rc=0 | `make check` |

---

## 8. ユーザー判断 (**2026-09-30 に 3 点とも推奨どおり承認**) (3 つ、Codex §6)

| # | 論点 | 選択肢 | 推奨 | 決定 |
|---|---|---|---|---|
| J1 | **既存 OS32 型 (`u8/u16/u32/i32`) を維持し、全面的な stdint 化をしないか** | (a) 維持 — 新規の独立パーサー・外部形式だけ `uint32_t` 可、同じ API 内で混在させない / (b) 全面 `<stdint.h>` 化 | **(a) 維持**。32 ビット限定 (ARM_GAUGE §10) で `u32` = ポインタ幅の前提は残る。混在が最悪 (V3_PLAN_DRAFT P0) なので境界を §4 のとおり決める | **推奨どおり承認 (2026-09-30)** |
| J2 | **内部コードで許す構文の範囲** | §4 の表 | **宣言位置・`//`・真偽型 (`<stdbool.h>`、純粋な真偽値だけ)・指示付き初期化子・寿命が明確な複合リテラル**を許可。**匿名構造体/共用体・`restrict`・`_Atomic`・TLS・`<threads.h>` は T0 では入れない** (別途) | **推奨どおり承認 (2026-09-30)** |
| J3 | **apps / game (submodule) の旗も T0 で変えるか** | (a) gnu89 のまま、SDK 後方互換の検証対象にする / (b) gnu11 化 (各 submodule の別コミット + 親の参照更新) | **(a) gnu89 のまま**。apps/game の保守は当面 os32 側 (FORK_PLAN)、`sdk/example/hello` も gnu89 互換の検証例として残す | **推奨どおり承認 (2026-09-30)** |

決定は [TASK_MEMMAP_V3](TASK_MEMMAP_V3.md) §0 の決定表に D 番号で記録し、この表の「決定」列を埋める。

---

## 9. [C1] の改訂案の文面 (Codex の案をそのまま。**CONSTRAINTS.md は §8 の決定後に直す**)

> **[C1] C11 (GNU11) を内部実装の基準とする。**
> OS32 本体、ブート、ユーザーランドおよび SDK 実装は `-std=gnu11` でビルドする。公開 SDK ヘッダは従来の C89/GNU89 互換性を維持し、SQLite 系は専用規則で GNU89 を維持する。
> 内部実装では `//` コメント、ブロック途中および `for` 内の宣言、`_Static_assert`、内部の真偽値、指示付き初期化子、寿命が明確な複合リテラルを許可する。既存コードの一括書換えは求めない。
> 暗黙の関数宣言、暗黙 int、VLA を禁止する。自作コードの旧式関数定義を禁止し、既存 vendor コードの例外は明記する。T0 では匿名構造体／共用体、`restrict`、atomic、TLS、スレッド API を新規導入しない。
> 既存の公開型・構造体配置・呼出し規約を維持する。型の運用詳細は `POLICY_DEV.md` §2 に定める。[C2]〜[C4] は変更しない。

注記: ID `[C1]` は据え置く (参照側の ID 整合は `check_constraints.py` がそのまま守る)。「理由」欄は「コードベース全体が `-std=gnu11` でビルドされ、SQLite 系だけ gnu89。混在の切り分けは `check-c-dialect` が実際の旗で検査する」に、「詳細」欄は POLICY_DEV §2 (C11 の採用範囲の表 = §4) に。J2 の決定で許可の列挙が変われば文面も合わせる。

---

## 10. 参照

- [V3_PLAN_DRAFT](V3_PLAN_DRAFT.md) §3 P0 (根拠は Codex X1 で改訂済み)、§4 D9、§5 C3、§6-4 の 4
- [TASK_MEMMAP_V3](TASK_MEMMAP_V3.md) §6 T0、D35 (Rust 生成器の未知型)、D36 (SDK ヘッダは C89 互換)
- [RUST_VS_C11](RUST_VS_C11.md) §1-1 (訂正は §2)、§5 C3、§7 (386 下限 — T0 の対象外だが `_Atomic` を入れない理由)
- [FORK_PLAN](FORK_PLAN.md) §3 g (v3 の最初の票 = この票)
- [PLAN](PLAN.md) §1 の 1 (C11 へ、SHM の記述は §2 F3 で訂正)
- [CONSTRAINTS](../../CONSTRAINTS.md) [C1]、[POLICY_DEV](../../POLICY_DEV.md) §2、`tools/check_constraints.py`
- Codex 提案: `scratchpad/x18/c11.md` (2026-09-30、gpt-6-astra、読み取りのみ。リポジトリには置かない)
