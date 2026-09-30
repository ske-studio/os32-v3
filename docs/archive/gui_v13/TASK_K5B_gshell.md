# K5b-W: アプリ 4 本の同時実行 — gshell (WM) 側の実装

> 発行: PM (2026-09-11) / 状態: **受入完了 (2026-09-12)**
> (K5b-K の最終署名で**確定**) / レーン: W (Rust、gshell)
>
> **K5b-K が足した KAPI v44 (スロット 180〜185、`sdk/rust/os32api/src/kapi_generated.rs` に生成済み)**:
> `exec_start(cmdline) -> i32` (>0 = app_id で最初の OP_WAIT まで進んで park 済み / 0 = park 前に終了・回収済み / <0)、
> `exec_resume(app_id, wait_ret) -> i32` (app_id = また park / 0 = 終了 / <0。印なしフレームは `OS32_ERR_STALE`)、
> `exec_park() -> i32` (成立すれば戻らない。OP_WAIT 以外の op からは `OS32_ERR_INVAL` を返して戻る)、
> `exec_kill(app_id) -> i32` (走っている本人は `OS32_ERR_STALE` — CTRL+STOP 経路)、
> `exec_app_state(app_id) -> i32` (0 空き / 1 走行 / 2 park)、`snd_focus(app_id) -> i32`。
> すべて owner 1 (シェル帯) からのみ。**gshell の `build/app.conf` の要求 KAPI 版を 44 に上げるのは W レーン。**
> カーネルの制約: CUI の入れ子の子 (`exec_run` で立った) は park できない (WM へ戻れないため)。
> 前提: [K5b-K](TASK_K5B_kernel.md) の KAPI v44 と機構が本家に入っていること / 設計: [K5a §設計 D0〜D11](TASK_K5_multiapp.md)、決裁 (2026-09-11)
> 排他: `userland/gshell/**`、`userland/gshell/host/**`。`sdk/rust/os32api` の生成物は K5b-K の再生成結果を**そのまま使う** (手で触らない)。
> **触らない**: `kernel/**` `exec/**` `kapi/**` `sdk/kapi.json`、`libos32gui` (C レーン)。

## ゴール

WM が **GetMessage 方式**で 4 本のアプリを回す: 起動は塞がない `exec_start`、`OP_WAIT` の中で
D11 の規則 (入力優先は連続 4 回まで、`turn_used` で 1 ラウンド 1 回、フォーカス優先はその下、
ID 昇順の巡回) に従って `exec_park` / `exec_resume`、終了・fault・kill は ID 単位で回収。
スケジューラは持たない。音はフォーカスに追従して排他 (決裁 D9-4)。

## 作業

1. **スロット割当** (契約 T2a、F7): `OP_INIT` で 0〜3 を配る (いまは常に 0)。所有者 ID と
   スロットの対応表、5 本目は `ERR_FULL`。`gui_owner_exit` でスロットを返す。
2. **起動** (D4、決裁 D9-5): `run_program` の `exec_run` を `exec_start` に。戻り値 `>0` = park 済み
   (表に登録)、`0` = park 前に終了 (何もしない)、`<0` = エラー表示。`LAUNCH` の SessionAction は
   「アプリを 1 本増やす」意味になる (現行の「今のアプリを Quit してから」は不要になるので、S2 の
   手順を 4 本前提に読み替え、票に差分を書く)。
3. **`op_wait` の中の譲り合い** (D11): `input_ready(k)` / `derived_ready(k)` を実物で算出
   (`ring::pending` / `session::quit[].pending` / `timer::has_expired` / `configure_pending` /
   `damage::has_deliverable_paint` / `deadline`)。`ma_should_park` / `ma_pick` と**同じ規則**を
   Rust に写し (定数 `INPUT_STREAK_MAX = 4`、`turn_used`、`last_run` は WM 側)、park すべきなら
   `exec_park()` (戻らない)。WM top-level は `exec_resume(id, wait_ret)` で次を起こす。
   1 本のときは現行の `wm_cycle` + `sys_halt` ループのまま (回帰ゼロ、ケース 13b)。
