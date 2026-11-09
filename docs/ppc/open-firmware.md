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
[未] IEEE 1275 の正式な stack/cell 規約、claim/release/map、再入可否の条件。

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
[未] 一般の PCI binding の可変セル配置とフラグの完全表。上の UniNorth 実装の
固定レコード長を全 OF 機種に一般化しない。

## 起動の引渡し

NetBSD の OF プログラム入口は `startup(vpd,res,openfirm,arg,argl)` へ進み、
第 3 引数の CI 関数を保存する
(N/sys/arch/macppc/stand/ofwboot/Locore.c:233)。
[推] 通常の GPR 引数対応では入口 r5 が CI に当たる。
その前に専用スタックと BAT を準備して BSS を消す (同 :69、同 :169)。
これは BSD の準備手順であり、OF が MMU 無効で入ってくる証拠ではない。

OF からの入口とブートローダーからカーネルへの入口を区別する。
NetBSD は ELF の entry とシンボル範囲を得た後、命令同期して chain へ進む
(N/sys/arch/macppc/stand/ofwboot/boot.c:350)。
[未] QuickSilver の OF 版ごとの入口保証、ELF/NOTE 要件、予約領域の正式条件。
IEEE 1275 と PowerPC processor binding、Apple の機種資料で補う。
