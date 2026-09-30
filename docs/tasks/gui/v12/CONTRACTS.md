# GUI シェル v1.2 — 追加契約

> 発行: PM (2026-09-06)  
> 親契約: [../API_CONTRACTS.md](../API_CONTRACTS.md)  
> 親設計: [../DESIGN.md](../DESIGN.md)  
> 作業分担: [TASKS.md](TASKS.md)
>
> v1.1 の G / T / U 契約は変更しない。本書は v1.2 で必要な事項だけを追加する。  
> 原則: 既存番号・構造体・共有ライブラリのジャンプ表は変更せず、**末尾追記のみ**。

---

## 0. v1.2 の範囲

v1.2 は HAL 拡張版ではなく、v1.1 で完成した GUI 基盤を**デスクトップ環境として完成させる版**とする。

v1.1 で実機通過した 9801 / PEGC / Cirrus の 3 バックエンド、Ring3 分離、共有ライブラリ、FEP、モーダル、入力ポンプを基盤として固定し、v1.2 では次を完成させる。

- タスクバー
- Start メニュー
- ランチャー
- 時計
- 標準ダイアログの結果返却
- ファイルマネージャ
- GUI セッション終了 / CUI 切替
- 既存 `/api/mouse` を使った自動回帰検証

**KAPI v42 は変更しない。** v43 は network / Host Services 用の予約であり、GUI v1.2 は使用しない。新機能は既存 `gui_call(op, arg)` と GUI 共有プロトコル、`libos32gui.shlib` の末尾追記で実現する。

`GUI_PROTO_VERSION` は 1 のままとする。古い WM が新 op を知らない場合は `OS32_ERR_NOSYS` で安全に拒否し、stale 組合せでメモリ破壊や別 op 実行を起こさない。

---

## 1. V12-S: セッションモデル

### S1. 外部 GUI アプリは同時に 1 本

v1.2 でも実行モデルは協調型シングルフォアグラウンドアプリとする。

- gshell はシェル帯に常駐する。
- 外部アプリは `exec_run()` で 1 本だけ実行する。
- 1 アプリは複数ウィンドウを所有できる。
- タスクバーのウィンドウボタンは process switch ではなく、そのアプリ内の window の raise / focus を行う。
- v1.2 では複数外部プロセスの同時 GUI 実行を導入しない。

### S2. アプリ実行中の別アプリ起動

WM の `gui_call` handler、X3、X4 から直接 `exec_run()` を呼んではならない。

別アプリ起動要求が発生した場合:

1. `LAUNCH(path)` を gshell 私有状態へ保存する。
2. 現在 owner へ `Quit{reason=REPLACE_APP}` を配送する。
3. 現 app が終了し、既存の `exec_run()` が gshell top-level へ戻る。
4. top-level が pending path を見て次 app を起動する。

アプリが Quit に応答しない場合は既存 CTRL+STOP で回収する。正常 exit / CTRL+STOP / fault kill のいずれでも pending SessionAction は失わない。

### S3. SessionAction

gshell は同時に 1 件だけ保持する。

```text
NONE
LAUNCH(path)
SWITCH_CUI
SHUTDOWN
```

新しい要求で既存 pending を上書きしない。既に pending がある場合は `OS32_ERR_FULL`。

### S4. Session request ABI

外部 app から gshell へ action を依頼するため、既存 modal range の末尾へ追加する。

```c
#define GUI_OP_MODAL_RESULT      65
#define GUI_OP_SESSION_REQUEST   66

#define GUI_SESSION_LAUNCH       1
#define GUI_SESSION_SWITCH_CUI   2
#define GUI_SESSION_SHUTDOWN     3

typedef struct {
    u8 action;
    u8 flags;       /* v1.2 は 0 のみ */
    u16 _pad;
    GuiString value;
} GuiReqSession;                    /* 260B */
```

`LAUNCH` の `value` は 1〜255B の絶対 path。`SWITCH_CUI` / `SHUTDOWN` は value 空。

戻り値:

- `0`: pending として受理。
- `OS32_ERR_INVAL`: action / flags / path 不正。
- `OS32_ERR_FULL`: 既に action pending。
- `OS32_ERR_NOSYS`: 古い WM。