4. **終了・fault・kill・CTRL+STOP** (D4): `gui_owner_exit(id)` でその ID の窓・サーフェス・タイマ・
   スロットを回収 (T4/U8)。止まっているアプリの Quit は `exec_kill(id)`。CTRL+STOP はフォーカス窓の
   アプリが走っているときだけ効く (T6)。
5. **音の排他** (決裁 D9-4、受入 G10): フォーカス切替 (`set_focus` / クリックでの前面化 / 終了で
   前面が変わる) のたびに `snd_focus(new_owner)` を呼ぶ (形は K5b-K の報告で確定)。
6. **可視・描画**: 4 本の窓が重なるときの露出計算は既存 (G4、`visible.rs`) で足りるはず。
   足りない箇所があれば票に書いて PM へ (勝手に契約を変えない)。
7. **ホスト試験** (`userland/gshell/host/wm_tests.rs`): `op_wait` の譲り合いを、モックした `exec_park` /
   `exec_resume` (`os32api::api_ptr()` で差し替え) で **モデルの 16 ケース 84 検査と同じ性質**を検査。
   特に: 5 本目 `ERR_FULL`、終了で 1 本分だけ回収、切替は `op_wait` の中でだけ (`exec_park` の
   呼び出し点が `op_wait` のループ先頭 1 点だけ)、1 本のときは `exec_park` が 0 回、
   `set_focus` を自分の 2 窓で交互に呼ぶアプリが他を飢えさせない (レビュアーの反例)。
   RED→GREEN を `tools/tests/k5b_gshell_tdd.md` に記録。

## 完了条件

`make programs` / `make check` (`check-gshell-host` を含む) EXIT=0、上記試験の GREEN と RED の記録、
`git diff --stat`。**配備・コミット・push・エミュレータ・ローカル AI 禁止。** 実機受入
(G1〜G6 / G9 / G10) は PM とテスター。

## 実機受入 (PM/テスター)

