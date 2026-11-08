# `arch/` — CPU 依存の実装

OS32 の移植点は **2 本の軸**に分かれている。

| ディレクトリ | 軸 | 何が入るか | 選ぶ変数 (既定) |
|---|---|---|---|
| `arch/<arch>/` | CPU | 割り込みの許可/禁止・保存/復元、CPU 停止、記述子表のロード、アドレス変換表の根の切替、ユーザモードへの遷移 | `ARCH` (`x86`) |
| `platform/<platform>/` | 機種 | ポート I/O、I/O ウェイト、機器のつなぎ方 | `PLATFORM` (`pc98`) |

契約 (どの実装も満たすべき宣言と註) は `include/` にあり、実装はそこに無い。
原始命令と ABI 定数の契約は **固定名**で実装を引く:

| 契約 | 何の契約か | 引く固定名 |
|---|---|---|
| [`include/io.h`](../include/io.h) | ポート I/O、割り込み制御、CPU 停止、IDT のロード | `arch_io.h` / `platform_io.h` |
| [`include/abi.h`](../include/abi.h) | 引数語の幅・整列とスタック整列 | `arch_abi.h` |
| [`include/cpu.h`](../include/cpu.h) | アドレス変換表の根と TLB・変換の有効化、ユーザモードへの遷移、別スタックでの呼び出し | `arch_cpu.h` |

    #include "arch_io.h"        /* arch/$(ARCH)/arch_io.h     */
    #include "platform_io.h"    /* platform/$(PLATFORM)/platform_io.h */
    #include "arch_abi.h"       /* arch/$(ARCH)/arch_abi.h    */
    #include "arch_cpu.h"       /* arch/$(ARCH)/arch_cpu.h    */

どの実装が来るかを決めるのは `build/config.mk` の

    ARCH     ?= x86
    PLATFORM ?= pc98
    INC_COMMON = -I. -Iinclude -Iarch/$(ARCH) -Iplatform/$(PLATFORM) $(SDK_INC)

の 2 本の `-I` が原始命令のヘッダを選ぶ。移植にはさらにツール、ソース集合、
製品 graph、ELF と配置の能力を選ぶ必要がある。`#ifdef __i386__` のような分岐を
本文に増やさないのがこの構成の目的で、`#ifdef` で 1 ファイルの中を分ける形に
戻してはいけない。

なお `arch/x86/arch_io.h` と `platform/pc98/platform_io.h` の**ファイル名が
契約側の `io.h` と違う**のは意図的。同名にすると `-I` の順で実装側が契約
ヘッダ自身を引き込む (あるいはその逆) 事故が起きる (`cpu.h` / `arch_cpu.h`
も同じ)。

**契約の無い、その arch だけの小物**は `arch_` で始めない名前にする
(いまは [`arch/x86/x86_desc.h`](x86/x86_desc.h) — GDT のロードと `ltr`)。
x86 にしか意味の無い仕組みなので `include/` に契約を置かず、x86 専用の
`arch/x86/kernel/gdt.c` / `arch/x86/kernel/tss.c` だけが直接引く。`ARCH` が別の値なら
見つからずに止まり、それが「このソースは x86 専用」という印になる。
`arch_` で始まる名前は**契約の実装**だけが使う (番人がこの区別を見る)。

## 新しいアーキテクチャを足す手順

1. **ディレクトリを作る。** `arch/<arch>/` を掘り、`arch_io.h` と
   `arch_cpu.h` を置く。既存の `arch/x86/arch_*.h` はコピー元ではなく
   **対照表**として使う — 写すのは命令列ではなく、`include/io.h` /
   `include/cpu.h` の註に書かれた契約のほう。

2. **契約を満たす。** `include/io.h` と `include/cpu.h` が挙げている原始命令を
   全部、同じシグネチャで実装する。`io.h` 側と `cpu.h` の MMU 4 種は宣言が
   あるので、型が食い違えばコンパイルが止まる。`cpu.h` の特権境界の 2 つ
   (`arch_enter_user` / `arch_call_on_stack`) だけは**マクロ**で、宣言が無い
   — 註だけが頼りなので取りこぼしやすい。とくに次の 2 つは命令を並べるだけでは
   満たせない:

   - `_idle()` は「割り込みを許可する」と「眠る」が**不可分**であること。
     2 つの命令に分けて書いてよいかは CPU ごとに違う (ARMv7/v8 なら
     `cpsie i` / `msr daifclr` + `wfi` を、wfi の wake-up event 保持が
     窓を閉じることを確かめたうえで並べる)。
   - `irq_save()` の戻り値は**不透明**であること。呼び手は中身を読まない
     約束なので、EFLAGS でも PRIMASK でも `DAIF` でもよい。

   - `arch_enter_user()` も**不可分**であること。割り込み禁止 → カーネル
     スタックの記録 → 変換表の根の切替 → ユーザモードへ、の途中に割り込みが
     入ると、記録したスタックと実際の根が食い違ったままハンドラが走る。

