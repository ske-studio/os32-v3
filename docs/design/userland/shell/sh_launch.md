# userland/shell/sh_launch.inc — 設計 (sh.bin の起動待ち)

> 状態: **現行 (2026-11-18)** — 出所: TASK_T9_sh (§1 D3a) の契約を今のソースと照らして要約
ソース: `userland/shell/sh_launch.inc` (`sh_launch`。`SHELL_AS_APP` の `main.c` が取り込む)

GUI 端末の中の `sh.bin` は入れ子の `exec_run` を使えない。外部プログラムは起動要求表 (KAPI `launch_req`・`launch_poll`) に積み、WM が別アプリとして起こす。ここは要求を出してから結果が出るまでの待ち方と、利用者への表示の約束。結果の種別と `$?` への写し方は `docs/design/userland/shell/exit_status.md` の「2. 外部プログラムの結果」。

## 送る前に断るもの

| 条件 | 表示 | `launch_req` |
|---|---|---|
| cmdline が `LAUNCH_CMDLINE_MAX` (256) バイト以上 | 断りの印 (`sh_refuse`) と上限 (255) | 呼ばない。255 バイトちょうどは通す |
| パイプの段の中 (`sh_pipeline_depth > 0`) | `sh: pipe to external command is not supported` | 呼ばない |
| 標準 FD にリダイレクトを張っている | `sh: redirect to external command is not supported` | 呼ばない |

## `launch_req` が受け付けなかったとき

| 戻り | 表示 |
|---|---|
| `OS32_ERR_INVAL` (GUI 外) | `sh: external programs need the GUI terminal` を **1 度だけ** (2 回目からは黙る。起動が 1 度通ったら印を下ろし、次に GUI 外へ落ちたらまた出す)。PATH の走査は止める |
| `OS32_ERR_FULL` | `sh: <名>: busy` |
| その他の負 | `sh: <名>: launch_req failed (<rc>)` |

どれも poll も `sys_yield` もせず負を返す。`<名>` は cmdline の先頭語 (64 バイトまで)。

## 待ち

- token を受けたら、`sys_yield()` で 1 回譲ってから `launch_poll(token, &st)` を読む、を繰り返す (poll ごとに 1 回譲る。譲ってから問い合わせる)。GUI 中の `sys_yield` は必ず `WAIT_POLL` へ park し、その間に WM が要求を取って子を起こし、回収を表へ返す。
- 待っている間は `kbd_getchar`・`kbd_trygetchar`・`ime_getkey` を呼ばない (鍵待ちへ park すると注入リングの 1 バイトを子と奪い合う)。
- 状態 (`st & 0xF00`):
  - `LAUNCH_ST_PENDING`・`TAKEN`・`RUNNING` → 待ちを続ける。
  - `LAUNCH_ST_DONE` → `EXEC_SUCCESS` (0)。何も表示しない。
  - `LAUNCH_ST_FAILED` (`0x300 + (-rc)`) → `-rc` をそのまま返す (下位が 0 なら `EXEC_ERR_GENERAL`)。`EXEC_ERR_NOT_FOUND` と `EXEC_ERR_GENERAL` では表示しない (PATH の候補の数だけ同じ行が出るため)。それ以外は `sh: <名>: launch failed (<rc>)`。
- `launch_poll` が負 (`OS32_ERR_STALE` など) → `sh: <名>: launch_poll failed (<rc>)` を出して抜け、`EXEC_ERR_GENERAL` を返す (回り続けない)。

## 検査

`make check-sh-exec-host` の `tools/tests/test_sh_launch.py` (`sh_launch_host.c`)。`sh_launch.inc` をそのまま取り込み、`launch_req`・`launch_poll`・`sys_yield`・`kprintf` だけを差し替え、kbd・ime の呼び出しに印を付けて 1 度も呼ばれないことを見る。