成功は action の**完了**ではなく**受理**だけを意味する。

### S5. Quit reason と sticky 配送

既存 `GUI_EV_QUIT = 12` の `sub`:

```c
#define GUI_QUIT_REASON_REPLACE_APP  1
#define GUI_QUIT_REASON_SWITCH_CUI   2
#define GUI_QUIT_REASON_SHUTDOWN     3
```

0 は予約。

Quit は control event なのでリング満杯で捨てない。WM は slot/owner に `quit_pending` を保持し、`OP_POLL` の返却準備または X3 で空きができるまで再配送する。`dropped` には加算しない。

### S6. SWITCH_CUI

**CUI へ戻る経路は Start → CUI mode → 確認ダイアログ → SessionAction の 1 本だけ** (2026-09-06 決定)。
確認は WM 内蔵の MessageBox (Yes / No、「CUI モードへ切り替えます。GUI の状態は保持されません」)
で、Yes で初めて `SWITCH_CUI` を立てる。1 キーで無言のまま CUI へ落ちる経路は作らない —
CUI へ戻ると GUI の状態 (窓・アプリ) は設計上維持されないので危険。

v1.1 の **ESC 即時切替と上部の `ESC:CUI F1..F5` バーは G5 で撤去済み** (2026-09-07、W3 §4.1)。
`gshell` の `DEBUG_SHORTCUTS` (既定 `false`) の下にデバッグ設備としてコードだけ残っており、
出荷形では ESC も F1〜F5 も効かず、上部バーも描かれない。SHUTDOWN も同じく確認ダイアログを経る。

SessionAction 実行は current app が終了して top-level へ戻ってから。

1. cursor hide。
2. `ime_set_render(NULL)`。
3. `gfx_shutdown()`。
4. `/etc/system.cfg` を `GUI=0` に更新。
5. `sys_switch_shell("/sys/shell.bin")`。
6. gshell exit。

cfg 更新に失敗した場合、shell switch を実行せず desktop へ戻してエラー表示する。永続設定と実 shell の不一致を作らない。

### S7. SHUTDOWN

v1.2 の Shut Down は**電源 OFF ではなく system halt** とする。

`sys_halt()` は 1 回の `hlt` であり IRQ 後に復帰するので、1 回呼んで終了扱いにしてはならない。

1. current app を Quit で終了させる。
2. cursor hide。
3. `ime_set_render(NULL)`。
4. `gfx_shutdown()`。
5. 必要なら TVRAM に `System halted. Reset to restart.` を表示。
6. `for (;;) { sys_halt(); }` に入り、通常 code へ二度と戻らない。

### S8. X4 bounded-work

syscall 境界ポンプ X4 では以下を禁止する。

- VFS directory 走査
- file 読み書き
- `exec_run`
- `system.cfg` 更新
- modal directory reload
- Start menu 項目生成
- full composite

X4 は input 取得、cursor sprite、小さい pending flag 更新まで。VFS、起動、終了処理、menu 構築は X3 または gshell top-level で行う。

---

## 2. V12-D: デスクトップ

### D1. タスクバー

タスクバーは WM 自身の UI とし、app 用 Window / Surface / SHM slot を消費しない。

- 高さ: 24px
- 画面下端固定
- 左: Start
- 中央: visible top-level window button
- 右: 時計
- WM 内部描画 (U8)
- taskbar 領域の mouse input は app へ配送しない

作業領域:

```text
work_height = screen_height - 24
```

- 640x400 -> 640x376
- 640x480 -> 640x456

既存 app との互換性のため、window が work area 外へ存在すること自体は fault にしない。新規配置・drag 確定時だけ work area へ clamp。

### D2. Start menu

Win95 風の WM 内部 menu。

最低項目:

- Programs
- File Manager
- Run...
- CUI mode
- Shut Down

Programs は `/usr/bin` の `.bin` を列挙し、v1.2 は最大 96 件。超過分は省略表示でよい。
テスト用バイナリ (`ring3_guard` 等、~40 本) も並ぶが v1.2 はそれでよい。manifest やアイコンによる
フィルタは対象外 (§8)。

