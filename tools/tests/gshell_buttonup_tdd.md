# gshell 「上位 UI の上の離し」— 残り被覆の TDD 記録 (2026-09-14)

対象: `docs/tasks/gui/v13/PLAN.md` の残件「T5b 撤去 (`1c98613`) で消えた
ホスト試験 4 本をパネルに依存しない形で書き直す」の**残り半分**。

作業 worktree: `.claude/worktrees/agent-a7b1f92c0b571bc96` (`fac0d89` から)。**コミットしていない**。
実行コマンド: `python3 userland/gshell/host/integration.py` (= `make check-gshell-host`、
`--test-threads=1`)。

---

## 何が残っていたか

消えた 4 本 (`host/terminal_tests.rs` の `check_wait_release_allows_immediate_app_press`、
`taskbar` / `modal` / `menu` / `fep`) は 2 つの性質を 1 本で見ていた。

1. 上位 UI の上の押下・離しが**アプリへ漏れない** (押していないボタンの離しを作らない)
2. その離しの**直後のアプリ内の押下が飲まれない**

`89ff220` の書き直し (`upper_ui_press_release_then_app_press(kind)` × 4) は
**上位 UI 自身が押下を取る**側から 1. と 2. を復元した。一方、元の 4 本が
T5b パネルの「最初の押下を食う」仕掛けで成立させていた裏側 —
**アプリが受けた押下と対になる離しを上位 UI の上で行う**経路 — は、
タスクバーの `button_up_on_taskbar_still_reaches_the_app_that_got_the_press` と
モーダルの `captured_release_while_modal()` しか無く、メニューと FEP が空いていた。
「次の押下を飲み込まない」もこちら側では 1 経路も見ていなかった。

現在の設計 (契約 D1「タスクバー領域の入力をアプリへ配送しない」は**対になる離しには
掛けない**、`input.rs` の `CAPTURE` / `release_capture()`) の上で、この裏側を
4 経路そろえたのが本記録。

## 足した試験

`userland/gshell/host/wm_tests.rs` — 助け 1 本 + 試験 4 本。

| 試験 | 押下 | 離しの位置 | 上位 UI の片付け |
|---|---|---|---|
| `app_release_over_taskbar_reaches_the_app_and_the_next_press_lands` | アプリのクライアント (`APP_PT`) | タスクバーの帯 (`(500, screen_h-1)`、Start でも窓ボタンでもない) | 不要 |
| `app_release_over_menu_reaches_the_app_and_the_next_press_lands` | 同上 | `startmenu::open_context()` で開いたメニューの中心 (`startmenu::rect()` から導く) | `startmenu::close()` |
| `app_release_over_modal_reaches_the_app_and_the_next_press_lands` | 同上 | `modal::rect()` の中 (押したままアプリが `OP_MODAL_OPEN`) | ESC |
| `app_release_over_fep_reaches_the_app_and_the_next_press_lands` | 同上 | `fep::rect()` の上 (`tc_visible` / `ime_is_active`=1) | `ime_is_active`=0 → `install`/`pre_cycle`/`post_cycle` |

各回の主張は同じ 3 つ:

1. 押下がアプリへ 1 件届く (`Button{down,1}`)
2. 上位 UI の上の離しが**同じ相手へ**届く (2 件目が `Button{up,1}`)
3. 上位 UI を片付けた直後のアプリ内の押下が届く (3 件目が `Button{down,1}`、
   間に無操作の周を挟まない)

座標は `startmenu::rect()` / `modal::rect()` / `fep::rect()` / `st.screen_h` から
導いていて、上位 UI の寸法は直書きしていない。静的な状態 (`modal` / `startmenu` /
`fep` / `CAPTURE`) は各回の末尾で戻す (`--test-threads=1` で 1 プロセスに全部載るため)。

## RED

現在の振る舞いは正しいので、試験がそのままでは RED にならない。**守っている規則を
壊す退行を一時的に入れて**、4 本が落ちることを確かめた (測定後に
`git checkout -- userland/gshell/src/input.rs` で戻し、成果物には含めていない)。

入れた退行は 2 か所。

