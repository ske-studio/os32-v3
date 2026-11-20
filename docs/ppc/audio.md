# PowerMac G4 QuickSilver と QEMU mac99 の音声装置

根拠の N・O・F は docs/ppc/README.md の「資料の扱い」の NetBSD・OpenBSD・FreeBSD の固定版。
以下で `N:` と `O:` の短いファイル名は `sys/arch/macppc/dev/`、
`F:` の短いファイル名は `sys/dev/sound/macio/` からの相対名。
Apple と QEMU 配布文書の版も同 README の略号に従う。

## 1. 筐体と codec を識別する

| 対象 | 根拠で確認できた範囲 |
|---|---|
| QuickSilver 2001 (PowerMac3,5) | [未] SKU ごとの codec、OF の `sound/compatible` と `deq/reg` |
| QuickSilver 2002 (PowerMac3,5) | [未] SKU ごとの codec、OF の `sound/compatible` と `deq/reg` |
| 年式を特定できない QuickSilver | NetBSD に `deq` の `compatible` 欠落の実機記述と、`speaker-id` 1-Wire の extint-gpio16 の注記がある (N:deq.c:78-102、N:snapper.c:2172-2184) |
| MDD (2002・2003) | KeyLargo の IIS と DB-DMA に Snapper が接続される。Apple は 44.1 kHz、16 bit を記載し、ステレオ入力と複数の出力を挙げる（下記）。QuickSilver への外挿は [推] |
| mac99 (ローカル QEMU 8.2.2) | MacIO の DBDMA と OpenPIC は help に出る。音声装置の確認範囲は §7 |

Apple の根拠は APPLE-MDD02 `2Architecture/Mojave_archi.html:100`（DMA Support、Sound Circuitry）、
`3Input-Output/Mojave_innout.html:100`（Sound System）および APPLE-MDD
`2Architecture/jos_archi.html:100`、`3Input-Output/jos_innout.html:100` の同節。
両版にはライン出力が DSP をバイパスし、差し込みによる自動 mute の対象外であるとの記載がある。
この動作を QuickSilver で確認するには、各年式の Developer Note または実機の経路と GPIO の記録が要る。

### OF ノードと BSD の probe

I2S の例は `mac-io/i2s/i2s-a/sound`。FreeBSD は `i2s-a` と `sound` を
名前で探し、NetBSD と OpenBSD の該当 probe は各階層の最初の子を調べる。
音声の `sound` と、I2C 側に置かれる `deq` / `codec` は別のノード。
FreeBSD は Xserve の `lightshow` を除く (F:i2s.c:154-179,185-203)。

| 系統 | OF と BSD の照合条件 |
|---|---|
| AWACS / Screamer | N は `awacs` / `davbus` を優先し、資源が足りる `i2s` に低優先度でも照合する。Screamer は当該ノードと子の `compatible` を調べる (N:awacs.c:284-301,384-399) |
| AWACS (O) | `awacs` / `davbus` かつ `reg` が 24 バイト以上、割り込みプロパティが 12 バイト以上 (O:awacs.c:241-263) |
| Screamer / Burgundy (F) | `davbus` に attach し、子の `compatible` が `screamer` または `burgundy` の場合に該当する mixer を選ぶ (F:davbus.c:95-110,539-572) |
| DACA (DAC3550A) | O は `i2s` の孫の `compatible=daca` に照合 (O:daca.c:90-119) |
| Tumbler (TAS3001) | O は `i2s` の孫の `compatible=tumbler`。F は I2C 上の `deq` とアドレスバイト `0x68` (O:tumbler.c:241-261、F:tumbler.c:137,325-340) |
| Snapper (TAS3004) | O は `snapper`、`platform-tas-codec-ref`、または PowerBook5,4 の `AOAKeylargo`。F は `deq` とアドレスバイト `0x6a`、または `codec` と `compatible=tas3004` (O:snapper.c:420-447、F:snapper.c:137,364-385) |
| Tumbler / Snapper (N) | `i2s` の孫の `snapper`、`tumbler`、`AOAKeylargo`、`AOAK2`、`AOAShasta`、`AOAbase`、または `platform-tas-codec-ref`。attach 後に I2C 側の codec を照合 (N:snapper.c:626-689,767-852) |
| Onyx (PCM3052) | O は `platform-onyx-codec-ref` または iMac G5 向け `layout-id` 0x2d / 0x56。F は I2C の `codec`、`pcm3052`、アドレスバイト `0x8c` を照合 (O:onyx.c:104-133、F:onyx.c:112,214-233) |