Directory scan は menu open 時の X3 で 1 回だけ行い、open 中は cache。毎 frame / X4 では走査しない。

`Run...` は Input dialog で絶対 path を受ける。PATH search / argument line は v1.2 対象外。

### D3. 時計

`sys_time()` を用い HH:MM を表示する。1 秒より細かい更新は不要。

表示が変わった時だけ時計矩形を dirty にする。時計更新だけで fullscreen present しない。

### D4. WM context menu

Desktop の right-click menu は WM internal overlay とする。最低 File Manager / Run... / Refresh Programs。

**右ボタンは v1.1 の gshell では一切扱っていない** (`input.rs` は左のエッジだけ)。W3 が右ボタンの
エッジ取り込み (`wm_owns_edge` と同じ領分判定) を足し、デスクトップ上は WM の menu、前面窓の
クライアント上はアプリへ `Button{button=2}` として配送する (F5 のクライアント overlay menu の前提)。

---

## 3. V12-M: 標準ダイアログ完成

### M1. `GUI_OP_MODAL_RESULT`

```c
#define GUI_OP_MODAL_RESULT 65

typedef struct {
    u16 dialog;
    u16 _pad;
} GuiReqModalResult;                 /* 4B */

typedef struct {
    i16 result;
    u16 dialog;
    GuiString value;
} GuiRespModalResult;                /* 260B */
```

`value`:

- MessageBox: 空
- File Open: 選択した絶対 path
- Input: 入力した UTF-8

最大 255B。path/text を切り詰めて別値として返してはならない。

### M2. completed result

WM は各 GUI slot につき 1 件だけ completed result を保持する。

- 未 consume result がある間、その slot の新しい `MODAL_OPEN` は `OS32_ERR_FULL`。
- wrong dialog ID は `OS32_ERR_STALE`。
- `MODAL_RESULT` 成功時は response を完全に書いてから consume。
- 二重 consume は `OS32_ERR_STALE`。
- owner 回収で result を破棄。

### M3. Modal event は sticky

完了時は result を先に保存し、その後 `GUI_EV_MODAL` を配送する。

Modal event も control event なので ring full で捨てない。pending bit を保持し、空きができるまで再配送する。これにより app は `on_modal` を受けて `MODAL_RESULT` を取得できる。

### M4. Input dialog

```c
#define GUI_MODAL_INPUT 4
```

prompt は既存 `GuiReqModal.message`。

- 1 line edit
- OK / Cancel
- UTF-8 codepoint 境界で caret 移動
- BS / DEL / HOME / END
- SHIFT+SPACE FEP
- RETURN=OK / ESC=Cancel
- 最大 255B

FEP 未確定 / candidate は modal caret に追従する。

---

## 4. V12-C: クライアント側追加

KAPI v42 / `GUI_PROTO_VERSION=1` / AppVTable 既存 layout を維持する。

`libos32gui.shlib` entry 0〜94 は固定し、以下を追加する。

```text
95  os32gui_modal_open
96  os32gui_modal_result
97  os32gui_file_open
98  os32gui_input_open
99  os32gui_session_request
100 os32gui_draw_icon16
```

`SHLIB_NFUNC = 101`。`OS32ShlibHeader.version` は **1 のまま** (末尾追記なので版は上げない。
新アプリ → 旧 shlib は `nfunc` 不足で bind 拒否する)。

`modal_result` は **event 到着後だけ呼ぶ API** とする。`OS32_ERR_AGAIN` は存在しないので、
未完成状態を問い合わせる口は公開しない (C4)。

互換条件:

- old app -> new shlib: 正常。
- new app -> old shlib: `nfunc` 不足で bind 拒否。
- new shlib -> old gshell: op 65/66 が `OS32_ERR_NOSYS`、wrapper はそのまま error を返す。
- GUI protocol version mismatch: bind で拒否。

File/Input dialog は非同期。open API が path/text を同期 return して nested event loop を作ってはならない。既存 `on_modal` 内から `modal_result()` で value を取得する。

---

## 5. V12-I: Icon16

v1.2 の標準 icon は 16x16 固定。

