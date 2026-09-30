# C4: v1.2 デスクトップ client API

> 発行: PM (2026-09-06) / 状態: **受入完了 (2026-09-07)**
> レーン: C / 前提: K5。W4 と並行可、結合試験は W4 後  
> 親: [TASKS.md](../../tasks/gui/v12/TASKS.md) / 契約: [CONTRACTS.md](../../tasks/gui/v12/CONTRACTS.md) V12-C / V12-I  
> 排他: `userland/rust/libos32gui/**`、`sdk/rust/os32api/src/gui/stub.rs`

## ゴール

v1.2 の modal result / file dialog / input dialog / session request / icon16 を `libos32gui.shlib` の公開 API として提供する。

KAPI v42 と AppVTable の既存配置は変更しない。

## 1. 共有ライブラリ ABI

既存 entry 0〜94 は完全固定。次を末尾へ追加する。

```text
95  os32gui_modal_open
96  os32gui_modal_result
97  os32gui_file_open
98  os32gui_input_open
99  os32gui_session_request
100 os32gui_draw_icon16
```

`SHLIB_NFUNC = 101`。`OS32ShlibHeader.version` は 1 のまま (末尾追記)。

この票では general-purpose popup menu ABI は作らない。File Manager の右クリック menu は C5 が既存 drawing/input 上に client-area overlay として実装する。v1.2 の「右クリックメニュー基盤」はこの実装パターンを成果物とし、popup Window / AppVTable 拡張を避ける。

## 2. API の意味

### modal_open

既存 `GUI_OP_MODAL_OPEN` の薄い wrapper。

```text
modal_open(parent, kind, message) -> DialogId / error
```

blocking wait を行わない。

### modal_result

```text
modal_result(dialog, out_value) -> result / error
```

`GUI_OP_MODAL_RESULT` を 1 回呼び、response の `GuiString` を caller buffer へコピーする。

- `ERR_STALE`: wrong id / already consumed
- `OS32_ERR_AGAIN` は存在しない (確認済み 2026-09-06)。よって `modal_result` は **`on_modal` の
  event 到着後だけ呼ぶ API** とし、未完成状態を問い合わせる口は公開しない。未完成で呼ばれた場合
  WM は保持中 ID と一致しないので `ERR_STALE` が返る (これを「未完成」の合図として使わない)。

クライアントの通常作法は `on_modal(dialog,result)` を受けてから `modal_result(dialog, ...)`。

### file_open

```text
file_open(parent, prompt) -> DialogId / error
```

`GUI_MODAL_FILE_OPEN` を open するだけ。同期的に path を返さない。

### input_open

```text
input_open(parent, prompt) -> DialogId / error
```

`GUI_MODAL_INPUT` を open するだけ。

### session_request

```text
session_launch(path)
session_switch_cui()
session_shutdown()
```

内部で `GUI_OP_SESSION_REQUEST` を使う。

`session_launch` は 1〜255B の絶対 path のみ受理する。呼び出し成功は「pending を受理した」だけで、起動完了ではない。

## 3. Icon16 ABI

論理形式を次で固定する。

```c
typedef struct {
    u8 pixels[128]; /* 16*16*4bpp。row-major。even x=high nibble, odd x=low nibble */
    u8 mask[32];    /* 16*16*1bpp。row-major。bit7 が左。1=opaque, 0=transparent */
} GuiIcon16;        /* 160B */
```

色 index は GUI system 16 色の role/index を使用する。

`draw_icon16(surface, x, y, icon)`:

- 16x16 固定。
- clipping を守る。
- mask=0 pixel は描かない。
- 9801 planar / PEGC packed8 / Cirrus の違いを caller に見せない。
- 拡大縮小しない。

Rust 側 `#[repr(C)]` と size assert を置く。

## 4. stale 組合せ

### old app + new shlib

既存 entry 0〜94 が同一なので正常。

### new app + old shlib

`bind()` の `nfunc < SHLIB_NFUNC` で起動拒否。

### new shlib + old gshell

shlib bind 自体は成功するが op 65/66 が `OS32_ERR_NOSYS`。wrapper はこのエラーをそのまま caller へ返し、panic / loop / memory corruption しない。

### GUI_PROTO_VERSION mismatch

v1.1 で未実走だった mismatch 拒否をこの票で実機試験する。

## 5. AppVTable

既存 AppVTable フィールドを追加・並べ替えしない。

Modal は既存 `on_modal` を使う。File/Input の value は callback 引数へ増やさず、callback 内で `modal_result()` を呼ぶ。

SessionAction の完了 callback は v1.2 では作らない。成功すれば current app は Quit を受けて終了する。

## 6. context menu 方針

v1.2 は popup-window ABI を対象外にしているため、client app の context menu は以下の pattern とする。

- owner window の client surface 内へ overlay 描画。
- menu rect は client rect 内へ clamp。
- right-button event で open。
- Pointer / Button / Key(ESC) を app 自身が解釈。
- close 時に overlay rect を invalidate して通常内容を再描画。

C5 でこの pattern を1つ実装し、必要なら後から library helper に昇格する。C4 では ABI を先走って固定しない。

## 7. テストアプリ

小さい `v12_api_test` を追加してよい。

試験:

- MessageBox -> on_modal -> modal_result(empty)
- File Open -> modal_result(path)
- Input -> modal_result(UTF-8)
- `session_launch` の request block が正しく生成されること（実際の app 置換は W3/G4）
- icon16 の transparent mask

## 完了条件 (G2 / G5)

- `make check-shlib`。
- entry 0〜94 が byte-for-byte 同順。
- `SHLIB_NFUNC=101`。
- old gui_demo + new shlib が動く。
- new test app + old shlib が `jump table too short` で安全に拒否。
- GUI_PROTO_VERSION mismatch を実機で拒否。
- old gshell 相当で op 65/66 が NOSYS のとき wrapper が安全にエラーを返す。
- icon16 が 3 backend で同じ見た目。
