# PowerMac G4 の ATA 制御器

対象は QuickSilver (PowerMac3,5) と QEMU mac99。
N・O・F の版と資料の扱いは docs/ppc/README.md の「資料の扱い」に従う。
BSD の略号は NetBSD `36eddebcc36c`、OpenBSD `b907757ada4d`、FreeBSD `6eae951cd51b`。
[未] は未確認、[推] は資料からの推論。

## 1. 機種と配線

| 機種・標準構成 | HDD | 光学 |
|---|---|---|
| QuickSilver 2001、733 MHz | 40 GB Ultra ATA/66 | CD-RW |
| 同 867 MHz | 60 GB Ultra ATA/66 | SuperDrive |
| 同デュアル 800 MHz | 80 GB Ultra ATA/66 | SuperDrive |
| QuickSilver 2002、800 MHz | 40 GB Ultra ATA/66 | CD-RW |
| 同 933 MHz | 60 GB Ultra ATA/66 | SuperDrive |
| 同デュアル 1 GHz | 80 GB Ultra ATA/66 | SuperDrive |

根拠は Apple 日本の発表の「標準システム構成」。
[2001-07-19](https://www.apple.com/jp/newsroom/2001/07/18Apple-Debuts-New-Power-Mac-G4-Line/)、
[2002-01-28](https://www.apple.com/jp/newsroom/2002/01/28Apple-Unveils-Dual-1-GHz-Power-Mac-G4/)。

2001 年機は KeyLargo の 2 系統。Ultra DMA/66 に起動 HDD (通常 device 0) と
追加 HDD (通常 device 1)、独立した EIDE に光学 (ATAPI device 0) と
オプションの Zip (ATAPI device 1) がつながる。両系統に DBDMA がある。
ATA/66 側の上限は PIO4・MWDMA2・UDMA4。追加 PCI ストレージは別経路。
Apple Developer Note [July 2001 PMG40701.pdf](https://leopard-adc.pepas.com/documentation/Hardware/Developer_Notes/Macintosh_CPUs-G4/PowerMacG418Jul01/PMG40701.pdf) pp. 26–28, 38–42、
[October 2001 pmg4.pdf](https://leopard-adc.pepas.com/documentation/Hardware/Developer_Notes/Macintosh_CPUs-G4/PowerMacG4_29Oct01/pmg4.pdf) pp. 26–27, 38–42。

2002 年機も KeyLargo の Ultra ATA/66 に HDD、独立した EIDE に光学と
オプションの Zip がつながる。Apple [Service Source 2002](https://manualzz.com/doc/1211036/apple-power-mac-g4-service-manual)
の Troubleshooting「QuickSilvers/Digital Audio」図 (ミラー 2312–2384 行) と
Exploded View「QuickSilver 2002」pp. 12–13 が根拠。両年式の標準基板図は
KeyLargo の 2 系統を示す。基板改版ごとの OF 値・IRQ・大容量対応は [未]。

MDD の APPLE-MDD02・APPLE-MDD は U2 の ATA/100 に HDD、KeyLargo の
ATA/66 に別の HDD、EIDE に光学という 3 系統を記載する。
ATA/100 の接続先を示す根拠は U2 節とブロック図を採る。
APPLE-MDD の同節末尾は KeyLargo の ATA/66 DMA を併記している。
両版の 2Architecture/Mojave_archi.html・jos_archi.html 各 100 行、
「Ultra ATA/100 Interface」「Ultra ATA/66 Interface」「Enhanced IDE Interface」。

## 2. OF と PCI での識別

| 経路 | BSD が照合する値と参照する資源 |
|---|---|
| MacIO、N | `wdc_obio_match` は `ATA` / `ata` / `ata0` / `ide`、または compatible `keylargo-ata` / `heathrow-ata` / `ohare-ata`。DMA 有効時に name `ata-4` で UDMA4 を選ぶ。N/sys/arch/macppc/dev/wdc_obio.c:91–113,127–147,184–231 |
| MacIO、O | `wdc_obio_probe` は reg が 8 B 以上で `ATA` / `ata*` / `ide`、または compatible `keylargo-ata` / `heathrow-ata`。16 B 以上で DMA、name `ata-4` / `ata-6` で UDMA4 / 5。O/sys/arch/macppc/dev/wdc_obio.c:106–145,185–209 |
| MacIO、F | `ata_macio_probe` は device_type `ata` / `ide` を取り、name `ata-4` なら UDMA4、その他は MWDMA2 を上限とする。F/sys/powerpc/powermac/ata_macio.c:154–177 |
| Kauai、N | `kauai_match` は Apple PCI ID の列挙。attach は PCI function を OF node に引き、`assigned-addresses` の第 3 セルを基点にする。N/sys/arch/macppc/dev/kauai.c:86–131 |
| Kauai、O | `kauaiatamatch` は Apple PCI ID の列挙。`uata` alias または `/pci@f4000000/ata-6` を探し、name と reg を読み、PCI メモリ BAR から子 `wdc` の資源を組む。探索ノードと PCI function の一致はコードの TODO。O/sys/arch/macppc/pci/kauaiata.c:69–121,137–185 |
| Kauai、F | `ata_kauai_probe` は 106b:0033 (UniNorth2)、003b、0043、0050、0069。attach は compatible `shasta-ata` / `K2-UATA` を派生品の区別に使う。F/sys/powerpc/powermac/ata_kauai.c:130–138,195–212,228–257 |
| CMD646 | PCI ID 1095:0646。F の表は rev 0x07 以上を 646U2 (UDMA2)、それより前を 646 (MWDMA2) に区分する。F/sys/dev/ata/ata-pci.h:442、F/sys/dev/ata/chipsets/ata-siliconimage.c:74–94,99–132 |

MacIO の `reg` はドライバが消費する `<taskfile offset, size, DBDMA offset, size>`
の並び。窓は親 MacIO 側の基点に対するオフセット。
N は DMA 窓を 0x100 B に限定、O は reg のサイズ、F はメモリ資源 0 / 1 を使う。
N/sys/arch/macppc/dev/obio.c:281–309、N/sys/arch/macppc/dev/wdc_obio.c:154–205、
O/sys/arch/macppc/dev/wdc_obio.c:166–191、F/sys/powerpc/powermac/macio.c:247–257,289–298。
表中の名前と compatible は照合条件。QuickSilver 年式ごとの完全な
`reg`、`compatible`、`interrupts` の実値は OF tree 待ち [未]。

## 3. レジスタ窓とタイミング

MacIO と Kauai は taskfile の stride が 0x10。下表は taskfile 基点からの byte offset。
N/sys/dev/ic/wdcreg.h:44–69、N/sys/dev/ic/wdc.c:181–185、N/sys/arch/macppc/dev/wdc_obio.c:154–179、
O/sys/arch/macppc/dev/wdc_obio.c:591–623、F/sys/powerpc/powermac/ata_kauai.c:58–80,259–275。

| オフセット | 読み / 書き |
|---|---|
| 0x000 | 16 bit Data |
| 0x010 | Error / Features |
| 0x020 | Sector Count |
| 0x030 | Sector Number / LBA low |
| 0x040 | Cylinder Low / LBA mid |
| 0x050 | Cylinder High / LBA high |
| 0x060 | Device/Head |
| 0x070 | Status / Command |
| 0x160 | Alternate Status / Device Control |
| 0x200 | MacIO timing、Kauai PIO・MWDMA timing (O/F) |
| 0x210 | Kauai UDMA timing (O/F) |

taskfile の Data 以外の通常アクセスは 8 bit、timing は 32 bit。
Kauai の PCI メモリ BAR からは DBDMA が +0x1000、taskfile が +0x2000、
Control が +0x2160、timing が +0x2200 / +0x2210。
F の名付けでは FCR が BAR+0x0、DMA IRQ が BAR+0x2300。
N/sys/arch/macppc/dev/kauai.c:126–131,169–200、O/sys/arch/macppc/pci/kauaiata.c:176–185、
F/sys/powerpc/powermac/ata_kauai.c:58–80,215–225,296–305。

### MacIO

下表は BSD が計算に使う cycle / active (ns) の組。
UDMA の active は同じレジスタ内の別フィールドに入る値で、
PIO/MWDMA のように cycle から引く値ではない。
N/sys/arch/macppc/dev/wdc_obio.c:262–296,311–421、
F/sys/powerpc/powermac/ata_macio.c:73–108,259–327、
O/sys/arch/macppc/dev/wdc_obio.c:256–282,326–469。

| mode | PIO N/F | PIO O | MWDMA N/F | MWDMA O | UDMA N/F | UDMA O |
|---|---|---|---|---|---|---|
| 0 | 600/180 | 600/165 | 480/240 | 480/215 | 120/180 | 114/0 |
| 1 | 390/150 | 383/125 | 165/90 | 150/80 | 90/150 | 75/0 |
| 2 | 240/105 | 240/100 | 120/75 | 120/70 | 60/120 | 55/0 |
| 3 | 180/90 | 180/80 | — | — | 45/90 | 45/100 |
| 4 | 120/75 | 120/70 | — | — | 30/90 | 25/100 |

| MacIO の +0x200 | tick とフィールド |
|---|---|
| `ata-4` PIO | 15 ns 切上げ。active は bit 0 から、recovery (cycle tick − active tick) は bit 5 から。mask 0x000003ff |
| `ata-4` MWDMA | active は bit 10 から、recovery は bit 15 から。mask 0x001ffc00 |
| `ata-4` UDMA | enable bit 20、cycle は bit 21 から、active は bit 25 から。mask 0x1ff00000 |
| 旧系統 PIO | 30 ns 切上げ。active は bit 0 から、recovery は bit 5 から。cycle − active − 4 を最低 1 に補正。mask 0x000007ff |
| 旧系統 MWDMA | 30 ns 切上げ。active は bit 11 から、recovery は bit 16 から。cycle − active − 1 を最低 1 に補正。half tick (bit 21) は 0。mask 0xfffff800 |

各 BSD の入力値を併記した。Apple の資料は対応モードまで示すため、入力値の差の
理由と実機で採用する値は [未]。APPLE-MDD の KeyLargo 概説には
「Multiword DMA Mode 4」とある。QuickSilver 2001 の上限は同機の
Developer Note July 2001 p. 41 と BSD の DMA2 が一致するため MWDMA2 を採る。

### Kauai (UniNorth2 系)

| mode | PIO N/F | PIO O | MWDMA N/F | MWDMA O | UDMA 有効時 N/F/O |
|---|---|---|---|---|---|
| 0 | 0x08000a92 | 0x08000a92 | 0x00618000 | 0x00618000 | 0x70c1 |
| 1 | 0x0800060f | 0x08000492 | 0x00209000 | 0x00492000 | 0x5d81 |
| 2 | 0x0800038b | 0x0800038b | 0x00148000 | 0x00149000 | 0x4a61 |
| 3 | 0x05000249 | 0x05000249 | — | — | 0x3a51 |
| 4 | 0x04000148 | 0x04000148 | — | — | 0x2a31 |
| 5 | — | — | — | — | 0x2921 |

O は +0x200 の PIO (mask 0xff000fff) と MWDMA (0x00fff000) を
read-modify-write、+0x210 の UDMA (0x0000ffff) は bit 0 で有効化。
F も 2 窓に同じフィールドを置き、要求開始時に unit 別の値を書き込む。
N の `calc_timing_kauai` は MWDMA 語を UDMA 語と合成して +0x210 に書く。
レジスタの意味を記述する際は O/F の一致を採る。
N の書き先と PIO1・MWDMA1/2 の定数差はベンダのレジスタ資料での照合待ち [未]。
N/sys/arch/macppc/dev/kauai.c:222–298,302–316、
O/sys/arch/macppc/dev/wdc_obio.c:285–304,317–323,516–532、
F/sys/powerpc/powermac/ata_kauai.c:143–182,313–367。

## 4. DBDMA

Kauai の DMA も DBDMA。PCI バスマスタを許可して BAR 内の DBDMA を使う。
CMD646 の PCI IDE バスマスタ資源とは別のインターフェース。
F/sys/powerpc/powermac/ata_kauai.c:270–285、F/sys/dev/ata/chipsets/ata-siliconimage.c:99–165。

| DBDMA 窓からの offset | little-endian 32 bit |
|---|---|
| 0x00 / 0x04 | Control / Status。Control 上位半語は下位半語の更新マスク |
| 0x08 / 0x0c | Command pointer high / low。N は high=0 |
| 0x10 / 0x14 / 0x18 | Interrupt / Branch / Wait Select |
| 0x1c / 0x20 / 0x24 | Transfer Mode / Data pointer high / low |
| 0x2c / 0x30 | Branch pointer high / low |

16 B 整列の little-endian 記述子は count 16 bit、command 16 bit、
DMA address 32 bit、command-dependent 32 bit、residual 16 bit、status 16 bit。
ディスクからメモリは INPUT、逆は OUTPUT。途中は MORE、末尾は LAST、
転送列の次に STOP を置く。N はページ単位に分け、O は DMA map の各 segment を使う。
N/sys/arch/macppc/dev/dbdma.h:27–35,80–155、N/sys/arch/macppc/dev/dbdma.c:43–97、
N/sys/arch/macppc/dev/wdc_obio.c:468–531、O/sys/arch/macppc/dev/wdc_obio.c:52–57,138–145,536–582、O/sys/arch/macppc/dev/dbdma.h:99。

N は停止状態を確認して command pointer を設定し RUN。停止時は RUN を落とし、
FLUSH と ACTIVE が消えるまで待つ。F は STOP で終端する 256 スロットの環、
記述子およびデータの DMA 同期、WAKE と ACTIVE/DEAD の確認を使う。
F は 16 bit count のオーバーフローを避けて segment を 126 × 512 B
(64,512 B) に制限し、共通 ATA DMA map の既定で 64 KiB 境界、2 B 整列を適用。
count 上限と境界は別の制約。O は 32 segment・各 0x8000 B 以下の DMA map をページ境界で分割する。
F/sys/powerpc/powermac/ata_dbdma.c:63–199,203–283、
F/sys/dev/ata/ata-dma.c:69–93,185–199,332–347。

## 5. attach、reset、モード設定

| 系統 | 順序と前提 |
|---|---|
| N MacIO | `wdc_obio_attach` で reg・IRQ、taskfile、DMA 資源、capabilities、shadow register を準備して `wdcattach`。DMA に構成フラグが要る。N/sys/arch/macppc/dev/wdc_obio.c:117–260 |
| N Kauai | `kauai_attach` で OF node と BAR 基点、IRQ、taskfile、DMA 窓を準備して `wdcattach`。N/sys/arch/macppc/dev/kauai.c:105–218 |
| O | `kauaiataattach` が PCI メモリを有効にして子に OF 名と資源を渡す。`wdc_obio_attach` は taskfile・level IRQ・DMA を準備して `wdcattach`、その後 `set_modes`。O/sys/arch/macppc/pci/kauaiata.c:93–185、O/sys/arch/macppc/dev/wdc_obio.c:130–229 |
| F MacIO | `ata_macio_attach` は taskfile、DBDMA を初期化し、既存 timing を unit ごとの初期値に取り込んで `ata_attach`。F/sys/powerpc/powermac/ata_macio.c:181–255 |
| F Kauai | `ata_kauai_attach` は BAR、IRQ、PCI bus master、DBDMA、初期 PIO 値の順に準備し、FCR に 7 を書いて `ata_attach`。7 のビット別の意味は [未]。F/sys/powerpc/powermac/ata_kauai.c:228–309 |

ATA のチャネル reset は device 0 を選び、デバイス割り込みを止めて SRST を
assert/deassert し、BSY の解除を待って ATA/ATAPI のシグネチャを判定する。
N の assert 保持は 2 ms、F は 10 ms と解除後の 100 ms 待ち。
これらは BSD の待ち値。F は初期 status で候補が無ければ reset を省く。
N/sys/dev/ic/wdc.c:547–613,740–803,1110–1194、
O/sys/dev/ic/wdc.c:589–698,744–832、F/sys/dev/ata/ata-lowlevel.c:470–595。

N/O の ATA 側のモード指定は `SET FEATURES`。N MacIO は unit 別に timing を
計算し、差があれば選択時に書く。O は接続機器の低いモードを共有値に反映する。
N Kauai は PIO を遅い側にそろえ、DMA 開始準備で再設定と readback。
F は `*_setmode` で値を保持し `*_begin_transaction` で要求の unit の値を書く。
F は両制御器で ATAPI DMA を無効化している。
N/sys/dev/ata/ata_wdc.c:232–269、N/sys/arch/macppc/dev/wdc_obio.c:301–439、
N/sys/arch/macppc/dev/kauai.c:222–245,302–316、
O/sys/dev/ata/ata_wdc.c:638–683、O/sys/arch/macppc/dev/wdc_obio.c:391–532、
F/sys/powerpc/powermac/ata_macio.c:208–214,259–341、
F/sys/powerpc/powermac/ata_kauai.c:270–275,313–367。

## 6. MPIC と割り込み

2001 Developer Note July 2001 pp. 26–27 は KeyLargo 内蔵 MPIC に内部
ソース・外部ピン・Uni-N の直列ストリームが入ると説明する。
MacIO ATA の device IRQ は OF の `interrupts` / `AAPL,interrupts` が入口。
F は interrupt-parent と #interrupt-cells を使い資源化する。N は先頭セル、
sense が利用できる条件では第 2 セルを用いる。O は level で登録する。
O の OpenPIC は IRQ に対応する source vector に sense を設定する。
O/sys/arch/macppc/dev/openpic.c:529–585,722–744。
N/O のプロパティ欠落時の 13 はドライバのフォールバック。
N/sys/arch/macppc/dev/obio.c:281–309、N/sys/arch/macppc/dev/wdc_obio.c:56,123–147,181–182、
O/sys/arch/macppc/dev/wdc_obio.c:52,150–183、F/sys/powerpc/powermac/macio.c:210–244。

Kauai の device IRQ は N が OF 値を優先し欠落等で 39 (Shasta は 38)、
O は Shasta 以外で 0x27 (=39)、F も K2/Shasta 以外で 39 を設定する。
N/O は PCI interrupt config が 0 の機種を記す。F の別 DBDMA IRQ 経路は
両機種とも無効。Kauai の同経路にある BAR+0x2300 への 0x80000000 書込は
無効分岐の記録。QuickSilver の ATA/66・EIDE 個別 MPIC source は [未]。
N/sys/arch/macppc/dev/kauai.c:133–165、O/sys/arch/macppc/pci/kauaiata.c:121–135、
F/sys/powerpc/powermac/ata_kauai.c:76–80,215–225,240–247,287–294、
F/sys/powerpc/powermac/ata_macio.c:68,242–250。

## 7. QEMU mac99 の資料上の範囲

QEMU-PM (11.1.50、`text/qemu-ppc-powermac.txt:147–164`) は PMAC IDE 2 系統と
HDD/CD-ROM を記載。ローカル 8.2.2 の `-M mac99 -device help` には
`macio-newworld`、`ide-hd`、`ide-cd` があり、`-device macio-newworld,help`
には `ide[0]` / `ide[1]` が `macio-ide`、ほかに `dbdma` と `pic` が出る。
`-device cmd646-ide,help` は device not found。
以上から mac99 の候補は MacIO IDE [推]。既定実体の OF 値、CMD646 の
内部実体の有無、DMA と割り込みの動作は help の範囲では [未]。

HDD・CD-ROM の接続指定例は下記。ローカル `-help` の `-drive` と
[QEMU Invocation の `-drive` 節](https://www.qemu.org/docs/master/system/invocation.html)による。
`index` は IDE 接続口の番号で、`bus` / `unit` は位置を指定する別形式。
例の OF alias、起動順と実際の認識は未確認。

```sh
qemu-system-ppc -M mac99 \
  -drive file=disk.img,format=raw,if=ide,index=0,media=disk \
  -drive file=install.iso,format=raw,if=ide,index=2,media=cdrom
```

## OS32 の HAL の口と照らす時の要点

- [HPT370](../tasks/realhw/TASK_HPT370.md) の PCI I/O と PowerMac の MMIO は taskfile の間隔・アクセス幅・制御窓が異なる。
- HDD と ATAPI 光学は別種の装置で、各チャネルには独立した device 0/1 の識別がある。
- ATA/66・ATA/100 は転送モードの上限を示す。LBA28/LBA48、sector 容量は媒体の識別情報に依存する。
- PIO・MWDMA・UDMA の設定は制御器側とデバイス側にあり、同じチャネルの相手にも制約される。
- DBDMA と PCI IDE バスマスタでは記述子・終了状態が異なる。count 上限と [HW2](../CONSTRAINTS.md#hw2-dma-の-64kb-境界) の境界制約は別に効く。
- ATA status、DBDMA ACTIVE/DEAD、MPIC の IRQ source はそれぞれ異なる情報を持つ。
- [PPC KAPI 補完計画](../decisions/ppc/PPC_KAPI_FILL_DESIGN.md)の ide 行と段 8 は、実 ATA 媒体の識別と sector 範囲・書込み権限を共通窓口の条件とする。
