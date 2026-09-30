# K5: v1.2 GUI プロトコル完成

> 発行: PM (2026-09-06) / 状態: **受入完了 (2026-09-07)**
> レーン: K / 前提: v1.1 G1〜G5 完了  
> 親: [TASKS.md](../../tasks/gui/v12/TASKS.md) / 追加契約: [CONTRACTS.md](../../tasks/gui/v12/CONTRACTS.md)  
> 排他: `sdk/include/os32/os32_gui_shared.h`、`sdk/rust/os32api/src/gui/proto.rs`  
> **KAPI v42 は変更しない。v43 は Host Services / network 用の予約なので GUI は使わない。**

## ゴール

v1.2 が必要とする「モーダル結果取得」と「外部アプリから gshell へのセッション要求」を、既存 `gui_call(op,arg)` の末尾追記だけで定義する。

既存 op / event / struct の番号・配置は変更しない。`GUI_PROTO_VERSION` は 1 のまま。

## 1. 追加 op

既存 `GUI_OP_MODAL_OPEN = 64` に続けて固定する。

```c
#define GUI_OP_MODAL_RESULT      65
#define GUI_OP_SESSION_REQUEST   66
```

80 の `GUI_OP_OWNER_EXIT` は既存カーネル内部 op のまま変更しない。67〜79 は予約。

未実装 op を受けた WM は `OS32_ERR_NOSYS` を返す。これにより GUI protocol version 1 のままでも「新 shlib + 古い gshell」は安全に失敗する。

## 2. Modal result ABI

```c
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

追加 modal kind:

```c
#define GUI_MODAL_INPUT 4
```

既存 0〜3 は不変。

`value`:

- MessageBox: 空
- File Open: 選択した絶対パス
- Input: 入力した UTF-8

最大 255B。

### 結果の取得規則

- WM は各 slot に completed modal result を 1 件だけ保持する。
- 未 consume result がある間、その slot の次の `MODAL_OPEN` は `OS32_ERR_FULL`。
- `MODAL_RESULT` の dialog が保持中 ID と違う場合 `OS32_ERR_STALE`。
- 成功した `MODAL_RESULT` は result/value を response に書いた後 consume する。
- consume 後に同じ ID を再取得すると `OS32_ERR_STALE`。
- owner 回収時に未 consume result も破棄する。

## 3. Session request ABI

外部アプリが別プログラムを直接 `exec_run()` せず、gshell トップレベルへ起動等を依頼するための経路。

```c
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

`value`:

- `LAUNCH`: NUL に依存しない長さ前置の絶対パス。1〜255B。
- `SWITCH_CUI`, `SHUTDOWN`: 空でなければならない。

戻り値:

- `0`: pending action として受理。
- `OS32_ERR_INVAL`: action / flags / path が不正。
- `OS32_ERR_FULL`: 既に別の session action が pending。
- `OS32_ERR_NOSYS`: 古い WM。

受理は**実行完了を意味しない**。X1 は gshell 私有メモリへ値をコピーし pending を立てるだけで、VFS / `exec_run` / cfg 更新を行わない。

同時に保持できる session action は 1 件。新要求で上書きしない。

## 4. Quit reason

既存 `GUI_EV_QUIT = 12` の `sub` に次を固定する。

```c
#define GUI_QUIT_REASON_REPLACE_APP  1
#define GUI_QUIT_REASON_SWITCH_CUI   2
#define GUI_QUIT_REASON_SHUTDOWN     3
```

0 は予約。既存イベント番号は変更しない。

Quit は通常イベントのように満杯で捨ててはならない。W3 は slot/owner に sticky `quit_pending` を保持し、リングに空きができるまで再配送する。`ring::add_dropped()` の対象にしない。

## 5. static assert / SSoT

C 側と Rust 側で以下を一致させる。

- op 65 / 66
- modal kind 4
- session action 1〜3
- quit reason 1〜3
- `sizeof(GuiReqModalResult) == 4`
- `sizeof(GuiRespModalResult) == 260`
- `sizeof(GuiReqSession) == 260`
- 各フィールド offset

`tools/check_gui_proto.py` の照合対象にも追加するが、スクリプト自体の変更は PM 所有。

## 6. 禁止事項

- `sdk/kapi.json` を変更しない。
- KAPI version を 43 に上げない。
- `GUI_PROTO_VERSION` を 2 にしない。
- 既存 op / event / struct の並べ替えをしない。
- `GuiString` の形を変えない。
- SHM にユーザポインタを載せない。

## 完了条件 (G0)

- `make check` / `check_gui_proto.py` が通る。
- C / Rust の値と size/offset が一致。
- KAPI は v42 / 180 関数のまま。
- 既存 v1.1 `gui_demo` が無変更でビルド。
- 古い WM が op 65/66 を受けた場合の期待値を `OS32_ERR_NOSYS` として契約・試験に固定。
