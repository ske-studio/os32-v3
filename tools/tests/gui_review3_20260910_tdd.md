# GUI レビュー指摘 4 件 (2026-09-10 第 3 回) — TDD 記録

対象レビュー: main `d739494` (GUI v1.2) に対する独立レビュー第 3 回、判定 Request Changes。
第 1 回 3 件は `c7d64e5` / `33c13dd`、第 2 回 2 件は `890d836` (いずれも feat/gui、main へは未マージ)。

**4 件とも実コードで到達可能性を確かめてから着手した。4 件とも挙動試験を書いた。**

---

## [P2] ファイル選択で 48B 以上の名前を別名として返す

`userland/gshell/src/modal.rs`

**到達可能性**: `ls_cb` は 256B の `DirEntryExt.name` から `NAME_LEN - 1 = 47`B だけ
写して NUL 止めする (`NAME_LEN = 48`)。`build_selection` / `enter_dir` はその
切り詰め済みの名前で path を組むので、48B 以上の名前は**実在しない path** か
**同じ 47B の接頭辞を持つ別ファイル**になる。日本語は 3B/字なので 16 字で届く。

契約 M1 (`docs/tasks/gui/v12/CONTRACTS.md:253`)
「最大 255B。path/text を切り詰めて別値として返してはならない」に反する。

**修正**: 収まらなかった行に `too_long` を立て、`enter_dir` と `build_selection`
で拒否する (表示はする)。既に同じ理由で `selection_len > VALUE_MAX` を拒否して
いるので、その規則の延長。あわせて表示用の切り詰めを UTF-8 境界へ戻す (§4-27)。

名前用バッファを 256B に広げる案は採らなかった: `[[u8; 256]; 96]` で 24KB になり、
常駐する gshell の .bss を +20KB する。レビューが挙げた 2 案のうち
「選択不能として扱う」を採った。

**RED** (`file_dialog_refuses_names_that_do_not_fit_instead_of_truncating`):

```
48B の名前を切り詰めた別名として返した: Ok("/bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb")
```

47 文字に切り詰まった別 path が実際に返っていた。**GREEN**: 32 passed。

---

## [P2] タイトルバーや枠の右クリックがアプリへ届く

`userland/gshell/src/input.rs`

**到達可能性**: `wm_right_down` は `st.hit_window()` (= `outer()`、タイトルバーと
枠を含む) で当たりを取り、前面窓なら client 矩形を見ずに `forward_button` する。
`forward_button` は `client_origin()` を引くので、タイトルバー上では**負の
クライアント座標**がアプリへ届く。契約 D4 (`CONTRACTS.md:225`) は
「前面窓の**クライアント上**はアプリへ」と書いている。

**X4 (ポンプ) 経路にも同じ穴があった** — レビューの指摘は X3 側だけだが、
`wm_owns_edge:626` も「前面窓の中の右押下はアプリへ」で client 矩形を見ずに
false を返しており、X4 はそのまま `forward_button` する。両方直した。

**修正**: `wm_right_down` と `wm_owns_edge` の両方で
`client_rect_screen().contains(mx, my)` を見る。外れたら WM が握り潰す。
左ボタンの「クライアント / 枠 → アプリ」は D4 の対象外なので**変えていない**
(枠の扱いは別途決める話)。

**RED/GREEN**: `right_click_on_titlebar_is_not_forwarded_but_client_still_is`。
タイトルバーの右クリックで Button が 0 件、クライアントでは 2 件 (押下+離し)。

---

## [P2] コピー中に File Manager を閉じると不完全な新規ファイルが残る

`userland/rust/filer/src/model.rs`

**到達可能性**: `fail()` は `created && dst_len > 0` なら出力を `unlink` するが、
`abort()` は fd を閉じるだけ。`abort()` の呼び出し元は `shutdown()`
(ESC / 閉じるボタン / Session Quit) と、タイマが張れなかったときの
`lib.rs:1081`。大きなコピーの途中で終了すると、途中まで書いたファイルが
正常なファイルとして残る。

**修正**: `abort()` でも `created` なら消す。ただし**進行中だった場合だけ**
(`was_active`) — `finish` の後や 2 回目の `abort` で消すと、**成功したコピーを
消してしまう**。`start()` は先頭で `abort()` を呼ぶので、ここを間違えると
2 本目のコピーが 1 本目の成果を消す。

**試験**: `tools/tests/test_filer_copy_abort.py` + `userland/rust/filer/host/model_tests.rs`。
実物の `model.rs` を取り込み、KAPI の open/close/read/write/unlink だけ差し替える
挙動試験 (実 FS には触れない)。3 本:

