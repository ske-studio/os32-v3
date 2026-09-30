# K5b-W — gshell 側 (アプリ 4 本の同時実行) の RED → GREEN 記録

> 票: [`docs/archive/gui_v13/TASK_K5B_gshell.md`](../../docs/archive/gui_v13/TASK_K5B_gshell.md) /
> 規則の正典: [K5a 設計 D11-3 / D11-3a](../../docs/archive/gui_v13/TASK_K5_multiapp.md) /
> 模型: [`multiapp_model_host.c`](multiapp_model_host.c) (ケース 12〜16、84 検査) と
> [`multiapp_model_tdd.md`](multiapp_model_tdd.md)
>
> 試験は `userland/gshell/host/wm_tests.rs` (実物の `GuiState` + `multiapp` に対して)、
> 走らせるのは `make check-gshell-host` (= `userland/gshell/host/integration.py`)。

## 基点

`0ac10d6` (feat/gui の先端。K5b-K の KAPI v44、起動修正 `8eb7eed`、sbrk 二段構え `7f5a819` を含む)。

| 回 | 状態 | 検査数 |
|---|---|---:|
| 0 | 着手前の回帰 (既存の WM 試験だけ) | 12 passed / 0 failed |
| 5 | 最終 (K5b-W 本体) | **27 passed / 0 failed** (旧 12 + 新 15) |
| 6 | 追補 W-1 の RED | 27 passed / 2 failed |
| 7 | 追補 W-1 の GREEN | **29 passed / 0 failed** (27 + 新 2) |
| 8 | 追補 W-2 (K5c 追随、決裁 A1 / A3) の RED | 32 passed / 2 failed |
| 9 | 追補 W-2 の GREEN | **34 passed / 0 failed** (29 + 新 5) |

## 回 1 — RED

**やったこと**: 票の試験 (模型ケース 12〜16 相当 + 票の追加検査 9 本) を先に書き、
`userland/gshell/src/multiapp.rs` は**枠だけ**置いた。規則を持つ 5 本の中身は
`TODO(K5b-W)` で、次を返すだけ:

| 関数 | RED での中身 |
|---|---|
| `input_ready` | `false` |
| `derived_ready` | `false` |
| `pick` | `0` (誰も起こさない) |
| `should_park` | `false` (誰も譲らない) |
| `sync_snd_focus` | 何もしない |

配線 (`op_wait` のループ先頭からの `maybe_park`、`GUI_OP_OWNER_EXIT` からの
`on_owner_exit`、`run_program` の `exec_start`、top-level の `resume_one`、
`session::ready_to_run`、`build/app.conf` の KAPI 44) は入れてある。

**結果**: `26 tests: 17 passed / 9 failed`。

```
FAILED  pick_follows_the_frozen_rule_focus_then_input_then_derived
        12a 入力群のフォーカスを選ばない  left: 0  right: 5
FAILED  should_park_matches_the_five_frozen_branches
        13a 据え置きが数えられていない    left: 0  right: 1
FAILED  derived_only_apps_each_get_exactly_one_turn_per_round        (14a 1 本も選べない)
FAILED  an_app_feeding_itself_focus_events_cannot_starve_another     (15a park が 1 度も起きない)
FAILED  a_derived_only_app_runs_within_the_re_derived_bound_across_rounds
        16 前提: ラウンド先頭で A が選ばれる  left: 0  right: 2
FAILED  resume_one_wakes_a_single_app_with_the_unread_count_as_the_wait_result
FAILED  op_wait_parks_when_another_app_is_ready                      (相手が ready でも譲らない)
FAILED  a_single_app_never_parks_and_keeps_the_old_wm_cycle_halt_loop
        op_wait のループが 1 周も回っていない (試験が空振り)  left: []  right: [2]
FAILED  snd_focus_is_called_once_per_focus_change
        初回が違う  left: []  right: [5]
```

