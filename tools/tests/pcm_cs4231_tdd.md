# TDD 記録 — CS4231 (MATE-X PCM) 再生ドライバ

票: [`docs/tasks/v3/TASK_PCM_CS4231.md`](../../docs/tasks/v3/TASK_PCM_CS4231.md) §2-3 / §3 E1
対象: `drivers/pcm_cs4231_math.c` (純粋部) と `drivers/pcm_cs4231.c` (I/O と状態機械)
走らせ方: `make check-pcm-cs4231-host` (= `python3 -B tools/tests/test_pcm_cs4231.py --target --mutate`)

## なぜホストで回すのか

NP21/W では**踏めない分岐**がこの層の中心にある。

| 見るもの | NP21/W で踏めない理由 |
|---|---|
| 1 周回った観測 (同じ半分で位置が戻る) | 実時間では tick が 10ms で回るので作れない。作れるのは「時計と DMA カウンタを手で置く」ホストだけ |
| 補充の余裕 (`REFILL_MARGIN` = 512 frame) を割った切り替え | 位置をピンポイントに置けない |
| drain の 3 段階と「出た + staged > 0 は完了しない」 | 末尾の 1 周ぶんを狙って観測できない |
| 初期化列 / 停止列 / RS の列の**順序** | 出てしまった音からは順序が読めない。I15→I14 の取り違えは NP21/W では症状すら出ない |
| 入口ガードで装置アクセスが **0 回** | 数えるには模型が要る |
| close / reclaim が各状態から **1 度だけ**解放すること | 二重解放はプールの `bad_free` にしか出ず、状態を跨いで撃ち分けられない |
| `dma_chan_setup` / `irq_register` / `kmalloc` の失敗の巻き戻し | エミュレータでは失敗させられない |

実物を**1 行も写さずに** `#include` する。`include/io.h` だけを
`tools/tests/pcm_hostshim/io.h` で差し替えて、ポート I/O と `irq_save` を模型へ回す
(`tools/tests/hostdrv_hostshim/io.h` と同じ作法)。`dma8237.h` / `irq_math.h` /
`pcm_cs4231.h` は**実物をそのまま**使うので、定数がずれればコンパイルで落ちる。

## 模型

- **装置**: 間接レジスタ 32 本 + R0 / R2 + 経路 `0F40h`。読み取り専用 (I11)、
  ID3-0 が固定の I12、**MCE の外では PEN しか書けない I9** まで真似る。
  `dev_init_busy` を立てると R0 の読みが `0x80` になり、コーデックへの書きが無視される
  (`0F40h` はコーデックの外なので INIT とは無関係 — ここを一緒に無視させると
  「巻き戻しで detach しない」の確認が空振りする)。
- **8237**: 残バイト数を `sim_set_pos(frame)` で置く。`-EAGAIN` も返せる。
- **プール / KHEAP**: リングはプール (16KB)、ステージングは `kmalloc` (16KB)。
  解放・leaked・失敗注入を数える。
- **時計**: `tick_count` と µs (**64 ビット**、`sys_time_now` と同じく lo / hi) を
  手で進める。`sim_tick_per_read` を立てるとポートを読むたびに tick が 1 進む
  (期限切れの経路を有限で終わらせるため)。
- **割り込みの台本** (2026-09-23、Codex 実装レビューの後に追加): 実機では IF=1 の
  命令の境目ならどこでも tick が入る。模型で「入り得る点」は 2 種類:
  (1) `irq_restore` が深さ 0 に戻した瞬間 (shim の `pcm_shim_if_on`)、
  (2) driver が置いた `PCM_PREEMPT(site)` (`pcm_cs4231.h` の `PCM_PP_*`、カーネルでは空)。
  台本は「site に来たら tick を 1 つ保留し、**IF=1 の最初の点で**配る」。IF=0 の区間の
  中の site では配らず、区間を出た最初の点で配る — だから「判定と公開が 1 つの禁止区間」
  なら tick は公開の**後**に、そうでなければ**間**に入る。配った tick は ISR と同じく
  深さ 1 (IF=0) で走る。`PCM_PP_WAIT` (close の停止待ちの 1 周) では時計を 1 tick
  進め、`wait_tick` なら `pcm_tick` も回し、`dev_play` なら装置の位置も 1 tick ぶん
  (441 frame @44.1k) 進める — close を最後まで通せる。

