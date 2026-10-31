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

## 内部設計と契約 (D1)

```c
/* console.cの3箇所: rshell_activeの判定・文字の順序は残す。 */
if (rshell_active) con_output_putchar(*str);
```

- consoleのrshell_activeはconsoleが保持（KAPI setterも維持）。con_sinkへの複写・描画抑止と独立、改行変換は従来serial_putcharに任せる。複写を一括putsに置換しない。

|所有者|残す状態・責務|呼ばれる文脈|
|---|---|---|
|kernel/console/console.c|cursor_x/y、scroll reserve、UTF-8処理、描画抑止、sink/bootlog/rshellの順序|起動前景・通常前景、既存診断の割込み側呼出しも待ち追加なし|

```c
/* kernel/console/console.cの例。抑止と論理状態の所有者は変えない。 */
void tvram_scroll(void) { con_backend_scroll(TVRAM_ROWS - g_scroll_reserve); }
/* clear: sink通知→GUIならreturn→backend_clear→cursor_x/y=0→sync */
/* cursor_sync: v86ならreturn→con_backend_cursor_at(cursor_x,cursor_y) */
```

- start時sink_disable→kbd_inject_discard→GUIだった場合だけclear→enableを保存。clearはV86でも書く現挙動を勝手に抑止しない。
