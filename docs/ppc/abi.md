# 32 ビット ABI — BSD で確認できる範囲

> 上位: docs/ppc/README.md

SysV PPC32 と EABI の完全な呼出規約は [未]。
ここでは BSD の実装が示す事実だけを整理する。64 ビット ELF ABI や Darwin の
呼出規約を 32 ビット SysV/EABI と同一視しない。

## レジスタとフレーム

| 項目 | 手元の根拠と確認範囲 |
|---|---|
| r1 | macppc の入口で専用スタックを設定する。N/sys/arch/macppc/stand/ofwboot/Locore.c:72 |
| r3〜r10 | FreeBSD の syscall 定義は first=3、8 本。F/sys/powerpc/include/frame.h:110。C の複合型引数の分類までは示さない |
| 戻り値 | NetBSD のカーネル setjmp は r3=0、longjmp は r3=1 を返す。N/sys/arch/powerpc/powerpc/setjmp.S:66、同 :103 |
| 保存 GPR | 同 setjmp は r1・r14〜r31 と CR/LR/CTR/XER を保存。N/sys/arch/powerpc/powerpc/setjmp.S:36 |
| r2 の扱い | FreeBSD のカーネル setjmp は r2 も保存。F/sys/powerpc/powerpc/setjmp.S:45 |
| r13 の扱い | NetBSD のカーネル入口マクロでは lwp0 を置く。N/sys/arch/powerpc/include/asm.h:241。EABI の SDA 規約を直接示す根拠ではない |
| スタック整列 | NetBSD の STACK_ALIGNBYTES は 15 (16 バイト整列)。N/sys/arch/powerpc/include/param.h:65 |
| フレームの例 | FreeBSD の 32 ビット callframe は先頭が dummy FP、その次が LR 保存場所。F/sys/powerpc/include/frame.h:100 |

BSD 間の差: NetBSD のカーネル setjmp の保存列に r2 は無く、FreeBSD にはある。
そのため OS32 の汎用保存規約の手本として片方をそのまま採らず、
両方を BSD 固有の例として扱う。呼出規約の採用は仕様取得後の設計決定に残す。
CR/CTR/XER を保存する実装から、全てが C ABI の callee-save だとは推論しない。
両 setjmp の f14〜f31/FPSCR はコメントのみで、FPU 保存命令は無い
(N/sys/arch/powerpc/powerpc/setjmp.S:65、F/sys/powerpc/powerpc/setjmp.S:74)。
これをユーザーランドの hard-float ABI の FPR 保存根拠にはしない。

## 型と byte order

NetBSD のコンパイラ既定型を使わない分岐では、int8 は char、int16 は short、
int32 は int、LP64 でない int64 は long long、uintptr は unsigned int
(N/sys/arch/powerpc/include/int_types.h:51、同 :65、同 :78)。
macppc の対象となる big-endian の既定は
N/sys/arch/powerpc/include/endian_machdep.h:3 に示される。
PCI MMIO のエンディアンは CPU の既定と分けて扱う。

[未] `long`・ポインタ・float/double/long double の幅と整列の完全表、
u64 の 8 バイト整列、構造体・union・bitfield の配置は ABI 原典で確定する。
型の幅だけから構造体の整列を推測しない。

## 次に原典で確認する項目

[未] SysV と EABI それぞれの r2/r13 の予約・SDA、r3〜r10 の整数対・構造体の分類、
f1 以降の引数・戻り値、r3/r4 の 64 ビット戻り値、callee-save GPR/FPR/CR、
スタックの backchain・LR 保存先・引数領域と SysV/EABI の整列差。

[未] `va_list` の GPR/FPR カーソル・overflow 領域・レジスタ保存域の形と昇格規則。
FreeBSD の機種ヘッダは共通 stdarg を含み、コンパイラの va_start を要求するだけ
(F/sys/powerpc/include/stdarg.h:31)。手元のヘッダから内部構造は確定できない。
必要資料は System V ABI PowerPC Processor Supplement (32-bit)、PowerPC EABI、
コンパイラの対応する `stdarg` 定義と ABI 実装資料。

## 追加した libc 資料で確認できること

整数引数は libc の makecontext でも先頭 8 個を r3〜r10、残りを
backchain/LR の 2 ワードより上のスタックへ置く
(N/lib/libc/arch/powerpc/gen/makecontext.c:60、同 :75)。
この関数は int 引数を扱うため、浮動小数点・構造体の分類には広げない。
NetBSD は SP を 16 バイト境界へ丸める (同 :62)。
FreeBSD はコメントでは 16 バイト整列だが、実際は `~0x1f` で 32 バイト境界へ
丸める (F/lib/libc/powerpc/gen/makecontext.c:74、同 :82)。
これはより強い実装上の整列であり、ABI が 32 バイトを要求する根拠にはしない。

NetBSD の libc _setjmp は r2 を「不要」と注記しつつ保存し、r13〜r31 も
保存列に含む (N/lib/libc/arch/powerpc/gen/_setjmp.S:19)。
同じ BSD のカーネル setjmp の保存集合とは異なる。
FreeBSD libc も r2 と r13〜r31 を保存し、さらに f14〜f31 を実際に stfd/lfd する
(F/lib/libc/powerpc/gen/_setjmp.S:51、同 :85)。
OpenBSD libc は r1・r14〜r31 と CR/LR/CTR/XER を保存する一方、
f14〜f31/FPSCR はコメントのみ
(O/lib/libc/arch/powerpc/gen/setjmp.S:86、同 :117)。
[推] この比較はユーザーランドとカーネルの保存規約を分ける必要を示す。
保存対象の多さから callee-save の完全な集合や OS32 の採用規約は決めない。

追加 libc の makecontext は `va_start` / `va_arg` を使うが、その展開は示さない
(N/lib/libc/arch/powerpc/gen/makecontext.c:76、
F/lib/libc/powerpc/gen/makecontext.c:93)。
[未] va_list の内部配置と u64 の型整列はこの追加資料だけでは確定できない。

単一の浮動小数点値の例として、NetBSD libc の fabs は f1 を入力・出力に使う
(N/lib/libc/arch/powerpc/gen/fabs_ieee754.S:11)。
[未] この例だけでは f 引数の本数、整数との混在、可変引数の規約は分からない。
OpenBSD の __va_list も __builtin_va_list への typedef であり、内部配置は示さない
(O/sys/arch/powerpc/include/_types.h:125)。