3. **機種側も要るなら `platform/<platform>/` を足す。** ポート I/O を持たない
   CPU では `inp`/`outp` は memory-mapped I/O (レジスタ番地への読み書き) に
   なる。CPU と機種は独立に選べるので、`arch/` を足すたびに `platform/` を
   足す必要はない (逆もまた同じ)。

4. **ビルドの選択を定義する。** `build/arch/<arch>.mk` にツールと機械旗、
   `<arch>-sources.mk` に CPU 所有の C/ASM/GAS を宣言する。
   `build/platform/<platform>.mk` は対応 ARCH・機種ソース・既定 feature を持つ。
   各 `build/features/*.mk` は早期には集合と遅期適用用の値だけを宣言する。
   `build/sources/order.mk` が既存のリンク/include 順を保持し、`build/select.mk`
   が所有者集合で filter する。共通候補の選択は移植成功を意味しない。
   `build/targets/<arch>-<platform>.mk` は製品の到達点を定める。
   `sdk/target_profile.py` は module codec・配置・guard の能力を定め、
   未実装の能力は入力を読む前に拒否する。

5. **選ばせる。** `make ARCH=<arch> PLATFORM=<platform> ...`。既定値は
   `build/config.mk` の `?=` なので、コマンドラインでも環境変数でも上書きできる。

6. **計る。** 移植度は合否ではなく数字で見る:

       python3 tools/check_arm_compile.py        # または make check-arm-compile

   基準値と経過は [`docs/tasks/portability/ARM_GAUGE.md`](../docs/tasks/portability/ARM_GAUGE.md)。
   この計測器は `ARCH=x86` の木を ARM コンパイラに通すもので、`arch/arm/` が
   出来ていなくても動く (だから受け皿だけ先に作れる)。

## 番人

`tools/check_arch_asm.py` (`make check` の `check-arch-asm`) が、カーネル側の
C ソースに `hlt` / `cli` / `sti` の直書きが無いことを検査する。直書きしてよいのは
`arch/<arch>/arch_*.h` (契約の実装側) だけで、**契約側の `include/io.h` /
`include/cpu.h` も検査対象** — 契約に実装が混ざったらそこで止まる。
`arch_` で始まらない arch 専用の小物 (`x86_desc.h`) は許可一覧に**入らない**
— 直書きの散らばる先を増やさないため。

命令列の一部としてしか意味を持たず切り出せない箇所は、その `asm` の直前の
コメントに `ARCH-ASM-OK` と**切り出せない理由**を書く。

## 今あるもの

    arch/x86/arch_io.h              i386 (唯一の実装): 割り込み・CPU 停止・lidt
    arch/x86/arch_cpu.h             i386 (唯一の実装): CR3 / CR0.PG / iret でのリング降下
    arch/x86/x86_desc.h             x86 専用の小物 (契約なし): lgdt とセグメント再設定、ltr
    platform/pc98/platform_io.h     PC-9801/9821 (唯一の実装)

`arch/arm/` は**まだ無い**。この骨格は受け皿を先に用意するためのもので、
ARM 実装は別票で行う。

## PowerPC の現在の到達点

`make ARCH=ppc PLATFORM=powermac` は共通候補と既存 bootstrap の C/GAS を
コンパイルする。出力は `build/out/ppc-powermac`、コンパイラは
`PPC_CROSS_DIR`（既定 `/home/hight/opt/cross-ppc`）から選ぶ。
原始命令ヘッダ等が未実装なので全 TU 成功・kernel リンク・起動は未達。
SDK/module/deploy は明示エラーとなる。x86 の feature off も compile-only である。

実測は `python3 -B tools/ppc/check_compile.py --source-profile selected --accept --json`。
必須 7 本、ELF32/BE/EM_PPC、実 `.d`、全 TU の試行と失敗分類を照合する。
既存 bootstrap は `make ppc-boot` / `make ppc-boot-check` で独立して扱う。
x86 の旧一覧を ARM に通す測定は別系列のまま維持する。

内部呼出属性は [`callconv.h`](../include/callconv.h) に集約する。
i386 は cdecl、他 target は通常 C ABI。PPC の ABI 定数は 4/4/16 の暫定値で、
selected compile の依存確認用であり PPC 実走を許可しない。