既存 12 本は 12/12 のまま GREEN。RED で落ちていない新規 5 本
(`the_fifth_app_gets_err_full...` / `owner_exit_reclaims_exactly_one...` /
`a_parked_app_is_folded_with_exec_kill...` / `exec_park_has_exactly_one_call_site...` /
`launch_no_longer_quits...`) は、規則ではなく**配線と既存の WM 実装**を押さえる検査
(スロット割当は元から 0〜3 を配る、回収は `reclaim_owner` が owner で閉じている、等)。
規則が空でも通るのが正しい — RED の対象は D11 の規則そのもの。

**RED の途中で直した試験側の 2 件** (規則とは無関係):

1. `mocks::init()` が `sys_halt` を差し替えていなかった。`op_wait` の待ちが
   「unmocked KAPI reached」で **abort** し (`extern "C"` の panic は巻き戻せない)、
   試験プロセスごと落ちていた。`nothing` を挿した。
2. `assert!(*MUTEX.lock().unwrap() == ..)` がガードを握ったまま落ちるので、
   RED の失敗が Mutex を中毒させて**次の試験まで巻き添え**にしていた
   (既存の `taskbar_press_release_...` まで FAILED になった)。
   `mocks` 側に複製を返す覗き口 (`snd_focus_calls()` 等) を足し、`init()` の
   ロックを `unwrap_or_else(|e| e.into_inner())` に変えた。

## 回 2 — GREEN

**やったこと**: `multiapp.rs` の 5 本を D11-3 のとおりに書いた。模型との対応:

| 模型 (`multiapp_model_host.c`) | 実物 (`userland/gshell/src/multiapp.rs`) |
|---|---|
| `MA_INPUT_STREAK_MAX` = 4 | `INPUT_STREAK_MAX` |
| `MA_STARVE_BOUND` = `(2N−2)×(STREAK+1)` = 30 | `STARVE_BOUND` |
| `ma_should_park` | `should_park` |
| `ma_pick` / `ma_pick_group` / `ma_round_remaining` | `pick` / `pick_group` / `round_remaining` |
| `ma_resume` の `turn_used` / `last_run` / `input_streak` | `mark_resumed` |
| `ma_launch` の同上 | `on_start` |
| 試験が直接立てていた `input_ready` / `derived_ready` | **実物の WM 状態から算出** (D11-1 の棚卸し) |

`input_ready` / `derived_ready` の材料はすべて実物:

| 群 | 材料 |
|---|---|
| 入力 | `ring::pending(slot)` / `session::quit_pending(slot)` |
| 導出 | `timer::has_expired` / `Win::configure_pending` / `damage::has_deliverable_paint` / **park 時に控えた `OP_WAIT` の期限** |

**結果**: `26 tests: 26 passed / 0 failed`。上限の等号検査

- 15c `b_at == 10` (`N`=2、反例 1 = 自分の 2 窓へ交互に `set_focus`)
- 16c `a2 == 30` (`N`=4、反例 2 = ラウンドまたぎ)

も一発で通った = 規則が模型と 1 対 1 で写っている (どちらかがずれていれば
等号検査が落ちる)。

## 回 3 — 後始末 (GREEN 維持)

ゲスト向けビルドで `dead_code` が 11 件出た (`STARVE_BOUND` と覗き口 9 本、
`request_kill`)。`request_kill` は `resume_one` の失敗経路 (`exec_resume < 0`)
から実際に使う 1 本道へ寄せ、残りは「試験・診断用 (ゲストからは呼ばない)」と
書いて `#[allow(dead_code)]`。

```
make CROSS_DIR=... gshell
  OS32X: gshell.bin (text=156924, bss=17964, heap=1048576, load=0x300000, api>=44)
make check-gshell-host   → 26 passed / 0 failed
```

## 回 4 — RED (自己レビューで見つけた固まり方)

**気付いたこと**: `should_park` は WM の表を見て「未追跡の ID は譲らない」と決めるが、
**表に載る id は `exec_start` の戻り値そのもの**なので、起動したてのアプリはまだ表に居ない。
`exec_start` は「アプリが最初に park する」まで戻らない (決裁 D9-5) ので、これは

```
top-level ── exec_start(B) ──▶ B が OP_WAIT へ ──▶ should_park(B) = false (未追跡)
   ▲                                                      │
   └──────────── 永久に戻らない ◀────────── B は park しない
```

