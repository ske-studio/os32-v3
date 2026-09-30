# ring3_guard — WM の文脈では KAPI の出力検査を効かせない (RED → GREEN の記録)

票: [`docs/archive/kernel_v21/TASK_KAPI_OUTPUT_GUARD.md`](../../docs/archive/kernel_v21/TASK_KAPI_OUTPUT_GUARD.md) §6 (追補 2026-09-26)
対象: `exec/ring3_str.c` (`ring3_guard_active`) / `kernel/gui.c` (`gui_call` / `gui_owner_exit` の印) /
`exec/exec.c` (`ring3_wm_depth`、3 つの門、ポンプ、longjmp 地点の立ち直し)
試験: `tools/tests/test_ring3_guard.py` (ホスト、実物を `#include`) + `kernel/kselftest.c` の `test_ring3_wm_guard` (ゲスト、実物の門)
実行: `make check-ring3-guard-host` (`check-par` の列。`--target` + `--mutate` 付き)
教訓: [`docs/POLICY_DEBUG.md`](../../docs/POLICY_DEBUG.md) §4-61

## 症状 (PM、NP21/W、2026-09-26)

GUI で File Manager (`/usr/bin/filer.bin`) を起動すると窓が出ずに静かに消える。`fault_kill_count` が
起動ごとに +1。ブレークで捕まえると `ring3_fault_kill` の呼び手は `wrap_mouse_poll+0x35` —
出力検査 `ring3_user_ranges_writable((u32)info, sizeof(MouseState), 0, 0)` が偽。そのとき
CR3 = アプリの PD、EBX = 0x0037ea88 (**シェル帯 = gshell のスタック**)。`ring3_range_reject_count` は 0 のまま。

## 原因

WM (gshell、CPL=0 の常駐シェル) は**アプリの syscall の中でしか走らない** (契約 T8 の X1 / X3 / X4)。
`wrap_gui_call` → `gui_call` → `gshell_gui_handler(OP_WAIT)` → `wm_cycle` → `capture_mouse` →
`mouse_poll(&mut mi)` の間、`ring3_in_syscall` は 1 のまま。書き側の門 `ring3_user_ranges_writable`
(2026-09-23 の 7ef4437 / 7ec4023 で入った) は `ring3_in_syscall` だけで「アプリ由来」と決めていたので、
gshell のスタックのポインタがアプリの PD で PTE を見られ、シェル帯には USER が無いので拒否 → アプリを kill。
書き側は断った理由を数えていなかったので `ring3_range_reject_count` は 0 のままだった。

(K2 の syscall 境界ポンプ X4 は `ring3_in_syscall = 1` の**前**に回るので、この経路は X1 / X3 = `gui_call` のハンドラ。)

## 直し

- 「カーネルが WM のコードへ入っている深さ」`ring3_wm_depth` を `gui_call` のハンドラ / ポンプ / `gui_owner_exit` の
  前後で数え、3 つの門 (`ring3_user_ranges_writable` / `ring3_user_range_ok` / `tramp_copy`) の判定を
  `ring3_guard_active(ring3_in_syscall, ring3_wm_depth)` に寄せる (深さ 1 以上 = 常駐側の直呼び扱い)。
