# hwdev/gf/runner.c — 設計
> 状態: **現行 (2026-10-30)** — 出所: GF_DRIVER_DESIGN (2026-10-29 確定、D0 で移した)
ソース: `hwdev/gf/runner.c`

runner が解読後・dispatch 前に REPEAT の `end_repeat` を対応する END_REPEAT PC で補完し、run は body-PC/end-PC/残り回数を frame に置く。命令内の待ちはすべて `gf_init_wait` に通し、累積待ちの一括計上・1000us 以下の分割・各刻みの前後の中断点/期限検査は runner が実装する。
既存の命令/呼出し/反復/累積待ち上限を保ち、命令と wait の刻みに中断点を置く。診断の main 間承認待ちは本番に持ち込まない。
本番全体のソフト上限は既存 `GF_INIT_EXEC_US` の 60 秒を採用（`O/kdebug/gfinit_exec.h:4`）。期限超過は既存失敗出口へ進むが、戻らない MMIO に壁時計上限は保証できない。
旧 plan executor の丸ごと移植や恒久的な二重実装はしない。
実 ROM fixture（27 種/193 命令）の旧展開との I/O trace 差分試験 1 本＋制御個別 case
skip/head、呼出し/反復上限

## 移し替えた設計

- 制御状態は `{pc,execute,head,call_frames,repeat_frames}`。executeのbit0=試行許可、bit1=skip、bit2=force（`I:59–81`）。forceは**書込許可・許可行・phaseを迂回しない**。
- skip中のHW読取は0、HW書込・TIMEは無し。NOT/RESUME/構造制御は進め、RESET/COMPUTE_MEMのforceを再現。条件は真でもskipを解除しない。SUB後の条件状態・headは共有する。
- 条件: `(R32&m)==v`。IO flag: `s=(VGA[index]&m)>>shift; (ROM[data+s]&dmask)==v`。欠けた表を偽として続行せず異常終了する。
- mainごとにexecute=1・head既定0で開始（`I:2323–2339`）。main番号順0→4を1コマンドずつ。SUB/CALLは同コマンド内、途中再開や任意pc開始は提供しない。
- 上限はmain1本につき動的4096命令、call深さ16、repeat深さ8、待ち累計5秒、送信を除く実行経過60秒。各内部MMIO・macro要素・repeatも予算を減らす。0回repeatは静的拒否、再帰・終端無しはhostで拒否を検証。
