# GUI レビュー指摘 [P2] (2026-09-10 第 4 回) — TDD 記録

対象レビュー: `feat/gui` (`2e6e8f3`) に対する独立レビュー第 4 回。
本記録は **[P2] モーダル表示中に、アプリへ渡した押下の「離し」が失われる** の 1 件と、
T5b 撤去 (`1c98613`) で被覆が消えた回帰試験 4 本の書き直しを扱う。

作業ブランチ: `worktree-agent-ab55c642ebcafd520` (`2e6e8f3` から)。**コミットしていない**。

---

## [P2] モーダル中に捕捉した離しが失われる

`userland/gshell/src/input.rs` `capture_mouse()`

### 到達可能性 (コードで確認)

押下をアプリへ配ったとき `forward_button()` が `Capture` (左右 1 本ずつ、`cap(button)`)
に配送先を記録し、対になる離しは `release_capture()` が**どこで離しても同じ相手へ**返す
(第 3 回の修正 `23147da`)。

ところがモーダルの分岐 (`input.rs:478` 付近) は

```rust
if modal::is_open() {
    ...
    if ctx.wm_ui() {
        if down_edge { let _ = modal::on_button(st, mx, my); }
        st.prev_buttons = btn;      /* ← release_capture を通らない */
    }
    return;
}
```

で、`release_capture()` を通らずに `prev_buttons` だけ進めて `return` する。よって

1. アプリのクライアント内で押下 → アプリへ `Button{down}`、`Capture` が立つ
2. **押したまま**アプリがダイアログを開く (`OP_MODAL_OPEN`)
3. 離す → この分岐で握り潰され、アプリに `Button{up}` が来ない

3. の時点で `prev_buttons` は 0 になっているので `up_edge` は二度と立たず、
アプリのウィジェットは armed のまま、`Capture` も立ちっぱなしで残る
(次の無関係な離しが古い相手へ飛びうる)。

### 直し方 (契約との整合)

契約 U4「モーダル中は宛先をダイアログに限定」は**新しい入力**に対する規則で、
モーダルが開く前の押下と対になる離しは、その押下を受けたアプリのもの。
X3 のモーダル分岐で `up_edge` / `rup_edge` のときに
`release_capture(st, mx, my, MOUSE_BTN_LEFT/RIGHT)` を呼んでから `prev_buttons` を進める。
捕捉が無ければ `release_capture()` は何も配らないので、モーダル中に**始まった**
押下・離し (U4 の対象) は今までどおりダイアログだけのものになる。
ダイアログへの `down_edge` 配送は変えていない。

X4 (ポンプ) 側はモーダル中に `prev_buttons` を進めないままなので、
ポンプが先に離しを見ても次の X3 が同じエッジを拾い直す (二重配送も欠落も無し)。

モーダルを**開く側**で捕捉を捨てる案 (Win32 の `WM_CANCELMODE` 相当) は採らない。
押したままダイアログを開くアプリでも「押下と離しが対になる」ほうが、
第 3 回の修正方針 (押下を配ったなら離しも配る) と一貫する。

### RED (修正前)

`userland/gshell/host/wm_tests.rs` に 3 本を追加した状態で、
`userland/gshell/target/gshell-host/integration-tests --test-threads=1 --exact <名前>`
(静的状態の持ち越しを避けるため 1 本ずつ単独で実行):

```
test input::tests::captured_release_is_delivered_while_modal_is_open ... FAILED
  wm_tests.rs:493: assertion `left == right` failed:
    モーダル中に捕捉した離しが失われた (button=1): [(true, 1, 158, 140)]
    left: 1   right: 2

test input::tests::captured_right_release_is_delivered_while_modal_is_open ... FAILED
  wm_tests.rs:493: assertion `left == right` failed:
    モーダル中に捕捉した離しが失われた (button=2): [(true, 2, 158, 140)]
    left: 1   right: 2

test input::tests::captured_release_survives_the_pump_while_modal_is_open ... FAILED
  wm_tests.rs:555: assertion `left == right` failed:
    X4 の後で離しが失われた: [(true, 1, 158, 140)]
    left: 1   right: 2
```

リングには押下 1 件しか無く、離しが失われていることがそのまま出ている。

負例 `new_press_during_modal_is_not_forwarded_to_the_app` は修正前から `ok`
(U4 が既に効いている side)。修正後も `ok` であることで U4 を壊していないことを示す。

### GREEN (修正後)

`make check-gshell-host` (= `python3 userland/gshell/host/integration.py`、`--test-threads=1`):

```
running 12 tests
test input::tests::button_up_on_taskbar_still_reaches_the_app_that_got_the_press ... ok
test input::tests::captured_release_is_delivered_while_modal_is_open ... ok
test input::tests::captured_release_survives_the_pump_while_modal_is_open ... ok
test input::tests::captured_right_release_is_delivered_while_modal_is_open ... ok
test input::tests::double_enter_keeps_fep_commit_in_the_input_dialog_result ... ok
test input::tests::fep_press_release_then_app_press_is_delivered ... ok
test input::tests::file_dialog_refuses_names_that_do_not_fit_instead_of_truncating ... ok
test input::tests::menu_press_release_then_app_press_is_delivered ... ok
test input::tests::modal_press_release_then_app_press_is_delivered ... ok
test input::tests::new_press_during_modal_is_not_forwarded_to_the_app ... ok
test input::tests::right_click_on_titlebar_is_not_forwarded_but_client_still_is ... ok
test input::tests::taskbar_press_release_then_app_press_is_delivered ... ok

test result: ok. 12 passed; 0 failed; 0 ignored; 0 measured; 0 filtered out
```