という固まり方になる。結果として

- 2 本目 B は park できない ⇒ **1 本目 A は二度と `exec_resume` されない** (画面が止まる)
- top-level へ戻れない ⇒ 3 本目の `LAUNCH` も実行できない

回 1〜3 の試験はどれも `on_start` を先に呼んでから `should_park` を見ていたので、
この経路を 1 本も通っていなかった。

**足した検査**: `a_just_launched_app_can_park_on_its_first_op_wait`

| 検査 | 内容 |
|---|---|
| a | 起動中 (`begin_start`) なら、未追跡の走っている ID でも譲れる |
| b | 譲るときに表へ載る (`is_tracked`) / `end_start` で本数が 1 増える |
| c | 対照群: 起動中でない未追跡 ID (CUI の入れ子の子) は譲らない |
| d | 対照群: **1 本目の起動では譲らない** (相手が居ない = 回帰ゼロ) |

**結果** (`begin_start` / `end_start` を「何もしない」で置いた状態):
`27 tests: 26 passed / 1 failed` — 新しい 1 本だけが a で落ちる。

## 回 5 — GREEN

`Multi.pending_start` (「`exec_start` を呼んでから戻るまで」) を足し、
`should_park` の先頭で「未追跡 かつ 起動中」なら `adopt_running(cur)` して
表へ迎えるようにした。`run_program` が `exec_start` を `begin_start()` /
`end_start(rc)` で挟む。

**結果**: `27 tests: 27 passed / 0 failed`。ゲスト向けビルドも警告なし。

```
make CROSS_DIR=... gshell
  OS32X: gshell.bin (text=156896, bss=17964, heap=1048576, load=0x300000, api>=44)
make check-gshell-host   → 27 passed / 0 failed
make check-kapi-version  → KAPI バージョン一致: v44 (4 箇所)
```

**限界 (残した)**: 起動中に「起動したアプリの CUI 入れ子の子」が `OP_INIT` まで
済ませて `OP_WAIT` に入ると、そちらを迎えてしまう。カーネルが `!a->gui` で park を
拒む (`appslot_park_check`) ので実害は `ring3_park_reject_count` が 1 増えることだけで、
その ID は `gui_owner_exit` で表から落ちる。受入 G7 でこのカウンタを見るときの注意。

## 検査の対応表 (票の要求 → 試験)

| 票の要求 | 試験 |
|---|---|
| 模型 16 ケース 84 検査と同じ性質 | `pick_follows_the_frozen_rule_...` (12a〜12f) / `should_park_matches_the_five_frozen_branches` (13a〜13f) / `derived_only_apps_each_get_exactly_one_turn_per_round` (14a〜14d) / `an_app_feeding_itself_focus_events_cannot_starve_another` (15a〜15d) / `a_derived_only_app_runs_within_the_re_derived_bound_across_rounds` (16a〜16e) |
| 5 本目 `ERR_FULL` | `the_fifth_app_gets_err_full_from_op_init_and_the_four_survive` |
| 終了で 1 本分だけ回収 | `owner_exit_reclaims_exactly_one_app_worth_of_state` |
| 切替は `op_wait` の中でだけ (`exec_park` の呼び出し点 1 点) | `exec_park_has_exactly_one_call_site_and_it_is_the_op_wait_loop_head` (ソース走査。`exec_park` / `exec_resume` / `maybe_park` が各 1 点、`maybe_park` は `handler.rs` の `op_wait` の中で `wm_cycle` より前) |
| 1 本のときは `exec_park` 0 回 | `a_single_app_never_parks_and_keeps_the_old_wm_cycle_halt_loop` (空振り防止に「`wm_cycle` が 1 周は回った」も同時に検査) |
| 自分の 2 窓で `set_focus` を交互に呼ぶアプリが他を飢えさせない (最悪 30 以内) | 15b/15c/15d (`N`=2 で 10、定数上限 30 の内側) と 16 系 |
| フォーカス切替で `snd_focus` が 1 回だけ | `snd_focus_is_called_once_per_focus_change` |
| (票の S2 読み替え) `LAUNCH` は 1 本増やす | `launch_no_longer_quits_the_running_apps_but_switch_cui_still_does` |
| (D11-3 の (2)) top-level は 1 周に 1 本だけ起こす | `resume_one_wakes_a_single_app_with_the_unread_count_as_the_wait_result` |
| (D4) 止めてあるアプリの Quit は `exec_kill` | `a_parked_app_is_folded_with_exec_kill_from_the_top_level` |
| (D9-5) 起動したてのアプリが最初の `OP_WAIT` で譲れる = `exec_start` が戻る | `a_just_launched_app_can_park_on_its_first_op_wait` (回 4 で見つけた固まり方の回帰) |