## RED → GREEN

| # | 日付 | RED で見えたこと | GREEN にした変更 |
|---|---|---|---|
| 1 | 2026-09-23 | `close_dl`: `pcm_close_ticks(4096, 22050)` の期待を 51 と書いたが実装は 50 | **試験の期待が誤り**。票の式 `ceil((ceil(staged/2048) + 3) × H / 10) + 3` を手で解くと 50。期待値を直した (票の数として明記されているのは staged = 0 の 17 / 31 だけ) |
| 2 | 2026-09-23 | `seq`: 「`PCM_IFACE_CAL1 \| PCM_IFACE_PEN` を `a` に持つ op は無い」の検査が落ちる | **試験が誤り**。`PCM_IFACE_CAL1 \| PCM_IFACE_PEN` == `0x09` == `PCM_I_IFACE` で、たまたま同じ値だった。意味のない検査なので消し、末尾 2 op を `kind/a/b` で直に照合する形にした |
| 3 | 2026-09-23 | `open`: `dev_init_busy` のとき「経路 `0x1A` が残る」が落ちる | **模型が誤り**。INIT 中の書き無視をポート全部に掛けていた。`0F40h` は C バスの結線でコーデックの外 (DS139PP2 の INIT はコーデックの R0-R3 の話) なので、経路だけ先に通すよう直した |
| 4 | 2026-09-23 | `open` が終わらない (無限ループ) | `pcm_poll` は `tick_count` の差で期限を測るので、模型で時計が止まっていると期限が来ない。`sim_tick_per_read` を足し、期限切れの経路でだけ立てる |

実装側は RED を 1 件も出さなかった (設計 v10 が 9 往復ぶん詰まっていたため)。
**試験の側の誤りが 3 件、模型の不足が 1 件** — 記録としてはそちらが本体。

### Codex 実装レビュー (2026-09-23) の後 — foreground と tick の競合

上の 21 ケースは状態を手で進めていたので、foreground と tick の**割り込み順序**を
1 つも見ていなかった (レビューの非 blocker)。台本 (模型の節) を足して 7 ケースを
書き、**修正前の driver (`f2d1e78`) に同じ位置の `PCM_PREEMPT` だけを差し込んで**
回した (差し込み点: close の RUNNING / RS_* 分岐の書きの直前、期限切れの
`state = STOP_REQ` と `pcm_stop_entry()` の間、停止待ちのループ)。既存 21 は GREEN のまま。