N の `deq_attach` は `compatible` 欠落時、7 bit の `0x34` を TAS3001、
`0x35` を TAS3004 と表示する。N の `snapper_defer` は同じ欠落時に
`0x34` を TAS3001 とし、その他には別機種の例外を含む既定分岐を持つ。
年式不明の QuickSilver のコメントにはその個体のアドレス値が載っていない。
DACA、Burgundy、Onyx を QuickSilver に割り当てる根拠も手元では未確認。
(N:deq.c:54-102、N:snapper.c:820-852)

## 2. データと制御の経路

PCM はメモリから MacIO の出力 DBDMA、I2S または DAVbus、codec の順に流れる。
録音のある構成では入力用 DBDMA を使う。N/O の I2S は `i2s-a` 相当の
`reg` から、制御・出力 DMA・入力 DMA の順に番地と長さを取る。
一部 G5 の `reg` 補正は機種依存の処理。
(N:snapper.c:696-733、O:i2s.c:74-102、F:davbus.c:493-519)

| DBDMA の要素 | 意味 |
|---|---|
| 記述子 | 16 バイト、先頭を 16 バイトに整列。転送要求長、命令、DMA アドレス、分岐先、残量、状態を持つ。各フィールドは little-endian |
| 転送と周回 | 出力は `OUT_MORE` / `OUT_LAST`、入力は `IN_MORE` / `IN_LAST`。分岐先を先頭にした命令で周回し、通知箇所に `INT_ALWAYS` を立てる |
| 開始 | チャネルの旧状態を落とし、コマンドリスト先頭の物理番地を command pointer に設定して RUN |
| 完了 | 割り込み側が記述子の状態を検査して消し、済んだ区間を PCM 層へ通知する。割り込みの到着回数だけで区間数は決まらない |

根拠は N:dbdma.h:28-62,82-133、N:dbdma.c:43-65、N:snapper.c:879-914,1369-1468、
O:i2s.c:129-193,583-664。F の `aoa_dma_set_program` はブロック単位の周回に
条件付きで STOP へ抜ける経路を持ち、`aoa_interrupt` は連続した完了スロットを処理する
(F:aoa.c:71-110,317-336)。

I2S の codec レジスタは別経路の KeyLargo I2C (Keywest、N の `ki2c`、O の `kiic`)。
N は `AAPL,i2c-rate`、`AAPL,address`、`AAPL,address-step` と
`i2c-bus` 子を扱う。O の直接書き込みはアドレスバイトと R/W bit、
subaddress とデータを渡す。TAS3001 では遅い取引を見込んだ待ち時間がある。
DAVbus の codec は MMIO の codec control/status を使い、AWACS の書き込みは busy を前後で待つ。
(N:ki2c.c:140-240,399-426、O:kiic.c:275-306、N:awacs.c:575-600)

| codec | 音量・mute の根拠 |
|---|---|
| AWACS / Screamer | codec reg 2 / 4 が出力の左右減衰。reg 1 の speaker mute `0x80`、headphone mute `0x200` と出力制御を使う (N:awacs.c:227-255,1036-1066、F:davbus.c:395-429,460-480) |
| Burgundy | codec command の `0x16000` が出力 mute、`0x16100`–`0x16500` が出力レベル。これらは codec のエンコード済み指定値 (F:davbusreg.h:88-103、F:davbus.c:203-258,272-295) |
| DACA | `0x02` に左右のアナログ音量を 2 バイトで書く。O のアドレスバイトは `0x9a` (7 bit `0x4d`) (O:daca.c:90-93,118,161-172) |
| TAS3001 / TAS3004 | `0x04` は左右各 3 バイトの音量係数。I2C アドレスバイトは `0x68` / `0x6a`、7 bit では `0x34` / `0x35`。出力切替の mute は MacIO GPIO にもある (O:tumbler.c:174-181,299-317,395-407、O:snapper.c:334-341,487-515,630-642、O:i2s.c:776-838) |
| Onyx | 左 `0x41`、右 `0x42` に 1 バイトずつ。アドレスバイト `0x8c` (7 bit `0x46`) (O:onyx.c:59-62,172-186、F:onyx.c:112,214-233) |

