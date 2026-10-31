# kdebug/gfinit_exec.c — 設計
> 状態: **現行 (2026-10-30)** — 出所: GF_DRIVER_DESIGN (2026-10-29 確定、D0 で移した)
ソース: `kdebug/gfinit_exec.c`

context は head/skip、有限の呼出し/反復 stack、ROM 借用参照、既存上限・時計・`gf_io`。bus は read/write（BAR/offset/width/値）、wait_us、point、局所復元。診断の記録・承認は adapter が担当する。
診断の段別承認/記録/中断/S0 は adapter に残し、C plan・A1/A4/VPLL/virtual が使う汎用機能は維持する。
旧 plan executor の丸ごと移植や恒久的な二重実装はしない。
保存した差分 trace/制御 case/診断段別承認・記録・中断/S0/C-only退行で統一を確認。

## GF6200_4A_DESIGN

- 制御状態は `{pc,execute,head,call_frames,repeat_frames}`。executeのbit0=試行許可、bit1=skip、bit2=force（`/home/hight/os32-tmp/refs/geforce6200/nouveau/drivers/gpu/drm/nouveau/nvkm/subdev/bios/init.c:59–81`）。forceは**書込許可・許可行・phaseを迂回しない**。

- skip中のHW読取は0、HW書込・TIMEは無し。NOT/RESUME/構造制御は進め、RESET/COMPUTE_MEMのforceを再現。条件は真でもskipを解除しない。SUB後の条件状態・headは共有する。

- 条件: `(R32&m)==v`。IO flag: `s=(VGA[index]&m)>>shift; (ROM[data+s]&dmask)==v`。欠けた表を偽として続行せず異常終了する。

- mainごとにexecute=1・head既定0で開始（`/home/hight/os32-tmp/refs/geforce6200/nouveau/drivers/gpu/drm/nouveau/nvkm/subdev/bios/init.c:2323–2339`）。main番号順0→4を1コマンドずつ。SUB/CALLは同コマンド内、途中再開や任意pc開始は提供しない。

- 上限はmain1本につき動的4096命令、call深さ16、repeat深さ8、待ち累計5秒、送信を除く実行経過60秒。各内部MMIO・macro要素・repeatも予算を減らす。0回repeatは静的拒否、再帰・終端無しはhostで拒否を検証。

> [要確認] 現行は診断も hwdev/gf/runner.c を使い、main 境界の 5 node を生成する。原文の静的展開・ordinal・verify 規則を現行実装と同一とは扱えない。

| 対象 | 実装・根拠 |
| --- | --- |
| TIME | GPU PTIMERは初期化中なので時計にしない。OSのsys_time_now＋cpu_delay_usで少なくとも指定µsを待つ。校正済みを確認、分割待ちで予算を検査。長い待ちは切上げ可、切下げ不可 |

## GF6200_4A_S2_LAYOUT

CR44へのSELECT/DATAはhead0のtarget、DATA後は値3ならhead1、それ以外head0。skip中もnouveauのCR44処理によるhead変化を制御状態に反映するがアクセスしない。

Qの読値は実行器の当該命令内の条件計算にだけ保持する。CのIMMへ読値や比較結果を詰めない。leaveは辺だけ検査し条件再評価を加えない。

## HWDEV_DESIGN

検査済み plan 実行

## REALHW_RECOVER_ADDENDUM

- 実行器はS2が展開したforward planをmain入口から次main直前まで進め、call/repeatの入口・復帰Cで深さを再検査する；4096枠は通ったnodeとEXPR内部READに課金し、静的な分岐総数を課金しない。

- S3の時計入口は校正済みの実行累積µs（実測UART送信時間を除く）を成功/失敗付きで受け、待ちは最小時間保証・有限pollを持つcallbackへ1000µs以下に分割する