| # | ケース | RED (修正前 `f2d1e78`) | GREEN にした変更 |
|---|---|---|---|
| 5 | `race_reclaim` | reclaim が IF を戻した瞬間の tick が RS_RESTART を実行: unmask 1 回・PEN=1・IEN=1 のまま free。RUNNING / DRAINING / RS_STOP / STOP_REQ でも tick が装置を読み書き (`isr_io` > 0) | blocker 1: 禁止区間の中で `s_claimed = 1` と終端状態 (証拠あり STOP_DONE / 無し FAULTED) を置いてから IF を戻す。`pcm_advance` は `s_claimed` なら装置に 1 度も触らない。`pcm_release` も立てる |
| 6 | `race_close_rs` | close が RUNNING を読んだ後の tick が喪失で RS_STOP (PEN=0) → close が DRAINING で上書き。停止待ちの間「DRAINING なのに PEN=0」を観測 (`probe_bad` > 0) | blocker 2: 状態の判定と DRAINING / `close_pending` の公開を 1 つの `irq_save` の中で |
| 7 | `race_close_restart` | close が RS_RESTART を読んだ後の tick が restart を完了 (RUNNING) → close は `close_pending` だけ立てる → RUNNING が見ずに再生し終えても期限切れ、rc = IO、`df_site` = 4 | blocker 2: 同上。加えて `advance_run` が RUNNING + `close_pending` を DRAINING にする (二重の守り) |
| 8 | `race_close_timeout` | 期限切れで STOP_REQ を公開 → 入口の前の tick が古い期限 (open の 0) と DRS=1 で FAULTED (site 2) → リング leaked、再 open は IO | blocker 3: 状態の公開と `pcm_stop_entry()` (新しい期限) を 1 つの `irq_save` の中で。`pcm_stop_entry` は FAULTED / claim の後は書かない。2 段目の期限切れも STOP_DONE を FAULTED で消さない |
| 9 | `race_volume` | 判定の後 (I6 と I7 の間) の tick が FAULTED にし、その後で I7 を書く | blocker 5: 判定と I6 / I7 の書きを 1 つの `irq_save` の中で (`cs_write_locked`) |
| 10 | `clock` | 起動 40 分後の open: 下位 32 ビットの差を巻き戻りと読んで時計が 0 に張り付き、15 tick 止めても RUNNING のまま (番犬が死ぬ)。50 分空けた再 open、2^32 µs 跨ぎでも同じ | blocker 4: 64 ビットで比べ、open と TIMEBASE (start / restart) で基準を置き直す |
| 11 | `write_contract` | `pcm_write(NULL, 4)` が NULL から写して SIGSEGV | 非 blocker: driver に `pcm_write_check()` (owner と状態) と NULL の拒否。KAPI wrapper は検査 → NULL → 範囲 → 長さの順 |

## 変異 (否定側)

`--mutate` は `drivers/pcm_cs4231_math.c` を**写しの上で 1 か所だけ**壊し、
どれか 1 つのケースが RED になることを見る。24 本。

**driver の変異 6 本 (25〜30、2026-09-23)** は上の競合の修正を 1 つずつ元に戻す
(1 本に複数の置き換えを持てる)。どれも交互の台本のケースで RED。

- 25: reclaim の `s_claimed` のガードと終端状態の両方を外す — **片方だけ外しても
  GREEN** (もう片方が守る。二重の守りなので、変異は両方を外す形にした)
- 26: close の入口の読みを禁止区間の外へ / 27: STOP_REQ の公開と停止の入口の間で IF を戻す
- 28: 時計を下位 32 ビットの差の推測に戻し、基準も置き直さない
- 29: set_volume の書きを禁止区間の外へ / 30: write の NULL 拒否を外す

**単独では観測できない守り** (防御として残す): `advance_run` の「RUNNING +
`close_pending` → DRAINING」は、入口が原子的なら RS_RESTART の完了と入口の
どちらの順でも到達しない。`pcm_stop_entry` の「FAULTED / claim の後は書かない」も、
呼び手が全部禁止区間の中で状態を見てから呼ぶので到達しない。票 §2-3 が名指しする順序の変異 5 つ
(I14 を I15 より先 / MCE 無しの I9 / MODE2 前の I24 / DRS を待たない mask /
期限を毎 tick 入れ直す) はすべて RED。

**変異にできなかった 1 つ**: 完了条件の `c->stg.staged == 0U`。
`frames > 0` のときに `last_data_half` を置き直して段階を `WAIT` へ戻す規則
(変異 11) と重なっており、`pcm_obs` の中だけを見れば到達できない
(残りがあれば必ず補充され、段階が戻る)。**票どおり両方を残した上で**、
変異は観測できる側 —「完了の遷移は DRAINING のときだけ」— に置き換えた。
`staged == 0` は防御であって、単独で試験できる条件ではないと記しておく。

## ケース一覧