```rust
/* (1) wm_button_up の先頭 — 契約 D1 を「対になる離し」にも掛けてしまう */
if taskbar::hit(st, mx, my) || startmenu::is_open() { return; }
if fep::rect().contains(mx, my) { return; }

/* (2) capture_mouse の modal 分岐 — 89ff220 以前の形 (release_capture を通らない) */
st.prev_buttons = btn;
```

```
test input::tests::app_release_over_fep_reaches_the_app_and_the_next_press_lands ... FAILED
test input::tests::app_release_over_menu_reaches_the_app_and_the_next_press_lands ... FAILED
test input::tests::app_release_over_modal_reaches_the_app_and_the_next_press_lands ... FAILED
test input::tests::app_release_over_taskbar_reaches_the_app_and_the_next_press_lands ... FAILED

assertion `left == right` failed: 上位 UI (taskbar) の上の離しが、押下を受けたアプリへ届かない: [(true, 1, 158, 140)]
assertion `left == right` failed: 上位 UI (menu)    の上の離しが、押下を受けたアプリへ届かない: [(true, 1, 158, 140)]
assertion `left == right` failed: 上位 UI (modal)   の上の離しが、押下を受けたアプリへ届かない: [(true, 1, 158, 140)]
assertion `left == right` failed: 上位 UI (fep)     の上の離しが、押下を受けたアプリへ届かない: [(true, 1, 158, 140)]

test result: FAILED. 91 passed; 9 failed
```

同じ退行で既存の `button_up_on_taskbar_still_reaches_the_app_that_got_the_press` /
`captured_release_is_delivered_while_modal_is_open` /
`captured_right_release_is_delivered_while_modal_is_open` /
`captured_release_survives_the_pump_while_modal_is_open` /
`right_click_on_titlebar_is_not_forwarded_but_client_still_is` も落ちる (計 9 本)。
新しい 4 本は既存の被覆と同じ規則の、**メニューと FEP へ広げた分**にあたる。

## GREEN

退行を戻した状態 (= `fac0d89` の `input.rs` そのまま + 試験の追加だけ)。

```
test input::tests::app_release_over_fep_reaches_the_app_and_the_next_press_lands ... ok
test input::tests::app_release_over_menu_reaches_the_app_and_the_next_press_lands ... ok
test input::tests::app_release_over_modal_reaches_the_app_and_the_next_press_lands ... ok
test input::tests::app_release_over_taskbar_reaches_the_app_and_the_next_press_lands ... ok

test result: ok. 100 passed; 0 failed; 0 ignored; 0 measured; 0 filtered out
```

基点 (`fac0d89`) は 96 passed。**製品コードの修正は無い** — 現在の
`release_capture()` 経路が 4 経路とも正しく、試験だけを足した。

## 設計判断 (現在の設計を変えない側を選んだもの)

- **FEP はマウスの領分を持たない**。`input.rs` は FEP へマウスを一切渡さず、
  `wm_owns_edge()` にも FEP の判定は無い (`89ff220` の記録と同じ確認)。
  `fep::rect()` はテキストカーソル位置に浮くのでアプリのクライアント上に載り、
  対になる離しはそのままアプリへ返る。FEP に矩形の当たり判定を持たせる案は採らず、
  「FEP 矩形の上の離しが取りこぼされない」という**元の被覆だけ**を試験にした。
- **メニューが開いている間の「対になる離し」もアプリへ返す**。`wm_button_up()` は
  `startmenu::is_open()` を見ずに `release_capture()` する。メニューの領分は
  **新しい**押下 (`wm_owns_edge()` / `wm_button_down()`) の話であり、対の離しは
  押下を受けた相手のもの、という契約 D1 の例外をそのまま踏襲した。
- 既存の `button_up_on_taskbar_still_reaches_the_app_that_got_the_press` は
  `gui_review3_20260910_tdd.md` が RED/GREEN の根拠として参照しているので**残した**。
  新しいタスクバー経路の 1 本は「次の押下を飲み込まない」を足した分だけ重なる。
  重複の整理は PLAN.md の別残件 (試験項目の棚卸し) の対象。
