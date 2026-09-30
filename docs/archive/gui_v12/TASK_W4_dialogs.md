# W4: 標準ダイアログ完成

> 発行: PM (2026-09-06) / 状態: **受入完了 (2026-09-07)**
> レーン: W / 前提: K5、W2  
> 親: [TASKS.md](../../tasks/gui/v12/TASKS.md) / 契約: [CONTRACTS.md](../../tasks/gui/v12/CONTRACTS.md) V12-M  
> 排他: `userland/gshell/**`

## ゴール

v1.1 で実装済みの MessageBox / File Open を、v1.2 の正式な非同期標準ダイアログ API に完成させる。Input dialog を追加し、結果をイベントリングとは独立に安全に取得できるようにする。

**入れ子ループは禁止。** すべて X3 の通常状態機械で動かす。

## 1. completed result

各 GUI slot に 1 件だけ completed result を保持する。

必要な情報:

```text
used
dialog_id
result
value_len
value[255]
modal_event_pending
```

ダイアログ完了順序:

1. completed result を slot に保存。
2. `GUI_EV_MODAL` の sticky pending を立てる。
3. リングに空きがあれば append。
4. 満杯ならイベントを捨てず、次の `OP_POLL` / X3 で再配送。

結果本体は event ring overflow の影響を受けない。

## 2. MODAL_OPEN の規則

- WM 全体では従来どおり active modal は 1 枚まででよい。
- caller slot に未 consume completed result がある場合 `OS32_ERR_FULL`。
- active modal がある場合 `OS32_ERR_FULL`。
- parent WindowId は caller owner の現存 window でなければ `OS32_ERR_STALE/INVAL`。
- dialog id は再利用時に進め、0 を使わない。

## 3. MODAL_RESULT

K5 の `GUI_OP_MODAL_RESULT = 65`。

- req.dialog と completed dialog が一致しなければ `OS32_ERR_STALE`。
- response へ result / dialog / GuiString を全部書いてから consume。
- consume 後の再取得は `OS32_ERR_STALE`。
- owner exit / kill 時は active modal、completed result、pending Modal event を全て破棄。

## 4. MessageBox

既存 3 種を維持する。

- OK
- OK / Cancel
- Yes / No

`value` は空。

ESC:

- OK: Cancel 相当ではなく OK で閉じてもよいが、v1.2 では **result=0 (Cancel)** に統一する。
- OK/Cancel, Yes/No: result=0。

RETURN は現在 focus button を決定する。

## 5. File Open

既存 `modal.rs` の File Open を拡張する。

- start directory は v1.2 では `/` のままでよい。
- directory は RETURN / double-click 相当で降りる。
- `..` で親へ。
- file 選択 + Open で絶対パスを completed `value` へ保存。
- Cancel / ESC は value 空。
- path は 255B を超える場合、その項目を Open 不可にし、切り詰めた別 path を返してはならない。

VFS `sys_ls` / directory reload は X3 だけ。X4 で実行しない。

## 6. Input dialog

`GUI_MODAL_INPUT = 4`。

表示:

- title: `Input`
- prompt: `GuiReqModal.message`
- 1 行 edit field
- OK / Cancel

入力:

- ANK / UTF-8 Text
- BS / DEL
- LEFT / RIGHT は v1.2 では byte cursor ではなく UTF-8 codepoint 境界で移動
- HOME / END
- SHIFT+SPACE FEP
- RETURN = OK
- ESC = Cancel

最大 255B。255B を超える Text は入れず、既存内容を保持。

FEP 未確定行 / 候補窓は input field の caret 位置に出す。`SET_TEXT_CURSOR` 相当の内部位置を modal 自身が FEP 描画へ渡す。

OK で completed value に UTF-8 を保存。Cancel は空。

## 7. damage / visible

modal は WM overlay として最前面。

- open / cursor move / focus change / text edit は modal rect の必要部分だけ dirty。
- close 時は modal rect を desktop/chrome dirty にし、その下の全 visible client と交差する部分へ damage を戻す。
- taskbar の上に modal を出すかは W3 work area に合わせ、原則 work area 内中央。

## 8. sticky control event

`Modal` 完了イベントは Pointer と違い、満杯で捨てない。

- pending bit を持つ。
- append 成功まで再試行。
- dropped counter を増やさない。
- completed result が consume 済みでも、既に ring に入った Modal event は古い通知として到着し得る。クライアントは `MODAL_RESULT` が `STALE` なら無視してよい。

## 9. owner cleanup

`gui_owner_exit()` の回収に以下を含める。

- active modal owned by owner
- completed result for owner slot
- modal_event_pending
- modal FEP/caret state

回収で別 owner の completed result を消さない。

## 完了条件 (G2)

- MessageBox OK/Cancel/Yes/No が mouse + key で動作。
- File Open で 255B 以下の絶対 path が `MODAL_RESULT` から取れる。
- Input に `nihongo` -> FEP 変換 -> 「日本語」が入り、その UTF-8 が result で取れる。
- result 未 consume のまま次 modal を開くと `ERR_FULL`。
- wrong dialog id / 二重 consume は `ERR_STALE`。
- ring を意図的に満杯にして dialog を完了しても、空きができた後に Modal event が届き result が失われない。
- modal 所有 app を CTRL+STOP しても次回 modal が `ERR_FULL` にならない。