---

## 消えた回帰試験 4 本の復元

`1c98613` (T5b 常駐表示パネル撤去) の本文が残件として挙げていた
「上位 UI 上での離しがアプリの次の押下を飲み込まない試験 4 本」を書き直した。
元の 4 本 (`terminal_tests.rs` の `check_wait_release_allows_immediate_app_press`) は
**パネルが最初の押下を食って `prev_buttons` を立てる**仕掛けに依存していた。

書き直しは `upper_ui_press_release_then_app_press(kind)` 1 本を
`taskbar` / `menu` / `modal` / `fep` の 4 本から呼ぶ形にした。性質は元と同じ:

> 上位 UI の上で押して離しても、アプリに**押していないボタンの離し**が飛ばず、
> **その直後の**アプリ内の押下は届く (間に無操作の周を挟まない)。

| 試験 | 押下を取るもの | 離しの位置 |
|---|---|---|
| `taskbar_press_release_then_app_press_is_delivered` | タスクバー (Start でも窓ボタンでもない帯) | 同じ点 |
| `menu_press_release_then_app_press_is_delivered` | `startmenu::open_context()` で開いたメニュー (外側の押下で閉じ、対の離しは swallow_up) | 同じ点 |
| `modal_press_release_then_app_press_is_delivered` | `modal::open_wm_message(GUI_MODAL_OK, ..., WM_PURPOSE_NOTIFY)` のダイアログ | 同じ点 |
| `fep_press_release_then_app_press_is_delivered` | **タスクバー** | `fep::rect()` の上 |

### 依頼どおりにできなかった点 (FEP)

依頼は「FEP 自身が押下を取る」形を求めていたが、**FEP はマウスの領分を持たない**。
`input.rs` は FEP へマウスを一切渡さず (`fep::` の呼び出しは全部キーボード経路)、
`wm_owns_edge()` にも FEP の判定は無い。`fep::rect()` はテキストカーソル位置に
浮くのでアプリのクライアント上に載り、そこを押すと素直にアプリへ配送される。

元の試験も同じで、FEP 矩形の上の押下を食っていたのは FEP ではなく T5b パネルだった
(`assert!(rect(st).contains(r.x, r.y))` = FEP 矩形がパネルの中にある、という主張)。
そこで元の被覆 (**FEP 矩形の上の離しが漏れない**) を保つため、押下はタスクバーに取らせ、
離しを `fep::rect()` の上で行う形にした。手順 (`tc_visible` / `tc_x` / `tc_y` を窓に設定 →
`ime_is_active` を 1 → `fep::install(); fep::pre_cycle(st); fep::post_cycle(st)` →
`assert!(!fep::rect().is_empty())`) は元の試験から写している。

### 依頼の座標から変えた点 (タスクバー)

依頼の `(30, h-12)` は **Start ボタンの上**で、押下で Start メニューが開く。
すると直後のアプリ押下が「メニューを閉じる」に化けて試験の主題がぼける
(実測で `上位 UI (taskbar) の離しの直後、アプリの押下が飲まれた: []` になった)。
`button_up_on_taskbar_still_reaches_the_app_that_got_the_press` と同じ
`(500, h-1)` (Start でも窓ボタンでもない帯) に変えた。

### 試験の後始末について

`modal` / `startmenu` / `fep` / `CAPTURE` はいずれも静的なので、各回の最後に
ダイアログを閉じ、FEP を off に戻し、押下と離しを対で終わらせている
(`--test-threads=1` で 1 プロセスに全部載るため、途中で panic した試験が
後続を巻き込む。RED の測定を 1 本ずつ単独で行ったのはこのため)。

---

## ホスト試験の結果 (最終)

| コマンド | 結果 |
|---|---|
| `CROSS_DIR=~/opt/cross make programs` | EXIT=0 |
| `CROSS_DIR=~/opt/cross make check` | EXIT=0 |
| `CROSS_DIR=~/opt/cross make check-gshell-host` | EXIT=0、12 passed / 0 failed |

`make check` の `check-manifests` を通すため、gitignore 済みの生成物を本家
`~/os32` から symlink した (`assets/fep.db`、`assets/fonts/ipaexg16.kcgfont`、
`assets/fonts/ipaexg_subset.ttf`、`build/out/unicode.bin`)。
`build/out/vmkernel.lz4` と `boot/*.bin` は worktree 内で `make kernel` / `make boot` して作った。

## 未確認

- **実機 (NP21/W) 未検証**。コーダー役の禁止事項 (配備・エミュレータ操作) のため、
  ホスト試験だけで完結させている。押したままダイアログを開くアプリでの
  「離しが届く / ウィジェットの armed が解ける」実挙動は未確認。
- 押下中に `OP_MODAL_OPEN` を呼ぶアプリを `userland/` 内には見つけていない
  (`libos32gui` の `modal.rs` / `shlib.rs` が API を出しているだけ)。
  submodule の `apps/` `game/` はこの worktree で未チェックアウトのため未調査。
  この経路は契約上到達可能というだけで、実機の再現手順は用意していない。
