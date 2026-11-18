# exec/exec.c — アプリの起動・復帰と資源回収

> 状態: **現行 (2026-10-31)**

アプリの実行文脈を切り替え、終了時に所有資源を回収する。
ユーザー syscall と WM の実行文脈も管理する。

## 落とし穴

- 呼出しをまたいで ESP などを呼出し側破壊レジスタだけに置くと復帰先のスタックが壊れる。
  現行の `exec_setjmp` は必要なレジスタと文脈の深さを保存し、`exec_longjmp` で復元する
  (`arch/x86/kernel/setjmp.asm`, `arch/x86/kernel/setjmp.asm`)。
  復帰後に使う文脈は呼出し前に控え、保存場所の寿命を守る (`exec/exec.c`)。
  実行文脈の回帰は `tools/tests/test_exec_r1.py` で確認する。

- 終了時に全 FD やパイプを回収すると、親や常駐処理の資源まで壊す。
  `exec/exec.c` は終了する owner の DB、FD、パイプを回収し、保護 FD を除外する。
  DB の rollback に FD が要るため DB の回収を FD より先にする。
  所有者別回収は `tools/tests/test_owner_reclaim.py` で確認する。

- syscall 中の WM の fault も、その syscall を出したアプリに帰属する。
  画面に例外が出ずアプリが消える場合、`fault_kill_count` とともに
  `ring3_wm_fault_count` を確認する (`exec/exec.c`)。
  WM 深さを消す前に後者を数える契約は `make check-ring3-guard-host` が検査する。

## 内部設計と契約 (D1)

- execではSTOP掃除の位置だけ転送。`kbd_inject_owner_exit(id)`（1091）とdiscard（1112）の順序・対象ownerを変更しない。

| 時点 | 誰が | 何を |
|---|---|---|
| アプリの起動 | exec | 私有ページの**最小量** (code/BSS・スタック・ヘッダの heap_size 分)・PD/PT (lease 窓の PT 1 枚を含む)・shlib data の複製 |

| 時点 | 誰が | 何を |
|---|---|---|
| アプリの終了 | exec (通常文脈) | owner=その AS のページを全部回収 (R5)。強制脱出でも回収はここ (R1) |

  - **強制脱出 (CTRL+STOP・番犬・gfx 拒否) の契約**: IRQ が行うのは `abort_req` を立てること (要求) と、割り込まれた文脈が CPL=3 のときに**割り込みフレームを捨てて exec_run の復帰点へ制御を移すこと** (制御移譲) の 2 つだけ。AS の破棄・物理の返却・owner ごとの回収 (`exec_teardown_app`、`exec_reclaim_owned` → `pcm_reclaim` …) は、復帰点で IF=1 に戻してから **通常文脈** (§3-5-3) で行う。

  1. **起動時に確保するのは最小量**: code/BSS・スタック (既定 256KB、OS32X ヘッダで可変)・ヘッダの `heap_size` 指定分 (未指定 = `MEM_EXEC_HEAP_MIN` 64KB、`memmap.h`)・shlib data の複製・PD/PT。これが取れなければ起動を拒否する。予算の決まったアプリ (ゲーム等) は `heap_size` で起動時に確保しきる — 旧 R2 の「起動時に確保しきる」はこの形として残る。

- execは「計算できるtrampアドレス」と「初期化済みring3_tramp_page」を区別する。
- `exec/exec.c` のexec_tramp_page_addrはraw配列の整列番地を返す。初期化済み判定の代用は禁止。

exec/exec.c
C3は外部プログラム向けで、この内部import用にkapi層や__cdeclを足さない。
exec実体は2フィールドの写し

execのwm_enter/leaveをFUNCとして呼ぶ。既存GETTER/SETTERは他のkselftest用に維持
ring3_wm_depth_underflow / ring3_wm_shell_gen
ring3_wm_leave内に残す。

| 記号 | 元の種類・操作 | r2での扱い |
|---|---|---|
| kapi / ring3_tramp_page | static読出し | 新FUNC kdi_selftest_exec_viewで取得 |

### T6. syscall 境界ポンプ (「1 アプリが戻らないと止まる」対策)

- カーネルは int 0x80 の入口で gshell が登録したフック `gui_pump()` を**上限付きで**呼ぶ
  (入力の取り込みとリングへの追記だけ。描画はしない)。アプリが KAPI を呼ぶ限り WM は
  入力を失わない。
- 純粋な計算ループの間は効かない。ISR 側の強制脱出キー (CTRL+STOP) で