[K5 §K5b の G1〜G10](TASK_K5_multiapp.md#段階-k5b--実装-k5a-凍結後に発注)。観測は `gui_bench` の `CLICK n`
(tvram) とカーネルシンボル (`ring3_switch_count` 等) を `emu_read_mem` で。

---

## 実装 (W レーン、2026-09-11)

`userland/gshell/src/multiapp.rs` を新設し、規則 (D11-3) と park / resume / 音の排他を
そこ 1 か所に集めた。`libos32gui` (C レーン) と `kernel` / `exec` / `kapi` /
`sdk/rust/os32api` の生成物は 1 バイトも触っていない。

| 票の作業 | 実装 |
|---|---|
| 1 スロット割当 | **既存で足りた**。`GuiState::alloc_slot` は元から 0〜3 を配り、`op_init` が空き無しで `OS32_ERR_FULL` を返す。`gui_owner_exit` の `reclaim_owner` もスロットを返す。足したのは `multiapp::on_owner_exit(id)` (譲り合いの表からその ID を落とす) だけ |
| 2 起動 | `run_program` の `exec_run` → `exec_start`。`>0` = `multiapp::on_start(id)` で表へ、`0` = 何もしない、`<0` = モーダル (`ERR_FULL` は「Too many programs (4 max)」と別文言) |
| 3 譲り合い | `handler::op_wait` のループ先頭 1 点から `multiapp::maybe_park(st, owner, deadline)`。top-level (`lib.rs` の単独ループ) が `multiapp::resume_one(st)` |
| 4 終了・fault・kill | 4 経路すべてカーネルの `gui_owner_exit` を通るので `GUI_OP_OWNER_EXIT` の 1 か所で回収。止めてあるアプリを畳む口は `multiapp::request_kill(id)` → top-level の `exec_kill` |
| 5 音の排他 | `multiapp::sync_snd_focus(st)` を `wm_cycle` の末尾 (X3 と単独ループ) から。フォーカスが動いた回だけ `snd_focus(new_owner)` を 1 回 |
| 6 可視・描画 | **既存で足りた** (`visible.rs` の露出計算は owner を見ずに Z 順だけで働く)。契約は変えていない |
| 7 ホスト試験 | `host/wm_tests.rs` に 15 本追加 (計 27 本)。RED→GREEN は [`tools/tests/k5b_gshell_tdd.md`](../../../tools/tests/k5b_gshell_tdd.md) |

`build/app.conf` の gshell 行の要求 KAPI 版を **42 → 44** に上げた
(`make check-kapi-version` は v44 で一致、ビルドの `OS32X: gshell.bin ... api>=44`)。

### 起床条件の実物 (D11-1 の棚卸し → `multiapp.rs`)

| 群 | 実物 |
|---|---|
| 入力 `input_ready(k)` | `ring::pending(slot_of(k)) > 0` / `session::quit_pending(slot)` (S5 の sticky Quit。`session.rs` に覗き口を足した) |
| 導出 `derived_ready(k)` | `timer::has_expired(k)` / `Win::configure_pending` / `damage::has_deliverable_paint` / **park 時に控えた `OP_WAIT` の期限** |

最後の 1 つは設計に無かった穴。カーネルの park は syscall フレームしか保存しないので、
`gui_call(OP_WAIT, timeout)` で待っていたアプリを park すると、**その期限を誰も持っていない**。
`multiapp::note_parked(id, deadline)` が WM 側で控え、`derived_ready` が見る (resume で消す)。

## S2 の読み替え (契約 V12-S §S2 → 4 本前提)

v1.2 の S2 は「別アプリ起動 = 今のアプリを Quit して置き換える」だった。
4 本同時になったので、**`LAUNCH` は「1 本増やす」**に変わる。契約文そのものは
v1.3 の CONTRACTS 側で PM が更新する前提で、実装は次のとおり:

| S2 の手順 | v1.2 | v1.3 (この実装) |
|---|---|---|
| 1. `LAUNCH(path)` を私有状態へ | 同じ | 同じ (`session::request`) |
| 2. 現 owner へ `Quit{REPLACE_APP}` | **配る** | **配らない**。`arm_quit` を呼ぶのは `SWITCH_CUI` / `SHUTDOWN` だけ |
| 3. 現 app が終了し `exec_run` が top-level へ戻る | 塞ぐ `exec_run` の性質 | **走っているアプリが `op_wait` で park して top-level へ戻る** |
| 4. top-level が pending path を見て起動 | 同じ | 同じ。ただし `exec_start` なので既存の 4 本はそのまま生きる |

top-level のゲートも分けた (`session::ready_to_run`):

- `LAUNCH` … 他のアプリが生きていても実行してよい (増やすだけ)。
- `SWITCH_CUI` / `SHUTDOWN` … GUI そのものを畳むので、従来どおり `owner_active` が
  偽になる (全アプリ回収済み) まで実行しない。

`GUI_QUIT_REASON_REPLACE_APP` は**誰も送らなくなった**。定数は残してある (T5 の末尾追記のみ)。

### D11-3 の規則に 1 行だけ足した (要 PM 確認)

`should_park` の先頭に「**top-level にしか出来ない仕事があるなら譲る**」を置いた
(`session::ready_to_run(st) || st.launch_pending`)。理由:

- `exec_start` は WM top-level からしか呼べない (契約 S2 / カーネルの `appslot_start_admit`)。
- top-level へ戻る道は park だけ。
- したがってこの 1 行が無いと、**アプリが 1 本走っている間は 2 本目を永久に起動できない**
  (1 本目は「他に ready が居ない」ので park せず、`exec_start` は塞いだまま)。

有界性 (D11-3a) は壊れない — 起動要求は有限個の事象で、消費されれば条件は消える。
模型 (`ma_should_park`) には無い分岐なので、模型を追随させるかは PM の判断。

### 実装で塞いだ穴: 起動したてのアプリは表に居ない

`exec_start` は「アプリが最初に park する」まで戻らない (決裁 D9-5) が、**表に載る id は
その戻り値そのもの**なので、起動中のアプリは譲り合いの表に居ない。「未追跡なら譲らない」で
弾くと `exec_start` が永久に戻らず、

- 2 本目 B が park できない ⇒ **1 本目 A は二度と `exec_resume` されない** (画面が止まる)
- top-level へ戻れない ⇒ 3 本目の `LAUNCH` も実行できない

という固まり方になる。`run_program` が `exec_start` を `multiapp::begin_start()` /
`end_start(rc)` で挟み、その間だけ `should_park` が走っている ID を表へ迎える
(`adopt_running`)。1 本目の起動では「他に ready が居ない」ので譲らない = 回帰ゼロは保たれる。
検査は `a_just_launched_app_can_park_on_its_first_op_wait` (回 4 / 回 5)。

限界: 起動中に「起動したアプリの CUI 入れ子の子」が `OP_INIT` を済ませて `OP_WAIT` へ入ると
そちらを迎えてしまう。カーネルが `!a->gui` で park を拒むので実害は
**`ring3_park_reject_count` が 1 増える**ことだけ (受入 G7 でこのカウンタを見るときの注意)。

## 決裁が要る点 (推奨付き)

### A1. CTRL+STOP の宛先は WM 側では付け替えられない (T6 / D4 のずれ)

D4 は「WM がフォーカス窓の owner を見て、走っている本人でなければ `exec_kill(その ID)`」
と書いているが、**カーネルは IRQ1 の時点で走っているアプリの `AppSlot.abort_req` を
無条件に立てる** (`drivers/kbd.c:235` → `ring3_abort_request` → `appslot_abort_request`)。
WM 側にそれを取り消す口は無い。フォーカスが別アプリのときに `exec_kill` を足すと
**2 本死ぬ** (走っている本人 + フォーカスの相手)。

- **実装したこと**: フォーカス窓の owner が走っている本人のときだけ `op_wait` を抜ける
  (`multiapp::abort_targets_current`)。別アプリなら打鍵を飲むだけで何もしない。
  アプリが 1 本のときは常に本人なので**現行と同じ**(回帰ゼロ)。
- **残る穴**: 走っているアプリの `abort_req` は立ったままなので、そのアプリが次に
  syscall を出した時点で畳まれる (= 意図しない 1 本が死ぬ)。
- **推奨**: カーネル側に「要求を降ろす / 宛先を差し替える」口を 1 つ足す
  (`exec_abort_clear()` か、`exec_kill` を走っている本人にも効かせる) — K レーンの小票。
  それまでは実機で「フォーカスが別アプリのときの CTRL+STOP」を受入対象にしない。

### A2. 生成 Rust 束縛が `i32` を `u32` として吐く

`sdk/kapi_rust_gen.py` の `TYPE_MAP` に `"i32"` が無く、未知型は既定の `"u32"` へ落ちる
(`c_type_to_rust` の末尾)。K5b-K が `kapi.json` に `"ret": "i32"` / `"i32 app_id"` で
書いたので、`kapi_generated.rs` は

```rust
pub exec_start: unsafe extern "C" fn(cmdline: *const u8) -> u32,
pub exec_resume: unsafe extern "C" fn(app_id: u32, wait_ret: u32) -> u32,
```

になっている。ABI (EAX の i32) は正しいので、W レーンは呼び出し側で `as i32` /
`as u32` を挟んで凌いだ (生成物は手で触っていない、[ABI1])。

- **推奨**: `TYPE_MAP` に `"i32": "i32"` (と `"i32 *": "*mut i32"`) を足して再生成。
  ABI は変わらないので版数は据え置きでよい。**`sdk/` は W レーンの排他外**なので手を付けていない。

### A3. `SWITCH_CUI` / `SHUTDOWN` に応答しないアプリの打ち切り

4 本のうち 1 本でも `Quit` を無視すると `owner_active` が偽にならず、GUI を畳めない。
v1.2 は「CTRL+STOP で回収」が逃げ道だったが、A1 のとおり宛先が選べない。
`exec_kill` を使った打ち切り (「Quit を配って N 周待っても残っていたら畳む」) は
**新しい方針**なので実装していない。

- **推奨**: v1.3 の受入で困ったら、`multiapp::request_kill` はもう在るので
  「SessionAction が pending のまま `resume_one` を M 周まわしても本数が減らない ⇒ 畳む」
  を足すだけで済む。M の値は決裁事項。

## 未確認 ([V4])

- **ゲスト未検証**。受入 G1〜G7 / G9 / G10 は PM とテスターの領分。ここまでは
  `make check-gshell-host` (26 本 GREEN) と `make ... gshell` が通っただけ。
- `exec_park` はホストでは戻ってくるので、「park の後に WM の状態が宙に浮かないこと」は
  試験できていない (呼び出し点が 1 点であることはソース走査で押さえた)。
- 全体ゲート (`make all` / `make external` / `make check`) は回していない (テスター担当)。

## 決裁 (2026-09-11、ユーザー)

| # | 決裁 | 実装先 |
|---|---|---|
| A1 | **カーネルに `exec_abort_clear` を足し、CTRL+STOP はフォーカス窓のアプリ宛にする** (契約 T6 どおり)。WM は走っている本人の abort を取り消し、フォーカス窓の ID を `exec_kill` | K 小票 (KAPI v45、末尾追記) → W 追随 |
| A2 | `sdk/kapi_rust_gen.py` の `TYPE_MAP` に `"i32"` を足して再生成 (ABI 不変、Rust の型だけ) | K 小票と同じコーダー ([ABI1] の範囲内) |
| A3 | `SWITCH_CUI` / `SHUTDOWN` で Quit に応答しないアプリは **Quit 配送後 N 周 (約 3 秒相当) 待って `exec_kill`** で畳む。確認ダイアログが既に「保存していない内容は失われます」と警告している | W 追随 (`multiapp::request_kill` は既存) |
| D11-3 追加 | 「`LAUNCH` 保留なら譲る」は PM 受入済み (`363da1e`) | 模型への追随は別途小票 |

## 実機受入 (2026-09-11、`f164805` = K + W、15MB 構成、テスター台本 + PM 観測)

| 受入 | obs | 判定 |
|---|---|---|
| ゲート / 配備 | `make clean/all/external/check` exit=0、`os32-cycle deploy` exit=0 (`vmkernel` 453,619 B)、`ver` = API v44 Build 08:39 | 合格 |
| G8 回帰 6 本 | 全通過 (obs 確認) | 合格 |
| **G1** 2 本同時 | `gui_bench` → Run `gui_demo`: 両方の窓が出てタスクバーに 3 窓ボタン (`step38`) | **合格** |
| **G2** 片方を閉じる | `gui_demo` を ESC で閉じても `gui_bench` が残り、クリックで `CLICK n = 2` | **合格** |
| **G7** 切替点 | `ring3_switch_count` 5 → 7 (クリックで resume)、`ring3_resume_bad_frame_count = 0`、`ring3_park_reject_count = 0`、`transition` は起動/終了の回数どおり | **合格** |
| **G4** CTRL+STOP はフォーカス窓だけ (K5c、`8d5ba3b` API v45) | `gui_bench` → Run `gui_demo` で 3 窓 (`step38`)。CTRL+STOP で `gui_demo` (Widgets / Help) だけ畳まれ `gui_bench` が残る (`step42`、タスクバー 1 本)。クリックで `CLICK n = 2` (`step46`)。ESC で CUI 復帰 (`grph_disp=0`)。`appslot_reclaim_count` 2、`ring3_resume_bad_frame_count` 0、`fault_kill_count` 0 | **合格** |
| **G10** 音の排他 (`9ee3ce0` の gui_demo BGM、HostDrv → `hsync`) | `gui_demo` 単独で Enable sound → `/api/sound` `keyreg` `f0..`、`peak` 9165、`fm_playing` 進行 (鳴る)。`gui_bench` 起動でフォーカス移動 → `keyreg` `00..`、`peak` 0、`fm_playing` 783 で停止 (止まる)。タスクバー `Widgets` で戻す → `f0..` / 進行 (復元)。ESC で `gui_demo` 終了 → 停止、`gui_bench` 生存 (`g10b4.png`)。`fault_generation` 0 | **合格** |
| **G5** fault するアプリを混ぜる | `gui_bench` 起動後に Run `/usr/bin/ring3_fault.bin` → 窓は出ず `fault_kill_count` 0 → 1 (`kernel.map` 0x150a74)、`gui_bench` は残りクリックで `CLICK n = 2` (`step39` / `step43`)、`fault_generation` 0 | **合格** |
| **G3** 5 本目 (32MB、K6-RAM `f6ec520` + `ram-32mb`) | `gui_bench` → `gui_demo` → `filer` → `v12_api_test` の 4 本が立ちタスクバー 5 ボタン (`step66`)。5 本目 `t5a_display` は「Too many programs (4 max) - close one first」で拒否、4 本は無事 (`g3b_after_return.png`)、`ring3_switch_count` 9 → 15、`fault_kill_count` 0。Start → CUI mode で 4 本とも畳まれ `appslot_reclaim_count` 4 (A3 の経路)、CUI 復帰 | **合格** (描画不良 W-3 は別記) |
| **G6** 8MB | `ram-8mb` で `gui_demo` を Run → 「Launch failed (… out of memory)」、`ring3_transition_count` 0。CUI から `gui_demo` を実行すると `[shlib] no memory for 4 data pages` / `Error: shlib data attach failed (out of memory)`、`exec_sbrk_tier_last` 1 (3 領域は張れた後に共有ライブラリ .data 4 ページが取れない) | 不合格 → K7 |
| **G6** 8MB 再試験 (K7 `8cc13d8` 配備、vmkernel 454,351 B) | `gui_demo` が立つ (Widgets / Help 完全描画、`exec_sbrk_tier_last` 2、`ring3_transition_count` 1、`used_pages` 256 → 711)。2 本目 `gui_bench` は「Launch failed (… out of memory)」で拒否され `gui_demo` は無事 (`used_pages` 711 のまま、`fault_kill_count` 0)。Start → CUI mode で回収 | **合格** |
| G9 | **ユーザー決裁 2026-09-12: 設計 D11 のホスト模型 (`test_multiapp_model.py` 92 検査 + `test_multiapp_impl.py`) を受入とし、実機 G9 は観測手段 (2 本以上を同時 ready にする端末アプリ等) が揃ってから追う**。K5b の受入はこれで完了 (G3 台本 `scratchpad/g3a.json` / `g3b.json` は K6-RAM の上限撤廃待ち。G6 は 8MB ini [D2]。G9 は 2 本以上が同時 ready になる観測手段が要る) | — |

証跡: `build/out/gui_gate/k5b_g1/*.png`、`tools/emu_agent/logs/playbook-20260911-135944-*`、G4 は `playbook-20260911-182823-*/shots/step{38,42,46}.png`、G5 は `playbook-20260911-224427-*`、G10 は `playbook-20260911-2236*〜2240*` (台本 `g10a` / `g10b1`〜`b4`、音は PM が `/api/sound` を 3 回ずつ採取)。

### 不具合 W-1: park 中のアプリの露出領域が再描画されない (要修正、G1 の完全合格を阻む)

- **観測** (PM、`gui_gate`): `gui_bench` 単独では窓全面 (item 00〜06、明るい背景) が描かれる (`a_bench_only`)。
  `gui_demo` を起動すると Widgets / Help の下に隠れた部分以外の **露出部分が黒**になり、
  3 秒後も 15 秒後も黒のまま (`b_both_3s` / `c_both_15s`)。その間 `ring3_switch_count` は 3 のまま
  (= park 中の `gui_bench` へ Paint が届いて resume される経路が働いていない)。
- 露出部をクリックして前面化すると `switch` は 3 → 7 と増え、`gui_bench` は**上半分 (Widgets に
  隠れていた分) だけ**描き、**下半分は黒のまま** (`d_after_click_bench_bottom`)。= 起動直後に失われた
  Paint が再発行されない。
- 見当 (確定ではない): `gui_demo` 起動時の `visible::recompute_and_expose` が `gui_bench` の露出を
  dirty にした後、park 中の `gui_bench` に対して (a) `derived_ready` (`damage::has_deliverable_paint`) が
  真にならない、(b) 真でも走っている `gui_demo` が `OP_WAIT` に入っていない (ポーリング) ので譲る機会が無い、
  (c) Paint が配送されたが park の間に落ちて再発行されない、のどれか。W レーンで host 試験
  (`wm_tests.rs`) から先に再現する。

#### W-1 の原因と修正 (W レーン、2026-09-11)

**原因**: `userland/gshell/src/lib.rs` の `run_program` が `exec_start` の直後に呼ぶ
`gfx_init()`。`gfx/gfx_core.c` の `gfx_init` は **VRAM の両ページをゼロクリアする**。
塞ぐ `exec_run` の時代はアプリが**終わってから**しか戻らなかったので消えるのは死んだ
アプリの画だけだったが、`exec_start` は park した時点で戻る (決裁 D9-5) ので
**生きているアプリのクライアント面まで消える**。WM はクライアント面を持たない (契約 G4)
ので描き直させるしか無いが、**遮蔽は露出を生まない** (`exposed = new_vis − old_vis` が空)
ため `visible::recompute_and_expose` は dirty を 1 つも足さず、
`damage::has_deliverable_paint` → `multiapp::derived_ready` が偽のまま =
`pick` が A を選ばない = `exec_resume` が呼ばれない (`ring3_switch_count` が動かない)。

観測 3 (クリックで上半分だけ描く) も同じ式で説明がつく — 前面化で足される dirty は
`new_vis − old_vis` = **隠れていた分だけ**で、元から見えていた下半分は dirty にならない。

見当は (a) が当たり。(b)(c) は外れ (`gui_demo` は `OP_WAIT` に入るし、`Paint` は
そもそも dirty が空なので発行されていない)。

**修正** (最小。D11 の規則は 1 行も触っていない — 規則は正しく働いており、
材料である `dirty` が失われていたので直したのは材料の側):

| # | 修正 |
|---|---|
| 1 | `gfx_init()` を呼ぶのは **`rc <= 0` のときだけ**。`rc > 0` は WM に attach 済みの GUI アプリが park しただけで、フルスクリーン GFX から戻す必要が無い (gshell 配下のアプリは `libos32gfx_attach` を使い `gfx_init` を呼ばない。`libos32gfx_attach` 自体は画を消さない) |
| 2 | それでも消すとき (`rc <= 0` = 起動失敗 / park 前に終了) のために `damage::invalidate_all_clients(st)` を新設。生きている全ウィンドウを全面 dirty にするので、park 中のアプリも導出群の ready に入り top-level の `pick` が起こす |

**試験**: `host/wm_tests.rs` に 2 本追加 (計 29 本)。
`a_parked_app_is_not_left_black_when_another_app_is_launched` /
`a_running_app_yields_to_a_parked_app_that_has_a_deliverable_paint`。
併せて `host/mocks.rs` の `gfx_init` を実物に寄せた (「何もしない」→ 画素を消す +
回数を数える) — そこまで模さないと不具合がホストで観測できず検査が空振りする。
RED → GREEN は [`tools/tests/k5b_gshell_tdd.md`](../../../tools/tests/k5b_gshell_tdd.md) の
「追補 — 不具合 W-1」。

**未確認 ([V4])**: **ゲスト未検証** (テスターの再配備待ち)。回したのは
`make gshell` (Rust 警告 0) と `make check-gshell-host` (29 passed) だけで、
全体ゲートは回していない。

### 受入 G3 の前半 (2026-09-11、`f164805`、15MB、テスター台本 66 手)

`gui_bench` → `gui_demo` → `filer` は起動 (3 本同時、タスクバーに 4 窓ボタン)、**4 本目 (`v12_api_test`) は
「Launch failed (not found, not executable, or out of memory)」のモーダル** (`ring3_transition_count = 3`、
`fault_kill_count = 0`、`reclaim = 0`)。原因は未確定だが D5 の見積どおり `EXEC_ERR_NOMEM` が最有力
(`heap_size = 0` の折半で 1 本目が大きく取る)。**15MB でも既定のままでは 3 本**。5 本目拒否 (G3 後半) は
4 本が立たないと検査できない → GUI テストアプリの `heap_size` を `build/app.conf` で明示するかはユーザー決裁。

### W-1 の実機確認 (2026-09-11 17:43、`3f29460` を配備、15MB、PM 観測 `gui_gate`)

`gui_bench` → `gui_demo` 起動 +3 秒: gui_bench の露出部 (item 03〜06) が**描かれたまま** (`b_both_3s`)。
露出部をクリックして前面化: 全面 (item 00〜06) が描かれる (`c_after_click_bench`)。`switch` 3 → 7。
→ **W-1 解消、G1 完全合格**。証跡: `build/out/gui_gate/k5b_w1/*.png`。

### 不具合 W-3: 3 本目以降で前面窓のクライアント面に後ろの窓の枠線が残る (G3 で観測、要修正)

- **観測** (PM、32MB、4 本起動直後): File Manager (3 本目) のリスト面とステータス行に、後ろの `gui_bench` の右辺
  (x≈443) が縦線、`Help` の下辺 (y=245) が x≈490〜583 で横線として黒 1px で描き込まれる (`scratchpad/g3_4apps_later.png`、
  拡大 `g3_crop.png`)。前面の v12 api test は正常。
- File Manager のタイトルをクリックして前面化すると消える (`g3_filer_front.png`) → 描画順 / クリップの問題で内容は無事。
- 2〜3 窓 (G1 / G4) では出ていない。G3 の合否 (5 本目拒否・4 本生存) には影響しないが、契約 (WM はクライアント面を持たない、
  枠は可視領域にクリップ) に反するので W レーンの小票で直す (コーダー発注 2026-09-11)。
- **原因** (コーダー): `wm::composite_rect()` のクローム描画が各窓の外形まるごとを描き、前面窓のクライアント面
  (WM は消せない) に背面窓の枠 1px が落ちていた。**修正**: 各窓の可視外形 (外形 − 前面窓の外形) の断片ごとに
  `chrome::draw_window_chrome(.., clip)` で描き、全プリミティブとタイトル文字 (セル単位) をクリップ。
  ホスト試験 `host/wm_composite_tests.rs` 3 本 (RED 2 → GREEN、`check-gshell-host` 37 passed)。**実機未確認**
  → **実機確認済み (2026-09-12、32MB、`36ccf15` + K7 `8cc13d8` 配備)**: 同じ 4 本起動で File Manager に線は出ない (`scratchpad/w3_4apps.png`)。同族の未修正: ドラッグ枠 `draw_drag_outline` はクリップ無し
  (他窓のクライアント面を横切ると 1px の軌跡が残りうる、再現手順外)。可視外形が 16 矩形を超えると枠が欠ける側に倒す。
