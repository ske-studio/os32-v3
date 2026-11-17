# exec/ring3_str.c — ユーザー向け文字列とアクセス判定の補助

> 状態: **現行 (2026-10-31)**

CPL=3 に返す文字列を写し、ページ権限とガードの判定を補助する。
実際のページ走査と実行文脈は呼出し元が管理する。

## 落とし穴

- WM はアプリの syscall の中でも動くため、syscall 中というだけで WM の出力先を
  ユーザーポインタ扱いすると、常駐シェルのスタックを拒否してアプリを落とす。
  `exec/ring3_str.c` の `ring3_guard_active()` は WM 深さを見て現在の呼出し文脈を判定する。
  一方、以前登録したバッファは登録時の由来を保持して検査する
  (`fs/fd_redirect.c`, `exec/redir_access.c`)。
  `make check-ring3-guard-host` と `make check-access-host` が各経路を検査する。

## 内部設計と契約 (D1)

`RING3_LS_SHIM_OFF` は式を変えず `exec/ring3_str.h` へ移し、構築側と移植先の共通定義にする。

ring3_guard_active / ring3_user_str

## `ring3_guard_active` — いまの呼び出しをアプリ由来と見るか

> 出所: TASK_KAPI_OUTPUT_GUARD (§6 の追補) の契約を今のソースと照らして要約 (2026-11-18)

ソース: `exec/ring3_str.c` (`ring3_guard_active`)。呼び手は `exec/exec.c` の 3 つの門 (`ring3_user_ranges_writable`・`ring3_user_range_ok`・`tramp_copy`) と `ring3_call_from_user`。

WM (gshell、CPL=0 の常駐シェル) はアプリの syscall の中で走るので、その間も `ring3_in_syscall` は 1 のまま。`ring3_in_syscall` だけで「アプリ由来」と決めると、WM が自分のスタックの番地 (シェル帯、USER が無い) を KAPI の出力に渡したときにアプリの PD で拒否され、アプリが kill される。判定は WM の深さ `ring3_wm_depth` と組で行う。

| `in_syscall` | `wm_depth` | 戻り値 | 意味 |
|---|---|---|---|
| 0 | 何でも | 0 | CPL=0 の直呼び。検査しない |
| 1 | 1 以上 | 0 | WM の文脈。常駐側の直呼び扱い |
| 1 | 0 | 1 | アプリ由来。検査する (不正なポインタは今までどおり kill) |
| 1 | 負 | 1 | 壊れた状態。安全側に倒して検査する |

- 深さが素通しにするのは **WM がいま渡すポインタ** だけ。アプリが前に登録したバッファ (`fd_redirect` の出力先) は、登録時の由来を `exec/redir_access.c` が保持し、深さに関係なく表を歩く。
- #PF/#GP の帰属 (`arch/x86/kernel/isr_handlers.c`) は `ring3_in_syscall` だけを見る。この判定はそこを変えない。

検査: `make check-ring3-guard-host` (`tools/tests/test_ring3_guard.py`、実物を `#include`) の表の段。
