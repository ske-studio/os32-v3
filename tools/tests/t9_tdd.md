# T9 ホスト TDD の記録 (K = 起動要求表 / W = WM 側の仲介)

票: [docs/archive/gui_v13/TASK_T9_sh.md](../../docs/archive/gui_v13/TASK_T9_sh.md) §1 D3 / D5 / D8、
§1a の ABI 表、§10 の non-blocker 1 / 2。
実行: `make check-launch-host` (= `python3 -B tools/tests/test_launch.py`) と
`make check-multiapp-model-host` (その中の `test_multiapp_impl.py`)。

実機・エミュレータ・`make` は**未実施** ([V4])。ここに書いてあるのは
ホストの gcc / i386-elf-gcc だけで踏めた範囲。

## 1. 何を実物で回すか

| ハーネス | 取り込む実物 | 見るもの |
|---|---|---|
| `tools/tests/launch_host.c` | `exec/launch.c` + `exec/appslot.c` | 要求表の遷移・権限・token の照合・連鎖の解決 |
| `tools/tests/multiapp_impl_host.c` (ケース 23 を追加) | 同上 + `kernel/kbd_inject.c` | `sys_yield` の park / resume (EAX 0・注入リング不変) と `exec_kill` の連鎖 |

カーネル帯の代わりにハーネスが持つのは 3 つだけ: 所有者 (`res_owner_get/set`)、
GUI 判定 (`con_sink_is_enabled`)、`kstrncpy`。スロットは実物の `AppSlot` を組み立てる
(`exec/exec.c` の起動経路は CR3 / pgalloc を引くのでホストへは持ち込めない)。

## 2. RED → GREEN

`exec/launch.c` を書いたあとに `launch_host.c` を書いたため、初回実行は最初から
全項目 GREEN だった。**「試験が仕様を捕まえているか」を別に確かめる**ため、
実装へ 3 つの欠陥を 1 つずつ埋め込んで RED を確認し、戻して GREEN に戻した
(いずれも票の non-blocker / blocker が実際に指摘した壊れ方)。

| 埋めた欠陥 | 落ちた検査 | 対応する票の指摘 |
|---|---|---|
| `row_finish` の孤児分岐を消す (孤児の完了を `IDLE` に落とさない) | `5k` `8c` | §10 non-blocker 1 (`ERR_FULL` の固着) |
| 回収通知で `child = 0` にしない | `2s` `4k` `7j` `7k` `8a` | §10 non-blocker 1 (再利用 ID への誤連鎖) |
| `launch_poll` が完了した表を解放しない | `2u` `2v` `3d` `3e` `4o` | §1a (完了は 1 度だけ渡す) |

いずれも `EXIT launch_host=1`。3 つとも戻した後の最終状態は `ALL PASS`。

## 3. 検査の並び (`launch_host.c`)

| 番号 | 見るもの |
|---|---|
| 1a〜1m | `launch_req` の門: GUI 外 / WM top-level / 宣言 `LAUNCHER` なし / 入れ子 `exec_run` の子 / NULL / 空 / 255B 超 → `OS32_ERR_INVAL`。通った後、同じ要求者の 2 本目は `OS32_ERR_FULL`、別の要求者は自分の表に積める |
| 2a〜2v | `take` (owner 1 専用・`cap < 256` は断る・昇順) → `report(rc>0)` (範囲外 / 死んだ ID は `INVAL` で `TAKEN` のまま) → `RUNNING` (`status = 0x100 + child`) → 子の回収通知で `DONE` + `child = 0` → `poll` が 1 度だけ渡して表は IDLE |
| 3a〜3f | `rc == 0` は即 `DONE` (park より前に終わった短命な子を取り逃がさない — §5 blocker 1)、`rc < 0` は `FAILED` (`status = 0x300 + (-rc)`) |
| 4a〜4o | `cancel`: `PENDING` / `TAKEN` は `AGAIN`、`RUNNING` は `KILL(child)` の `PENDING` へ (child は保持)、`take` で `kind = KILL` / `arg = 子`、回収通知で `DONE`、その後の `report` は `STALE` (§10 non-blocker 2)、`DONE` への `cancel` は `STALE` |
| 5a〜5n | 要求者の退場 = 孤児回収。`requester` が -1 になり、再利用 ID からの `launch_req` は回収が終わるまで `FULL`、完了は poll を待たず `IDLE`。子を持たない要求者の退場は即 `IDLE` |
| 6a〜6j | token: 要求者をまたいで単調増加、他人の token は `INVAL`、知らない / 0 / 負は `STALE`、ID を再利用しても古い token は当たらない |
| 7a〜7l | `launch_child` / `launch_chain`: 端末 → sh → 子 の 3 段、途中から、不正 ID、`max 0`、末尾を畳んだ後の縮み、壊れた表の環でも止まる |
| 8a〜8d | `launch_selftest()` (ブート時 kselftest が踏むのと同じ 3 項) が 0 を返し、表を空にして戻る |

## 4. 検査の並び (`multiapp_impl_host.c` ケース 23)

| 番号 | 見るもの |
|---|---|
| 23c〜23j | `sys_yield` の park: tick の間引き**なし**で成立、`ring3_yield_count` が増え `ring3_poll_yield_count` は動かない、状態は `WAIT_POLL` のまま、印は `parked_from_yield` だけ、`exec_app_state` は 4 のまま、WM top-level へ戻る |
| 23k〜23p | **譲っている間に届いた打鍵を吸わない**: 注入リングに 2 バイトある状態で resume → `EAX = 0`、`kbd_inject_pending()` は 2 のまま (§6 blocker 1 = `parked_from_yield` を足した理由) |
| 23q〜23D | 端末 → sh → 子 を要求表で組む (`req` → `take` → `report`)。`launch_child` で連鎖が読める |
| 23E〜23I | CTRL+STOP の形: WM が末尾を解決して `exec_kill(末尾)` → **1 本だけ**畳まれ、親の表が `child = 0` になる |
| 23J〜23O | `exec_kill(端末)` は子孫ごと、**末尾から**畳む (`ma_kill_order` が `[sh, 端末]`)。畳み終えた後に表が 1 本も残らない |
| 23P〜23U | 譲れない文脈: WM top-level と CUI の入れ子 `exec_run` の子。どちらも `OS32_ERR_INVAL` で `ring3_park_reject_count` に載り、入れ子の子からの `launch_req` も断られる |

`ma_resume_poll` と `ma_reclaim_res` と `ma_kill_chain` は、`exec/exec.c` の
`exec_resume` / `exec_reclaim_owned` / `exec_kill` の該当部分を**そのまま写した**形
(exec.c はカーネル一式を引くのでホストへ `#include` できない)。印から EAX の出所を導く
対応表そのものは `appslot_resume_source()` として実物に置いたので、写しているのは
「どこから値を取るか」ではなく「取った値をフレームへ書く」手順だけ。

## 4b. Codex 実装レビュー 往復 1/3 の修正 (2026-09-13)

blocker 3 件はいずれも `exec/launch.c` の §1a からの逸脱。直したあと、**3 点を元に戻すと
RED になる**ことを確認してから GREEN に戻した (下の「落ちた検査」は実際の出力)。

| 直したところ | 落ちた検査 (戻したとき) |
|---|---|
| `launch_take` が `buf == NULL` を `INVAL` にしていた | `2b2` `2b3` |
| `launch_cancel` の要求者不一致が `INVAL` だった | `4c` `5h2` |
| `launch_report(KILL)` が `TAKEN` のまま来たとき `child = 0` + `DONE` にしていた | `9e` `9f` `9h` `9i` `9j` `9l` `9o` `9p` |