## この試験で**測っていないこと** ([V4])

- **ゲスト未検証**。受入 G1〜G7 / G9 / G10 は PM とテスターの領分。ここはホストの
  ロジック試験だけで、CR3 の載せ替え・longjmp・`ring3_resume` は 1 度も走っていない
  (そちらはカーネル側の `tools/tests/multiapp_impl_host.c` と実機受入)。
- `exec_park` はホストでは**戻ってくる** (ゲストでは longjmp して戻らない)。
  したがって「park の後 WM の状態が宙に浮かないこと」は検査できていない。
- 操作感 (クリックから窓が反応するまでの ticks)、`INPUT_STREAK_MAX = 4` が
  打鍵の連続を取りこぼさない最小値かどうか (D11-7 の申し送りのまま)。

---

# 追補 — 不具合 W-1 (park 中のアプリの露出領域が再描画されない)

> 発見: 2026-09-11 の実機受入 (`f164805`、15MB 構成、証跡 `build/out/gui_gate/k5b_g1/*.png`)。
> 票の記載は [`TASK_K5B_gshell.md` の「不具合 W-1」](../../docs/archive/gui_v13/TASK_K5B_gshell.md)。
> 基点: `d56fda2` (feat/gui の先端)。

## 観測 (PM)

1. `gui_bench` 単独 → 窓全面が描かれる。
2. Run で `gui_demo` を起動 → `gui_bench` の**露出部分が黒**になり、15 秒待っても黒のまま。
   その間 `ring3_switch_count` は動かない (= park 中の `gui_bench` が誰にも起こされていない)。
3. 露出部をクリックして前面化 → `switch` 3 → 7、`gui_bench` は**隠れていた上半分だけ**描き、
   下半分は黒のまま。

## 回 6 — RED

**足した検査** (`userland/gshell/host/wm_tests.rs`、計 29 本):

| 検査 | 内容 |
|---|---|
| `a_parked_app_is_not_left_black_when_another_app_is_launched` | park 中の A (1 窓、dirty 無し) の上に B の窓がある状態で 2 本目を起動 (`exec_start` → 3)。**A のクライアント面の画が残っている**か、残っていないなら**全面 dirty で描き直させる**か、どちらかは成り立つこと。後者なら `derived_ready(2)` が真で `pick == 2` (= top-level が起こす相手になる) |
| `a_running_app_yields_to_a_parked_app_that_has_a_deliverable_paint` | (1) A に配送できる `Paint` がある間は走っている B が `OP_WAIT` で譲る (`should_park(3)`)、(2) A を起こして `OP_POLL` させると `Paint` が配られ**配送できる dirty は残らない** (B に隠れた分だけが残る = 契約 G4)、(3) B の窓を閉じると A の露出部が dirty になり、また `pick == 2` |

**モックを 1 つ実物に寄せた**: `host/mocks.rs` の `gfx_init` は「何もしない」だったが、
ゲストの `gfx_init` (`gfx/gfx_core.c`) は **VRAM の両ページをゼロクリアする**。
そこまで模さないと「画が消えたのに描き直させない」不具合がホストで観測できず、
検査が空振りする。呼ばれた回数 (`mocks::GFX_INITS`) も数える。

**結果**: `29 tests: 27 passed / 2 failed` — 新しい 2 本だけが落ちる。

```
W-1: 起動で A の画が消えた (gfx_init 1 回) のに描き直させない (dirty len=0)
```

## 原因 (実ソース)

`userland/gshell/src/lib.rs` の `run_program`:

```rust
let r = (a.exec_start)(path.as_ptr()) as i32;
/* アプリがフルスクリーン GFX を使って抜けた場合に備えて描画モードを戻す。 */
(a.gfx_init)();          /* ← VRAM の両ページをゼロクリアする */
```

- 塞ぐ `exec_run` の時代、ここへ戻るのは**アプリが終わったとき**だけだったので、
  `gfx_init` が消すのは死んだアプリの画だけで実害が無かった。
- `exec_start` は**アプリが最初に park した時点で戻る** (決裁 D9-5)。同じことをすると
  **生きているアプリのクライアント面まで消える**。
- WM はクライアント面を持たない (契約 G4) ので、消した画を取り戻す道は
  「本人に `Paint` を出して描き直させる」しかない。ところが**遮蔽は露出を生まない**ので
  直後の `visible::recompute_and_expose` は dirty を 1 つも足さない
  (`exposed = new_vis − old_vis`、B が被さった A では空)。
- 結果 `damage::has_deliverable_paint(A)` が偽 → `multiapp::derived_ready(A)` が偽 →
  `pick` が A を選ばない → `exec_resume` が呼ばれない (= `switch_count` が動かない)。
- 観測 3 (クリックで上半分だけ描く) も同じ式で説明がつく: 前面化の
  `recompute_and_expose` が足す dirty は `new_vis − old_vis` = **B に隠れていた分だけ**で、
  元から見えていた下半分は dirty にならない。

PM の見当 (a)。(b) と (c) は外れ — `gui_demo` は `OP_WAIT` に入るし、`Paint` は
そもそも積まれていない (dirty が空なので発行されない)。

## 回 7 — GREEN

**最小の修正** (`lib.rs` の `run_program`、`damage.rs` に 1 関数):

1. `gfx_init()` を呼ぶのは **`rc <= 0` のときだけ** にした。`rc > 0` は
   「アプリが最初の `OP_WAIT` まで進んで park した」= WM に attach 済みの GUI アプリで、
   フルスクリーン GFX で抜けたわけではない (gshell 配下のアプリは `libos32gfx_attach`
   を使い `gfx_init` を呼ばない — gotcha §4-20。`libos32gfx_attach` 自体は画を消さない)。
   復帰処理が要らない以上、画を消す理由も無い。
2. それでも消すとき (`rc <= 0` = 起動失敗 / park 前に終了) のために
   `damage::invalidate_all_clients(st)` を足した。生きている全ウィンドウを全面 dirty に
   するので、park 中のアプリも導出群の ready に入り top-level の `pick` が起こす。

D11 の規則 (`INPUT_STREAK_MAX` / `turn_used` / フォーカス優先 / 上界 30) は**触っていない**。
規則は正しく働いており、材料 (`dirty`) が失われていたのが原因なので、直したのは材料の側。

**結果**: `29 tests: 29 passed / 0 failed` (既存 27 本は維持)。

```
make CROSS_DIR=... gshell
  OS32X: gshell.bin (text=157056, bss=17964, heap=1048576, load=0x300000, api>=44)
  (Rust の警告 0。ld の 2 件は既存のツールチェーン警告)
make check-gshell-host   → 29 passed / 0 failed
```

## この追補で**測っていないこと** ([V4])

- **ゲスト未検証**。G1 の完全合格 (`gui_bench` の露出部が起動後も描かれたまま) は
  テスターの再配備待ち。ホストで押さえたのは「WM が画を消さない / 消したら描き直させる」
  という判断だけで、実機の VRAM は 1 バイトも見ていない。
- 全体ゲート (`make all` / `make external` / `make check`) は回していない (テスター担当)。
- 観測 3 の「クリック後に上半分だけ描く」は、上の式で説明はついたが**実機では未再現**
  (ホスト試験は起動直後の状態までしか作っていない)。

# 追補 W-2 — K5c への追随 (ユーザー決裁 2026-09-11 A1 / A3)

> 票の末尾「決裁」: [`docs/archive/gui_v13/TASK_K5B_gshell.md`](../../docs/archive/gui_v13/TASK_K5B_gshell.md)
> 前提: K5c (`exec_abort_clear` = KAPI v45、Rust 生成器の `i32`) を取り込んだ上で作業。