制御窓で使うオフセットの例を示す。番地の起点は各ノードの `reg` で得る。

| 窓 | オフセットと用途 |
|---|---|
| AWACS | `0x00` sound control、`0x10` codec control、`0x20` codec status (N:awacs.c:194-199,575-600) |
| I2S | `0x00` interrupt control/status、`0x10` serial format、`0x60` data word size (O:i2sreg.h:27-37、O:i2s.c:744-768) |
| MacIO | `0x3c` FCR1 の `0x1000` で I2S0 clock、`0x2000` で I2S0 enable (O:i2sreg.h:35-47、N:snapper.c:2079-2081) |

## 3. 標本化・形式・クロック

| 実装 | 確認できる PCM 形式 |
|---|---|
| NetBSD AWACS | signed 16 bit BE・ステレオ。7.35 / 8.82 / 11.025 / 14.7 / 17.64 / 22.05 / 29.4 / 44.1 kHz の表 (N:awacs.c:179-190,1206-1244) |
| NetBSD I2S | signed 16 bit BE・2 ch。TAS3004 は 32 / 44.1 / 48 kHz、TAS3001 はさらに 96 kHz、Onyx は 44.1 / 48 / 96 kHz (N:snapper.c:480-522) |
| OpenBSD I2S | signed linear BE。パラメータの上限は 16 bit・2 ch、実際の I2S word size は入出力とも 16 bit ステレオ。レートは 44.1 / 48 kHz に丸める (O:i2s.c:218-268,675-699,737-770) |
| FreeBSD AOA (DAVbus / I2S) | 再生 1 本、signed 16 bit BE・2 ch・44.1 kHz 固定 (F:aoa.c:191-207,339-344,365-390) |

I2S の O/N は 44.1 kHz に 45.1584 MHz、48 kHz に 49.152 MHz の
親クロックを選ぶ。MCLK は 256 × fs、SCLK は 64 × fs で、両段の分周は 4。
再生と録音は同一クロック。NetBSD のクロック設定には 24 bit の分岐もあるが、
公開形式表は 16 bit。Apple の MDD 公称値と BSD の公開形式はそれぞれの適用範囲で採用する。
(N:snapper.c:941-969,1642-1739、O:i2s.c:225-240,689-739)

## 4. attach と初期化の順

| 実装 | 関数と処理順 |
|---|---|
| N I2S | `snapper_attach` が MMIO・DMA・IRQ を用意。 `snapper_defer` が OF の codec 参照と I2C の `deq` を照合し、`snapper_init` で FCR、GPIO、reset、codec、出力選択を設定して PCM を登録。I2C が得られないとソフト音量へ分岐 (N:snapper.c:669-764,767-876,2012-2064,2067-2249) |
| O I2S | codec ごとの attach → `i2s_attach` でマップ、DMA、IRQ、クロック、GPIO → 各 defer で `kiic` を探し audio を登録 → codec 初期化。Tumbler/Snapper は 44.1 kHz 設定、I2C 状態ビットの有効化、reset、codec の初期値と音量を処理 (O:i2s.c:67-126、O:tumbler.c:264-296,416-467、O:snapper.c:450-484,651-715) |
| O DACA / Onyx | `daca_defer` → `daca_init` は I2S 44.1 kHz と I2C コントローラの設定。`onyx_defer` は reset、音量、入力ソースを設定 (O:daca.c:121-158、O:onyx.c:136-169) |
| F I2S | `i2s_attach` → `i2s_setup` (44.1 kHz、16 bit、64 × fs) と DMA IRQ → I2C を待つフック → `i2s_postattach` の codec reset、`aoa_attach`、初期ポート状態、GPIO callback 有効化 (F:i2s.c:185-252,443-575,649-690,729-758) |
| N/O AWACS | `awacs_attach` で MMIO・DBDMA・IRQ → subframe 0、44.1 kHz、port-change IRQ → codec と出力先。N は Screamer の追加レジスタにも書く (N:awacs.c:304-420,427-493、O:awacs.c:266-399) |
| F DAVbus | `davbus_attach` で MMIO・出力 DMA IRQ → codec 種別と control IRQ → subframe 0 と 44.1 kHz → `aoa_attach` と `screamer_init` / `burgundy_init` (F:davbus.c:137-188,333-359,493-574、F:aoa.c:365-390) |