3 つ目のために**ケース 9** を足した: `RUNNING` → `cancel` → `take` → `report(0)` (回収通知より
先) → poll は `RUNNING(child)` のまま → `owner_exit(child)` → poll が `DONE`。さらに
「report が先に来た後で要求者が退場しても、子の所有が残っているので孤児回収に載る」
(`9o` `9p`) — `child` を落としていた版ではこの子が誰にも回収されなくなる。

## 4c. Codex 網羅レビュー 往復 7 の R1 (2026-09-13)

`sys_getcwd` が `fs/vfs.c` の static `cwd` (カーネル帯) をそのまま返していた。
カーネル帯の PTE に USER ビットは無いので、CPL=3 の `cd` / `pwd` が戻り値を
読むと #PF → fault kill。写し先は KAPI トランポリンページ (RO+USER) の
「表 + スタブ」の後ろ (`exec/ring3_str.h` の `RING3_USTR_OFF`)。

| ハーネス | 取り込む実物 | 見るもの |
|---|---|---|
| `tools/tests/ring3_str_host.c` | `exec/ring3_str.c` | 経路の分岐・上書き・上限長・写し場が無いとき |

ホスト試験は 4 ケース 20 検査:

| 番号 | 見るもの |
|---|---|
| 1a〜1g | CPL=0 は static `cwd` をそのまま、CPL=3 は写し。写し先はページ内でスタブの後ろ。中身は一致し、元の `cwd` は書き換えない |
| 2a〜2d | 写しは 1 本を毎回上書き (前の中身が尾に残らない) |
| 3a〜3e | 上限長 (`RING3_USTR_CAP - 1` 文字) でも切れず NUL 終端が入り、1 バイト先を汚さない |
| 4a〜4d | `scratch = 0` / `cap = 0` (= `exec_init` より前) と `src = NULL` は素通し |

番地の式 (`RING3_USTR_OFF + RING3_USTR_CAP <= 4096`、スタブの後ろ) は
ホスト側とカーネル側の両方で `STATIC_ASSERT` に固定した — KAPI が増えて
1 ページに収まらなくなったら**ビルドが落ちる** (実機では「`cd` の直後に
`pwd` が化ける」としか見えない)。

ページ属性 (PTE の USER ビット) はホストでは踏めないので、カーネル側の
`exec_tramp_user_selftest()` (3 項) を `kselftest_run_post_exec()` から呼ぶ。
`kselftest_run()` は `exec_init()` より**前**に走りトランポリンページがまだ
無いため、この項だけ `kernel/kernel.c` が `exec_init()` の直後に別口で回す。
**この 3 項は実機でしか踏めない (未実施)。**

## 4d. Codex 網羅レビュー 往復 8 の T1 (2026-09-13)

標準 FD のリダイレクト表 (`fs/fd_redirect.c`) が全アプリ共有で、park/resume でも
切り替わらなかった。表を **ID ごとの枠** (`exec/appslot.c` の `g_redir[]`) にして、
park で走っていた ID の枠へ移し resume で戻す。

`tools/tests/multiapp_impl_host.c` は **実物の `fs/fd_redirect.c` を取り込む**ように
した (`res_owner_set/get` の実体もそちらへ移り、所有者タグが本物になる)。VFS は
この票の対象外なので `vfs_open` / `vfs_close` / `vfs_seek` / `vfs_read_fd` /
`vfs_write_fd` だけ最小の偽物を置き、「どの表に書き込みが入ったか」「何回閉じたか」を
数える。ケース 24 は 36 検査:

| 番号 | 見るもの |
|---|---|
| 24a〜24i | sh が stdout をファイルへ → park → **いまの表はコンソールへ戻る** → WM が別アプリを起動 → その stdout はコンソールで、sh の `/tmp/out` は増えない (**反例 1**) |
| 24j〜24m | resume で sh の表が戻り、続きが同じファイルへ入る |
| 24n〜24x | パイプバッファ版: park でバッファも枠へ移り、別アプリの `sys_write(1)` は sh の .bss を書かない (**反例 2**)。sh の `buf_len` は resume 後も不変 |
| 24y〜24D2 | park したまま `exec_kill` → `vfs_close_owned(id)` が枠の背後の `file_fd` を閉じ、WM の表は触られない。`24B2` で **`vfs_close` の呼び出しが 1 回だけ**、`24D2` で枠が空に戻る |
| 24E〜24J | 走ったまま終了 → いまの表から外すときに 1 回だけ閉じる (`24J2` も呼び出し回数 1) |

RED は「修正を戻すと落ちる」形で確認した (実際の出力):

| 戻したところ | 落ちた検査 |
|---|---|
| park / resume の持ち替え (`redir_switch_out` / `_in`) を効かなくする | `24f` `24h` `24i` `24m` `24r` `24t` `24u` `24x` |
| `appslot_reclaim` の枠の後始末 (`fd_redirect_clear_state`) を外す | `24D2` |

`exec/appslot.c` が `fd_redirect.h` を引くようになったので、`appslot.c` を取り込む
他のハーネス (`launch_host.c` / `sbrk_tier_host.c`) には空の錠 (4 本) を置き、
それぞれの runner の include に `fs` を足した。

## 4e. Codex 網羅レビュー 往復 9 の non-blocker (2026-09-13)

4d の初版は `appslot_reclaim` が `fd_redirect_close_state()` で枠の `file_fd` を
閉じていたが、その `file_fd` は `fd_redirect_to_file` の `vfs_open` が **その ID の
owner タグ**で取ったものなので、`exec_reclaim_owned` の (2) `vfs_close_owned(id)`
(`exec/exec.c:746`) が先に同じ FD を閉じている。つまり park 中 kill では同じ FD に
`vfs_close` が 2 回掛かっていた (いまの `vfs_close` と回収順では間に FD 再利用が
入る反例は無いが、「二重 close しない」という説明が実態と違った)。

→ 枠は **閉じずに空にするだけ** (`fd_redirect_clear_state`) にし、閉じるのは
`vfs_close_owned` の 1 か所へ寄せた。`fd_redirect_close_state` は削除。

ホスト側がこれを見られなかったのは、偽 VFS に owner タグが無く、
`ma_reclaim_res` が実物の `vfs_close_owned` に当たるものを呼んでいなかったため。
偽 VFS に `host_fd_owner[]` と **FD ごとの `vfs_close` 呼び出し回数**
(`host_fd_close_calls[]`) を足し、`host_vfs_close_owned()` を `ma_reclaim_res` の
(2) に置いた。「閉じた回数」だけでは 2 回目が `in_use` 落ちで数えられないので、
**呼び出し回数**で見るのが要点。

| 戻したところ | 落ちた検査 |
|---|---|
| `appslot_reclaim` を `fd_redirect_close_state()` に戻す | `24B2` |

`24A0` (file_fd に ID の owner タグが付く)、`24J2` (走ったまま終了でも呼び出し
1 回)、`24D2` (畳んだ ID の枠が空に戻る = 再利用 ID へ古い表を渡さない) も
同じケースに足した。枠の後始末は close 回数では見えないので、`g_redir[]` を
直に見る検査にしてある (ハーネスは `appslot.c` をそのまま取り込んでいる)。

## 4f. 受入 S6 の差し戻し (CTRL+STOP の連鎖、2026-09-13)