## 直したこと 3 つ

1. **型の追随 (A2 の余波)**: K5c で `exec_start` / `exec_resume` / `exec_park` /
   `exec_kill` / `exec_app_state` / `snd_focus` の Rust 束縛が `i32` になった。
   `multiapp.rs` の `exec_resume(k as u32, wait_ret as u32)` / `exec_kill(id as u32)` が
   E0308。`as u32` と、対になっていた `as i32` の往復キャストを 4 か所
   (`multiapp.rs` の park / resume / kill、`lib.rs` の `exec_start`) から落とした。
   挙動不変 — ABI は前から EAX の `i32`。
2. **A1: CTRL+STOP はフォーカス窓のアプリ宛** (契約 T6)。
3. **A3: `SWITCH_CUI` / `SHUTDOWN` で Quit に応答しないアプリの打ち切り**。

## A1 の形 — 「取り消したい」も予約として持ち越す

カーネルは IRQ1 で「いま走っているアプリ」にしか abort を立てられない
(`appslot_abort_request`)。契約 T6 の宛先はフォーカス窓のアプリなので、
フォーカスが別のアプリなら走っている本人の要求を降ろす必要がある。

ところが K5c の注記どおり **`exec_abort_clear` も `exec_kill` も owner 1
(= WM top-level) からしか呼べない**。`op_wait` ハンドラの中では owner が
アプリ ID なので `OS32_ERR_INVAL` になる。そこで:

```text
  op_wait の中 (owner = アプリ ID)
    st.abort_seen かつ フォーカス ≠ 本人
      └ multiapp::redirect_abort()  ── abort_clear_req = true / request_kill(フォーカス)
  should_park の (a)
    └ has_top_level_work() が真 → 譲る (= exec_park)
  top-level (owner 1、lib.rs の単独ループ → multiapp::resume_one)
    └ drain_top_level()  ── (1) exec_abort_clear()  (2) exec_kill(id) を 1 本
```

`should_park` の (a) に予約を足したのが肝で、これが無いと **アプリが 1 本のとき**
(誰も ready でないので (b) の `other_ready` が偽) に top-level へ戻る道が無く、
付け替えも打ち切りも永久に実行されない。

`drain_top_level` は**取り消しを畳むより先に**行う。逆にすると `exec_kill` の
間に走っている本人が syscall の出口を通って巻き添えで死ぬ。

1 本しか居ないときは「フォーカス = 本人」なので `abort_targets_current` が真、
= これまでどおり待ちを抜けてカーネルに畳ませる (回帰ゼロ)。

## A3 の形 — 定数の根拠

`session.rs` に `QUIT_GRACE_CYCLES = 300`。`arm_quit` (= `SWITCH_CUI` /
`SHUTDOWN` のときだけ呼ばれる) で猶予を仕掛け、`session::x3_cycle`
(`wm::wm_cycle` が 1 周に 1 回呼ぶ) で 1 ずつ減らす。0 になったら
`multiapp::request_kill_all()`。

根拠: 待っている間の WM の 1 周は `op_wait` の待ちループも単独ループも
「`wm_cycle` → `sys_halt`」で、`sys_halt` は PIT (100Hz = 10ms) で必ず起きる。
1 周 = 10ms なので **300 周 ≒ 3 秒**。アプリが走り続けている間は `sys_halt` を
通らないぶん 1 周が短くなるだけなので、3 秒はこの待ちの**上界**になる。

順序は決裁どおり「全アプリへ Quit → 待ち → 残りを kill → 全回収 →
SessionAction 実行」。最後の 2 つは既存の `ready_to_run`
(`!owner_active` = 全回収の確認) と top-level の `resume_one` が担う。
Quit に応じたアプリは `gui_owner_exit` で `multiapp` の表から落ちているので
`request_kill_all` の対象に入らない = 畳まれない。確認ダイアログが既に
「保存していない内容は失われます」と警告しているので追加の UI は無し。

## 回 8 — RED