I2S のシリアル形式変更は CLKSTOPPEND を消し、FCR でクロックを止め、
停止通知を待ち、FORMAT を書き、クロックを戻す。待ち上限の実装は N で約
1 ms、O で約 0.5 ms。両者とも待ち切れないときに FORMAT 書き込みへ進む。
codec reset 待ちにも実装差があり、reset の assert 前は N が 100 ms、O は 1 s。
これらはドライバの待ち時間であり、QuickSilver の最小保証値は [未]。
(N:snapper.c:1731-1769,2012-2027、O:i2s.c:744-770,1011-1025)

## 5. 割り込みとジャック検出

- I2S port の OF `interrupts` には制御・出力 DMA・入力 DMA の順で
  (source, flags) が並ぶ。N/O は flags bit 0 で level / edge を選び、
  この I2S 経路の制御 IRQ は登録せず、DMA の 2 本を登録する。
  F は出力 IRQ resource 1 を edge、active-low として使う。
  (N:snapper.c:737-753、O:i2s.c:104-116、F:i2s.c:220-238)
- DAVbus の O/N は 3 組の 2-cell または 3 個の 1-cell を扱う。1-cell 時の
  既定 sense は O が level、N が edge。F の MacIO は `interrupts` を優先し、
  `AAPL,interrupts` にフォールバックし、`interrupt-parent` と親の
  `#interrupt-cells` を使って第 1 cell を IRQ 資源に写す。
  (O:awacs.c:302-320、N:awacs.c:335-376、F:sys/powerpc/powermac/macio.c:201-244)
- MPIC 配下では OF の source と親コントローラが経路を決める。
  割り込み親や nexus をたどる際は [Open Firmware](open-firmware.md) の解説も参照。
  O の OpenPIC は source の番号で source vector register を選び、同番号を
  vector に設定する。CPU 外部割り込みでは IACK で vector を得て EOI を返す。
  DBDMA チャネル ID（N ヘッダの audio out `0x8` / in `0x9`）と
  MPIC source は別の番号。QuickSilver 各版の数値は [未]。
  (O:openpic.c:173-193,566-584,607-682、N:dbdma.h:67-77)
- I2S は MacIO の `gpio` の `audio-gpio` / `name` から
  `headphone-detect`、`amp-mute`、`headphone-mute` 等を探す。
  検出の極性は `audio-gpio-active-state`、N の出力 mute 極性は
  `platform-do-*-mute` property から取得する経路がある。
  ヘッドホン／line-out の GPIO IRQ と DMA IRQ は独立し、内蔵スピーカーの
  経路はアンプ mute、対応する外部スピーカーの識別は別の 1-Wire を使う。
  (N:snapper.c:1904-2009,2086-2209、O:i2s.c:776-940)
- AWACS 系では codec status の端子ビットと port-change IRQ を使う。
  機種でビットと極性が異なる。N は割り込みから処理スレッドを起こす。
  (N:awacs.c:427-479,1247-1291、F:davbus.c:434-457,578-600)

## 6. 停止・再開・バッファ不足

N の I2S halt は `dbdma_stop` → `dbdma_reset` → callback 解除。
stop は RUN を下げて FLUSH し、ACTIVE と FLUSH が落ちるのを待つ。
O の halt も stop/reset する。再始動は
trigger で記述子列を作り直して先頭から開始する。低水準の
`dbdma_continue` (RUN+WAKE、PAUSE/DEAD 解除) は別操作。
(N:dbdma.c:69-114、N:snapper.c:992-1015,1369-1468、O:i2s.c:279-298,583-664)