実機 (feat/gui `dc2f78d` の gshell、カーネル `ecaba37`) で、端末 (ID 2) → sh (ID 3) →
kbd_echo (ID 4) の連鎖に CTRL+STOP を 2 回打つと **sh と端末が両方消えた**
(`ring3_abort_count` 0 → 1、`fault_kill_count` 0 → 2)。原因は IRQ1 の
`ring3_abort_request()` が「そのとき走っていた slot」に `abort_req` を立てる K2 の経路で、
T9 以後は sh が `WAIT_POLL` で毎 tick、端末が 100ms タイマで回るため、IRQ1 の落ち先が
D8 の宛先 (連鎖の末尾) と無関係になる。

判定を純関数 `appslot_abort_admit(gui_mode, now_tick)` に切り出したので、ホストで
そのまま踏める。ケース 25 は 25 検査:

| 番号 | 見るもの |
|---|---|
| 25d〜25g | GUI 中の IRQ1 は要求を立てない。`abort_req` は 0 のままで、syscall 出口でも畳まれず、端末も sh も生き残る (**実機の反例そのもの**) |
| 25h〜25m | 暴走の逃げ道: `APP_RUNAWAY_TICKS` の 1 つ手前では立てず、到達したら立てて畳む。畳まれるのはその 1 本だけ |
| 25n〜25p | `appslot_mark_scheduled` で起点が進むので、譲っている限り暴走にならない |
| 25q〜25r | gfx 拒否 (T8 D1a) と V86 の脱出は `appslot_abort_request()` の直呼びで、関門を通らず GUI 中も立つ |
| 25s〜25u | WM (シェル帯) が走っているときは GUI / CUI どちらでも誰にも載せない |
| 25v〜25y | **CUI は 1 バイトも変えない** — tick を問わず立ち、次の安全地点で畳まれる (K2 の逃げ道) |

RED は 2 方向から確認した (実際の出力):

| 戻したところ | 落ちた検査 |
|---|---|
| GUI 判定を外す (修正前 = 常に立てる) | `25d` `25e` `25f` `25g` `25i` |
| 暴走の逃げ道を落とす (GUI 中は常に立てない) | `25i` `25j` `25l` `25m` `25n` `25r` |

ブート時は `appslot_abort_admit_selftest()` (4 検査) を `kselftest_run()` から踏む。
`tick` が一周する境界 (`0xFFFFFF00` + 200) も自己診断の側で見ている — u32 の引き算なので
差で判定すれば正しく出る。

## 4g. 暴走判定の起点を syscall 入口へ (S6 合格後の補正、2026-09-13)

4f の初版は起点 (`last_resume_tick`) を start / resume でしか更新しなかった。
GetMessage 型の GUI アプリ (端末) は WM がその `op_wait` の**中**で回っている間
resume を通らないので、**2 秒イベントを待っただけで「暴走」に見える**。その状態で
IRQ1 が端末の CPL=3 実行中に落ちると、スタブの即 kill が D8 の宛先より先に端末を
畳む (`sh> ` で待っているだけの状態から CTRL+STOP 1 回で到達しうる)。

→ 欄を `last_kernel_tick` に改め、`ring3_syscall_dispatch` の入口でも更新する
(代入 1 つだけ。hot path なので関数呼び出しは足さない)。

ケース 25 に (f) を足した (6 検査):

| 番号 | 見るもの |
|---|---|
| 25z〜25B | 300 tick (3 秒) 待っても、その間 10 tick ごとに KAPI を呼んでいれば暴走ではない。端末に `abort_req` は立たない (**D8 の宛先より先に畳まれない**) |
| 25C〜25F | 最後の KAPI から境界の 1 つ手前では立てず、`APP_RUNAWAY_TICKS` に達したら立てて畳む (計算ループの唯一の逃げ道は健在) |

RED (実際の出力):

| 戻したところ | 落ちた検査 |
|---|---|
| syscall 入口での控えを外す (start / resume だけに戻す) | `25A` `25B` `25C` |

kselftest の 4 項は欄の名前が変わっただけで通る (自己診断は欄を直に置いてから
`appslot_abort_admit` を叩く形なので、起点の更新経路には依存しない)。

## 4h. K5c の経路を戻す (S6 再試験の 3 回目、2026-09-13)

補正版 (`50cb48a`) の再試験で 1 回目 (kbd_echo) と 2 回目 (sh) は合格したが、
**3 回目 (連鎖の末尾 = 端末自身) が畳まれなかった** (`ring3_abort_count` 0)。
前回版で 3 回目が動いていたのは暴走の誤判定で端末に `abort_req` が立ち、K5c の
経路が偶然通っていたため。

「GUI 中は IRQ1 由来を立てない」が広すぎた。`in_op_wait` のとき (WM がそのアプリの
`gui_call(OP_WAIT)` の中で回っている) は、割り込まれた文脈が CPL=0 (WM のコード)
なのでスタブの即 kill は起きず、要求は必ず WM のハンドラが見る。カーネルが宛先を
決めてしまう害が無いので、ここは従来どおり通す。

ケース 25 に (g) を足した (12 検査):

| 番号 | 見るもの |
|---|---|
| 25G〜25J | OP_WAIT の**外** (syscall から戻って CPL=3 で走っている) への IRQ1 は立てない — D8 の宛先を待つ |
| 25K〜25N | OP_WAIT の**中**への IRQ1 は立てる (K5c)。本人宛ならそのまま syscall 出口で畳まれる (**連鎖の末尾 = 自分自身**) |
| 25O〜25R | 別宛なら WM が `exec_abort_clear` で降ろせて、本人は畳まれない (K5c の分岐) |

RED (実際の出力):

| 戻したところ | 落ちた検査 |
|---|---|
| `if (a->in_op_wait) return 1;` を外す | `25L` `25M` `25N` `25O` |

kselftest は 5 項目になった (`GUI keeps the K5c path (inside gui_call OP_WAIT)`)。
自己診断も `in_op_wait` を立てた状態 / 落とした状態の両方を踏む。

## 5. 最終実行 (2026-09-12)

```
$ python3 -B tools/tests/test_launch.py
HOST ILP32 GNU89 COMPILE PASS
launch request table (T9 K)
...
  ok   8d 自己診断は表を空にして戻る
ALL PASS
TARGET i386-elf GNU89 -Werror COMPILE PASS
EXIT launch_host=0

$ python3 -B tools/tests/test_multiapp_impl.py
HOST ILP32 GNU89 COMPILE PASS
...
  ok   23U その拒否も弾き数に載る
ALL PASS
TARGET i386-elf GNU89 -Werror COMPILE PASS
```

`tools/check_kapi_version.py` と `tools/check_constraints.py` も通した。
`make` (clean build / `make check` 全体 / `make external`) と実機は未実施。

# A. 端末 (t5a_display) の要求表切り替え — ホスト TDD の記録

票: [TASK_T9_sh.md](../../docs/archive/gui_v13/TASK_T9_sh.md) §1 D4 / D9、§1a の ABI 表。
実行: `make check-t5a-host` (= `cargo test --manifest-path
userland/rust/t5a_display/host_tests/Cargo.toml --target x86_64-unknown-linux-gnu --offline`)。
実機・エミュレータ・`make` は**未実施** ([V4])。

## A-1. 何を実物で回すか

