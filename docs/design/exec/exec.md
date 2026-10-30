# exec/exec.c — アプリの起動・復帰と資源回収

> 状態: **現行 (2026-10-31)**

アプリの実行文脈を切り替え、終了時に所有資源を回収する。
ユーザー syscall と WM の実行文脈も管理する。

## 落とし穴

- 呼出しをまたいで ESP などを呼出し側破壊レジスタだけに置くと復帰先のスタックが壊れる。
  現行の `exec_setjmp` は必要なレジスタと文脈の深さを保存し、`exec_longjmp` で復元する
  (`arch/x86/kernel/setjmp.asm:30`, `arch/x86/kernel/setjmp.asm:55`)。
  復帰後に使う文脈は呼出し前に控え、保存場所の寿命を守る (`exec/exec.c:347`)。
  実行文脈の回帰は `tools/tests/test_exec_r1.py` で確認する。

- 終了時に全 FD やパイプを回収すると、親や常駐処理の資源まで壊す。
  `exec/exec.c:1054` は終了する owner の DB、FD、パイプを回収し、保護 FD を除外する。
  DB の rollback に FD が要るため DB の回収を FD より先にする。
  所有者別回収は `tools/tests/test_owner_reclaim.py` で確認する。

- syscall 中の WM の fault も、その syscall を出したアプリに帰属する。
  画面に例外が出ずアプリが消える場合、`fault_kill_count` とともに
  `ring3_wm_fault_count` を確認する (`exec/exec.c:1508`)。
  WM 深さを消す前に後者を数える契約は `make check-ring3-guard-host` が検査する。