`userland/gshell/host/mocks.rs` に `exec_abort_clear` の差し替え
(`ABORT_CLEARS` の回数記録) を足し、束縛を `i32` 版へ直した上で、
`wm_tests.rs` に 5 本追加。実装側は**空実装**
(`multiapp::pending_top_level_work` → `false`、`session::QUIT_GRACE_CYCLES` の
定数だけ置いて `quit_grace_left` → `0`) で走らせた。

| 新しい試験 | RED |
|---|---|
| (a) `ctrl_stop_with_focus_on_the_running_app_keeps_the_kernel_abort` | pass (回帰の見張り。空実装でも通るのが正しい) |
| (b) `ctrl_stop_with_focus_on_another_app_clears_the_abort_and_kills_the_focused_one` | **FAIL** 「取り消しと kill が top-level へ持ち越されていない」 |
| (c) `switch_cui_folds_an_app_that_ignores_quit_after_the_grace_cycles` | **FAIL** 「Quit を配ったのに猶予が始まっていない」 |
| (c') `switch_cui_kills_nobody_when_every_app_answers_the_quit` | pass (対照群。空実装でも通るのが正しい) |
| (d) `a_single_app_keeps_the_old_ctrl_stop_path` | pass (回帰の見張り) |

```
make check-gshell-host → 32 passed / 2 failed
```

(a) / (c') / (d) が RED で落ちないのは意図したとおり — この 3 本は
「**増やした経路が 1 本のときの挙動を変えていない**」ことの見張りなので、
何もしない実装でも通る。落ちる側 (b) / (c) が新しい振る舞いを釘付けにする。

## 回 9 — GREEN

実装は 4 ファイル:

| ファイル | 足したもの |
|---|---|
| `multiapp.rs` | `abort_clear_req`、`redirect_abort` / `request_kill_all` / `has_top_level_work`、`drain_kills` → `drain_top_level`、`should_park` の (a) に予約を追加 |
| `handler.rs` | `op_wait` の CTRL+STOP 分岐で、宛先が別アプリなら `redirect_abort` |
| `session.rs` | `QUIT_GRACE_CYCLES` / `quit_grace`、`arm_quit` で仕掛け、`x3_cycle` で `tick_quit_grace` |
| `host/mocks.rs` | `exec_abort_clear` の差し替えと `ABORT_CLEARS`、v45 の `i32` 束縛 |

D11 の規則 (`INPUT_STREAK_MAX` / `turn_used` / フォーカス優先 / 上界 30) は
**1 行も触っていない**。`should_park` に足したのは (a) の「top-level にしか
出来ない仕事」の項で、起動要求と同じく**有限個の事象**なので D11-3a の上界は
変わらない。

**結果**: `34 tests: 34 passed / 0 failed` (既存 29 本は維持)。

```
make CROSS_DIR=... gshell
  OS32X: gshell.bin (text=157056, bss=17964, heap=1048576, load=0x300000, api>=45)
  (Rust の警告 0。ld の 2 件は既存のツールチェーン警告)
make check-gshell-host   → 34 passed / 0 failed
make check-kapi-version  → v45 一致 (4 箇所)
make kernel              → EXIT=0
```

## この追補で**測っていないこと** ([V4])

- **ゲスト未検証**。CTRL+STOP の付け替え (A1) も Quit 無視の打ち切り (A3) も、
  実機で押したところは 1 回も見ていない。ホストで押さえたのは
  「WM が誰に何を予約し、top-level で何を呼ぶか」という判断だけ。
- `exec_abort_clear` が**実際に owner 1 からしか通らない**ことと、降ろした後に
  本人が生き延びることはカーネル側の領分 (`tools/tests/multiapp_impl_host.c`
  ケース 20 が実物の `AppSlot` で検査済み、K5c)。ホストのモックは回数を数えるだけ。
- `QUIT_GRACE_CYCLES = 300` が実機で何秒になるかは**未実測**。PIT 100Hz から
  換算した上界で、忙しいときは短くなる。3 秒が体感として妥当かは実機の判断待ち。
- 全体ゲート (`make all` / `make external` / `make check`) は回していない (テスター担当)。
