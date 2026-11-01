# tools/tests/test_mtrr_wc.py — MTRR WC の模型試験

> 状態: **現行 (2026-11-01)**

実物の `arch/x86/kernel/mtrr_wc.c` を読み、明示した CPU primitive 区間だけを
fake CPUID/MSR/CR/EFLAGS/WBINVD/TLB に置換する。
ID-bit の反転・復元、能力判定、組の選択、設定/復元/rollback は実物を実行する。
実ヘッダと `include/types.h` を使い、freestanding の ILP32 ELF を host32 の
明示した runner で実行する。fixture はこの Python に含む。

能力無しでは MSR write・CR4・WBINVD が無いことを検査する。
最大拡張 leaf、36bit fallback と対応時の上位 mask、PAT2、V=0 最小番号、
非 WC 競合、部分 WC、BIOS WC の借用、保存 BASE/MASK、他組・固定域・PAT の不変を検査する。
設定/復元は PGE と IF の両値について、readback を含む命令と値の列を照合する。
readback の一時失敗と復元も確認できない失敗、release 失敗と二重 release を区別する。
identity 失敗後の借用解除も装置に依存せず同じ CPU 排出を使う。

`--mutate` は一意な文字列を置換し、コンパイル成功後の実行で
明示した `FAIL line` と rc=1 を要求する。コンパイル失敗や signal は RED と数えない。
CHECK の covers は実際に読む・組む CPU ソースとヘッダ、型、host32 のみ。
PTE・台帳・surface の実物の統合試験は後段の T1 が担当し、この試験はそれらを写さない。

## 落とし穴

物理アドレス幅を変えるケースでは BIOS 側の RAM MTRR mask もその幅に合わせる。
36bit の mask を 40bit CPU に流すと、RAM の有効範囲が変わり競合の模型になる。
模型は実機の WC と PCI 書込みの排出を証明しない。