- `abort_deletes_the_partial_output_it_created` … 本題。RED で失敗する 1 本。
- `abort_keeps_an_existing_destination_it_did_not_create` … 上書きコピーは消さない。
- `abort_after_a_completed_copy_never_deletes_the_result` … 過剰削除の番人。

RED: `1 failed` (上の 1 本だけ)。GREEN: `3 passed`。

---

## [P2] アプリ内で押したボタンをタスクバー上で離すと Button-up が失われる

`userland/gshell/src/input.rs`

**到達可能性**: `wm_button_up` は `taskbar::hit()` で早期 return するので、
クライアント内の押下がアプリへ行った後にタスクバー上で離すと、対になる
離しが消える。アプリのドラッグ状態やウィジェットの armed が解けない。
右ボタン (`wm_right_up`) も同じ。

**修正**: 押下をアプリへ配ったときに配送先を捕捉し (`Capture`、左右 1 本ずつ)、
離しは**どこで離しても同じ相手へ**返す。捕捉が無い = その押下を配っていない
ので、離しも配らない (押していないボタンの離しを作らない、という契約 D1 の
本来の狙いはこちらで担保される)。捕捉した窓が消えていれば捨てる
(`win_by_id` で確認するので、窓の破棄フックは要らない)。

X4 経路の離しも `release_capture` を通す。ここだけ `forward_button` を直に
呼ぶと捕捉が残り、次の無関係な離しが古い相手へ飛ぶ。

**RED/GREEN**: `button_up_on_taskbar_still_reaches_the_app_that_got_the_press`。
クライアント内で押下 → タスクバー上で離す → Button が 2 件 (押下+離し)。

---

## 共通の道具

`tools/tests/os32api_host.py` を新設した。`sdk/rust/os32api/src` の実物から
`KernelAPI` を読み、全スロットを panic スタブで埋めた `mock_api()` 付きの
ホスト rlib を組む。gshell の `host/integration.py` と filer の新しい試験が
同じ手を使うので、生成規則を 1 か所に寄せた (KAPI の形はここには書かない)。
`integration.py` はこれを使う形に置き換えた — 置換後も 32 passed で変化なし。

## 実機確認 (2026-09-10、NP21/W 9801 planar 400 ライン、PM が `tools/gui_gate.py` で実施)

観測点は `gui_bench` の `CLICK n` (text VRAM に書く。`/api/tvram` で読む)。
`gui_bench` は `on_raw` で `Button` を数えるので、**WM がアプリへ配ったか**そのものが見える。
窓は `Rect(24,150,420,220)` → タイトルバー中央 (234,161)、クライアント (234,260)、
タスクバー (300,388)。スクリーンショットで窓の位置を確認してから打った。

| 操作 | `CLICK n` | 判定 |
|---|---|---|
| 左クリック (クライアント) — 対照 | 0 → 2 | 1 クリック = +2 (押下+解放) |
| **右クリック (タイトルバー)** | 2 → 2 | **+0 = アプリへ届いていない** (修正前は +2) |
| 右クリック (クライアント) | 2 → 4 | +2 = 契約 D4 どおり届く |
| **クライアントで押下 → タスクバー上で解放** | 4 → 6 | **+2 = 解放が届いた** (修正前は +1) |

その後 ESC で gui_bench を閉じ、Start → CUI mode → Yes で CUI へ戻り、rshell も復旧した。

**道具側で 2 つ罠を踏んだ** (`docs/POLICY_DEBUG.md` §4-31):
rshell を ESC で抜けずに `/api/key` の text を打つと 4 文字ごとのコマンドになる。
`gui_gate.py` の Start メニュー行座標が 5 行前提のままで、v1.3 の 6 行では
1 行ずれて「CUI mode」が **Shut Down** に当たり、ゲストを 1 回 halt させた
(リセットで復旧。NHD は無傷)。行座標は項目数から導く形に直した。

## 未実施

- 残り 4 件 (48B 名の拒否 / 右クリックでウィジェット不動 / コピー中断の後始末 /
  Enter 連打) は**エミュレータでの再現試験をしていない** ([V4])。ホストの挙動試験
  (RED→GREEN) が根拠。台本化の難しさから PM 判断で見送った (承認済み)。
- `feat/gui` → `main` は未マージ。第 1 回の P1 (filer のデータ損失) を含め、
  main には 9 件とも入っていない。
