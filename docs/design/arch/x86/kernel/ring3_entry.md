# arch/x86/kernel/ring3_entry.asm — ユーザー空間への入出と syscall

> 状態: **現行 (2026-10-31)**

int 80h のフレームとセグメントを処理し、ユーザー空間へ戻す。
再開時のアドレス空間と TSS の切り替えも担う。

## 落とし穴

- **syscall 中の割込み**: 割込みゲートで IF が消えたまま KAPI の待機処理に入ると IRQ を待てず停止する。
  通常の CPL=3 経路はカーネルセグメント復元後に `sti` し、USER_DS 復元前に `cli` する (`arch/x86/kernel/ring3_entry.asm:33`, `arch/x86/kernel/ring3_entry.asm:61`)。
  後者は復帰途中の IRQ によるセグメント破壊を防ぐ。VM 経路は別扱いで、`sys_exit` は longjmp で戻る (`arch/x86/kernel/ring3_entry.asm:51`, `arch/x86/kernel/ring3_entry.asm:58`)。
  キーボード待ちと終了の実ゲスト確認が必要。直接の IF 試験は未確認。
