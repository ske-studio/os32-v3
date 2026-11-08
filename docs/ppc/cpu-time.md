# Time Base と Decrementer

> 上位: docs/ppc/README.md


TB は上位・下位を合わせて読む時刻カウンタ。
NetBSD の 32 ビット側は `mftbu` → `mftb` → `mftbu` と読み、上位が変わればやり直す
(N/sys/arch/powerpc/include/cpu.h:285)。下位だけでは桁上がりを扱えない。

DEC は SPR `0x016` (N/sys/arch/powerpc/include/spr.h:133)。
NetBSD は TB の周波数を `ticks_per_sec` として、`ticks_per_sec / hz` を DEC に設定する
(N/sys/arch/powerpc/powerpc/clock.c:108)。割込み処理では DEC の遅れを読んで
周期を加え、次回値を書き戻す (同 :149)。単に毎回一定値に戻す実装とは異なる。
周波数は OF の CPU ノードの `timebase-frequency` から取得する
(N/sys/arch/powerpc/oea/ofwoea_machdep.c:304)。CPU クロックと同値とはしない。
MPC7450UM 表 1-3、4.6.9 (Rev 3.1) では DEC の MSB の 0→1 が例外条件。
配送には EE=1 と上位優先例外が無いことが必要で、0x900 に入る。
TB/DEC の歩進は bus clock の 1/4 (同 2.3.5.1、4.6.9)。
7455 では外部 TBEN 信号と HID0[TBEN] の両方を有効にする (同 表 2-6)。
nap では TB は動作を続けるが、sleep とは区別する (同 表 2-6、10.2)。
OF から読む周波数とこのクロック源を区別し、CPU core clock から直接換算しない。
PEM 6.4.9 は要求が例外を取るまで保留され、複数要求は 1 回にまとめられると定める。
ソフトウェアによる DEC 書込みで MSB が 0→1 になった場合も要求を発生する。
[未] sleep 復帰と CPU 間 TB 同期。
