# userland/gshell/src/multiapp.rs — 設計

> 状態: **現行 (2026-11-18)** — 出所: TASK_K5_multiapp (§K5a-8、D11-3・D11-3a)・TASK_K5B_gshell (決裁 A1・A3)・TASK_K7_input・TASK_T8_fullscreen_gfx (§7 D8) の契約を今のソースと照らして要約
ソース: `userland/gshell/src/multiapp.rs`

GUI アプリ 4 本の同時実行のうち、**誰を次に起こすか・走っているアプリが譲るか**を決める WM 側の規則。カーネルは機構 (`exec_start`・`exec_park`・`exec_resume`・`exec_kill` と印のゲート) だけを持ち、順番を決めない。協調切替で、時間で横取りする仕組みは無い。

## 呼ぶ点

- `exec_park` を呼ぶのは `maybe_park` の 1 か所だけで、そこは `handler::op_wait` のループの先頭 (`wm_cycle` より前、WM の 1 周が終わった地点)。park が成立しなかったとき (`OS32_ERR_INVAL`) だけ戻り、普通の待ちを続ける。
- `exec_resume` を呼ぶのは WM top-level の `resume_one` の 1 か所だけ。1 周に起こすのは 1 本。
- アプリが 1 本だけのときは park しない (`exec_park` 0 回、従来の `wm_cycle` + `sys_halt` の待ち)。

## 起床の群

| 群 | ready の条件 |
|---|---|
| 入力群 (`input_ready`) | スロットのリングに未読がある (`ring::pending`)・積めずに残った Quit (`session::quit_pending`)・鍵待ち (`APP_STATE_WAIT_KEY`) に注入が届いている (`key_ready`) |
| 導出群 (`derived_ready`) | 入力群でなく、期限切れのタイマ・未通知の `Configure`・配送できる `Paint` (`damage::has_deliverable_paint`)・park した `OP_WAIT` の期限 (WM が `note_parked` で控える。カーネルは持たない) のどれか |
| ポーリング群 (`poll_ready`) | `APP_STATE_WAIT_POLL`。常に ready だが優先度は最下位。`ready` には入れない |

- `key_ready` は `kbd_inject_pending` が 0 で、この ID に WM が注いだ候補も無ければ偽。真でも未読の保証ではなく、`exec_resume` が `OS32_ERR_AGAIN` を返したら候補を降ろし、その周の turn・巡回の起点・据え置きの数えを巻き戻す (`save_turn`/`restore_turn`)。
- スロットを持たない鍵待ち・ポーリングのアプリ (端末から起動した CUI プログラム) も起こす相手 (`wait_ret` は 0、カーネルが上書きする)。それ以外のスロット無しは表から落とす。

## 次に起こす 1 本 (`pick`)

1. ラウンド: 生きていて、このラウンドの turn を使っておらず (`turn_used`)、走っておらず、ready な本数 (`round_remaining`) が 0 なら全員の turn を配り直す。配り直しても 0 ならポーリング群へ降りる (ここだけ)。
2. 入力群にフォーカス窓の owner (`front_owner`) が居て turn が残っていれば最優先。
3. 入力群を `last_run` の次から ID 昇順に巡回 (`pick_group(true)`)。
4. 空なら導出群を同じ巡回で (`pick_group(false)`)。導出群ではフォーカスを優先しない。
5. ready が 1 本も無ければ誰も起こさない (0)。

ポーリング群 (`pick_poll`): top-level にしか出来ない仕事 (下の (a) の各項) がある周は選ばない。前回ポーリングから起こした ID の次から巡回し、同じ ms に同じアプリを 2 回起こさない (`poll_tick`、印は `mark_resumed` が実際に再開する直前の ms で付ける)。全員起こし済みなら 0 で、呼び手は `sys_halt` で次の tick を待つ。

`mark_resumed`: turn を使った印・期限の消去・`last_run` = その ID・`input_streak` = 0。起動したて (`on_start`・`adopt_running`) も turn を使った扱い (そのラウンドの追加 turn にしない)。

## 走っているアプリが譲るか (`should_park(cur)`)

判定の順が契約:

0. 表に無い ID は譲らない。ただし `exec_start` の最中 (`begin_start` 〜 `end_start`) なら、その ID を表に迎えて (`adopt_running`) 判定を続ける。これが無いと起動したてのアプリが最初の `OP_WAIT` で park できず、`exec_start` が戻らない。最初の 1 本は相手が居ないので譲らない。
1. (a) top-level にしか出来ない仕事があれば譲る: `session::ready_to_run`・`LAUNCH` の保留 (`launch_pending`)・`exec_abort_clear` と `exec_kill` の予約 (`has_top_level_work`)・鍵待ちの通知・全画面の後始末 (`fullscreen_restore_pending`)・起動要求表 (`launch_work_pending`)・設定レジストリの予約と通知 (`settings::pending`)。他に ready が居なくても、自分の入力の据え置き中でも譲る。この譲りは `input_streak` を動かさない。
2. (b) 自分以外に ready が居ず、ポーリングで譲った 1 本 (`poll_live`) も居なければ譲らない。
3. 自分が入力群で、連続の据え置き `input_streak` が `INPUT_STREAK_MAX` 未満なら、数えを 1 増やして譲らない。
4. それ以外は譲る (自分が ready でない、他に入力・導出がある)。

## 飢餓の上界

- `INPUT_STREAK_MAX` = `MAX_APPS` = 4。
- `STARVE_BOUND` = (2N − 2) × (`INPUT_STREAK_MAX` + 1)。N = 4 で 30、N = 2 で 10。ready なアプリは、自分が park した `OP_WAIT` から数えてこの回数の `OP_WAIT` 以内に走る (ラウンドをまたぐ待ちを含む)。前提は 3 つ: 起算点は park した `OP_WAIT`・その間ずっと ready・アプリの集合が変わらない (途中の起動・終了・kill は測っていない)。
- 上界はタイト: N = 2 で自分の 2 窓へ交互に `set_focus` して入力を湧かせ続けるアプリの相手はちょうど 10、N = 4 のラウンドまたぎでちょうど 30 で走る。
- (a) の項は有限個の事象なので上界を変えない。ポーリング群はラウンドにも上界にも数えない。

## top-level の仕事 (`resume_one` の前段)

- `drain_top_level`: **CTRL+STOP の取り消しを畳むより先に**行う。`abort_clear_req` があれば `exec_abort_clear` を呼び、張本人 (`exec_stop_culprit`) か予約した宛先を 1 本畳む。次に `kill_req` の 1 本を `exec_kill` する (1 周に 1 本)。逆順だと、走っていた本人が `exec_kill` の間に syscall の出口で巻き添えで死ぬ。
- CTRL+STOP の宛先 (`abort_target`): STOP park の張本人が居ればそれ。居なければ頭 = 全画面中は画面の所有者、それ以外はフォーカス窓の owner で、そこから起動要求表の連鎖 (`launch_child`) の末尾まで辿る (同じ ID が 2 度出たら止める)。
  - `op_wait` の中 (owner がアプリ ID) では `exec_abort_clear` も `exec_kill` も通らないので、宛先が走っている本人でなければ `redirect_abort` で予約だけし、(a) で譲って top-level で実行する。
  - 宛先が走っている本人 (`abort_targets_current`、宛先が無い場合も含む) なら、カーネルの abort のまま待ちを抜けて畳ませる (アプリが 1 本のときの従来の経路)。
- 止めてあるアプリの Quit と、起こせなかったアプリ (`exec_resume` が `OS32_ERR_AGAIN` 以外の負) は `request_kill` → `drain_top_level` の `exec_kill` で畳む。
- `on_owner_exit` (`GUI_OP_OWNER_EXIT`): その ID の 1 本分だけを表から落とす (巡回の起点・ポーリングの控えも、同じ ID の再利用でずれないよう落とす)。`exec_kill` が子孫ごと畳んだ ID も `forget_freed` で落とす。
- `sync_snd_focus`: フォーカス窓の owner が変わったときだけ `snd_focus` を 1 回呼ぶ。窓が無ければシェル帯。

## セッションの打ち切り (`SWITCH_CUI`・`SHUTDOWN`)

全アプリへ Quit → `session::QUIT_GRACE_TICKS` (300 tick = 100Hz の論理 tick で 3 秒、WM の周回数に依らない) 待つ → 残りを `request_kill_all` → 全回収 → `SessionAction` 実行。Quit に応じたアプリは表から落ちているので畳まれない。全員が応じれば誰も畳まない。スロットを持たないアプリ (鍵待ち・ポーリング) は Quit を積む先が無いので猶予を待たずに `request_kill_slotless` で畳む。`LAUNCH` は既存のアプリを Quit させない (1 本増やす)。

## 検査

`make check-gshell-host` (`userland/gshell/host/wm_tests.rs`、実物の `GuiState` と `multiapp`)。規則の模型は `tools/tests/multiapp_model_host.c` (`test_multiapp_model.py`) で、`ma_should_park`・`ma_pick`・`MA_INPUT_STREAK_MAX`・`MA_STARVE_BOUND` がここと 1 対 1。ポーリング群の巡回と ms の間引きだけは模型が ID 昇順のままで、`wm_tests.rs` が持つ。
