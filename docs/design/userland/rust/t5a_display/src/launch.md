# userland/rust/t5a_display/src/launch.rs — 設計

> 状態: **現行 (2026-11-18)** — 出所: TASK_T9_sh (§1 D4・D9、§1a の ABI 表) の契約を今のソースと照らして要約
ソース: `userland/rust/t5a_display/src/launch.rs` (KAPI を呼ぶのは `userland/rust/t5a_display/src/guest.rs`)

端末 (t5a_display) が起動要求表 (KAPI `launch_req`・`launch_poll`・`launch_cancel`) から返った値をどう読み、次に何をするかを決める純粋な部分。KAPI の呼び出しと配線は `guest.rs` にあり、ホストでは動かない。

## 端末が守ること

- 接続モード (子が走っている間) の進み具合は `launch_poll` の答えだけで決める。con_sink の `EXIT` レコードは表示にとどめ、モードの判定に使わない。
- 接続モードの ESC は `launch_cancel` を出す。子には打鍵を注がない。
- 端末自身の終了や `exit` では取消を出さない (要求者の退場はカーネルが孤児回収する)。
- 完了 (`DONE`・`FAILED`) は `launch_poll` が消費して初めて表が空く。消費しないまま token を捨てると、以後の `launch_req` が `OS32_ERR_FULL` に固着する。

## status の読み方 (`phase`)

| status (`launch_poll` が書く) | `Phase` |
|---|---|
| `0x000` (`ST_PENDING`) | `Pending` |
| `0x001` (`ST_TAKEN`) | `Taken` |
| `0x100 + 子 ID` (`ST_RUNNING`) | `Running(子 ID)` |
| `0x200` (`ST_DONE`、下位は無視) | `Done` |
| `0x300 + (-rc)` (`ST_FAILED`) | `Failed(rc)` (符号を戻した元の負の rc) |
| 上位が 0 で下位が 0・1 以外、上位が知らない値、負の status | `Unknown` |

## 1 件の進み (`Attach`)

`launch_req` が返した token (> 0) で始める。子 ID は `Running` で分かったものを控える (表示だけに使う)。

`poll(rc, status)`:

| 入力 | 結果 |
|---|---|
| `rc < 0` (理由を問わない) | `Finish(Lost(rc))` — プロンプトへ戻る |
| `Done` | `Finish(Done)` — 何も出さない |
| `Failed(code)` | `Finish(Failed(code))` — `launch failed (<code>)` を出す |
| `Unknown` | `Finish(Lost(status))` — 接続モードに居座らない |
| `Running(child)` | 取消が再試行待ちなら `Retry`、子 ID が変わったら `Redraw`、それ以外 `Idle` |
| `Pending`・`Taken` | 取消が再試行待ちなら `Retry`、それ以外 `Idle` |

取消 (`Cancel`: `No` → `Retry` → `Armed`):

- `escape()`: `No` のときだけ真を返して `Retry` にする (`launch_cancel` を出す)。既に出していれば偽 (ESC の連打でも 1 回にまとめる)。
- `cancelled(rc)`: `OS32_ERR_AGAIN` (WM がまだ取っていない = `PENDING`・`TAKEN`) なら `Retry` にして次の poll (タイマ) で再試行する。`RUNNING` になれば通る。0 (取消が積まれた) と `OS32_ERR_STALE` (もう完了している) は `Armed`。どちらも token を捨てず、`launch_poll` が `DONE`・`FAILED` を消費するのを待つ。token が本当に合わなければ poll が負を返して `Lost` で戻る。いずれも `Redraw`。

## 最下行

接続モードの最下行は `[running id=<子>] ESC=cancel`、取消中は `[cancelling id=<子>] wait` (`prompt.rs` の `attached_row`)。負の戻り値は `message_rc` で `launch failed (-13)` の形に添える。

## 検査

`make check-t5a-host` (`userland/rust/t5a_display/host_tests`、`#[path]` で `launch.rs`・`prompt.rs` を取り込む)。`launch.rs` の試験はカーネルの要求表の最小の写し (`Table`。完了は poll が消費して初めて IDLE) を置き、ESC の直後に子が終わっていても表が固着しないことを見る。