- `ring3_in_syscall` の意味 (#PF/#GP の帰属、`kernel/isr_handlers.c`) は変えない。
- WM を longjmp で抜ける地点 (`exec_park*` ×4 / `ring3_kill_kind` / `kapi_sys_exit`) と**ディスパッチャの入口**で深さを 0 に戻す
  (不変条件: CPL=3 の syscall は必ず深さ 0 から始まる)。
- 書き側の拒否も `ring3_range_refuse` で数える (`RING3_RANGE_WR_TRIVIAL` / `WR_TABLE` / `WR_PDE` / `WR_PTE` = 7〜10)。

## RED → GREEN

| # | 見るもの | RED (直す前) | GREEN (直した後) |
|---|---|---|---|
| 1 | `ring3_guard_active(1, 1)` (ディスパッチ中 + WM の文脈) | 1 (ガード = gshell のポインタを拒否) | 0 (常駐側の直呼び扱い) |
| 2 | `ring3_guard_active(1, 0)` (ディスパッチ中 + アプリ由来) | 1 | 1 (**変えない** — アプリの不正なポインタは今までどおり kill) |
| 3 | `ring3_guard_active(0, *)` (CPL=0 の直呼び) | 0 | 0 (変えない) |
| 4 | `gui_call` のハンドラの中の深さ | 0 (印が無い) | 1。入れ子の `gui_call` で 2、戻って 1、抜けて 0 |
| 5 | `gui_owner_exit` のハンドラの中の深さ | 0 | 1、戻って 0。WM 未登録なら印も立てない |
| 6 | 負の深さ (壊れた状態) | — | ガードを効かせる (安全側) |
| 7 | ゲスト (kselftest): ディスパッチ中を装ってカーネル帯のローカル変数を門に渡す | 深さ 0: 拒否 / 深さ 1: 拒否 | 深さ 0: 拒否 + `ring3_range_reject_last` = `WR_PDE` か `WR_PTE` / 深さ 1: 素通し / leave で再び拒否 |
| 8 | (代行レビュー P2) アプリ (ID 2) が登録した fd 1 のバッファが RO になり、WM のハンドラ (深さ 1) が fd 1 へ書く | 素通し (カーネルがアプリ指定の番地へ書く) | 表を歩いて拒否 → kill。書けるページなら書ける。WM が張ったバッファは深さ 1 で素通し |
| 9 | (同) アプリが RO / 帯外のページを登録 | 登録できる (ラッパの門だけ) | `fd_redirect_to_buffer` も歩いて -1、表は変えない |
| 10 | (同、ゲスト kselftest `test_fd_redirect_origin_guard`) 由来=アプリの項目 + カーネル帯のバッファ + 深さ 1 | — | `fd_redirect_buf_write_ok` = 0 (数えられる)、由来=WM なら 1、深さ 0 の登録は -1 |

実行結果 (2026-09-26、ホスト):

```
1 ring3_guard_active の表        6/6 ok
2 gui_call の前後で深さが対になる   16/16 ok
3 gui_owner_exit の前後で深さが対になる 7/7 ok
4 アプリが登録したバッファは WM の中でも表を歩く 18/18 ok
HOST ILP32 GNU89 EXIT ring3_guard_host=0
TARGET i386-elf GNU89 -Werror COMPILE PASS
```

否定側 (`--mutate`、写しの上で実物の 1 行を壊す):

```
MUTATION 1 RED: WM の文脈を見ない (= 直す前の判定)
MUTATION 2 RED: gui_call が印を立てない
MUTATION 3 RED: gui_call が印を下ろさない
MUTATION 4 RED: owner_exit が印を立てない
MUTATION 5 RED: アプリが登録したバッファの書きを文脈つきの門に戻す (= 直す前)
MUTATION 6 RED: 登録の由来を記録しない
MUTATION 7 RED: 登録時の二重の守りを外す
MUTATIONS 7/7 RED
```

### 追記 (2026-09-26、代行レビュー P2) — ime_set_render は常駐側だけ

KAPI `ime_set_render` の target を `kernel/gui.c` の `gui_ime_set_render` に替え、owner 1 かつ
`ring3_call_from_user()` が偽のときだけ `ime_set_render` へ渡す。ホストの §5 (5a〜5k) が
「アプリ由来の登録 (表 / NULL) は断って描画先を変えない」「owner 1 でも CPL=3 由来は断る」
「owner 2 の WM 文脈も断る」「常駐側の登録と NULL は通る」「アプリの回収では触らず、gshell (owner 1)
の終了で NULL に戻る」を見る。`sdk/kapi.json` の target は試験の入口で確かめる。

```
MUTATION 8 RED: ime_set_render の門を外す (= 直す前)
MUTATION 9 RED: ime_set_render の門が由来 (ring3_call_from_user) を見ない
MUTATION 10 RED: ime_set_render の門が owner を見ない
MUTATION 11 RED: gshell の終了で FEP の描画先を戻さない
MUTATIONS 11/11 RED
```

### 追記 (2026-09-26、代行レビュー P3) — gui_register も同じ門、WM の中で落ちた観測点

`gui_register` の門を `gui_ime_set_render` と同じ「owner 1 かつ `ring3_call_from_user()` が偽」に揃えた。
ホストの §6 (6a〜6g) が「gshell の top-level (owner 1、ディスパッチの外) の登録は通って gui_call が届く」
「owner 1 でも CPL=3 由来は断り、登録済みのハンドラは変えない」「owner 2 はディスパッチ中でも WM 文脈でも断る」
「NULL は断る」を見る。§4 の 4d は登録を top-level から行うように直した (それまでディスパッチ中の owner 1 で
登録していた — 新しい門では断られる筋書き)。

WM の中で落ちるとアプリの kill として畳まれる (§4-61 の「例外 0 件・`fault_kill_count` だけ増える」)。
`exec/exec.c` の `ring3_kill_kind` が深さを 0 に戻す**前**に `ring3_wm_fault_count` を +1 (FAULT のみ)、
`kernel/isr_handlers.c` の 2 つの kill の行に深さ 1 以上で ` (in WM)` を足す。exec.c / ISR はホストで組めないので
字面で見る (`static_checks`、変異は写しの上で同じ関数に通す)。

```
MUTATION 12 RED: gui_register の門が由来 (ring3_call_from_user) を見ない (= 直す前)
MUTATION 13 RED: gui_register の門が top-level の正当な登録を断る (過剰)
MUTATION 14 RED: ring3_wm_fault_count を深さを 0 に戻した後で数える (常に 0)
MUTATION 15 RED: ring3_wm_fault_count を数えない
MUTATION 16 RED: 例外の kill の行に (in WM) を付けない
MUTATION 17 RED: #PF の kill の行に (in WM) を付けない
MUTATIONS 17/17 RED
```

## この試験が見ていないもの

- **実際に filer の窓が出ること** — NP21/W で PM が確かめる (`fault_kill_count` が増えない、`ring3_range_reject_count` が 0 のまま)。
- `ring3_wm_depth` の longjmp 地点の立ち直し (`exec_park*` / `ring3_kill_kind`) — exec/exec.c はホストで組めない。
  ディスパッチャの入口の `ring3_wm_depth = 0` が構造的に守る (深さが残っても次の syscall で 0)。
- gshell の他の出力付き KAPI (`gfx_screen_info` / `gfx_stats` / `launch_take`) — 同じ門を通るので同じ直しで通る。
- 登録時の歩きを `_always` から文脈つきの門へ戻す変異は**同値** (登録時に由来がアプリ = 深さ 0 + ディスパッチ中で、
  そこでは 2 つの門が同じ答えを返す) なので変異に入れていない。
- `ring3_wm_fault_count` / ` (in WM)` が**実際に**増える / 出ること — ゲストで WM の中に #PF を起こす筋書きは無い
  (字面の検査だけ)。
