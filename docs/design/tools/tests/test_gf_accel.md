# tools/tests/test_gf_accel.py — 受入の producer と consumer

> 状態: **現行 (2026-11-01)**

実物 gf_accept.c の CPU primitive 区間だけを fake CPUID/MSR/EFLAGS に置換する。
実物 gf_present.c とヘッダで ILP32 ELF を組み、host32/qemu で3方式を動かす。
時計/PCI/PTE/ページ割当て/reportを模型化し、時刻区間中のreportを失敗させる。
生成された生レコードを実物の gf_accel.py に渡し、矩形/byte/point/100×3個/
nearest-rank/失敗除外/元PTE不変/版の拒否/BIOS WC SKIPを照合する。
正常転送後は CLIENT 全面に戻ることを検査する。解放の模型は実契約の成功1・失敗0に
従い、成功後の復元と失敗時の中断を試す。clock/pointの失敗も別経路で試す。

wm.rs の受入専用モジュールをそのまま rustc で組み、mock API で20回のidle、
10回のwarmup、300更新を通す。出力数、通番、total/event/composite/presentの時間と
和を実行で検査する。通常のwmの回帰は既存のgshell-hostが担当する。
さらにgshell全体を既存のhost API/描画/入力模型と組み、実物handlerのCOMMITと
Pump→input→cursor→flush_presentをwm_cycle外で実行する。各経路の10回warmupと
300件、転送だけのtotal=present、周期間の長い描画/待機時間の除外、空COMMIT/
動かないカーソル/ガードされたPumpの除外、計測中の出力なしを検査する。

`--mutate` は実効型照合の削除、BIOS WCのUC群への混入、GUI present計時の削除を
一意な目印で変え、コンパイル成功後の実行で期待違反を確認する。
解放判定の逆転と周期外の計時開始の削除も戻し、CLIENT復元とCOMMIT/Pump両経路の
試験が実行時にREDになることを確認する。
byte store幅の変異は既存のtest_geforce.pyが担当する。
この試験は実CPUのMTRR設定・PCI drain・カメラ表示・GUI全体の台本を代替しない。