`host_tests/src/lib.rs` が `#[path]` で端末の純粋モジュールをそのまま取り込む。
今回足したのは `src/launch.rs` (要求表の読み方と取消の進み具合) で、KAPI を呼ぶ
`guest.rs` は**ホストでは動かない** — `launch_req` / `launch_poll` / `launch_cancel` の
呼び出しと戻り値の配線は実機でしか確かめられない。ここで固定したのは
「返ってきた値をどう読むか」と「次に何をするか」。

試験数は 48 → **59** (`launch.rs` 8 本、`prompt.rs` に接続モードの最下行と
`message_rc` の 2 本、既存の遷移表 1 本を D4 / D9 の形へ書き換え)。

## A-2. RED → GREEN

`launch.rs` を書いた後に試験を書いたため初回から GREEN だった。**試験が仕様を
捕まえているか**を別に確かめるため、票が実際に指摘した壊れ方を 1 つずつ埋めて
RED を確認し、戻して GREEN に戻した (K の §2 と同じやり方)。

| 埋めた欠陥 | 落ちた検査 | 対応する票の指摘 |
|---|---|---|
| `phase()` が `FAILED` を `Done` に倒す | `status_decodes_into_the_abi_phases` `a_failed_launch_carries_the_negative_rc_back` | §1a (`0x300 + (-rc)`)、D4 の `launch failed (rc)` |
| `poll` が負の `rc` と未知の status を `Idle` にする | `a_negative_poll_returns_to_the_prompt_instead_of_hanging` | D4 (異常系でも固まらない) |
| `cancelled(AGAIN)` を `Armed` にする (再試行しない) | `again_retries_the_cancel_on_the_next_timer` | D9 (`OS32_ERR_AGAIN` は次のタイマで自動再試行) |
| `RUNNING` の周に取消の再試行を出さない | `again_retries_the_cancel_on_the_next_timer` | D9 (`RUNNING` になって初めて `KILL(child)` が通る) |
| `step(Attached, Escape)` を `Next::Prompt` に戻す (第 4 版の「ESC で子を残す」) | `mode_transitions_follow_the_ticket` | D9 (ESC = `launch_cancel`、`DONE` を待つ) |

戻した後の最終状態は `59 passed; 0 failed`。

## A-3. 検査の並び (`src/launch.rs`)

| 検査 | 見るもの |
|---|---|
| `status_decodes_into_the_abi_phases` | `PENDING` / `TAKEN` / `RUNNING + child` / `DONE` / `FAILED + (-rc)`、未知の値と負の status は `Unknown` |
| `a_short_lived_child_is_done_on_the_first_poll` | 短命な子 (`rc == 0` → `DONE`) を最初の poll で受ける (§5 blocker 1) |
| `running_then_done_walks_the_child_id_into_the_status_row` | `PENDING` → `TAKEN` → `RUNNING(3)` (子 ID が分かった周だけ描き直す) → `DONE` |
| `a_failed_launch_carries_the_negative_rc_back` | `FAILED` は元の負の `rc` でプロンプトへ |
| `a_negative_poll_returns_to_the_prompt_instead_of_hanging` | `OS32_ERR_STALE` / 未知の status でも接続モードに居座らない |
| `escape_cancels_once_and_waits_for_done` | ESC → `cancel` 0 → 接続モードのまま `DONE` を待つ。2 度目の ESC は出さない |
| `again_retries_the_cancel_on_the_next_timer` | `AGAIN` → 次の poll で再試行 → `RUNNING` で通る → `DONE` |
| `stale_cancel_returns_to_the_prompt` | `STALE` (もう `DONE` / `FAILED`) はそのままプロンプトへ |

`prompt.rs` 側は `attached_row_names_the_child_and_offers_cancel` (最下行が
`[running id=3] ESC=cancel` / `[cancelling id=3] wait`)、`message_rc_prints_the_
negative_return_value`、`mode_transitions_follow_the_ticket` (`Done` / `Failed` /
`CancelRequested`、`Exit` は遷移表から外れた)。

## A-4. 実行 (2026-09-13)

```
$ cargo test --manifest-path userland/rust/t5a_display/host_tests/Cargo.toml \
      --target x86_64-unknown-linux-gnu --offline
test result: ok. 59 passed; 0 failed; 0 ignored; 0 measured; 0 filtered out

$ cargo check --release -p t5a_display      # userland/rust workspace
    Finished `release` profile [optimized] target(s)

$ python3 -B tools/check_constraints.py
制約チェック OK — 規則 16 件、参照側 CLAUDE.md
```

`cargo clippy -p t5a_display` は**この票より前から**落ちる
(`storage.rs:22` の `clippy::mut_from_ref` が deny)。今回足した / 触った
`launch.rs` `prompt.rs` `guest.rs` には clippy の指摘は 1 件も無い。
## A-5. 実装レビュー往復 1/3 の blocker (A、2026-09-13)

指摘: **ESC の `STALE` 経路で要求表が残り、以後の起動が `FULL` に固着する**。
反例 = 子が終了 → 次の 100ms poll より先に ESC → `launch_cancel` が `OS32_ERR_STALE`
→ 旧実装は `Step::Finish` で token を捨ててプロンプトへ。K の契約
(`include/launch.h`) では **完了した表は `launch_poll` が消費して初めて `IDLE`**
なので、以後その表は誰にも消費されず、同じ端末の `launch_req` が常に `FULL`。

直し: `Attach::cancelled()` は `STALE` でも token を捨てず `Cancel::Armed` に
して `Step::Redraw` を返す (「`STALE` = もう完了している = 消費だけ残っている」と
読む)。接続モードのまま次のタイマで `launch_poll` を続け、`DONE` / `FAILED` を
消費した時点でプロンプトへ。本当に不一致なら poll が負を返して `Lost` で戻る
(その場合の表は既に `IDLE`)。

試験: 端末側だけでは表の寿命が見えないので、`launch.rs` の試験に**カーネルの
要求表の最小の写し** (`Table`: `req` / `start` / `fail` / `child_exit` / `poll` /
`cancel`、完了は poll が消費して初めて `IDLE`) を置き、`tick()` で
`guest.rs::poll_launch` と同じ順序を回した。

| 埋め戻した欠陥 | 落ちた検査 |
|---|---|
| `cancelled(STALE)` を `Step::Finish(Done)` に戻す (レビュー前の実装) | `escape_after_the_child_already_exited_does_not_wedge_the_table` `stale_cancel_keeps_the_token_and_waits_for_the_poll` |

足した / 直した検査 (48 → 59 → **62**):

| 検査 | 見るもの |
|---|---|
| `stale_cancel_keeps_the_token_and_waits_for_the_poll` | `STALE` でも token を保つ → 次の poll が `DONE` を消費して戻る。token 不一致なら poll の負で `Lost` |
| `escape_after_the_child_already_exited_does_not_wedge_the_table` | 反例そのもの。ESC の直後は `req()` が `FULL`、poll で `DONE` を消費した後は**次の起動が通る** |
| `a_failed_launch_also_frees_the_table_only_through_the_poll` | `FAILED` も消費して初めて表が空く |
| `escape_while_pending_retries_until_the_kill_is_queued` | `AGAIN` → 再試行 → `RUNNING` で取消が積まれる → 回収通知 → `DONE` を消費 (模型ごしの通し) |

実行: `62 passed; 0 failed`、`cargo check --release -p t5a_display` 警告 0、
`python3 -B tools/check_constraints.py` OK。`make` / 配備 / 実機は**未実施** ([V4])。

## 6. W 節 — WM (gshell) 側の RED → GREEN (2026-09-13)