```c
typedef struct {
    u8 pixels[128]; /* row-major 4bpp。even x=high nibble / odd x=low nibble */
    u8 mask[32];    /* row-major 1bpp。bit7=左。1=opaque / 0=transparent */
} GuiIcon16;        /* 160B */
```

- 色 index は GUI system 16 色。
- clipping 必須。
- mask=0 は描かない。
- 拡大縮小なし。
- PNG / BMP / ICO decoder は v1.4 以降。

WM と libos32gui は同じ論理形式を使う。

---

## 6. V12-F: File Manager

Win3.1 風 2 pane。

左:

- directory tree の簡易表示
- indent + `[+]` / `[-]`
- lazy load

右:

- current directory file list
- name / size / type

必須:

- navigate
- `.bin` launch
- mkdir
- rename
- delete / rmdir
- file copy
- 同一 FS 内 move
- right-click context menu
- confirmation / input dialog

専用 TreeView ABI は作らない。既存 listbox + client logic で実装。

### F1. Launch

filer は `exec_run()` を直接呼ばない。`GUI_OP_SESSION_REQUEST / LAUNCH(path)` を使い、Quit を受けて終了した後に gshell が次 app を起動する。

### F2. VFS

KAPI v42 既存の `sys_ls/stat/mkdir/rename/unlink/rmdir/open/read/write/close` だけで実装する。KAPI 追加は禁止。

### F3. Copy

file のみ。4096B buffer で copy し、大きい file は 1 event-loop 周につき最大 16KB 程度へ分割して UI / Quit を生存させる。short write を扱う。

partial destination は error を表示し、可能なら cleanup する。overwrite は confirmation 必須。

### F4. Move

同一 FS の `sys_rename` だけ。cross-FS copy+delete は対象外。

### F5. client context menu

popup Window ABI は作らず、owner window client surface 内の overlay とする。client rect 内へ clamp。ESC / outside click で close し、close rect を invalidate。

---

## 7. V12-P: 性能・安全条件

- X4 で VFS / exec / cfg update をしない。
- clock 更新で fullscreen present しない。
- Start menu directory scan は open 時のみ。
- taskbar は app Window 上限 16 を消費しない。
- taskbar は app SHM slot 4 本を消費しない。
- Quit / Modal completion event は ring full で捨てない。
- session action は owner kill でも失わない。
- file copy は event loop へ定期的に戻る。
- PC98 / PEGC / Cirrus の 3 backend で同じ desktop 機能を提供する。
- v1.1 G1〜G5 回帰を維持する。
- v1.1 の Ring3 display isolation、PC98 fallback BB、PCD/PWT 保持を変更しない。

---

## 8. v1.2 対象外

- 複数外部 app 同時実行
- preemptive GUI
- GUI terminal
- CUI output redirect
- app 用 popup-window ABI
- drag & drop
- recursive directory copy/delete
- cross-FS move
- thumbnail
- PNG / BMP / ICO 等の任意 icon format
- new GFX backend
- 640x480 超の解像度
- Start/Run の command-line argument / PATH search
- Programs の manifest / アイコン / 種別によるフィルタ
- true power-off control

---

## 9. 配備 / stale 規則

- KAPI は v42 のままなので kernel/API 全体の version bump はしない。
- GUI wire protocol は version 1 のまま、op を末尾追記。
- shlib jump table を増やしたら `make clean && make all`。`make external` は apps / game が
  libos32gui を使わない限り不要 (SDK の libos32gfx を変えたときだけ要る。CLAUDE.md の規則)。
- v1.2 の gshell / shlib / v1.2 app は同一 image として配備する。
- old app compatibility は維持する。
- `make check-shlib` / `check_gui_proto.py` で stale を gate する。

---

## 10. 完了定義

v1.2 完成とは、CUI command 入力なしで次を完走できること。

```text
OS boot
 -> GUI desktop
 -> Start
 -> File Manager
 -> file operations
 -> GUI app launch
 -> another app launch
 -> CUI switch または system halt
```

かつ同じ手順が:

- PC-9801 planar 640x400
- PEGC 640x480
- Cirrus 640x480

の 3 backend で成立し、v1.1 の全 gate を regression しないこと。