(`ring3_abort_request` / `ring3_abort_check`、IRQ1 で CS.RPL=3 のときだけ即時、KAPI 実行中
なら安全な syscall 境界で畳む)。GUI KAPI-loop は KAPI 完了後に WAIT_POLL へ退避し、
  WM top-level の既存宛先解決 → exec_kill へ返す。OP_WAIT 中は既存 abort_req 経路を保つ。
  X3 は宛先リングに空きがなくても raw を読み、STOP make 以外を dropped / OVERFLOW として捨てる。
  raw 満杯時の CTRL+STOP make は末尾を置換し、捨てた1件を数える。X4 の空き待ちは維持する。
  CUI でも CTRL+STOP で CPL=3 アプリが kill される。

  [本体][sbrk (最低 256KB)][ガード][exec_heap]〜スタックガード直下、の順に動的に決める。
  exec_heap の大きさは OS32X ヘッダの heap_size 指定があればそれ、0 なら空きを折半。
  併せて、CPL=3 の sbrk 上限が RING3_HEAP_TOP になっていて sbrk が exec_heap に
伸び得た重なりも解消 (上限 = ガード直下)。

> [要確認] 現行 exec/exec.c の AS 配置と exec/exec_heap.c は appmem による伸長を使う。原文の最低 256KB・空き折半が現行の全経路に適用されるか確認する。

| 採用 | 解く欠点 | 386 でのコスト |
|---|---|---|
| **syscall 境界で WM の入力ポンプ** + ISR の強制脱出キー | 1 アプリが机を止める | プリエンプション無しの範囲で最善。純粋な計算ループには効かない |

## stdin の KBD 再開

未完了の sys_read は AppSlot の進捗を保持し、KBD 再開時に文字を残して syscall を再実行する。
1文字を EAX に返す扱いは単独の getchar/getkey に適用する。
状態の寿命とフレーム復帰の契約は [exec_stdin](exec_stdin.md) を参照。

## WM の深さ `ring3_wm_depth` の寿命

> 出所: TASK_KAPI_OUTPUT_GUARD (§6 の追補) の契約を今のソースと照らして要約 (2026-11-18)

ソース: `exec/exec.c` (`ring3_wm_depth`・`ring3_wm_enter`・`ring3_wm_leave`・`ring3_context_save`・`ring3_context_clear`・`ring3_context_restore`)・`exec/exec_syscall.inc` (ディスパッチャの入口)・`exec/exec_fault.inc` (`ring3_kill_kind`)・`arch/x86/kernel/isr_handlers.c`。

「カーネルが WM のコードへ入っている深さ」。判定の表は `docs/design/exec/ring3_str.md` の `ring3_guard_active` の節。

- 増減は `ring3_wm_enter` / `ring3_wm_leave` の対だけ (呼び手は `kernel/gui.c` の `gui_call`・`gui_owner_exit` と syscall 境界のポンプ)。入れ子は 2、3 と数え、戻るたびに 1 ずつ減る。
- 対になっていない `leave` は深さを 0 で止め (負にしない)、`ring3_wm_depth_underflow` を数える。
- **不変条件: CPL=3 の syscall は必ず深さ 0 から始まる。** ディスパッチャの入口 (`exec/exec_syscall.inc`) が `ring3_wm_depth = 0` を書く。WM を longjmp で抜けて `leave` を通らなかった (park・kill・exit) 深さが残っても、次の syscall で 0 に戻る。残ると 3 つの門がアプリのポインタを素通しする穴になる。
- longjmp をまたぐ起動・復帰は、`ring3_context_save` が深さを控え、子の文脈は `ring3_context_clear` で 0 から始め、親へ戻るときに `ring3_context_restore` で控えた深さを戻す。
- WM の中の #PF/#GP や門の拒否はアプリの kill として畳まれる (例外画面は出ず `fault_kill_count` だけ増える)。`ring3_kill_kind` は種別が FAULT で深さが 1 以上なら `ring3_wm_fault_count` を +1 する。**深さを 0 に戻す前に数える** (後に置くと常に 0)。CTRL+STOP (ABORTED) は数えない。
- ISR の 2 つの kill の行 (例外と #PF) は深さが 1 以上なら ` (in WM)` を足す。kill するかどうかの判定は変えない。

検査: `make check-ring3-guard-host` (`tools/tests/test_ring3_guard.py`)。exec.c と ISR はホストで組めないので、`ring3_wm_fault_count` の位置と ` (in WM)` は本文の字面で見る。
