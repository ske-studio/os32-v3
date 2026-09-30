# RUST_VS_C11 — v3 で Rust にする部品と C11 に揃える部品 (U5 の再確認)

> 状態: **設計中 (2026-09-30)** — **方針確定**。[V3_PLAN_DRAFT.md](V3_PLAN_DRAFT.md) §7-1 **U5** に付いたユーザー指示 (2026-09-30)「**Rust 化するほうが有利なものを再度確認する。C11 化するのでそれとの実装コスト比較で**」への答え (調査票)。U5 自体は同日に決定済み: **IRQ 文脈で Rust を呼ぶ合成器 (音源) は認める**。**§5 の判断 7 点は同日ユーザーが決定** (1〜5 は推奨どおり、6 nightly は固定済み、7 TCP/IP は足さない) し、Codex の事実確認 (§8: 命令制限の記述の訂正、バージョンアップでは直らない) を受けて **ユーザー決定 (a): Rust で作る部品も 386 下限を守る — 最終成果物の検査を必須の工程に** (§7)。実装 (§3-3 の 3 点・§7-2 の検査器) は未着手で、T5c / T7b の前段。それまでの状態: 草案 (2026-09-30、初版)。決定の一覧は [TASK_MEMMAP_V3](TASK_MEMMAP_V3.md) **D36**。
>
> 発行: コーダー `claude-fable-5-1` (feat/gui `d0284b81`、KernelAPI v68)、PM の指示による。改訂: 同日 (基点 `37190d76`) — Codex (gpt-6-astra、読み取りのみ) の事実確認 2 件 (`x18/rustisa.md` 命令制限の記述の正誤、`x18/rustver.md` バージョンアップで直るか) とユーザー決定 (`memdebate/user_2.md` 末尾) を反映。
> 読んだもの: [V3_PLAN_DRAFT](V3_PLAN_DRAFT.md) (P0 の C11 化・P2〜P10・§4 D8・§5 C3)、[TASK_MEMMAP_V3](TASK_MEMMAP_V3.md) (D1〜D34、§3-5 池の運用規則、§4-2 モジュール形式、§4-4 コアとモジュールの大きさ、§5-4 OpenType、§6 T0〜T7)、[PLAN.md](PLAN.md) §5-5 (合成器)、[TASK_PCM_CS4231](TASK_PCM_CS4231.md)、[PORT_CANDIDATES](PORT_CANDIDATES.md)、[DESIGN_APP_FIRST](../../DESIGN_APP_FIRST.md) §12、[CONSTRAINTS](../../CONSTRAINTS.md) [C1]、[V4_GAME_PLATFORM_DRAFT](../../V4_GAME_PLATFORM_DRAFT.md) §7、[ARM_GAUGE](../portability/ARM_GAUGE.md) §10、[M0_PORTABILITY_AUDIT](../arch_port/M0_PORTABILITY_AUDIT.md)、そしてソース (`userland/rust/`、`userland/gshell/`、`lib/os32_lz4/`、`sdk/rust/`、`sdk/kapi_rust_gen.py`、`build/*.mk`、`build/out/kernel.map`)。改訂で足したもの: Codex の実測 (`rustc --print target-cpus / cfg`、`objdump -d` の命令欄の集計、同梱 LLVM 22.1.4 の C API による生成実験)、[LLVM #58470](https://github.com/llvm/llvm-project/issues/58470)。
> **事実は `file:line` で示す。「推測」と書いたものは未確認。** 行数は `wc -l` (2026-09-30)、大きさは `ls -l` / `size` / `nm` の実測 (`build/out/` は手元ビルド)。

---

## 0. 結論 (1 段落)

Rust が**確実に効く**のは「外部入力を解釈する純粋計算で、割り当てもポインタの受け渡しも要らず、同じソースをホスト (x86_64) でも試験したい」部品に限られる。v3 でそれに当たるのは **(1) 音源の合成器** (決定済み) と **(2) OpenType の読み出し層 + ラスタライザ + グリフキャッシュ** (T7b。置き場が Rust の libos32gui / gshell なので選択の余地がほぼ無い) の 2 つ、それに**既に Rust のもの (GUI 一式・`os32_lz4`) の維持**。それ以外 — 台帳・ページング・割り込み・exec/ローダ・shlib・モジュールローダ・FS (ext2 / iso9660 / FAT)・FEP・SQLite・82557・PCM ドライバ・SDK の libc 周り・CUI シェル・画像/音声デコーダ — は **C11 に揃える**。理由は §2 の表のとおりで、要約すると: (a) C11 化のコスト自体が小さい (§1-1) ので「C11 にするくらいなら Rust に」という取引は成立しない、(b) カーネル核の部品は生ポインタと台帳の操作が本体で Rust の安全性が `unsafe` の中に消える、(c) FS・FEP・exec は**既にある C を v3 で動かす**票 (T2〜T5) であって書き直しの票ではない、(d) デコーダは移植の C を使う方針 (PORT_CANDIDATES §2-0) が先にある。Rust 側で v3 の前に直すべき穴が 3 つある (§3-3: panic が `loop {}`、モジュール形式に合わない PIC の再配置、**共通 Rust コードの生成命令が OS32 の 386 下限と整合していない**)。3 つ目の中身: 固定 nightly の既定 CPU は `generic` で `cmov` は出ないが、`font_test.elf` には i486 の `bswap` が 45 件ある。穴は `cpu` の指定が無いことそのものではなく、**`cpu = "i386"` を指定しても LLVM は `bswap` / `cmpxchg` / `xadd` を出す** (未解決の既知問題、バージョンアップでは直らない) ことで、**Rust で作る部品も 386 下限を守る** (ユーザー決定 (a)) には回避実装と最終成果物の検査が要る (§7)。

---

## 1. 前提

### 1-1. C11 化 (P0 / T0) の範囲とコスト

| 項目 | 事実 | コスト (推測) |
|---|---|---|
| いまの規約 | [C1] C89 (GNU89) 厳守 ([CONSTRAINTS.md](../../CONSTRAINTS.md) §C1)。`-std=gnu89` は `build/config.mk:112` (`CFLAGS_COMMON`)、`build/boot.mk:15`、`build/kernel.mk:110` (NE2K ホスト試験)、`build/kernel.mk:175` (SQLite 本体の専用規則)、`build/programs.mk:368` (SQLite 単体) の 5 か所 | 旗の書き換え 5 行。**SQLite の 2 か所は据え置き** (amalgamation の専用規則なので C11 化の対象外にできる)。**訂正 (2026-09-30、Codex、[TASK_C11_MIGRATION](TASK_C11_MIGRATION.md) §2 F1・F2)**: `kernel.mk:175` は SQLite 本体でなく `os32_sqlite_test.c` 用。本体 (`kernel.mk:169`) と VFS (`:172`) は `CFLAGS_SQLITE` で、それは `CFLAGS_COMMON` を継承する (`config.mk:158`) → 共通旗を gnu11 にすると SQLite も gnu11 になるので**言語指定の分離が要る**。旗は `build/*.mk` の 5 か所の外にも: `tools/tests/*.py` 89 ファイル・167 出現、`apps/Makefile:37`、`game/Makefile:51`、`sdk/example/hello/Makefile:28`。**実施 (2026-09-30、T0)**: 言語指定を `build/config.mk` の `C_STD` (gnu11) / `C_STD_SQLITE` (gnu89) / `C_DIALECT_ERRORS` に分け、機械の旗は `CFLAGS_MACHINE`。SQLite 系 (`CFLAGS_SQLITE`、`kernel.mk` の `os32_sqlite_test.o`、`programs.mk` の `SQLITE_SA_CFLAGS`) は `C_STD_SQLITE`。検査は `make check-c-dialect` |
| `_Static_assert` | 自作 `STATIC_ASSERT` は `include/types.h:36-39` の負の配列長。カーネル側 (`kernel fs exec drivers gfx net lib include kapi boot`) で **102 か所**。`kernel/shm.c` の 2 本は定数式でなく黙って無効 ([PLAN.md](PLAN.md) §1) | マクロ本体を `_Static_assert(cond, #name)` に置き換えれば **1 行**。無効だった 2 本はコンパイルエラーになるので、そこだけ式を定数にするか起動時検査に落とす。**訂正 (2026-09-30、TASK_C11_MIGRATION §2 F3・F4)**: `shm.c` の 2 本は **2026-09-17 に修正済み** (`shm.c:25` のコメント、今は `MEM_SHM_GUI_OFFSET` の定数式) — T0 では変えない。102 件はマクロ定義 1 件 (`types.h:37`) を含み**呼出しは 101 件**。非定数式で残るのは `tss.c:17` (→ `offsetof`)、他に gnu89 でも出る暗黙宣言 5 件を先に直す (同票 §3)。**実施 (2026-09-30、T0)**: マクロ本体は `_Static_assert(cond, #name)`、`tss.c` は `offsetof` |
| gnu89 → gnu11 で意味が変わるもの | `inline` の外部定義の扱い (gnu89 と C99 以降で逆)。カーネル側に **`static` でない `inline` は無い** (grep 2026-09-30、`lib/sqlite3` のコメント以外 0 件)。`//` コメントもカーネル側に 0 件 | 影響なし。**書き直しは要らない** — 解禁 (ブロック途中の宣言・`//`・`_Static_assert`・`<stdint.h>` の型) だけ |
| 規則と検査 | [C1] の本文、`CLAUDE.md` の規則行、`tools/check_constraints.py` (ID で照合)、`docs/POLICY_DEV.md` §2 | 文書 3 か所 + 検査器。**訂正 (TASK_C11_MIGRATION §2 F5)**: `check_constraints.py` は ID の参照整合だけで **[C1] の言語検査 (`//`・宣言位置・C99 機能) はしていない** — T0 で `check-c-dialect` を新設する。文書は `AGENTS.md:22` と `08_build.md:94` も対象 |
| SDK ヘッダと apps/game | `sdk/include/os32/*.h` を C89 互換に保つか (V3_PLAN_DRAFT §5 **C3**、未決)。apps/game (submodule、C) の保守は当面 os32 側。ユーザーランドの旗は `-std=gnu89 -march=i386` (PORT_CANDIDATES §1) | **決裁 1 件** (§5 の 4)。「SDK ヘッダは C89 互換のまま、in-tree は gnu11」が最も安い |
| 型 | `u32` か `<stdint.h>` か (V3_PLAN_DRAFT P0)。32 ビット限定 (ARM_GAUGE §10) なので `u32` = ポインタ幅の前提は残る | 選ぶだけ。混在が最悪 |

**まとめ**: T0 は「全ファイルに触れる書き直し」ではなく **旗 + マクロ 1 行 + 規則の改訂** で済む。相対コストは **小** (数日、推測)。PLAN §1 の「1 (C11) と 2 (再配置) を同時に動かさない」は保つが、それは切り分けのためであって作業量のためではない。

> したがって「C11 に上げるコストがかかるなら Rust に書き直したほうが得」という比較は**成立しない**。比較すべきは「新規に書く部品を C11 で書くか Rust で書くか」と「既にある部品を書き直す価値があるか」の 2 つ。

### 1-2. Rust の今の使われ方と道具 (事実)

| 項目 | 事実 |
|---|---|
| ツールチェーン | `rust-toolchain.toml`: **nightly-2026-04-29** + `rust-src`。`build-std = core, alloc, compiler_builtins` (`userland/rust/.cargo/config.toml`、機能 `compiler-builtins-mem`)。nightly は build-std のため (安定版では `-Zbuild-std` が使えない) |
| ターゲット | `sdk/rust/i686-os32-none.json`: `llvm-target i686-unknown-none`、ポインタ 32、`panic-strategy abort`、`disable-redzone true`、`features -sse,-sse2,-mmx`、リンカ `i386-elf-gcc`。**`cpu` の指定が無い → 固定 nightly-2026-04-29 (rustc 1.97.0-nightly、LLVM 22.1.4) では target-cpu は `generic`** (`rustc -Z unstable-options --print target-cpus`、Codex 実測 2026-09-30)。`llvm-target=i686-unknown-none` は `target-cpu=i686` を意味しない。LLVM `X86.td` で `generic` = X87 + CX8、**CMOV 無し** (`i386` / `i486` = X87、`i586` = X87 + CX8、`i686` = X87 + CX8 + CMOV) — 通常のコード生成で `cmov` は出ないが、**386 互換は保証しない**: 手元の `font_test.elf` (SHA-256 `ced83acc…`) には i486 以降の **`bswap` が 45 件** (`draw_string` 等、`objdump -d --no-show-raw-insn` の命令欄の集計)。`kernel.elf` / `gshell.elf` は `cmov` / `bswap` / `cmpxchg` / `rdtsc` / `cpuid` / SSE とも 0 件だが、**検査した成果物・命令に限る観測で保証ではない**。386 下限を維持する共通コードは、CPU 指定に加えて LLVM の既知制限 ([#58470](https://github.com/llvm/llvm-project/issues/58470)) と**最終成果物**を検証する (§7)。**i586 を代替下限にはしない**。i686 専用の合成器は対応 CPU に限定してロードする。**`relocation-model` の指定が無い** → 既定は PIC (`--print cfg` に `relocation_model="pic"`): `os32_lz4` 本体 `.o` の再配置は `R_386_GOTPC` ×3、`R_386_PLT32` ×1、`R_386_PC32` ×5 (`readelf -rW`、archive 全体では 360 / 299 / 375)。**PIC の問題と CPU 命令セットの問題は独立** |
| プロファイル | `opt-level 2`、`lto = true`、`panic = "abort"` (`userland/rust/Cargo.toml`、`lib/os32_lz4/Cargo.toml`、`userland/gshell/Cargo.toml`) |
| ユーザーランド | ワークスペース 16 クレート (`userland/rust/Cargo.toml`)、**22,587 行** (ホスト試験を除く)。gshell (`userland/gshell/`、独立ワークスペース) **14,565 行**、CPL=0 でシェル帯に常駐 (D10 の例外)。libos32gui = 共有ライブラリ (`libos32gui.shlib` 126.5KB、Rust の staticlib を `shlib.ld` でリンク、`build/programs.mk:616-636`)。libos32term / libos32term_render 852 行 (ホストで `cargo test`、`build/sdk.mk:137-140`)。外部クレートは **font_test だけ** (ttf-parser 0.21 + ab_glyph_rasterizer、`userland/rust/font_test/Cargo.toml`)。libos32gui と gshell は「外部クレート禁止」(CONTRACTS C8、`libos32gui/Cargo.toml` の冒頭) |
| バイナリの大きさ | `gshell.bin` 219.3KB (C の CUI シェル `sh.bin` 91.7KB とは機能が違うので比較にならない)、`about.bin` 12.6KB、`filer.bin` 42.4KB、`hello_gfx.bin` 20.5KB、`font_test.bin` 104.9KB (ttf-parser + ラスタライザ込み) |
| KAPI の Rust 生成 | `sdk/kapi_rust_gen.py` が `kapi.json` から `sdk/rust/os32api/src/kapi_generated.rs` (289 行) を生成 (`build/programs.mk:561-562`)。中身は `#[repr(C)]` 構造体 + `unsafe extern "C" fn` のフィールド (`kapi_rust_gen.py:148-151`)。**安全ラッパは生成しない** (ヘッダの説明文に「安全ラッパー」とあるが実体は無い)。OS32X ヘッダ v3 の `.os32_kapi_layout` 刻印は os32api も打つ ([KAPI_SPEC](../../KAPI_SPEC.md) §4-0)。C ⇄ Rust の構造体照合は `tools/check_gui_proto.py` (GUI プロトコル)、`tools/check_kapi_version.py:151-160` (版と `cfgro.rs` の export 名) |
| アロケータ・panic (ユーザーランド) | `os32api` の `GlobalAlloc` は KAPI `mem_alloc` / `mem_free` (`sdk/rust/os32api/src/lib.rs:148-169`)。panic ハンドラは `kprintf` で位置とヒープ残量を出して **`loop {}`** (`lib.rs:175-208`)。CPL=3 なら CTRL+STOP で殺せるが、**CPL=0 の gshell が panic すると機械が止まる** |
| カーネルへのリンク実績 | **`lib/os32_lz4`** (151 行): `#[no_mangle] pub unsafe extern "C" fn lz4_decode(src, csz, dst, cap) -> i32` (`src/lib.rs:34-39`)、境界は全部スライス、`build/kernel.mk:159-179` で `libos32_lz4.a` を **`-lgcc` より前に**リンク。`kernel.map`: `lz4_decode` = **503 バイト** (0x1f7)、呼び手は `drivers/kcg.o`。**副作用 2 つ**: (a) `.a` の `compiler_builtins` が `__udivdi3` / `__umoddi3` / `__divdi3` 等を供給している (`nm`、libgcc より先に見つかるため)、(b) `compiler-builtins-mem` の `memcpy` (0x23) は `--gc-sections` で捨てられ `kmemcpy` が勝つ (`kernel.map` の `.text.memcpy` は番地 0)。panic ハンドラは **`loop {}`** (`lib.rs:147-150`) — カーネル内で踏めば無言で固まる。C 版 `lib/lz4.c` (122 行) は userland の `lz4` コマンドが今も使い (`build/programs.mk:150-158`)、ブートは `boot/lz4_mini.c` と `loader_fat_new.asm:674-684` の asm 版 — **同じ LZ4 が 4 実装** |
| FFI の型 | `kapi_rust_gen.py:19-40` の変換表 (`int → i32`、`const char * → *const u8`、未知のポインタは `*mut u8`、未知の型は `u32`)。構造体ポインタ (`OS32_Stat *`) は `*mut u8` に潰れる = Rust 側で型を持たない |
| デバッグ・`kernel.map` | Rust の内部シンボルは **v0 マングル** (`_RNv…`) で `kernel.map` に載る。`tools/np21w_mcp/server.py:5,30,89` はシンボル名を `kernel.map` の文字列そのままで解く → デバッガから見つけたい関数は `#[no_mangle]` の C 名にしておく必要がある。`c++filt` 相当のデマングルは道具に無い |
| ビルド時間 (手元、2026-09-30) | `os32_lz4`: 変更なし 0.5 秒、**clean (build-std 込み) 17.5 秒**。libos32gui 変更なし 0.1 秒。中間物 `userland/rust/target` 293MB + `lib/os32_lz4/target` 123MB。CI は `rustup toolchain install` + `Swatinem/rust-cache` (`.github/workflows/build.yml:77-91`)。make は cargo を `FORCE` で毎回呼ぶ (`programs.mk:571`) |
| ホスト試験 | 同じクレートを x86_64 でも組む作法が既にある (libos32gui `host_tests/`、t5a_display `host_tests/`、libos32term)。**C 側にも同じ作法がある**: `drivers/pcm_cs4231_math.c` (448 行) を純粋部として切り出し 21 ケース + 変異 24 本 ([TASK_PCM_CS4231](TASK_PCM_CS4231.md) §2-1 進捗)、`kernel/dma_pool_math.c`、`kernel/v86_gcap_math.c` |
| 浮動小数点 | 現ターゲットは SSE / SSE2 / MMX 無効・**x87 有効**で、soft-float ではない (`+soft-float` 未指定、`--print cfg` に `target_feature="x87"`)。通常の実行時 `f32` / `f64` 演算は x87 を使う (定数畳み込みで消える演算や単なるビット操作まで「必ず x87 命令になる」ではない)。手元の `font_test.elf` の x87 命令は **9,730 件** (`f` で始まる命令名 + `wait` を命令欄で集計、GNU objdump 2.46 と `i386-elf-objdump` で一致。初版の 7,932 件は集計式と対象のハッシュが無く差の原因は不明 — 現在の成果物では 9,730 が正)。カーネルは CR0.EM=0 + `fninit` (`kernel/kentry.asm:26`) で、`IRQ_COMMON` (`kernel/isr_stub.asm:433`) は `pushad` → `irq_dispatch` → `popad` だけ、`fsave` / `fnsave` / `frstor` / `fxsave` / `fxrstor` の保存経路は `kernel/` `exec/` に無い (PORT_CANDIDATES §1、U7)。→ **技術要件は「IRQ から到達する処理とその呼び出し先で、保存していない x87 状態を使わない」**。IRQ 文脈の Rust で `f32` / `f64` を禁じるのはそのための保守的な実装規約 (言語仕様上の禁止ではなく、C の `float` / `double` や呼び出し先にも同じ制約がかかる) |
| 移植性 | 32 ビット限定 (ARM_GAUGE §10)。ARM なら target JSON を 1 つ足すだけで build-std は通る (推測、未実施)。M0 監査は Rust を対象外にした (`M0_PORTABILITY_AUDIT.md:282`) |
| 方針の文書 | V4 §7「KAPI / arch / drivers は当面 C / asm、Rust は上位から」(`V4_GAME_PLATFORM_DRAFT.md:258-279`)。PLAN §5-5 P2「合成器の核は Rust (`no_std`、C ABI)、PCM の DMA 割り込みから呼ぶ」。食い違いは V3_PLAN_DRAFT §4 D8 → U5 で「認める」に決定 (2026-09-30) |

### 1-3. カーネル文脈 (IRQ) で Rust を呼ぶときの制約 (合成器に効く)

| 制約 | 出典 | Rust 側の約束 |
|---|---|---|
| ISR は池を確保・解放しない (R1) | TASK_MEMMAP_V3 §3-5-1 | `alloc` を使わない (`#![no_std]` + `extern crate alloc` 無し)。状態は呼び手 (C のドライバ) が渡す固定長の構造体 |
| `irq_dispatch` は IF=0・ネスト無し | [TASK_HAL_WIRING](TASK_HAL_WIRING.md) §1-1 (`:102`) | 1 回の呼び出しの上限時間を決める (PLAN §5-5: 描画の粒度 256 frame)。IF=0 区間の契約 (< 10ms、TASK_PCM §2-1) の内側 |
| スタックはカーネルスタック 16KB (0x3FC000〜) を ISR も使う | TASK_MEMMAP_V3 §2-1 | 大きな配列をスタックに置かない。作業域は状態構造体の中 |
| x87 は退避されない | §1-2 | `f32` / `f64` 禁止 (整数演算から、TASK_MEMMAP_V3 §3-5-3 の 3)。表の事前計算は `const` (コンパイル時) か init (通常文脈、整数演算) で。検査は **IRQ から実行されるコードに絞る** (§7-2 の 4。`.a` 全体に 0 件を求めると `compiler_builtins` のメンバーの x87 1,765 件で不合格になる — リンクで捨てられるコードと IRQ から実行されるコードを区別する) |
| panic は許されない | `os32_lz4` の `loop {}` が前例 | 添字はマスク (`& (N-1)`) と `wrapping_*` で境界検査を消す設計にし、それでも残る panic は「合成器を止めて無音 + カウンタ」にする専用ハンドラ (§3-3) |
| モジュール形式 | TASK_MEMMAP_V3 §4-2 | 内部 `R_386_PC32` と `R_386_32` 以外は `mkmod.py` が拒否 → Rust の既定 (PIC、`GOTPC` / `PLT32`) では通らない。**`relocation-model = "static"` の target JSON** が要る (§3-3) |
| 386 下限 | §7 (ユーザー決定 (a)) | 合成器も 386 に無い命令を出さない。byte swap は 386 命令だけの inline asm か外部 C 関数、atomic は使わない (IF=0 の中なので要らない)。最終モジュールの実行コードを検査 (§7-3)。「速い CPU でしか載せない」(PLAN §5-5) は**時間**の条件で、命令セットの条件ではない — i686 専用に組む版を作るなら対応 CPU に限定してロードする別の成果物にする |

---

## 2. 候補ごとの比較

列の読み方: **今** = 現行の言語と行数 (`wc -l`)。**v3** = 新規 / 書き直し / 維持 / モジュール化 (中身は変えない)。**Rust の利点** = メモリ安全が効く所 (外部入力のパーサ、回り込み演算、表の添字) と試験のしやすさ。**Rust のコスト** = FFI 境界・`no_std` で使えない物・大きさ・IRQ 文脈・ABI。**C11 のコスト** = C11 で書く / 保つときのコスト。**推奨** は Rust / C11 / どちらでも。**相対コストは §4**。

### 2-1. 新規に書く部品

| # | 部品 | 今 | v3 | Rust の利点 | Rust のコスト | C11 のコスト | 推奨 |
|---|---|---|---|---|---|---|---|
| N1 | **音源の合成器** (OPNA = FM 6 + SSG 3 + リズム 6 + ADPCM、PLAN §5-5 P2、T5c の後の P3) | 無し。移植元は NP21/W `fmgen` (C++、`src/sound/fmgen/*.cpp` **4,762 行**) か NP2 の C 版 (`opngenc.c` 664 + `opngeng.c` 285 + `opna.c` 483 + `psggen*.c` 298 + `rhythmc.c` 159 + `adpcm*.c` 575 = **2,464 行**) | **新規** (移植) | **効く**: 位相・エンベロープの回り込みと表の添字が全部 (`wrapping_*` + マスク添字)、同じクレートをホストで組んで NP21/W の `/api/sound?pcm=1` (E0 で実装済み) の生 frame と md5 で突き合わせられる。純粋計算で割り当て無し = Rust の得意な形そのもの | IRQ 文脈の制約 §1-3 (alloc / f32 / panic / スタック / 時間)。NP2 の C 版は表の初期化に `double` + `pow` / `sin` (`opngenc.c:59-103`) → **表を `const` で焼く**か整数で作り直す。FFI: `extern "C" fn opna_write(state, reg, val, t_us)` / `opna_render(state, out, n)` の 2〜3 本、状態構造体は Rust 所有・C は不透明ポインタ。モジュール形式に合わせる target JSON (§3-3)。大きさ: 合成器の表 (正弦・エンベロープ) は数十 KB、コードは C と同等 (LLVM、推測) | C11 なら移植元の C 版をほぼそのまま (回り込みは `u32` の暗黙、添字はマスクを手で書く)。**ホスト試験は `*_math.c` 分離で同じことができる** (PCM の前例)。差は「回り込み・添字の誤りをコンパイラが見つけるか、試験で見つけるか」 | **Rust (決定済み、U5)**。理由: 純粋計算・外部入力 (レジスタ列)・ホスト md5 試験の 3 条件が全部そろう唯一の部品。V4 §7 を「純粋計算の staticlib は例外」と改訂 |
| N2 | **OpenType の読み出し層** (ストリーミング: `cmap` `loca` `hmtx` 等 100KB を常駐、`glyf` は字形ごと `lseek` + `read`) | `font_test` (Rust、354 行 + ttf-parser 0.21 + ab_glyph_rasterizer)。ファイル全体を `Face::parse` に渡す形なので**そのままは使えない** (TASK_MEMMAP_V3 §5-4)。**注記 (ユーザー決定 2026-09-30)**: 日本語 OpenType はリポジトリに含めない (ビルド時に `tools/fetch_fonts.py` が同意つきでダウンロード。v3 の OpenType 層はそれを配布物に入れる — 同意した人がビルドした物)、サブセット化は廃止、CUI は KCG ROM のみ、移植先の GUI 基本フォントは寛容ライセンスの欧文 OpenType (候補: Go fonts = 3-clause BSD、DejaVu = Bitstream Vera License。選定は T7b) | **新規** (数百行、推測) | **効く**: フォントファイルは外部入力 (`loca` の offset/length、輪郭点の個数、複合グリフの参照) で、壊れたファイルでも□に落とすだけで済む境界検査が Rust の既定。ttf-parser は zero-alloc・`no_std` | 置き場が **libos32gui (Rust shlib) と gshell (Rust)** (§5-4) なので C で書くと逆に FFI が要る。**CONTRACTS C8 (外部クレート禁止)** との衝突: ttf-parser を依存に足すか、`glyf::Table` 相当を自前で組む (§5-4 の案は自前) か → §5 の 3。C アプリ (apps/game) への字形提供は shlib の C ABI export (既に `draw.rs:568` で文字を描いている) | C11 で書くなら stb_truetype (C、PD、PORT_CANDIDATES §2-0 の候補) を `malloc` 無し・`float` 有りで移植 → Rust 側の gshell / libos32gui から FFI で呼ぶ二重構造。CPL=3 の x87 は問題ない (P6 で退避を決めるまで前景 1 本) | **Rust**。理由: 置き場が Rust、入力が外部、ttf-parser の下地がある。C アプリは shlib 経由で同じ字形を得る |
| N3 | **グリフキャッシュ** (16px 1bpp 2,048 字 × 40B = 80KB、私有。共有は後 U21) | 無し | **新規** | 固定長配列 + LRU は Rust でも C でも同じ。所有権は「アプリ私有 (shlib data) / gshell 私有」で Rust の借用検査が静的に効く | N2 と同じ置き場なので分けて考えない | — | **Rust** (N2 と一体) |
| N4 | **画像 / 音声デコーダ** (zlib / PNG / JPEG / Ogg / MP3) | ゲストに `lib/puff.c` (展開だけ)。移植候補は zlib、stb_image / libpng、libjpeg-turbo、Tremor、minimp3 (PORT_CANDIDATES §2-0、全部 C) | **移植** (v3 後半、P9 の練習台 1〜6) | 教科書的には Rust が最も効く領域 (外部入力のパーサ)。miniz_oxide / png / zune-jpeg / lewton などの `no_std` 対応クレートはある | **利用者が C** (apps / game / 移植アプリは全部 C) なので Rust で書いても C ABI の包みが要る。PORT_CANDIDATES の目的は「他人の C を OS32 の Makefile で通す手順の確立」で、Rust 化はその目的を外す。CPL=3 で動くので不正ポインタはアプリ 1 本を殺すだけ (安全性の見返りがカーネル内より小さい)。`alloc` が要るものが多い | 移植元がそのまま (C89 / C99)。作業は `Makefile` と設定ヘッダ | **C11 (移植の C を使う)**。Rust のアプリが要るときはそのクレートを依存に足すだけで、OS32 側の部品にはしない |
| N5 | **モジュールローダの再配置の読み出し** (ヘッダ + `R_386_32` の位置表 + インポート表 + `mem_size` / `bss_zero`、T4 / T5b / T5c) | 無し。近い物は `kernel/shlib.c` 318 行 (共有ライブラリの再配置・ジャンプ表) と `exec/os32x_hdr.c` 37 行 | **新規** (300〜500 行、推測) | 表の読み出しは外部入力 (ディスク上の `.mod`) — 各エントリの offset < `mem_size` の検査は Rust なら型で書ける。Rust 単体でホスト試験できる | ローダの本体は「台帳から owner 付きで取る → 展開 → ベース加算 → スロットに実番地 → init」で、**台帳・P2V・MMIO はすべて C** (T1)。同梱モジュール (T5b) は `paging_init` の前 (PG=0 の可能性) で走る。Rust にすると台帳への FFI が増え、`unsafe` の塊になり利点が消える。検証の大半は **ホスト側の `mkmod.py`** がやれる (未対応の再配置種別の拒否、外部データの拒否) | 小さい C。純粋部 (表の検査) を `mod_math.c` に切ってホスト試験 + 変異 (PCM / dma_pool の前例と同じ) | **C11** (どちらでも寄り)。理由: 台帳との結合が本体、ブート早期、mkmod.py が生成時に検証する |
| N6 | **`mem_map` / `mem_unmap` と libc 側の伸長・trim** (R2、D23〜D25、T2) | `sdk/crt/syscalls.c` 184 行 (`_sbrk` は `sbrk_heap_limit` を見て断るだけ、`:162`)、`exec/exec_heap.c` | **新規** (カーネル側 + SDK 側) | — | os32api の `GlobalAlloc` (KAPI `mem_alloc`) も R2 に合わせて直す必要がある (Rust アプリ側の allocator を `mem_map` の上に置く = Rust 側のコスト) | newlib の `_sbrk` と `malloc` のフック (C)。利用者の大半が C | **C11** (SDK)。Rust 側は os32api の allocator の追従だけ |
| N7 | **82557 ドライバ** (L-B 10〜15KB、静的、決裁 (b)) | `net/link.c` 1,312 行、`drivers/ne2000.c` 1,049 行、`drivers/lgy98.c` 177 行 (全部 C)。**TCP/IP は無い** (Host Services はリンク層のフレーム、EtherType 0x88B5) | **新規 (C、着手中)** | NIC ドライバ: 無い (MMIO・DMA 記述子・IRQ が本体) | 82557 を Rust にすると HAL_WIRING の `irq_register` / `dma_alloc` / `pci_find` への FFI が本体になる | 既存の C の形 (ne2000 / lgy98) を写す | **C11**。TCP/IP は**足さない** (ユーザー決定 2026-09-30、§5 の 7: [LEGACY_LIVING_PRESERVATION](../../LEGACY_LIVING_PRESERVATION.md) の方針に反する) |
| N8 | **Unicode → JIS の二分探索** (28KB の `.rodata` 組表、T3 / T7a) | `lib/unicode_jis_table.h` に 7,063 組、`lib/utf8.c` が 128KB 表を索引 | 書き直し (小) | 無い | — | 数十行 | **C11** |
| N9 | **番犬・強制脱出の 2 段化** (P6、R1 の移譲 / 回収、T2) | `exec/exec.c` 2,614 行 (`ring3_abort_check` `:635`、`:1452`) | 書き直し (exec の一部) | 無い (割り込みフレームと longjmp の操作) | — | exec の中 | **C11** |

### 2-2. 既にある部品を v3 で動かす (書き直しではない票)

| # | 部品 | 今 | v3 | Rust に書き直す価値 | 推奨 |
|---|---|---|---|---|---|
| E1 | **台帳 (pgalloc / physmem / memory_boot)・ページング・割り込み・DMA** | `kernel/pgalloc.c` 489、`physmem.c` 180、`memory_boot.c` 225、`paging.c` 1,304、`irq.c` 214、`drivers/dma8237.c` 290、`kernel/dma_pool_math.c` 179 (C) | **T1〜T3 で大きく書き換える** (台帳の新設、P2V/V2P、帯の切り直し) | 無い。物理番地・PD/PT・ポートの操作が本体で、Rust では全部 `unsafe`。`_Static_assert` と kselftest と地図検査 (`gen_memmap.py --check`) が検査の道具。**T1 の P2V/V2P 監査を `check_constraints.py` に足す** (TASK_MEMMAP_V3 §3-4) のは C のまま可能 | **C11** |
| E2 | **exec / OS32X ローダ / shlib** | `exec/exec.c` 2,614、`launch.c` 458、`appslot.c` 1,040、`os32x_hdr.c` 37、`kernel/shlib.c` 318 | **T2 で帯を 0x80000000 へ** (最も侵襲的な票) | 無い。T2 の差分に言語の切り替えを混ぜると壊れたときの切り分けが消える (PLAN §1 の理屈と同じ) | **C11** |
| E3 | **ファイルシステムのパーサ** (ext2 / iso9660 / FAT) | ext2 ≈ **3,874 行** (`fs/ext2_dir.c` 1,214、`ext2_inode.c` 584、`ext2_super.c` 542、`ext2_file.c` 538、`ext2_vfs.c` 505、`ext2_fmt.c` 408、`ext2_layout.c` 83)、`iso9660.c` 715、`fatfs_vfs.c` 775 + FatFs `ff.c` **7,084** (ChaN、第三者) + `diskio.c` 400、`vfs.c` 849、`vfs_fd.c` 707 | ext2 は**コア**、FAT / iso9660 はモジュール化 (中身は変えない、T5b / T5c) | **理屈の上では最も効く所** (ディスク上のメタデータは外部入力。2026-09-06 の cross-link は `ext2_g_aux` の流用 = エイリアスの誤りで、Rust の借用検査が捕まえ得た型)。**だが書き直しの量が 5,000 行級**、VfsOps 22 本の FFI、`kmalloc` / `OS32_ERR_*` / ROFS 方針の写し、受入 (NHD の実データ) が要る。v3 の票にこの作業は無い | **C11 (維持)**。新しい FS を足すことになったら (無い) その 1 本を Rust で書くのは可 |
| E4 | **FEP** (ローマ字かな・変換・辞書) | `kernel/ime.c` 916、`ime_dict.c` 618、`ime_romkana.c` 256、`ime_render_tvram.c` 76 (C)。辞書は SQLite | **モジュール化** (T5a、案 A → B)、facade の bounded copy (FEP_BOUNDARY、D31) | ローマ字かなの状態機械 (256 行) は Rust のホスト試験向きだが小さく、既に動く。変換は SQLite の C API を呼ぶ側なので Rust にしても `unsafe` の FFI が本体 | **C11 (維持)** |
| E5 | **SQLite** | `lib/sqlite3/sqlite3.c` (C89 amalgamation、専用の旗 `kernel.mk:175`) | モジュール化 (T4) | 無い (決定 D12: C のまま) | **C (専用規則、gnu89 のまま可)** |
| E6 | **PCM ドライバ (CS4231)** | `drivers/pcm_cs4231.c` 763 + `_math.c` 448 (純粋部、ホスト試験 21 + 変異 24) | モジュール化 (T5c)。合成器 (N1) の**顧客** — `pcm_advance` (IF=0) から `opna_render` を呼ぶ | 無い (レジスタ・DMA・状態機械)。既に C で「純粋部を分けてホスト試験」を達成している | **C11 (維持)**。Rust の合成器を呼ぶ側 |
| E7 | **V86 モニタ / gfx バックエンド / FDC / IDE / ATAPI / kbd / mouse / serial** | 全部 C (`drivers/` `gfx/` `kernel/v86*.c`) | コアかモジュール | 無い (装置) | **C11** |
| E8 | **CUI シェル・rshell・コマンド群・userland/lib** | `userland/shell/` + `userland/lib/` ≈ 19,930 行 (C)、25 コマンド | 帯の再リンク (T2) だけ | 無い | **C11** |
| E9 | **SDK (crt / syscalls / ヘッダ / newlib)** | `sdk/crt/crt0.asm` 39、`crt0_c.c` 51、`syscalls.c` 184、`sdk/include/` (C89 互換の要否 = C3) | `mem_map` の追従 (N6)、`opendir` 等の穴 (PORT_CANDIDATES §1) | 無い (利用者が C) | **C11** (ヘッダの C89 互換は §5 の 4) |
| E10 | **ブートローダ** | `boot/` (asm `.8086` + C `-std=gnu89` `boot.mk:15`) | T5b / T6b (集積域・同梱域) | 無い (実モード・PG=0) | **C11 可 (旗だけ)**。asm はそのまま |

### 2-3. 既に Rust のもの (維持)

| # | 部品 | 今 | v3 で触る所 | 判断 |
|---|---|---|---|---|
| R1 | **gshell** (WM、CPL=0、シェル帯) | Rust 14,565 行、`unsafe` 182 か所 | T2: `stub.rs` の 0x400000 → `MEM_SHLIB_BASE` (TASK_MEMMAP_V3 §2-3 ⑦)、BB の所有 (D19、`fullscreen.rs` `cursor.rs`)、trim 通知の配送 (D24)。T7b: ラスタライザを静的に持つ (§5-4) | **Rust のまま**。**panic 方針だけ決める** (§3-3): CPL=0 で `loop {}` は機械が止まる |
| R2 | **libos32gui** (shlib) + stub | Rust、`unsafe` 150 か所、外部クレート禁止 (C8) | T2: 池 + AS ごと写像 (物理はページ単位)、T7b: ラスタライザ + 私有キャッシュ (N2 / N3) | **Rust のまま**。C8 の扱いは §5 の 3 |
| R3 | **Rust の GUI アプリ** (filer / edit_gui / about / t5a_display / 試験 8 本) と libos32term | Rust | 全再ビルド (D7)。os32api の allocator を R2 に追従 (N6) | **Rust のまま** |
| R4 | **`lib/os32_lz4`** (カーネル内 LZ4) | Rust 151 行、`lz4_decode` 503B、呼び手 `drivers/kcg.o` (T7a で `.kcgfont` 廃止 → **呼び手が消える可能性**。他の利用者は `kernel.map` に無い) | T7a の後に「カーネルに残る Rust」があるか確認。残るなら §3-3 の 3 点 (panic / `cpu` / リンク順) を直す。残らないなら**合成器が最初のカーネル内 Rust** になり、`os32_lz4` は削って 4 実装を 3 に減らせる | **維持 (T7a まで)**。T7a で利用者が消えたら**撤去の候補** (§5 の 5) |
| R5 | **os32api / `kapi_rust_gen.py`** | 生成の Rust 束縛、安全ラッパ無し | KAPI の追記 (`mem_map` / `mem_unmap` / `unicode_to_jis` …) は `kapi.json` → 再生成で追従。U8 (fork で 1 回整理) なら生成器も | **維持** |

---

## 3. 推奨の方針

### 3-1. どこを Rust に、どこを C11 に

| 層 | 言語 | 根拠 |
|---|---|---|
| カーネル核 (台帳・ページング・割り込み・DMA・exec・shlib・モジュールローダ・VFS・FS・FEP・装置ドライバ・kselftest) | **C11** | 生ポインタと台帳の操作が本体。T1〜T5 は書き直しではなく組み替え。V4 §7 の「KAPI / arch / drivers は C / asm」を v3 でも維持 |
| SQLite | C (専用規則) | D12 |
| **カーネル文脈で走る純粋計算のモジュール** (合成器) | **Rust (`no_std`、`alloc` 無し、C ABI、staticlib)** | U5 決定。V4 §7 を「**純粋計算の staticlib は例外**」と改訂 (V3_PLAN_DRAFT §4 D8 の解消案どおり) |
| 常駐シェル・GUI (gshell / libos32gui / term / Rust アプリ) | **Rust (維持)** | 既に Rust。書き戻す理由が無い |
| OpenType (読み出し層・ラスタライザ・キャッシュ) | **Rust** | 置き場が Rust (libos32gui / gshell)、入力が外部 |
| SDK (crt / libc / ヘッダ)・CUI シェル・コマンド・userland/lib・apps / game・移植 (デコーダを含む) | **C11 / C89 互換ヘッダ** | 利用者が C。移植は移植元の C を使う |

判断の物差し (この票が使ったもの、次に候補が出たときも同じで判定できる): **(1) 純粋計算か** (割り当て・ポインタの受け渡しが無い)、**(2) 入力が外部か** (ディスク・ネット・レジスタ列・フォント)、**(3) 同じソースをホストで試験して実機の出力と突き合わせたいか**、**(4) 置き場が既に Rust か**。4 つのうち 3 つ以上なら Rust、(1) が欠ければ C11。**カーネル核はほぼ全部 (1) が欠ける**ので C11 に揃う。DESIGN_APP_FIRST §12 の 7 (「汎用 OS らしさのためだけの複雑性」) にも合う: 言語の混在自体が複雑性なので、Rust を足すのは見返りが明確な所だけ。

### 3-2. 混在の約束 (提案)

| 項目 | 約束 |
|---|---|
| **FFI の置き場** | Rust クレートは `lib/<名前>/` (カーネル向け) か `userland/rust/<名前>/` (ユーザーランド)。C 側の宣言は **C のヘッダを正典** (`lib/lz4.h:32` の前例) にし、Rust 側は `#[no_mangle] pub unsafe extern "C" fn` で**同じ名前・同じ引数順**。引数は `i32` / `u32` / `*const u8` / `*mut u8` / 不透明ポインタだけ (`kapi_rust_gen.py` の変換表 `:19-40` と同じ範囲)。構造体は `#[repr(C)]` + `size_of` の `const` assert + C 側 `_Static_assert` (T0 で解禁) の**両側**で寸法を固定 (`check_gui_proto.py` の前例) |
| **所有権の境界** | **バッファは呼び手 (C) が所有**し、Rust は `from_raw_parts` で借りるだけ (`os32_lz4` の形)。Rust が長寿命の状態を持つときは **C の構造体の中に Rust の状態を置く** (`#[repr(C)]` の不透明な塊、大きさは `const` で公開) — カーネル文脈の Rust は自分で確保しない (R1) |
| **panic 方針** | (a) カーネル文脈 (合成器・`os32_lz4`): `#[panic_handler]` は **`kpanic` 相当の C 関数を呼ぶ** (位置を `kprintf` してから停止) — 無言の `loop {}` は禁止。合成器は panic を**設計で消す** (マスク添字・`wrapping_*`・固定長)。(b) gshell (CPL=0): panic → **CUI シェルへ落ちる**か再起動 (P6 の番犬の形と一緒に決める)。(c) CPL=3 アプリ: 今の `loop {}` を `sys_exit` に変える (CTRL+STOP 無しで戻れる) |
| **浮動小数点** | カーネル文脈の Rust は `f32` / `f64` を使わない。`make check` に「**IRQ から実行される Rust コード** (合成器の入口 `opna_*` から到達する関数) に x87 / SSE / MMX 命令が無い」を足す (§7-2 の 4)。**カーネルにリンクする `.a` 全体を対象にしない** — `libos32_lz4.a` の `compiler_builtins` メンバーに x87 が 1,765 件あり (LZ4 本体 `.o` は 0 件)、リンクで捨てられるコードと IRQ から実行されるコードを区別する。`kernel.elf` の x87 589 件は SQLite の関数群を含み、それだけで IRQ 中に実行されるとは判断できない |
| **ターゲット** | `i686-os32-none.json` に **`cpu = "i386"`** と必要な feature の無効化を明記し、`core` / `compiler_builtins` / 依存クレートも同じ設定で組む (build-std)。**ただしそれを合格条件にしない** — LLVM は `cpu=i386` でも `bswap` / `cmpxchg` / `xadd` を出す (§7-1)。**i586 は 386 互換の代替にならない** (最低 CPU を Pentium に上げる別の方針変更で、採らない — 初版の「i386 相当か少なくとも i586」は撤回)。i686 専用の合成器 (PLAN §5-5「速い CPU でしか載せない」) を作るなら対応 CPU に限定してロードする別の成果物にする。モジュール用に **`relocation-model = "static"`** の JSON (`i686-os32-mod.json`、推測: 1 ファイル) を足し (§1-3)、最終リンク後の再配置種別も検証する |
| **ビルド** | cargo は make から (`FORCE`、既存)。`rust-toolchain.toml` の nightly は **固定済み (nightly-2026-04-29)。再考しない。更新は必要が出たときだけ** (ユーザー決定 2026-09-30、§5 の 6。build-std のため nightly から離れられない)。**更新したら §7-2 の最終成果物の検査を全成果物でやり直す** (バージョンアップで 386 の問題は直らない、§7-1)。CI は `rust-cache` (既存)。clean ビルドの +18 秒 (build-std) は許容 |
| **検査** | `check_kapi_version.py` (版・export 名)、`check_gui_proto.py` (構造体) の形で、新しい境界ごとに「C ⇄ Rust の寸法・名前の照合」を 1 本足す。**386 下限の検査 (§7-2 の 4) は最終成果物に対して `make check` の必須の工程**。`kernel.map` に載る Rust の入口は `#[no_mangle]` の C 名 (デバッガの都合、§1-2) |
| **外部クレート** | カーネル文脈: **禁止** (合成器は自前)。libos32gui / gshell: C8 のまま禁止か、ttf-parser だけ許すか (§5 の 3)。CPL=3 アプリ: 可 (font_test の前例) |
| **文書** | 新しい Rust 部品の票に「移植性の節」(SURVEY_N1 の規則) と「IRQ 文脈の制約 §1-3 を満たす根拠」を書く |

### 3-3. Rust 側で v3 の前 (T5c の合成器より前) に直す 3 点

1. **panic ハンドラの `loop {}`** (`lib/os32_lz4/src/lib.rs:147-150`、`sdk/rust/os32api/src/lib.rs:208`) → §3-2 の方針へ。
2. **共通 Rust コードの生成命令を OS32 の 386 下限と整合させる** — 既定 `generic` で `cmov` は出ないが `bswap` は出る (`font_test.elf` に 45 件)。`i686-os32-none.json` に `cpu = "i386"` を明記するが、**`cpu` の明示だけを互換性成立の根拠にしない** — §7-2 の手段 (byte swap の inline asm / 外部 C 関数、atomic を使わない、最終成果物の検査) で守る。これとは別に、既定の PIC (`GOTPC` / `PLT32`、モジュール形式が拒否) をモジュール形式に合わせて見直し、最終リンク後の再配置種別を検証する。
3. **カーネルのリンク順** (`build/kernel.mk:179`: Rust の `.a` が `-lgcc` より前で `__udivdi3` 等を供給している) — 害は無いが意図した状態ではないので、`.a` を `-lgcc` の後に置くか「Rust の compiler_builtins を使う」と決めて注記する。

---

## 4. 実装コストの見積もり (相対値、**推測**)

単位: **S** = 数日、**M** = 1〜2 週、**L** = 1 か月級。「Rust」「C11」はその部品をその言語で書いたときの見込み。T 票の中の作業は票の見積もりに含まれるので、ここでは**言語の選択で増減する分**だけを見る。

| 部品 | Rust | C11 | 差の中身 |
|---|---|---|---|
| T0 C11 化そのもの | — | **S** | 旗 5 行 + マクロ 1 行 + `shm.c` の 2 本 + 規則 3 文書 + 検査器 + C3 の決裁 (**訂正 2026-09-30**: 実際の作業は [TASK_C11_MIGRATION](TASK_C11_MIGRATION.md) §3 — SQLite 旗の分離、既存欠陥 5 + 1 件、ホスト試験 89 ファイルの旗、`check-c-dialect`、変異試験の更新。`shm.c` は修正済みで対象外。相対コストは依然 S) |
| N1 合成器 | **M〜L** (移植 2,464〜4,762 行 + 表の `const` 化 + ホスト md5 試験 + IRQ 制約の検査 + target JSON) | **M** (C 版をほぼそのまま + `_math.c` 分離 + ホスト試験) | Rust は +S〜M (表の整数化・target JSON・panic 方針・`unsafe` 境界の設計)。見返りは回り込み・添字の誤りをコンパイル時に出せること |
| N2 + N3 OpenType | **M** (読み出し層 数百行 + ラスタライザの取り込み + キャッシュ + T7b の受入) | **M + S** (stb_truetype 移植 + Rust からの FFI + C8 の話は消えるが二重構造) | C11 のほうが**高い** (置き場が Rust) |
| N4 デコーダ | (Rust クレート + C ABI 包み: M) | **S〜M / 本** (移植元そのまま) | C11 のほうが安い |
| N5 モジュールローダ | M (台帳 FFI + PG=0 対応) | **S〜M** | C11 のほうが安い |
| N6 mem_map / libc | — | **M** (T2 の一部) | Rust 側は os32api の allocator 追従 **S** が別に要る (言語に関係なく発生) |
| N7 82557 | L (HAL への FFI) | **M** (着手中) | C11 |
| E3 FS を Rust に書き直す (参考) | **L+** (5,000 行級 + 実データ受入) | 0 (維持) | v3 の票に無い |
| E4 FEP を Rust に (参考) | L | 0 (維持) | 同上 |
| R1〜R3 GUI を C に戻す (参考) | 0 | **L++** (37,000 行級) | 選択肢にならない |
| §3-3 の 3 点 | **S** | — | 合成器より前に |
| 混在の約束の検査 (§3-2、x87 / 寸法 / 名前) | **S** | — | `make check` に 2〜3 本 |
| 386 下限の検査器 + byte swap / atomic の回避実装 (§7-2) | **S** | — | 検査器 1 本 (最終成果物の `objdump` 集計) + `font_test` の `bswap` 45 件の解消 (T7b の中) |

合計の見方: **Rust を足すことで v3 全体に増えるコストは S〜M** (§3-3 + 検査 + 合成器の Rust 分) で、Rust を N4 / N5 / N7 にまで広げると M〜L ずつ増える。**C11 化は S** で、Rust の採用範囲と独立。

---

## 5. ユーザーの判断 (決定済み、2026-09-30)

7 点とも同日にユーザーが決定した (`memdebate/user_2.md` 末尾)。推奨と違う所は **6** (再考不要) と **7** (足さない)。あわせて Codex の事実確認 (§8) を受けた **(a)** を決定。

| # | 判断 | 選択肢 (初版) | **決定 (ユーザー 2026-09-30)** |
|---|---|---|---|
| 1 | **Rust の適用範囲を §3-1 の表で確定する** (V4 §7 の改訂文言「純粋計算の staticlib は例外」を含む) | 表どおり / 広げる (N4 デコーダ、N5 ローダ) / 狭める (OpenType も C) | **表どおり** |
| 2 | **panic 方針** (§3-2): カーネル文脈 = `kpanic` 相当、gshell = CUI へ落ちる or 再起動、CPL=3 = `sys_exit` | 採る / 別案 | **採る**。gshell の形は P6 の票と一緒に |
| 3 | **外部クレート (C8) と ttf-parser** — T7b で libos32gui / gshell にラスタライザを置くとき、(a) ttf-parser + ab_glyph_rasterizer を依存に足して C8 を「審査した crates は可」に改訂する、(b) 自前で `glyf` / `cmap` / `loca` の読み出しとラスタライザを書く、(c) ttf-parser のソースを `userland/rust/vendor/` に写して依存にしない (MIT なので可) | (a) / (b) / (c) | **(c) MIT ソースを vendor に写す** (C8 の意図 — crates.io に依存しない、オフラインで組める — を保つ。表記は §6-1 の第三者コード一覧へ)。写すときに `bswap` を出す経路を §7-2 の手段で置き換える (§7-3) |
| 4 | **SDK ヘッダの C11** (V3_PLAN_DRAFT §5 C3) | (a) `sdk/include` は C89 互換のまま / (b) SDK も C11 | **(a) C89 互換のまま** (apps / game / 移植は gnu89 でも gnu11 でも組める。Rust の束縛は `kapi.json` からの生成なので影響なし) |
| 5 | **`os32_lz4` の扱い** — T7a で `.kcgfont` が消えると呼び手 (`drivers/kcg.o`) が消える見込み | (a) 撤去して C 版 `lib/lz4.c` に戻す / (b) 残して §3-3 の 3 点を直す | **(b) を合成器の前例として直し、T7a で呼び手が消えたら (a) 撤去** — 順序は §3-3 → T7a |
| 6 | **nightly の固定方針** | 採る (fork 時に更新、以後年 1〜2 回) / 別 | **既に固定済み (nightly-2026-04-29) — 再考不要。更新は必要が出たときだけ** (定期更新はしない。更新時は §7-2 の検査を全成果物でやり直す) |
| 7 | **TCP/IP を足すとき Rust を候補にする**という記録を残すか (v3 の範囲外) | 残す / 消す | **足さない — 記録も残さない**。TCP/IP を足すこと自体が設計ポリシー [LEGACY_LIVING_PRESERVATION](../../LEGACY_LIVING_PRESERVATION.md) (レガシー実機の動態保存、ネットワークの解釈はホスト側) に反する。§2-1 N7・§3-1 から候補の記述を削った |
| (a) | **Rust で作る部品の命令セット下限** — Codex の事実確認 (§8) で「既定 CPU は generic、`cmov` は出ないが `bswap` は 45 件、`cpu=i386` でも LLVM は `bswap` / `cmpxchg` / `xadd` を出す (未解決)」が判明 | 386 を守る / Rust の部品だけ 486 以上にする | **下限 386 を Rust でも守る** — 最終成果物 (LTO・最終リンク後、`compiler_builtins`・依存・asm 込み) を検査し、386 に無い命令 (`bswap` / `cmpxchg` / `xadd` / `cmov` / `cmpxchg8b` 等) が無いことを**必須の工程**にする。出ていれば **Rust で作らないか、コードで避ける** (§7) |

決定の一覧への登録: [TASK_MEMMAP_V3](TASK_MEMMAP_V3.md) **D36**。V4 §7 の本文の改訂 (決定 1) は fork 後の os32-v3 で行う (§6)。

---

## 6. この票でしないこと

- コードの変更 (target JSON・panic ハンドラ・リンク順の修正と §7-2 の検査器・回避実装は指摘まで。実施は別の票か T5c / T7b の前段)。
- 合成器の設計 (PLAN §5-5 P2 が前提表を持つ。設計票は P3 の段で起こす)。
- FS / FEP / exec を Rust に書き直す見積もりの精密化 (v3 の票に無いので参考値のまま)。
- V4 §7 の本文の書き換え (fork 後の os32-v3 で、決定 1 に従って 1 行改訂する)。
- TCP/IP の候補記録 (決定 7 で「足さない・記録も残さない」)。
- 実 386 での起動・到達試験 (`bswap` の経路を実行すれば不正命令になる、という評価は静的集計からの推論。実機は Ra266 = Pentium II で、この ISA 差による障害は予想しない)。

---

## 7. 386 下限の守り方 (ユーザー決定 (a)、2026-09-30)

**決定**: Rust で作る部品も OS32 の下限 **386** を守る。根拠となる既存の下限: C は `-march=i386` (`build/config.mk:112`)、`CPU: Intel 386+` の表示 (`userland/shell/cmd_base.c:64`)、GUI の下限 386 と 486 ティアの見送り ([DESIGN](../gui/DESIGN.md) `:22` `:31`)。**最終成果物 (LTO・最終リンク後、`compiler_builtins`・依存クレート・asm 込み) を検査し、386 に無い命令が無いことを必須の工程にする。出ていれば、その部品は Rust で作らないか、コードで避ける。** NP21/W は `CMOV` / `BSWAP` を実装している (`np21w-src/src/i386c/ia32/instructions/data_trans.c:372,830`) ので、**エミュレータで動くことは 386 互換の証拠にならない**。実機 Ra266 (Pentium II) も同じ。

### 7-1. バージョンアップでは直らない (Codex `rustver.md`、2026-09-30)

| 事実 | 内容 |
|---|---|
| [LLVM #58470](https://github.com/llvm/llvm-project/issues/58470) は **Open** | `-march=i386` でも `bswap` / `cmpxchg` (8/16/32) / `xadd` を生成する報告。`closed_at` 無し、修正 PR の紐付け無し、三命令を修正済みとする記録無し。コメントで紹介される D59566 (commit `7339e61b`、2019-03-21、Clang 9 以降) は **cx8 の無い CPU で 64 ビット atomic (`cmpxchg8b`) をインライン化しない**変更で、三命令の禁止ではない |
| 最新側のソースも同じ | 最新 nightly が参照する Rust 用 LLVM でも `i386` と `i486` の CPU 定義は同じ feature・tuning。`BSWAP32r` / `LCMPXCHG` / `LXADD` に 386 除外の条件が無い (`X86.td` / `X86InstrMisc.td` / `X86InstrCompiler.td`)。「issue が閉じ忘れられているだけ」ではない |
| 版の比較 | 固定版 `1.97.0-nightly (37d85e592 2026-04-28)` / **LLVM 22.1.4**、最新 stable `1.98.1` / 22.1.8、最新 nightly 系列 `1.101.0-nightly` / 23.1.1 (2026-09-28 時点の公開 docs、配布 manifest は未取得)。**22.1.8 / 23.1.1 への更新を 386 互換の修正として採用する根拠は無い** |
| 同梱 LLVM の実測 | 固定版の `libLLVM.so.22.1-rust-1.97.0-nightly` の C API で、triple `i686-unknown-none`・**`cpu = i386`**・features `-sse,-sse2,-mmx,-cx8`・O2 相当で手書き IR を ASM 化: `llvm.bswap.i32` → **`bswapl`**、`atomicrmw add seq_cst` → **`lock xaddl`**、`cmpxchg seq_cst` → **`lock cmpxchgl`**。三命令すべてが出た (LLVM バックエンドの実測。Rust フロントエンド経由の通し検証は読み取り専用環境で未完了) |
| 禁止スイッチが無い | `-bswap` / `-xadd` / `-cmpxchg` は `is not a recognized feature for this target (ignoring feature)` で無視され、生成結果は変わらない。`-cx8` は `cmpxchg8b` だけ。`cpu=i386,-cx8` でも `--print cfg` は `target_has_atomic="8"/"16"/"32"/"ptr"` → **CPU 設定だけで Rust の atomic API の公開範囲は変わらない** |
| `compiler_builtins` も信用できない | 固定版のソースで `__bswapsi2` 等は `swap_bytes()` を呼ぶ。既存の OS32 用 `libcompiler_builtins-….rlib` に `bswap` 7 件 (`__bswapsi2` 1、`__bswapdi2` 2、`__bswapti2` 4)。最終リンクで捨てられれば入らないが、**入るかどうかは最終成果物を見るまで分からない** |
| 公式ターゲットに 386 は無い | `i686-unknown-linux-gnu` = `pentium4` (Tier 1)、`i586-unknown-linux-gnu` = `pentium` (Tier 2)、名前が `i386-apple-ios` のものも要求 CPU は Penryn。独自 JSON で `cpu="i386"` は指定できるが、**実 80386 で動くコード生成は公式に保証されない** |

一般の Rust コードを回避なしで 386 へコンパイルするには LLVM 側に 386 用の合法化処理と atomic の代替経路を足して保守する必要がある — この票の範囲外で、**「新版に上げれば直る」修正は無い**。

### 7-2. 手段 (回避実装 + 最終成果物の検査)

| # | 手段 | 内容 | 位置づけ |
|---|---|---|---|
| 1 | **CPU 設定の明示** | `i686-os32-none.json` (とモジュール用 JSON) に `cpu = "i386"` と必要な feature の無効化を書き、`core` / `compiler_builtins` / 依存クレートも同じ設定で再構築する (build-std なので自動) | **前提。合格条件にしない** (7-1) |
| 2 | **byte swap は 386 命令だけで書く** | 純 Rust のシフト・マスクへの書き換えは最適化で再び `bswap` に認識され得る (#58470 の再現例)。確実なのは **386 命令だけの inline asm / 外部 asm**、または **GCC `-march=i386` で作る外部 C 関数** (LLVM の再最適化を避ける境界を保ち、生成物を検査する)。`u32::from_be_bytes` / `swap_bytes` / `to_be` の使用は OpenType の読み出し (big-endian) と `compiler_builtins` に集中する (推測 — `font_test` の 45 件の由来は T7b で特定する) | 回避実装 |
| 3 | **atomic は OS32 の同期契約で実装する** | `fetch_add` / `compare_exchange` (CAS) を**使わない**。通常の読み書きや `volatile` への置き換えで済ませることもしない。OS32 は**単一 CPU**で、カーネル側の排他は **IF の保存・復元 (割り込み禁止区間)** (TASK_MEMMAP_V3 §3-5 R1、[TASK_HAL_WIRING](TASK_HAL_WIRING.md) §1-1 の `irq_dispatch` = IF=0・ネスト無し) — 合成器は IF=0 の中で呼ばれるので atomic 自体が要らない。CPL=3 からは KAPI を使う。target JSON の `max-atomic-width` / `atomic-cas` は API の利用を絞る候補だが **LLVM の命令禁止スイッチではない** | 回避実装 |
| 4 | **最終成果物の検査を `make check` に** | 検査対象は**最終リンク後の実行コード** (`build/out/kernel.elf`、`gshell.elf`、`libos32gui.shlib` の ELF、各 `.bin` / `.mod` の元 ELF、合成器を含むモジュール) — `.a` / `.rlib` / `.o` ではない (捨てられるコードを数えない、取り込まれた `compiler_builtins`・依存・asm を漏らさない)。方法は `objdump -d --no-show-raw-insn` の**命令欄**の集計 (`lock` / `rep` 等の接頭辞を剥がして次の語を見る。全セクションを強制逆アセンブルする `-D` は使わない)。**禁止**: `cmov*`、`bswap*`、`cmpxchg*` (`cmpxchg8b` を含む)、`xadd*`、`cpuid`、`rdtsc`、XMM / MM レジスタ参照、`fxsave` / `fxrstor` / `ldmxcsr` / `*fence` / `emms`。**x87 (`f*` + `wait`) は IRQ から到達するコードに限って 0 件** (§3-2)。三命令の検査は最低限で、386 対応の判定は命令・エンコーディング全体で行う。**toolchain 更新時 (§5 の 6) も必須**。検査器は `tools/check_constraints.py` と同じ作法 (ID で報告) で 1 本 (名前は仮に `tools/check_isa386.py`、推測) | **必須の工程** |
| 5 | **落ちたときの扱い** | その部品は **Rust で作らない (C11 へ) か、2・3 で避ける**。検査を緩めない (「NP21/W で動く」「Ra266 で動く」は理由にならない) | ユーザー決定 (a) |

### 7-3. 受入条件への結び付け

| 部品 | 受入条件 (§7 由来) | 票 |
|---|---|---|
| **合成器** (N1) | 最終モジュールの実行コードに **386 に無い命令 0 件、かつ x87 / SSE / MMX 0 件** (整数演算限定、TASK_MEMMAP_V3 §3-5-3 の 3)。表の初期化も整数 (§2-1 N1)。atomic を使わない (IF=0 の中)。IRQ から到達する関数は `alloc` 無し・panic 無し (§1-3)。**設計票に「§7-2 の 1〜4 を満たす根拠」の節を持つ** (§3-2 文書) | T5c の後の P3 (設計票は P3 の段で起票) |
| **OpenType** (N2 / N3) | `libos32gui.shlib` と `gshell.elf` の最終コードに 386 に無い命令 0 件。**`font_test.elf` の `bswap` 45 件は既知の問題として記録し、T7b で解消する** — vendor に写す ttf-parser / ab_glyph_rasterizer (§5 の 3 (c)) と読み出し層の big-endian 読みを 7-2 の 2 で置き換え、`font_test` を検査器で通す。x87 は CPL=3 と gshell の前景では可 (P6 で退避を決めるまで前景 1 本、§2-1 N2) | T7b |
| **既存の Rust** (gshell / libos32gui / Rust アプリ / libos32term / `os32_lz4`) | 今の `kernel.elf` / `gshell.elf` の 0 件は観測にすぎない → **検査器を `make check` に足した時点で全成果物を通す**。JSON に `cpu = "i386"` を足した再ビルド (D7 の全再ビルド) の後も再検査。`os32_lz4` は §3-3 の 3 点と一緒に (§5 の 5) | §3-3 (T5c の前段)、T2 の全再ビルド |
| **toolchain 更新** | 更新のたびに 7-2 の 4 を全成果物で。バージョンアップを 386 対応の修正として扱わない | §5 の 6 |

### 7-4. 実害の評価 (Codex `rustisa.md` §4、確認事実と未検証を分ける)

| 環境 | 評価 |
|---|---|
| 実 386 | 確認した `bswap` の経路を実行すれば**不正命令** (#UD)。実 386 での起動・到達試験は未実施 |
| Pentium 100MHz | `bswap` は対応。i686 専用化して `cmov` が出ると非対応。処理速度と命令対応は別条件 |
| Ra266 = Pentium II | `bswap` / `cmov` / `cmpxchg8b` とも対応世代。この ISA 差による障害は予想しない (実機試験は未実施) |
| NP21/W | `CMOV` / `BSWAP` を実装 (`inst_table.c:1799` 等で登録)。動いても 386 互換の証拠にならない |
| IRQ の x87 破壊 | CPU が新しくても解消しない。FPU の保存・復元契約 (P6) の問題 |

---

## 8. Codex の事実確認との対応 (2026-09-30、基点 `37190d76`)

Codex (gpt-6-astra、`codex exec -s read-only`) は読み取りと静的集計のみ (ビルド・ゲスト実行・実機は未実施)。**ユーザーは判定をすべて受け入れ、(a) を決定** (`memdebate/user_2.md` 末尾)。

### 8-1. `x18/rustisa.md` — 命令制限の記述の正誤

| 初版の記述 | 判定 | この改訂での修正 |
|---|---|---|
| §0「`cpu` がないことが穴」 | 不正確 | 問題は指定の欠落そのものではなく、386 下限と実際の生成命令の不整合 → §0・§3-3 の 2 |
| §1-2「既定 CPU で生成」 | 正しいが説明不足 | 固定 nightly では **`generic`** と明記 (`--print target-cpus`) → §1-2 ターゲット |
| 「未指定なので i686 命令が出得る」 | 不正確 | `generic` と `i686` を区別: CMOV は無効、`bswap` は実測あり (45 件) → §1-2・§3-3 |
| kernel / gshell の 0 件は保証ではない | 正しい | 「検査した成果物・命令に限る観測」と明記 → §1-2 |
| SSE 無効なので `f32` は x87 | 現設定では正しい | x87 有効・soft-float 未指定を条件として明記 → §1-2 浮動小数点 |
| `font_test` の x87 7,932 件 | 現在の成果物に対して誤り | **9,730 件** に訂正 (旧値の原因は不明、集計式を記録) → §1-2 |
| IRQ の `f32` / `f64` 全面禁止 | 規約として妥当、事実説明として不正確 | 「IRQ から到達する処理で保存していない x87 状態を使わない」という技術要件と、保守的な規約とを区別 → §1-2・§1-3 |
| §3-2「i386 相当か少なくとも i586」 | 誤り | **i586 は 386 互換の代替にならない** (最低 CPU を Pentium に上げる別の方針変更)。撤回 → §3-2 ターゲット |
| 合成器は i686 可 | 条件付きで正しい | 対応 CPU に限定してロードする別の成果物、という条件を付けた → §1-3・§3-2 |
| §3-2「`.a` 全体に x87 0 件」の検査 | 過剰 (今の `libos32_lz4.a` でも `compiler_builtins` の 1,765 件で不合格) | **IRQ から実行されるコードに絞る** → §1-3・§3-2 浮動小数点・§7-2 の 4 |
| §3-3 の 2、PIC と再配置の指摘 (3・1・5) | 実測部分は正しい (本体 `.o` の値) | CPU 問題と分離し、最終モジュールの再配置種別は別途検証 → §1-2・§3-3 の 2 |
| 実害 | 実 386 のみ確認経路で #UD、Ra266 / NP21/W では出ない | NP21/W・Ra266 で動くことを 386 互換の根拠にしない → §7・§7-4 |

### 8-2. `x18/rustver.md` — バージョンアップで直るか

| 問い | 答え | 反映 |
|---|---|---|
| LLVM #58470 は直ったか | **Open**。関連修正 D59566 は cx8 無しの 64 ビット atomic だけ | §7-1 |
| 新しい版 (22.1.8 / 23.1.1) なら直るか | 最新側のソースでも `i386` = `i486`、`BSWAP32r` / `LCMPXCHG` / `LXADD` に 386 除外なし。**更新を修正として採用する根拠は無い** | §7-1、§5 の 6 (更新は必要時のみ、更新時は再検査) |
| `cpu=i386` を指定すれば出なくなるか | **出る** — 同梱 LLVM 22.1.4 で `bswapl` / `lock xaddl` / `lock cmpxchgl` を実測 | §7-1、§3-2 ターゲット (合格条件にしない) |
| feature で禁止できるか | `-bswap` / `-xadd` / `-cmpxchg` は存在しない (ignoring feature)。`-cx8` は `cmpxchg8b` だけ | §7-1 |
| `compiler_builtins` は安全か | `bswap` 7 件 (`__bswap{si,di,ti}2`)。最終リンクで捨てられるかは成果物次第 | §7-1、§7-2 の 4 (検査対象は最終リンク後) |
| 現実的な手段 | CPU 設定の明示 (前提) + byte swap の inline asm / 外部 C 関数 + atomic を同期契約で実装 + 最終成果物の検査 (toolchain 更新時も) | §7-2 → ユーザー決定 (a) |
