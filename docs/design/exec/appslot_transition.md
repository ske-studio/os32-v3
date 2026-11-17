# exec/appslot_transition.inc — 設計

> 状態: **現行 (2026-11-18)** — 出所: TASK_K5_multiapp (D0 の C1〜C6)・TASK_K5B_kernel・TASK_K7_input (D1・D5、§5 B〜D)・TASK_T8_fullscreen_gfx (§7 D8) の契約を今のソースと照らして要約
ソース: `exec/appslot_transition.inc` (`exec/appslot.c` が取り込む)・`exec/exec_schedule.inc` (`exec_resume`)

協調切替の park (アプリが WM へ制御を返す) と resume (WM がアプリを再開する) の可否を、AppSlot の表だけで決める。WM の行儀を信じず、カーネルが弾く。走っているアプリは常に 1 本で、切替は park → WM top-level → resume の往復だけ (プリエンプションは無い)。各入口の更新の順は `docs/design/exec/appslot_park.md`。

## 状態と印

| `exec_app_state` | 状態 | 印 (park の由来) |
|---|---|---|
| 0 | `APP_STATE_FREE` | — |
| 1 | `APP_STATE_RUNNING` | — |
| 2 | `APP_STATE_PARKED` | `parked_from_wait` (`gui_call(OP_WAIT)` の中) |
| 3 | `APP_STATE_WAIT_KEY` | `parked_from_kbd` (`kbd_getchar`・`kbd_getkey` の待ち) |
| 4 | `APP_STATE_WAIT_POLL` | `parked_from_poll` (`kbd_trygetchar` 等のポーリング)・`parked_from_yield` (`sys_yield`)・`parked_from_stop` (GUI STOP の WM 返却) |

値の追加は互換 (既存の 0〜2 の意味は動かさない)。

## park できる文脈

共通の資格 (`appslot_park_kbd_check`): 現在の ID がアプリの範囲 (シェル帯でない)・生存かつ RUNNING・GUI 起動 (`a->gui`)。どれかに外れたら `OS32_ERR_INVAL` で `ring3_park_reject_count` を 1 増やし、呼び手は従来の待ち (hlt) に落ちる。CUI の入れ子 `exec_run` の子は park できない (longjmp の行き先が親アプリの中になり WM へ戻れない)。

| 入口 | 追加の条件 | 成立したとき |
|---|---|---|
| `appslot_park_check` (OP_WAIT) | `gui_call(OP_WAIT)` の中 (`g_cur_op_is_wait` と `a->in_op_wait`)。他の op・`gui_call` の外は弾く | PARKED、`ring3_switch_count` は resume 側 |
| `appslot_park_kbd_check` (kbd 待ち) | OP_WAIT の中である必要は無い | WAIT_KEY、`ring3_kbd_park_count` |
| `appslot_park_poll_check(now_tick)` (ポーリング) | 同じ tick の 2 回目は `OS32_ERR_AGAIN` (10ms に 1 回まで)。間引きは表の検査より**先**で、弾かれた試みも tick を控える。間引きは違反ではないので弾き数に数えない | WAIT_POLL、`ring3_poll_yield_count` |
| `appslot_park_yield_check` (`sys_yield`) | 間引かない | WAIT_POLL、`ring3_yield_count` |

成立すると cur と res owner はシェル帯 (WM top-level) へ戻る。

## resume (`appslot_resume_check(id)`)

- 呼べるのは WM top-level だけ (`g_cur` も res owner もシェル帯)。それ以外・未知の ID・畳まれた ID は `OS32_ERR_INVAL`。
- **その状態に対応する印が立っているフレームだけ**を起こす。印が無ければ `OS32_ERR_STALE` で `ring3_resume_bad_frame_count` を 1 増やし、状態も印も変えない (switch_count も増えない):
  - WAIT_KEY は `parked_from_kbd` だけ (OP_WAIT の印では起こせない)。
  - WAIT_POLL は poll・yield・stop のどれか (kbd の印では起こせない)。
  - PARKED は `parked_from_wait` だけ。
- 成立 (`appslot_resume_commit`) で印は全部消える (1 回きり。同じフレームは二度起こせない)。状態は RUNNING、cur と owner はその ID。成功は `ring3_switch_count` を 1 増やす。

## resume で EAX に入れるもの (`appslot_resume_source` → `exec_resume`)

| 由来 | EAX |
|---|---|
| OP_WAIT (`APP_RESUME_SRC_WAIT`) | WM が渡した `wait_ret` |
| kbd (`APP_RESUME_SRC_KBD`) | 注入リングからこの ID が取れる 1 バイト (1 回の resume で 1 バイトだけ、残りはリングに残る)。取れなければ起こさず `OS32_ERR_AGAIN` (印も状態も残り、WM は次の周に試せる)。`sys_read` の途中なら取らずに覗くだけで、syscall を再実行させる |
| poll (`APP_RESUME_SRC_POLL`) | 取れれば 1 バイト、空なら -1 (キーなし)。空でも起こす |
| yield (`APP_RESUME_SRC_YIELD`) | 0。注入リングを読まない |
| stop (`APP_RESUME_SRC_KEEP`) | 保存した EAX のまま。注入リングを読まない |

## 畳む

止めてあるアプリ (PARKED・WAIT_KEY・WAIT_POLL) は WM top-level の `exec_kill` で畳める。走っている本人は `exec_kill` できず (`OS32_ERR_STALE`)、CTRL+STOP の経路で畳む (`appslot_kill_check`、`exec/appslot.c`)。畳んだら WM top-level へ戻る。

## 検査

`tools/tests/test_multiapp_impl.py` (`multiapp_impl_host.c`)。実物の `exec/appslot.c` と `kernel/kbd_inject.c` を取り込む。`exec_resume` の kbd 分岐はホストに持ち込めないので、ハーネスの `ma_resume_kbd` に写してある (写した部分の正しさは言わない)。