F の AOA は条件分岐で STOP に移すよう指示し、40 ms 待って stop と
状態クリアを行う。次の START は slot 0 から。N の I2S の PMF 登録は
suspend/resume の callback が NULL。電源復帰後の codec・クロック・GPIO
再設定の要否は QuickSilver 実機で [未]。
(F:aoa.c:258-298、N:snapper.c:757-763)

完了 IRQ は消費した区間を知らせる。上位からの PCM 補充不足は、
DBDMA の DEAD や I2S FIFO の状態と別に扱う必要がある。引用した
N/O/F の転送完了処理は記述子の状態を消して上位を呼ぶ経路で、
QuickSilver における FIFO underrun の検出・通知・自動復旧の仕様は [未]。
周回記述子が指すメモリを更新しない場合、同じデータを再び送る可能性がある [推]。
(N:snapper.c:879-914,1369-1417、O:i2s.c:129-160,583-622、F:aoa.c:71-110,317-336)

## 7. QEMU mac99 の資料で確認できる範囲

ローカル版は 8.2.2 (Debian `1:8.2.2+ds-0ubuntu1.18`)。
採取先は `/home/hight/os32-tmp/run/ppc-doc-av/`。

| help / 文書 | 確認結果 |
|---|---|
| `-M mac99,help` | USB の on/off がある。音声固有の machine option は一覧に出ない (`qemu-audio-help.txt:1-20`) |
| `-M mac99 -device help` | PCI の AC97・ES1370・HDA・virtio-sound、USB の usb-audio、ISA の音源等が列挙される。登録された候補の一覧で、mac99 への接続・ゲスト再生は [未] (同 :205-219) |
| 個別の `-device NAME,help` | Screamer、AWACS、DAVbus、I2S、Burgundy、DACA、Tumbler、Snapper、Onyx の各綴りは登録名として見つからない。USB audio・ES1370・AC97 は `audiodev` を公開 (`qemu-audio-device-help.txt:4-71`) |
| `-device macio-newworld,help` | 公開の子プロパティには DBDMA、GPIO、IDE、ESCC、OpenPIC 等が出る。音声関連の子は表示されない (同 :73-96) |
| `-audio help` | none、ALSA、OSS、WAV はホスト側バックエンド (`qemu-audio-help.txt:282-286`) |
| QEMU-PM (11.1.50 master) | `qemu-ppc-powermac.html:146-175` の対応周辺装置一覧に音声なし。missing の一覧は未記入 |

採用できる結論はローカル版の公開オプション・型の一覧まで。
mac99 が標準で公開する OF の音声ノード、埋め込みモデルの有無、実際の再生は
help と配布文書の範囲では [未]。MacIO の DBDMA が表示されるだけでは、
音声チャネルの動作は確認できない。

## OS32 の HAL の口と照らす時の要点

- 装置列挙の根拠は OF のバス、codec 参照と資源。QuickSilver と mac99 は別々に能力を確認する。
- 各 BSD が使う signed 16 bit BE と、AV HAL 確定設計の S16LE / U8 には形式差がある。
- 可用レートとチャンネル数は codec とクロック設定に依存し、固定 44.1 kHz の実装例もある。
- 位置と補充の単位は DBDMA の記述子の完了区間。まとめて完了した複数区間を数えられる。
- RUN/ACTIVE/FLUSH、記述子の更新、無音区間は停止・一時停止・drain の照合に関係する。
- PCM の転送、I2C の codec 制御、GPIO の端子切替は独立した経路と割り込みを持つ。
- PCM 補充不足、DMA エラー、codec やクロックの初期化失敗は別の事象として観測される。

HAL の前提は [TASK_AV_HAL](../tasks/v3/TASK_AV_HAL.md) §1 と
[AV_HAL_DESIGN](../decisions/sound/AV_HAL_DESIGN.md) §3。