票: §1 D3 (2)(3)(4) / D5 / D8、§10 non-blocker 2 / 4。
実行: `make check-gshell-host` (= `python3 userland/gshell/host/integration.py`)。
検査は `userland/gshell/host/wm_tests.rs` の末尾「票 T9-W」節に 9 本追加した。
KAPI の代用は `userland/gshell/host/mocks.rs` (`launch_pending` / `launch_take` /
`launch_report` / `launch_child`、`get_tick`、`exec_kill` が `FREE` に落とす ID)。
**表そのものの遷移は実物で検査済み** (§3 の `launch_host.c`) なので、W の検査が
見るのは「WM が いつ・何を・どの順で 渡したか」だけ。

RED は「検査を先に書き、`drain_launch_requests` を `false` を返すだけの仮実装に
した状態」で取った。9 本中 **7 本が RED** (残り 2 本 = `session_launch` の
モーダル維持と連鎖の環は、仮実装でもたまたま通る負例側の検査)。

| 検査 | 見るもの | RED |
|---|---|---|
| 1 `a_taken_launch_goes_through_run_program_and_reports_the_child` | LAUNCH は `run_program` (`begin_start`/`end_start`/全画面判定) を通り、戻りをそのまま `launch_report` へ。モーダルを出さない | ✔ |
| 2 `a_failed_launch_is_reported_without_a_wm_modal` | `rc < 0` は `FAILED(rc)` として返すだけ (D3 (3)) | ✔ |
| 3 `a_taken_kill_folds_the_chain_and_forgets_every_freed_id` | KILL → `exec_kill(arg)` → `FREE` を**全部** forget (D8)。`launch_report` の `OS32_ERR_STALE` を再試行しない (§10 2) | ✔ |
| 4 `the_session_launch_path_still_shows_the_failure_modal` | Start → Run... は従来どおりモーダル (回帰) | — |
| 5 `a_pending_launch_request_makes_the_running_app_yield` | `should_park` の (a) に `launch_pending` (D3 (2)) | ✔ |
| 6 `a_pending_launch_request_stops_the_wm_from_resuming_a_polling_app` | 要求のある周は WM の番 (`pick_poll` の門) | ✔ |
| 7 `polling_apps_are_woken_in_rotation_and_only_once_per_tick` | `WAIT_POLL` 群は巡回、同じ tick に 2 回起こさない、tick が進めば再開 (D5) | ✔ |
| 8 `ctrl_stop_is_redirected_to_the_tail_of_the_launch_chain` | 宛先は連鎖の末尾。`abort_targets_current` は末尾 == cur のときだけ真 (D8) | ✔ |
| 9 `a_cycle_in_the_launch_chain_does_not_hang_the_ctrl_stop_lookup` | 環でも止まる | — |

GREEN 後の最終実行 (`--test-threads=1`): `65 passed; 0 failed` (追加前は 56)。
`python3 -B tools/check_constraints.py` (規則 16 件 OK)、`python3 tools/check_gui_proto.py`
(定数 105 / 構造体 31 一致)、`test_launch.py` / `test_multiapp_impl.py` /
`test_multiapp_model.py` (C 側の回帰、ALL PASS) も通した。
`cargo check --release` (`userland/gshell`、i686-os32-none) は警告 0。
`cargo clippy` は**着手前から** `src/lib.rs:127` (`main` の生ポインタ) で
`deny` に当たって落ちる — 29 件の警告も含めて追加分は 1 件も無い。

**未実施** ([V4]): `make` (全ターゲット)、配備、エミュレータ、実機。

### 実装レビュー 第 1 版 (2026-09-13) の blocker 2 件 — RED → GREEN

検査を先に書き、`run_program` に `via` 引数だけ足して**振る舞いは元のまま**にした
状態で RED を取った (2 本とも RED)。

| 検査 | 反例 | RED の見え方 |
|---|---|---|
| 10 `a_cui_only_program_from_the_launch_table_is_reported_as_a_failure` | 端末の `sh` で `exec /usr/bin/v86.bin` → 入口が拒否 → `RUN_REFUSED` = 0 → 表が `DONE` | `入口の拒否を DONE (rc = 0 = 正常終了) として報告した: [(11, 0)]` |
| 11 `a_cui_only_program_from_the_start_menu_still_opens_the_modal` | 上の裏 (Start → Run... は従来どおりモーダル、表へ報告しない) | — (回帰側、最初から GREEN) |
| 12 `a_tick_boundary_between_pick_and_resume_does_not_allow_a_second_wake` | tick N で `pick` → 再開前に PIT が N+1 → 記録は N の集合 → 次の `pick` が集合を捨てて同じ N+1 に 2 回目 | `pick` が 0 でなく 3 を返す |

検査 12 の tick 境界は `mocks::set_tick_script(&[100, 101])` で作る
(`get_tick` を呼び出しごとに進める = `pick` は 100、`mark_resumed` は 101 を見る)。

直し方:

- **blocker 1**: `run_program(st, path, via)` に依頼元 `LaunchVia::{Wm, Table}` を足し、
  `Table` の入口の拒否は `OS32_ERR_INVAL` (負) を返してモーダルを出さない。
  `RUN_REFUSED` は `Wm` 経路だけの戻り値になった。
- **blocker 2**: 「この tick で起こし済み」を `Multi` のビット集合 (一括消去) から
  `App::poll_tick` / `poll_tick_valid` (**1 本ごと**、消去なし) へ。記録は `mark_resumed` が
  `get_tick` を読んだ時刻 = 実際に再開した tick。`pick_poll` は
  `poll_tick_valid && poll_tick == now` の相手だけ飛ばす。

最終実行: `68 passed; 0 failed` (T9-W は 12 本)。`cargo check --release` 警告 0、
`check_constraints.py` / `check_gui_proto.py` も通した。追加した unsafe 呼び出しには
SAFETY コメントを 1 つずつ付けた (non-blocker)。

### 実機受入 S6 不合格の修正 (2026-09-13) — RED → GREEN

現象 (実機 `ecaba37`、API v49): 端末 (2) → `sh` (3、`sys_yield` の `WAIT_POLL`) →
`kbd_echo` (4、`WAIT_KEY`) の連鎖で CTRL+STOP を 3 回送っても何も畳まれない
(`ring3_abort_count` = 0、`ring3_yield_count` 11141 = WM は毎 tick 単独ループを回っている)。

原因: `abort_seen` を消費する点が `handler.rs` の `op_wait` (アプリの中で WM が回る周) と
`lib.rs` の**全画面**分岐しか無かった。`sh` が譲っている間は `should_park` が端末を park
させるので WM は `standalone_loop` (ウィンドウモード) に居り、そこに経路が無い。
IRQ1 の `appslot_abort_request()` は `g_cur` がシェル帯だと何も立てない (= `ring3_abort_count`
が増えない) ので、raw リング由来の `abort_seen` だけが唯一の手がかりだった。

RED は `top_level_abort()` を「従来の全画面分岐だけ」の仮実装にして取った (3 本とも RED)。

| 検査 | 見るもの | RED の見え方 |
|---|---|---|
| 13 `ctrl_stop_at_the_window_mode_top_level_folds_the_tail_of_the_chain` | ウィンドウモードの top-level で末尾 (4) だけ畳む → 2 回目は次の末尾 (3) | 戻り値 0 / `exec_kill` 0 回 |
| 14 `a_pending_redirect_reservation_is_not_executed_twice_at_top_level` | `redirect_abort` の予約がある周は直接畳まない (予約は `drain_top_level` が実行) | `abort_seen` が降りない |
| 15 `ctrl_stop_with_no_target_only_clears_the_request` | 宛先 0 なら `exec_abort_clear` だけ | `abort_clear_calls` 0 |

