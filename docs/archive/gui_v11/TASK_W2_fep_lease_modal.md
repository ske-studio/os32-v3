# W2: FEP を WM で持つ、14 色リースと 2 色クローム、モーダルと標準ダイアログ

> 発行: PM (2026-09-05) / 状態: **受入完了 (2026-09-06)**
> レーン: W / 前提: W1 完了 + C2 完了 (ウィジェット側の `Text` / `Palette` 受け)
> 親: [TASKS.md](../../tasks/gui/TASKS.md) / 契約: U2a, G8, U4 / 設計: [DESIGN.md](../../tasks/gui/DESIGN.md) §9.4
> 排他: `userland/gshell/**`

## ゴール

ゲート G3 の WM 側 3 点: 日本語入力が GUI で使える、ゲーム系アプリが 14 色を借りられる、
ダイアログが入れ子ループ無しで動く。

## 作業

### A. FEP (契約 U2a)

1. SHIFT+SPACE を `input.rs` で捕まえて FEP オン/オフ (アプリには渡さない)。
2. オン中はキーを**先に** FEP (`ime_*` KAPI) に通す。未確定文字列があるとき FEP が消費した
   キー (かな入力、BS / DEL、SPACE / 矢印 / 数字 / ROLL の候補操作、RETURN、ESC) は
   **`Key` として配送しない** (契約 U2a 改訂。変換中の BS が本文まで消す事故を防ぐ)。
   確定文字列は `Text` (8B ずつ UTF-8 境界で分割、`more`。契約 U2) にしてフォーカス窓へ。
   未確定が空のときは BS / 矢印などは通常どおり `Key` で届く。SHIFT+SPACE は常に消費。
   G0b-4 の筋書きで確認する。
3. 未確定文字列と候補窓は `ime_render` の関数表に **GFX バックエンド**を足して WM が描く
   (`ime_render.h` は K 排他ではないがカーネル側。関数表の実体を gshell 側に持てるか K に確認。
   持てなければ WM 側に同等の描画を書き、TVRAM 版と同じ見た目にする)。
   描く位置は `SET_TEXT_CURSOR(window, x, y)` (C2 のテキストボックスが送る)。
4. 候補窓の領域は損傷として扱い、閉じたら下のウィンドウに `Paint` を出す。

### B. パレット リース (契約 G8)

1. `LEASE_PALETTE(first, count)` (色は引数バッファ): フォーカス窓の所有者だけ受理、範囲は
   `gfx_screen_info().lease_mask / lease_first / lease_count` で検証、`gfx_lease_palette` (H1) へ。
2. リース中の**フォーカス切替**でシステム色へ戻し、`Palette{active:false}` を旧、
   `Palette{active:true}` を新 (リース持ちなら) へ配送。イベント種別は末尾追記済み (K1 共有ヘッダ)。
3. **2 色クローム** (`chrome.rs` の第 2 モード): 文字と枠 = `TEXT`、面 = `WINDOW`、
   影と無効面 = `DITHER50`、フォーカス = `DOTTED`、アクティブタイトル = 黒地白文字、
   非アクティブ = 白地黒文字、デスクトップ = 市松。`Style.flags` の 4 つを使う。
   ウィンドウ外の色崩れは許容 (ユーザ決定 2026-09-04)。
4. フルスクリーン GFX プログラム (アプリが `exec` で起動するもの) の前後で WM がパレット
   全体を退避・復元し、`leave` / `enter` (H1) を呼ぶ。復帰後は全画面 present。

### C. モーダル (契約 U4)

1. `OPEN_MODAL(spec)` → `DialogId`。WM が入力の宛先をそのダイアログに限定。親は `Paint` を
   受け続ける。完了で `Modal{dialog, result}`。
2. 標準ダイアログを WM 側に: メッセージボックス (OK / OK+Cancel / Yes+No)、
   ファイル選択 (既存 `libos32filer` の見た目、実装は gshell 内の窓として)。
   標準ダイアログの中身は WM 自身の窓なので契約 U8 (直接呼び出し) で描く。

## 完了条件 (ゲート G3 の W 側)

- FEP 手順 (メモリ os32-fep-testing: `ime on`、SHIFT+SPACE は `/api/key` で `SHIFT%2BSPACE`)
  で gui_demo のテキストボックスに「日本語」が入る。候補窓が窓の上に出て消える。
- 14 色リースのテスト (`userland/tests/lease_test`、C2 が置く) がフォーカス中に
  独自パレットで描き、クロームが 2 色になり、フォーカスを外すと戻る。
- メッセージボックスを開いた状態で親のタイマが動き続け (親が `Paint` を受ける)、
  OK で `Modal` イベント 1 件が届く。gui_demo のループは 1 つのまま。
