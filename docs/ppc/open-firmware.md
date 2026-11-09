# Open Firmware

> 上位: docs/ppc/README.md

## Client interface

NetBSD のラッパーはサービス名ポインタ、入力数、出力数、入出力セルの束を
`openfirmware(&args)` へ渡す (N/sys/arch/powerpc/powerpc/openfirm.c:86)。
この対象の int セルは 32 ビット。package の phandle と、open して得る instance の
 ihandle は区別し、`instance-to-package` で変換する (同 :170)。

| サービス | BSD の根拠 | 主な役割 |
|---|---|---|
| peer / child / parent | N/sys/arch/powerpc/powerpc/openfirm.c:86、同 :114、同 :142 | 木の走査 |
| getproplen / getprop | 同 :198、同 :229 | プロパティ長・内容 |
| finddevice | 同 :345 | path から package |
| package-to-path | 同 :414 | package の path |
| call-method | 同 :454 | instance のメソッド |
| open / close | 同 :554、同 :587 | instance の取得・解放 |
| read / write | 同 :612、同 :659 | instance による入出力 |
| exit | 同 :813 | firmware へ戻る |

`call-method` では CI 呼出しの失敗と method の catch result を別々に検査する
(同 :489)。入力・出力のセル順もラッパーの C 引数順と単純に同じとはしない
(同 :485、同 :494)。
NetBSD は呼出し中に EE/RI を落とし、SMP ならロックも使う (同 :62)。
これは BSD 固有の呼出し保護。OS32 の MMU 切替後にもそのまま呼べるという保証ではない。
OF-PPC 7.1 は引数配列の番地を r3、復帰番地を LR に置いて CI 入口へ移る規約。
r3 の CI 成否と配列内のサービス結果を区別する。r1/r2・CR・r13〜r31 等を保存し、
LR/CTR/XER は破壊可能。これは OF 固有の呼出規約であり SysV/EABI の代用ではない。
OF-PPC 11.5 は firmware を単一スレッドとして扱う。同時呼出しの例外は
idle-self/stop-self/enter/exit で、enter/exit 自体は同時に 1 CPU に限る。
通常の CI を呼ぶ側は排他と CPU 移動の防止を担う。
[未] IEEE 1275 本体の cell 配列・claim/release の完全な規約。

## 読むノードとプロパティ

| ノード / 属性 | 確認する事実と根拠 |
|---|---|
| `/memory` の reg / available | 総 RAM と利用可能範囲を別々に読む。N/sys/arch/powerpc/powerpc/ofw_machdep.c:201、同 :264 |
| `/chosen` の stdin / stdout | 値は ihandle。N/sys/arch/powerpc/powerpc/ofw_machdep.c:174 |
| `/chosen` の mmu | ihandle から package を得て translations を読む例。N/sys/arch/powerpc/powerpc/ofw_machdep.c:378 |
| `/chosen` の bootpath / bootargs | NetBSD ブートローダーが取得。N/sys/arch/macppc/stand/ofwboot/boot.c:263 |
| `/cpus` | 子を peer で列挙し reg を読む。N/sys/arch/macppc/macppc/mainbus.c:89 |
| CPU の timebase-frequency | TB の周波数。N/sys/arch/powerpc/oea/ofwoea_machdep.c:304 |
| バスの #address-cells / #size-cells | 番地・長さのセル数を区別。N/sys/arch/powerpc/oea/ofwoea_machdep.c:450 |
| PCI の ranges | PCI 空間とホスト空間の対応。N/sys/arch/macppc/pci/uninorth.c:92、同 :121 |
| macio の assigned-addresses | 割当済み PCI 番地から基底を得る。N/sys/arch/macppc/dev/obio.c:219、同 :234 |

`ranges` はバス間の窓、`assigned-addresses` は装置への割当てとして区別する。
PCI のセル形式と番地変換は後述。UniNorth の固定レコード長を全 OF 機種に一般化しない。

## 起動の引渡し

NetBSD の OF プログラム入口は `startup(vpd,res,openfirm,arg,argl)` へ進み、
第 3 引数の CI 関数を保存する
(N/sys/arch/macppc/stand/ofwboot/Locore.c:233)。
OF-PPC 8.2.3 も入口 r5 を CI 関数の番地と定め、BSD の引数対応と一致する。
その前に専用スタックと BAT を準備して BSS を消す (同 :69、同 :169)。
これは BSD の準備手順であり、OF が MMU 無効で入ってくる証拠ではない。

OF からの入口とブートローダーからカーネルへの入口を区別する。
NetBSD は ELF の entry とシンボル範囲を得た後、命令同期して chain へ進む
(N/sys/arch/macppc/stand/ofwboot/boot.c:350)。
[未] QuickSilver の OF 版ごとの入口保証、ELF/NOTE 要件、予約領域の正式条件。
IEEE 1275 本体と QuickSilver の機種資料で補う。

## Processor binding の入口と MMU

OF-PPC 8.2.1〜8.2.3 は supervisor、EE=0、FP/ME=1、r2=0、
r1 は 16 バイト整列、少なくとも 32 KiB の stack growth と 32 バイトの linkage area を要求する。
r3/r4 は platform binding に予約、r6/r7 は引数用だが、標準 OF が引数配列を用意する
保証は無い (同 8.2.4)。bootpath/bootargs と混同しない。
cache は有効でロードされたコードの I/D 整合性は確保される (同 8.3)。

real/virtual mode は別であり、r1/r5 も対応する番地形式になる。
OF-PPC 4.2.5〜4.2.6 は CI 呼出し時の変換状態と firmware のマッピングを要求する。
MMU を引き取って virtual-mode OF を使い続ける場合は map/unmap/translate callback が必要
(同 8.5.2)。BSD の EE/RI 保護だけではこの契約を代替しない。

## PCI のセルと割込み

OF-PCI 2.2.1.1 の PCI 番地は phys.hi/mid/lo の 3 セル。
hi の n/p/t は非再配置・prefetchable・別名/低位範囲の指定、ss は空間コード
(00=config、01=I/O、10=32-bit memory、11=64-bit memory)。
残りは bus 8・device 5・function 3・register 8 ビット。mid/lo が 64 ビット番地を表す。
セル内の仕様ビット 0 は LSB で、PowerPC マニュアルの MSB=0 表記と異なる。

assigned-addresses は 3 セルの PCI 番地と 2 セルのサイズの組。
n=1 の絶対番地も **PCI domain 内**であり、CPU 物理番地とは限らない。
reg と同じ順番とは限らず、BAR を表すフィールドを照合する
(OF-PCI 4.1.2、10)。固定の先頭要素だけで全リソースを解釈しない。
ranges は child 番地・parent 番地・size の組で、親側のセル数を読む。
PCI child の hi には n/p/ss だけが有効で、窓の空間判定には ss だけを比較する
(OF-PCI 12)。窓内オフセットを親側基底に加えて上位バスへ変換する。

PCI child の interrupts は INTA=1〜INTD=4 の pin で、CPU/PIC IRQ 番号ではない
(OF-PCI 4.1.1)。macio 子の Apple 固有の番号形式と混同しない。
[未] interrupt-map-mask と複数段の割込み nexus の一般規則は、この PCI binding だけでは
完結しない。IEEE 1275 の割込み mapping の推奨規約と実機 tree が必要。
