# コーダー2 タスク: libos32gui (Rust GUI ライブラリ) の新規開発

> 発行: PM (2026-09-03) / 状態: **完了記録 (2026-09-03)**
> 担当: コーダー2 (Opus 5) / 前提: なし (今すぐ着手可)
> 親: [PLAN.md](PLAN.md) / GUI トラック: `docs/ROADMAP.md` v1.x
> 依存方針: [CONTRACTS.md](CONTRACTS.md) **C8 (厳守)** / 排他: C7

## ゴール

Win3.1 / 早期 Win95 風の GUI シェル (`gshell`) とアプリが載る土台となる、
Win32API 風の GUI ライブラリを Rust で作る。**現行 CPL=0 の KAPI に対して
開発する** — リング3 (コーダー1 の背骨) の完成を待たない。M2 のトランポリンが
ソース無変更で効く (CONTRACTS C3/C4) ので、後から CPL=3 に載る。

## 依存の鉄則 (CONTRACTS C8 — 最重要)

- **外部クレートを追加しない。** `os32_lz4` と同じく `no_std`、staticlib、
  `panic = "abort"`、std/alloc なし (固定配列とスライス)
- **描画は既存 libos32gfx を `extern "C"` で呼ぶ。Rust で再実装しない。**
  embedded-graphics 等は入れない (2026-09 調査で in-repo libos32gfx と重複、
  統合面で劣ると確認済み)
- あなたが作るのは libos32gfx に **無い層だけ**:
  ウィンドウ管理 / Z オーダー / フォーカス / メッセージディスパッチ / ウィジェット

## FFI 対象 (libos32gfx の主な C API)

`userland/lib/gfx/libos32gfx.h` を `extern "C"` で宣言して呼ぶ:

```
libos32gfx_init(api)                      初期化
gfx_clear(color) / gfx_present()          全消去 / VRAM 転送
gfx_pixel / gfx_rect / gfx_fill_rect      プリミティブ
gfx_fill_tri                              三角形塗り
gfx_save_rect / gfx_restore_rect          矩形退避/復元 (ウィンドウ背後の保存に)
gfx_blit / gfx_blit_colorkey              サーフェス転送 (スプライト/カーソル)
gfx_surface_*                             オフスクリーンサーフェス
```

ダーティ矩形・ページフリップは libos32gfx が内包する。ウィンドウ単位の部分更新は
`gfx_save_rect`/`gfx_restore_rect` と gfx のダーティ機構を使う。

## 構成 (os32_lz4 に倣う)

```
userland/rust/libos32gui/
  Cargo.toml     crate-type=["staticlib"], no_std, panic=abort, opt-level=2, lto
  src/lib.rs     #![no_std] + extern "C" 公開 API
```

- ビルドは `build/programs.mk` の Rust 規則 (DEFINE_RUST_PROGRAM 相当) に倣って
  PM/背骨と調整して 1 エントリ足す (`build/programs.mk` はコーダー1 排他なので、
  追加が要るときは PM 経由)
- ターゲットは `sdk/rust/i686-os32-none.json`、`os32api` クレートを使う

## 実装順

1. **ウィンドウ管理**: Window 構造体 (位置/サイズ/Zオーダー/タイトル)、
   固定数の配列で保持 (no_alloc)。作成/破棄/移動/前面化
2. **描画**: タイトルバー・枠・閉じるボタン・クライアント領域を libos32gfx で描く。
   Z オーダー順に、背後は gfx_save/restore で退避
3. **メッセージディスパッチ**: キーボード+マウスを統合イベントキューにし、
   フォーカスウィンドウへ配送 (Win32 の GetMessage/DispatchMessage 相当)
4. **基本ウィジェット**: ボタン・ラベル・テキストボックス・リストボックス

各段でデモ (簡単なウィンドウを出す小プログラム) を用意し、実機で目視確認できる
ようにする。実機確認は検証セッション or あなたのローカルで。

## 排他と禁止 (CONTRACTS C7)

- **あなたの排他**: `userland/rust/libos32gui/**`
- **触ってはいけない**: カーネル (`kernel/**` `exec/**` `sdk/gen_kapi.py` は
  コーダー1 排他)、`tools/check_privileged.py` `userland/tests/faultprobe/**` (PM)
- `build/programs.mk` への追加は PM 経由 (コーダー1 排他ファイル)

## 完了条件 (初回スコープ)

- `libos32gui` が staticlib としてビルドされる (外部クレート依存ゼロ)
- ウィンドウを 2 枚出し、Z オーダー・移動・前面化・閉じるが動く
- マウス/キーがフォーカスウィンドウに届く
- 1 種以上のウィジェット (ボタン) が押せる
- `make check` の check_privileged を通る (Rust なので特権命令は出ないはず)

## PM への連絡

- `build/programs.mk` へのビルドエントリ追加 (コーダー1 排他なので依頼)
- CPL=3 化で問題が出たとき (M2 完了後の結合確認は PM が段取り)