| ケース | 見るもの |
|---|---|
| `cont` | 連続性の判定表 (h0 × h1 × p の全組)。1 → 0 の折り返しが正常、同じ半分で戻れば喪失。**2 半周期以上の空白は保証外**であることも期待に書いてある |
| `refill` | `REFILL_MARGIN` の境目 (ちょうど 512 は通る、511 は通らない) |
| `drain` | drain の 3 段階の遷移 |
| `drain_short` | `write(1 frame) → close` の丸ごと 1 周 |
| `start` | `pcm_start` の配り方と drain の初期段階 (2048 未満は「読んでいる」から) |
| `rate` | レート → I8 (`0x5B` / `0x57`)、半周期、番犬の期間 |
| `close_dl` | close の期限式 (staged と rate) |
| `vol` | percent → 減衰とミュート、101 以上の拒否 |
| `stg` | ステージングの予約 / 公開 / 消費、物理末尾の 2 分割、**2047 + 2 + 消費 + 4096** の反例 |
| `pack` | カウンタ 3 本の詰め方と飽和 |
| `pos` | 残バイト数 → リング全体の frame 番号 |
| `obs` | 1 回の観測の統合 (切り替え・underrun の順・喪失・`-EAGAIN`・番犬・drain・完了) |
| `seq` | 初期化列 / start / 停止の入口 / 後続 tick / RS_RESTART / abort の**順序** |
| `open` | 検出・巻き戻し (NOSYS / NOMEM / BUSY / IO)、`irq_save` の釣り合い |
| `write` | frame 倍数の切り捨て、空き、満杯からの自動開始、owner 照合 |
| `close` | 未転送なしの即時解放、drain の 1 周、DRS 待ち、期限切れ → FAULTED → leaked |
| `rs` | 喪失 → RS_STOP → RS_RESTART → RUNNING、RS の期限切れ → STOP_REQ |
| `guard` | 入口ガードで装置アクセス **0 回**、PI の ack と `IRQ_HANDLED` |
| `reclaim` | 全状態からの回収が **1 度だけ**、他人の ID では動かない、FAULTED は leaked |
| `volume` | I6/I7 の両方に入ること、owner 照合 |
| `init` | 起動時の検出。装置が無くても CLOSED のまま、tick が装置を触らない |
| `race_reclaim` | reclaim が IF を戻した瞬間の tick が装置に 0 回、再始動しない (blocker 1) |
| `race_close_rs` | close の入口と RUNNING → RS_STOP の tick。止まった装置の DRAINING が無い (blocker 2) |
| `race_close_restart` | close の入口と RS_RESTART の完了の tick。最後まで再生して rc = 0 (blocker 2) |
| `race_close_timeout` | close の期限切れと tick。古い期限で FAULTED にしない、リングを返す (blocker 3) |
| `race_volume` | set_volume と FAULTED にする tick。FAULTED の後の書きが 0 回 (blocker 5) |
| `clock` | 起動 40 分後の初回再生・50 分空けた再 open・2^32 µs 跨ぎで番犬が効く、代用時計で戻らない (blocker 4) |
| `write_contract` | 未 open・非 owner・NULL は bytes = 0 でも負 (非 blocker) |

## ここで確かめていないこと ([V4])

- 実際に音が出ること、PI の累積回数、左右の取り違え → 票 E2〜E5 (NP21/W、PM)
- INIT 中の書き無視・校正時間・XTAL2 の有無・auto-init ビット → 票 E6 (実機)
- `dma_chan_remaining` の合成そのもの → `tools/tests/test_dma8237.py`
- 割り込みが入り得る点は模型では `irq_restore` と `PCM_PREEMPT` だけ。**置いていない
  場所の競合はこの試験では見えない** — 見ているのはレビューが名指しした 4 つの順序
- IF=0 の実時間 (restart のリングクリア 16KB + 写し 16KB) → NP21/W / 実機の測定 (未)
- KAPI wrapper の本体 (`kapi/kapi_generated.c`) はホストで回していない。driver 側の
  `pcm_write_check` / NULL 拒否だけを `write_contract` で見る
