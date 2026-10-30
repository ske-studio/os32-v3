# kernel/console/console.c — テキストコンソールの表示とカーソル

> 状態: **現行 (2026-10-31)**

文字出力と論理カーソルを管理し、表示操作を backend に渡す。
GUI や V86 からの復帰時にも表示状態を整える。

## 落とし穴

- V86 が変更したカーソル状態を戻さないと、古い位置で点滅が残りうる。
  可視状態は CSRFORM の DC ビットで制御し、論理位置と同期する
  (`kernel/console/console.c:394`, `kernel/console/console.c:399`)。
  V86 の CUI 復帰は `kernel/v86/v86_io.c:241` から表示復元へ進む。
  復元は `platform/pc98/display.c:36` でカーソルも再設定する。
  V86 復帰時のカーソル同期を直接守る専用試験は確認できていない。