直し方: `standalone_loop` の分岐を `top_level_abort()` に括り、全画面は従来の
`redirect_abort` 予約のまま (実機 T8 F8 で通っている形)、ウィンドウモードは
`multiapp::abort_at_top_level()` が **owner 1 の文脈でその場で**
`exec_abort_clear()` → `exec_kill(abort_target())` → `forget_freed()` を実行する。
予約にせず直接実行なのは、`request_kill` が「WM の表に載っている ID」にしか積めず、
連鎖の末尾が表から落ちていると CTRL+STOP が黙って消えるため。

最終実行: `71 passed; 0 failed`。`cargo check --release` 警告 0、
`check_constraints.py` / `check_gui_proto.py` も通した。実機再試験は PM / テスター側 ([V4])。

### 受入 S6 の 3 回目 (本人宛て) が不安定だった件の修正 (2026-09-13) — RED → GREEN

現象: 端末 1 本 (GetMessage 型、100ms タイマ) に CTRL+STOP を送る 3 回目が **3 回中 2 回失敗**。
IRQ1 の着地点で経路が分かれる:

| IRQ1 が着地した時点 | カーネルの `abort_req` | 結果 |
|---|---|---|
| 端末が `op_wait` の中 (`in_op_wait` = 1) | 立つ | `break` → syscall 出口で畳まれる (成功した回) |
| 端末が自分のタイマ処理を CPL=3 で走らせている最中 | **立たない** (暴走ではない) | `break` しても出口に要求が無く**消える** (失敗した回) |

K5b / K5c の G 検査が通っていたのは下の行の窓が小さかっただけで、`abort_seen` (raw リング由来) は
立っているのに畳む手段が無い、という穴だった。

