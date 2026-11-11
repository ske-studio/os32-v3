# PPC_SHELL_EXEC_DESIGN — PowerPC の最終の到達点 S-B (外部の USER アプリを起動し終了して呼び手へ戻る) の詳細設計

> 状態: **確定 (2026-11-11)** — USER の ABI (入口・sc の syscall・rfi と例外・子だけの終了)、OS32X の LE の wire を BE の CPU で読む直し、命令の公開の cache 同期、KAPI の 8B の stub、PPC の SDK (crt0・link・libgcc を link しない)、既存の exec の経路を PPC でも通すための facade の段 (F01〜F20)。全体の計画と中間 S-A は PPC_SHELL_DESIGN。段の状態は票 TASK_PPC_SHELL が持つ。

> 出所: ~/os32-tmp/run/ppc-shellb/DESIGN.final.md (gpt-6-astra 起草 r1→r2、Opus 5.5 の第 1 回 Request changes・第 2 回 条件付き Approve、PM が N1〜N4 を反映。R4 は PM 決定 (a) = 既存の exec の閉包を facade で切る、本線 PM も賛成)。中身は変えていない。

