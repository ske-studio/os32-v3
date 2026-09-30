# C3: libos32gui の共有ライブラリ化 (ジャンプ表 + バージョン照合)

> 発行: PM (2026-09-05) / 状態: **受入完了 (2026-09-06)**
> レーン: C / 前提: C2 完了。K3 と同時 (一斉再ビルドを 1 回で)
> 親: [TASKS.md](../../tasks/gui/TASKS.md) / 設計: [DESIGN.md](../../tasks/gui/DESIGN.md) §9.3 (案 A)
> 排他: `userland/rust/libos32gui/**`、`sdk/rust/os32api/src/gui/**`

## ゴール

libos32gui を `libos32gui.shlib` (OS32X、`OS32X_FLAG_SHLIB`) として 0x400000 に常駐させ、
アプリ側は**先頭ジャンプ表を呼ぶ薄いスタブ**だけをリンクする。アプリの .bin から
描画・ウィジェット・スタブの本体が消える。

## 作業

1. **ジャンプ表** (`shlib.rs`): 先頭 4KB に `magic`, `version = GUI_PROTO_VERSION`, `nfunc`,
   `entry[]` (`#[repr(C)]`、`#[link_section = ".shlib_hdr"]`)。関数は `extern "C"` で
   **末尾追記のみ** (KAPI と同じ作法)。
2. **リンカスクリプト** `sdk/link/shlib.ld` (C3 が書き、PM が sdk に置く): `. = 0x400000`、
   `.text/.rodata` の後にページ境界で `.data/.bss` (K3 が「アプリごとの物理ページ」に
   する範囲)。ヘッダに data ページ数を書く。
3. **アプリ側スタブ** (`os32api::gui::stub`): 起動時に `0x400000` の `magic` と `version` を
   照合 (`GUI_PROTO_VERSION` と不一致なら `dbg_print` + exit)、以後は `entry[i]` を呼ぶ。
   Rust の所有型 API (C2) はスタブの上にそのまま載る (アプリのソース無変更)。
4. **状態の置き場**: ライブラリの `static mut` (ウィジェット配列、クリップスタック、
   スロット情報) は `.data/.bss` に集める。アプリごとの物理ページになるので、アプリ間で
   共有されない (K3 の機構)。**`.text` に書く経路を作らない**。
5. **ビルド**: `build/programs.mk` に shlib の規則 (PM 経由)。`userland/deploy.yaml` に
   `/sys/lib/libos32gui.shlib`。`gui_demo` / `gdi_test` / `lease_test` をスタブ版に切り替え。

## 鉄則

- 位置依存 (再配置無し)。`0x400000` は `MEM_SHLIB_BASE` を写す (K3 の memmap.h)。
- バージョン不一致は**起動しない**。黙って別関数へ飛ぶ stale の罠を作らない
  (メモリ os32-verify-traps)。
- ジャンプ表は末尾追記のみ。

## 完了条件 (ゲート G4 の C 側)

- `gui_demo.bin` が C2 版より明確に小さい (数値を票に残す)。
- ライブラリの版を 1 つ上げてアプリを再ビルドしないと、アプリが `VERSION` で起動を拒む。
- 2 本のアプリ (gui_demo + lease_test) を続けて起動してもウィジェット状態が混ざらない
  (アプリごと data ページ)。