直し方: `handler.rs` の `abort_targets_current` が真の枝で `break` をやめ、
**本人宛ても予約に統一**する (`multiapp::reserve_abort_self(st, cur)` =
`abort_clear_req = true` + 宛先が本人なら `request_kill(cur)`)。次の `maybe_park` が
`has_top_level_work` で譲らせ、top-level の `drain_top_level` が
`exec_abort_clear` → `exec_kill(本人)` → `forget` を行う (#2 で sh を畳んだのと同じ経路)。
宛先が無い周 (`abort_target` = 0) は取り消しだけ予約する。
**例外**: owner が WM の表に載っていない (= `should_park` が譲らせない = top-level へ戻る道が
無い) 1 本だけは従来どおり `break` する — 予約を残すと `has_top_level_work` が立ちっぱなしになる。

| 検査 | 見るもの | RED |
|---|---|---|
| `ctrl_stop_with_focus_on_the_running_app_keeps_the_kernel_abort` (改訂) | 2 本居て宛先 = 本人 → 予約 → 次の `op_wait` で park → top-level で `exec_abort_clear` 1 回 → `exec_kill(2)` | ✔ (`pending_top_level_work` が偽) |
| `a_single_app_keeps_the_old_ctrl_stop_path` (改訂) | **1 本でも**同じ (押した周だけ park する) | ✔ (`1 本なのに予約が積まれた`) |
| 16 `ctrl_stop_in_op_wait_with_no_target_reserves_only_the_clear` | 宛先 0 → `exec_abort_clear` だけ、誰も畳まない | ✔ |
| 17 `ctrl_stop_in_op_wait_with_a_child_still_folds_the_tail` | 宛先が別 (子あり) は従来どおり連鎖の末尾 | — (回帰側) |

押していない周の「1 本なら park しない」(回帰ゼロ) は
`a_single_app_never_parks_and_keeps_the_old_wm_cycle_halt_loop` が引き続き見ている。
全画面の枝 (`lib.rs` → `redirect_abort(st, 0)`) は `cur` = 0 なので影響なし。全画面の所有者が
`op_wait` から本人宛てで来た場合も同じ予約経路に乗る (`abort_target` = 所有者 = `cur`)。

最終実行: `73 passed; 0 failed`。`cargo check --release` 警告 0、
`check_constraints.py` / `check_gui_proto.py` も通した。実機再試験は PM / テスター側 ([V4])。

### 模型との差 (報告済み)

D5 の巡回と tick の間引きは **WM の領分** (D11-5: カーネルは順を決めない) なので、
`tools/tests/multiapp_model_host.c` の `ma_pick_poll` は T8 の「ID 昇順」のまま
残してある (ケース 19p)。`multiapp.rs` の `pick_poll` とはこの 1 点だけ 1 対 1 で
なくなった — 模型側に tick を持ち込むと K レーンが着地させた
`multiapp_impl_host.c` のケース 19 / 22 / 23 を巻き込むため。
---

# T9-S ホスト TDD の記録 (sh.bin の起動待ち)

票: [TASK_T9_sh.md](../../docs/archive/gui_v13/TASK_T9_sh.md) §1 D3a。
実行: `make check-sh-launch-host` (= `python3 -B tools/tests/test_sh_launch.py`)。

`tools/tests/sh_launch_host.c` が `userland/shell/sh_launch.inc` を**そのまま**
`#include` し、KernelAPI の `launch_req` / `launch_poll` / `sys_yield` / `kprintf`
だけを差し替える。見るのは 4 経路と 1 つの禁止:

| 経路 | 台本 | 期待 |
|---|---|---|
| DONE | PENDING → TAKEN → RUNNING → DONE | `0`、poll ごとに 1 回譲る、印字なし |
| FAILED | `0x300 + 3` (NOT_FOUND) / `0x300 + 4` (NOMEM) | `-3` は黙って返す (PATH 走査が続く)、`-4` は `sh: <名>: launch failed (-4)` |
| STALE | `launch_poll` が `-11` | 抜けて `-1`、`launch_poll failed (-11)` |
| FULL | `launch_req` が `-13` | `-1`、`sh: ls: busy`、poll も yield もしない |
| 禁止 | `kbd_getchar` / `kbd_trygetchar` / `ime_getkey` に印 | 全経路で 1 度も呼ばれない |

`launch_req` のその他の失敗 (`-10`) は `launch_req failed (rc)`、GUI 外の `-9` は
**1 行だけ**出して次の PATH 候補へ回す (`sh: external programs need the GUI terminal`)。
実行結果は全 36 項目 `ALL PASS`、`TARGET i386-elf GNU89 -Werror COMPILE PASS`。

### RED (2026-09-13 追記)

第 1 版では GREEN しか記録していなかったので、直す前の形に戻して落ちることを
採った。`sh_launch.inc` の `sys_yield` の直後に `kbd_trygetchar()` を挟み、
`FAILED` の判定を `if (0)` にすると:

```
  FAIL 1g 待ちの間 kbd/ime を呼ばない
  FAIL 2a status の -rc をそのまま返す
  FAIL 2b FAILED を見たらやめる
  FAIL 3a NOMEM を返す
  FAIL 3b 名前は cmdline の先頭語
```

戻すと `ALL PASS`。禁止 (kbd を覗かない) も経路 (FAILED) も、台本を通すだけでは
なく**外すと落ちる**ことを確かめてある。

---

# T9-S ホスト TDD の記録 2 (行再描画と source 中の exit)

票: [TASK_T9_sh.md](../../docs/archive/gui_v13/TASK_T9_sh.md) §1 D2(d) と
実装レビュー (往復 1/3) の blocker 1 / 2。
実行: `make check-sh-shell-host` (= `python3 -B tools/tests/test_sh_shell.py`)。

`tools/tests/sh_shell_host.c` が `userland/shell/sh_redraw.inc` と
`userland/shell/cmd_script.c` を**そのまま** `#include` する。差し替えるのは
KernelAPI 表 (出力 4 本・ヒープ・疑似ファイル・コンソール座標の呼び出し数え) と、
シェルの他モジュールが出す `execute_command` / `env_*` / `shell_register_cmds` だけ。

| 見るもの | 期待 |
|---|---|
| 延長 (TAB 補完) | 増えたバイトだけ印字、改行なし、座標 KAPI を 1 度も引かない |
| 非延長 (履歴・BS・候補一覧の後) | `\n` + プロンプト + 行全体、座標 KAPI なし |
| 行末より前のカーソル | 桁数ぶんの BS (端末の BS は消さずに 1 セル左) |
| `exit` の次の行 | 走らない、FD を残さない |
| `exit` のあとの `goto` ループ | ラベルも `goto` も走らず戻ってくる |
| ネストした `source` | 内側で `exit` → 外側の後続も走らない |

### RED → GREEN

**blocker 1** — `sh_redraw.inc` を修正前 (常駐と同じ座標依存の `redraw_line`) に
戻すと、同じ試験が落ちる:

```
       got "sh> lssh> " want "s"
  FAIL 1a 増えた 1 バイトだけを印字する
  FAIL 1c 変化なしなら無音
  FAIL 1d console_set/get_cursor を 1 度も引かない
       got "sh> catsh> " want "\nsh> cat"
  FAIL 2a 改行 + プロンプト + 行全体
  FAIL 2a' 作り直しも座標を引かない
       got "sh> ca sh> " want "\nsh> ca"
  FAIL 2b 縮む側も作り直す
```

**blocker 2** — `cmd_script.c` の `#ifdef SHELL_AS_APP if (sh_exit_flag) break;` を
外すと:

```
       got "echo 1|exit|echo 2" want "echo 1|exit"
  FAIL 3b exit の後は実行しない
       got "echo a|exit|goto loop" want "echo a|exit"
  FAIL 4b ラベルも goto も走らない
       got "source /inner.sh|exit|echo inner2|echo outer2" want "source /inner.sh|exit"
  FAIL 5b 内側も外側も後続を止める
```

(4b の RED で `goto loop` が 1 回で止まっているのはハーネスの記録上限 16 行の
おかげで、実物では `:loop` ← `goto loop` が永久に回る。)

どちらも戻すと全 19 項目 `ALL PASS`、`TARGET i386-elf GNU89 COMPILE PASS`。
実機・エミュレータ・`make` は**未実施** ([V4]) — 端末上の見え方 (受入 S5) は
PM の実機確認に委ねる。

### 往復 2/3 の追加 (2026-09-13)

**画面カーソルの位置** (再レビュー blocker 2)。CUI 直起動の `hel` → LEFT → TAB は
内容を変えないので写しは `hel` のままだが、画面カーソルは 1 つ左にある。写しに
`sh_drawn_pos` を持たせる前は差分だけ出て `hep ` に化けた:

```
       got "p " want "\nsh> help "
  FAIL 3a 差分ではなく行を作り直す
       got "d" want "\nsh> abcd"
  FAIL 3e その次も延長せず作り直す
```

`sh_drawn_pos == sh_drawn_len` を延長の条件に足し、`redraw_line` が自分の置いた
カーソル位置を写しへ残すようにすると `ALL PASS`。ui.c の LEFT / RIGHT / HOME でも
`sh_drop_drawn()` して次回を作り直しに倒してある (ケース 3f)。

**ハーネスの `goto` オフセット** (non-blocker)。`cmd + 7` は `goto loop` を `op` と
読んでいて、ラベルが見つからず `script_abort_flag` で止まっていた — つまり巻き戻し
からの脱出を検査できていなかった。`cmd + 5` に直すと、`sh_exit_flag` の判定を外した
RED で本当に回り続けることが見える:

```
       got "echo a|exit|goto loop|goto loop|… (15 回)" (走りすぎ) want "echo a|exit"
  FAIL 5b ラベルも goto も走らない
```

全 25 項目 `ALL PASS`。

**起動時の profile 内の `exit`** (再レビュー blocker 1) は `shell_run` の行ループの
入口に判定を移して直したが、ループ自体が `sh_getkey` と絡むのでホストへは持ち込んで
いない。ケース 5c (`source` から戻った時点で印が立っている) までが試験の範囲で、
「入口で見て抜ける」ことは実機確認 (受入 S5) に委ねる。

### 往復 3/3 の追加 (2026-09-13)

**行末 BS が画面を消さない**。端末の BS (`libos32term` の `model.rs` `write_inner`)
はカーソルを 1 セル左へ動かすだけでセルを消さないので、BS を 1 回出して短縮後の
写しを確定すると `echo abc` → BS×2 → `x` が画面 `echo axc` / バッファ `echo ax` に
なる。`sh_backspace_tail()` を BS 1 回だけに戻した RED:

```
       got "\b\bx" want "\b \b\b \bx"
  FAIL 4b 出力は BS 空白 BS の 2 回 + 'x'
       got "\b" want "\b\b  \b\b"
  FAIL 4e 全角の先頭は 2 セルぶん消す
```

`got "\b\bx"` が報告どおりの姿 — カーソルだけ 2 つ戻って `x` が `b` を上書きし、
`c` が残る。`\b` + 空白 + `\b` に直すと全 30 項目 `ALL PASS`。

### 往復 5/5 の追加 (2026-09-13)

**パイプバッファがカーネル帯**。`sys_pipe_get_buf()` が返すのは
`fs/pipe_buffer.c` が kmalloc した番地で、それを `sys_redirect_fd_buf()` へ渡すと
`exec/exec.c` の `ring3_ptr_ok` に落ちて CPL=3 の sh が `ring3_fault_kill()` される
(`sh> echo a | cat` で強制終了)。`sh_pipe.inc` を「`g_api->sys_pipe_*` をそのまま
返す」= 直す前の形に戻し、ハーネスの偽 KAPI に**カーネル帯に見立てた別の配列**を
返させた RED:

```
  ok   5a 2 本を別々に確保できる
  FAIL 5b 返るのは sh の静的配列の中
  FAIL 5c 2 本目も配列内で別の番地
  ok   5d 上限を超えたら負を返す
```

枠の貸し借り (5a / 5d〜5h) は直す前も通るので、**落ちるのはポインタの出どころを
見る 2 本だけ** — 欠陥の在りかと試験の狙いが一致している。sh 自身の `.bss` から
配る形に戻すと全 38 項目 `ALL PASS`。

外部段が混じるパイプを断る `sh_stage_is_builtin()` は `main.c` の static で、
`g_cmds` の登録表と `main()` (newlib) に依存するためホストへは持ち込んでいない。
ここは実機確認 (受入 S5) に委ねる。

### 往復 6 の追加 (2026-09-13、B2 / B4 / B5)

`sh_ls.inc` と `sh_launch.inc` もハーネスへ取り込み、偽 KAPI の**全ハンドラに
呼び出しカウンタ**を仕込んだ。直す前の形へ戻した RED:

| 戻したもの | 落ちる項目 |
|---|---|
| B2: `sh_ls_collect_cb` の中で `sys_isatty` を呼ぶ (旧 `vfs_ls_cb` / `glob_cb` と同じ形) | `FAIL 6a コールバック内の KAPI 呼び出しは 0 回` |
| B4: `sh_launch` 入口のパイプ判定を `if (0)` に | `got "sh: external programs need the GUI terminal\n" want "sh: pipe to external command is not supported\n"` → `FAIL 7b` |
| B5: `sh_erase_cells` を BS 1 回だけに | `got "\b" want "\b \b"` → `FAIL 8a` / `FAIL 8b` |

戻すと全 46 項目 `ALL PASS`。

**B7 (増分ビルド)** は `make -n` の dry-run で見た (`make` の実行は禁止)。
`sh_obj/ui.o` を古い時刻に、`sh_redraw.inc` を新しい時刻にしてから:

```
RED (依存なし): make -n sh の `-c userland/shell/ui.c` 0 行
GREEN (依存あり): 1 行
```

### 往復 7 の追加 (2026-09-13、R2 / R3 / R4 / R6 / R7)

`main.c` の引数分解と glob を `sh_args.inc` へ切り出し (中身は 1 行も変えて
いないので常駐の `.o` は不変)、`cmd_fs_shared.c` / `cmd_file.c` もハーネスへ
取り込んだ。`<stdio.h>` は python 側が置く薄いシム (`printf` だけ)。
直す前の形へ戻した RED:

```
       got "" want "sh: redirect to external command is not supported\n"
  FAIL 9c 理由を出す                    (R2: sh_launch の判定を外した)
  FAIL 10a 255 バイトそのまま写る        (R3: 写しの名前幅を 128B に戻した)
  FAIL 10b 長さも変わらない
  FAIL 11c 未展開の target* ではなく実体名に化けている   (R4: ふるいを外した)
  FAIL 11d 多すぎたら印を立てる
  FAIL 11e 理由を出す
  FAIL 12a 格納は max_args - 1 個まで     (R7: 上限を max_args に戻した)
  FAIL 12b 最後の枠は NUL 終端用に空いている
  FAIL 13a write 失敗で負 (mv は原本を消さない)          (R6: 0 を返す形に)
  FAIL 13b read 失敗でも負
```

11c の RED は「不一致 200 件 + 末尾に一致 1 件」で、ふるいが無いと先頭 128 件で
切れて一致を取り逃がし、`cat target*` が**未展開のまま**渡る。`argv[1]` に `*`
が残ることで見ている (先頭 2 文字だけを見ていた最初の書き方では、リテラルの
`target*` も `ta` で始まるため通ってしまった)。

戻すと全 60 項目 `ALL PASS`。

**常駐 `.o` の突合** (`1cdeaa1` 比較): R6 / R7 を一時的に戻した状態では
`cmd_base.o` (`__TIME__`) 以外すべて一致 — つまり R2〜R5 と `sh_args.inc` の
切り出しは常駐に 1 バイトも影響していない。R6 / R7 を入れると
`cmd_file.o` と `main.o` が変わる (仕様どおり、S7 の例外)。

### 往復 8 の追加 (2026-09-13、I1 / I2 / I3 / I4)

`cmd_mnt.c` もハーネスへ取り込み (`<stdlib.h>` のシムを 1 つ追加)、`dd` を実物
のまま回してバッファ長を見る。このとき偽の `kprintf` / `printf` が `%u` を
**読み飛ばして可変引数を消費していなかった**ため、後続の `%s` が数値を
ポインタとして参照して落ちた — 書式を 1 つ読むごとに必ず 1 つ消費する
`fmt_run()` に直した (ハーネス側の欠陥)。

直す前の形へ戻した RED:

```
  FAIL 12a 多すぎる行は負を返す (I1)          (I1: 黙って切り捨てる形に戻した)
       got "" want "sh: too many arguments\n"
  FAIL 12b 理由を出す
  FAIL 12g glob の合計超過も行ごと捨てる
  FAIL 12'f 畳めば同一と分かる                (I2: 正規化を外した)
  FAIL 12''a cd0 は ATAPI の 1 セクタぶん確保する  (I3: cd も 1024B に戻した)
  FAIL 12''b 確保長は 2048B
```

**I4 だけは「落ちる」ではなく「返ってこない」**。`wildcard_match` を再帰版に
戻すと `*a`×20 を `a`×40 に当てる項目 (12''c) で止まり、20 秒の timeout でも
返らない:

```
rc=124 (124 = timeout: 20 秒たっても返らない)
--- 最後に出た行 ---
  ok   12''b' cd 以外は 1024B のまま
```

反復型に戻すと即座に戻り、既存の一致 / 不一致 8 ケース (12''d〜12''k) も不変。
全 78 項目 `ALL PASS`。

**常駐 `.o` の突合** (`23a5d8f` 比較): I1〜I4 を一時的に戻すと `cmd_base.o`
(`__TIME__`) 以外すべて一致 — T2 は SHELL_AS_APP のみで常駐に 1 バイトも
影響していない。I1〜I4 を入れると `main.o` (I1 / I4)、`cmd_fs_shared.o` (I2)、
`cmd_mnt.o` (I3) が変わる (仕様どおり、S7 の例外)。

T2 の拒否そのもの (`sh_is_cui_only`) は `main.c` の static なのでホストへは
持ち込んでいない (B3 / B6 と同じ理由)。

### 往復 9 の追加 (2026-09-13、C-1 と I-1〜I-6)

`cmd_env.c` / `cmd_sys.c` もハーネスへ取り込み、`env_set` / `env_expand` の
スタブは実物に置き換えた。直す前の形へ戻した RED:

```
  FAIL C1a * は * で始まる名前にも当たる      (C-1: '*' を通常文字の後ろに戻した)
  FAIL C1d 末尾一致
  FAIL I1 IdeInfo は 96B (phys_sector_size を含む)
  FAIL I2 展開が受け皿に収まらなければ負
  FAIL I2' 展開なしでも長すぎれば負
  FAIL I3 収まらない結合は負を返す
  FAIL I4 40 文字の名前が切れずに写る
  FAIL I5 hd0 は 512B
       got "dd: wrote # bytes -> /dd.out\n" want "dd: write failed at sector # (wrote # bytes)\n"
  FAIL I6 write 失敗で中止して報せる
```

C-1 の RED は旧版で通っていた 8 ケース (C1f〜C1m) を**巻き込まない** — 落ちるのは
`*` で始まる名前に関する 2 本だけで、回帰の範囲と一致する。戻すと全 102 項目
`ALL PASS`。

**常駐 `.o` の突合** (`3f919a5` 比較): 変わるのは `main.o` (C-1 / I-2 の呼び手)、
`cmd_sys.o` (I-1)、`cmd_env.o` (I-2)、`cmd_fs_shared.o` (I-3)、`cmd_file.o`
(I-3 / I-4)、`cmd_mnt.o` (I-5 / I-6)。`cmd_base.o` の差は `__TIME__` 1 バイト。
C-1 は回帰修正なので常駐にも入るのが正しい。

**ホストに載せていないもの** (実機確認 = 受入 S5 に委ねる): B3 の判定
(`sh_has_redirect` / `sh_name_is_builtin`) と B6 の段ループの `sh_exit_flag`、
B4 の事前判定は、いずれも `main.c` の static で `g_cmds` の登録表と `main()`
(newlib) に依存するため 1 翻訳単位へ持ち込めない。B4 は**最終起動口**側
(ケース 7) で押さえてあるので、事前判定をすり抜けても捕まる。
