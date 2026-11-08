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
[未] DEC の符号境界での例外発生と保留、EE による配送条件、休止時の TB の動作、
CPU 間の TB 同期はメーカー資料で確定する。
