# TRIDENT_V6_DESIGN — Trident ProVidia 9682 の設計 v6 (段 2 を HW-BASIC で実機に出し、ドライバへ移す)

> 出所: ~/os32-tmp/run/trident-r6/DESIGN.final.md (astra 起草 r1→r2、Opus レビュー 2 回、N1 は PM が反映)。
> 状態: **現行 (2026-11-05)** — 段の状態は票 TASK_TRIDENT_DRIVER が持つ。

# Trident 設計票 v6 r2 — HW-BASIC で 640×480×8 を確かめてからドライバへ

作成: 2026-11-05 / モデル ID: GPT-6。設計のみ。シングルタスク・協調切替、Ra266 内蔵 9682 と 98 表示の共存を前提とする。

## r1→r2 の変更
- F1: 汎用 `mmiofillW address count value` を追加し、行単位の描画、HW-BASIC正典/実装、既存試験への追記を段2aに置く。
- F2: 順6の直後に protect_off → Enable、パレット→作画 とし、長い作業中の同期リセットを解除する。
- F3: CR03=80h・CR05=1Eh に訂正し、試験期待値は KGA_ENABLE_ON_ZERO の式から求める。
- F4: mode=0でGR06を採取し、段2bで元値維持/05hの冷間比較、§5にレガシー窓の重なりを追加する。
- F5: 11-05の根拠をT:109–114へ訂正し、実物との食い違いを3件に修正する。
- F6: xnfcallocによるゼロ初期化を根拠にCR3A/CR50へ00hを書き、CR3Aの元値をログに残す。

## 1. v5 からの変更点と実物の確認

以下の `T` は `docs/tasks/realhw/TASK_TRIDENT_DRIVER.md`、`H` は `docs/decisions/realhw/HW_BASIC_DESIGN.md`、`S` は `userland/tests/hw/trident.hwb`。リポジトリは指定された読み取り専用の木。
参照略号: `D` = `/home/hight/os32-tmp/refs/trident/trident_driver-1.4.0.c`、`A` = `/home/hight/os32-tmp/refs/trident/xf86-video-trident/src/trident_dac.c`、`P` = 同 `trident_pll.c`、`R` = 同 `trident_regs.h`、`V` = `/home/hight/os32-tmp/refs/xorg-vgahw/vgaHW.c`。いずれも MIT 系。

| # | 実物との食い違い・旧前提 | v6 の扱いと出典 |
|---|---|---|
| 1 | T:121–122 の PCI config だけでは 9680/9682 を識別できないという制限に、チップ側の採取結果が加わった | T:109–110 の SR09=10h は 9682 (`D:2525–2529`)。PCI configだけの限界は維持し、識別済みとして扱う |
| 2 | T:135–140 は BAR1 の VGA レジスタ配送未確認 | T:110 により BAR1+3C0h〜3DFh は応答確認済み、Command=0002 のまま使用。43Cxh/83Cxh の個々の機能まで確認済みとはしない |
| 3 | T:252 はモニタが I-O DATA・型番未記録 | 今回は NEC LCD172VXM (`T:111`)。過去の別モニタの測定値を移用しない |

- 食い違いは上の **3 件**。11-05 の新事実は `T:109–114` に記録済み。採取ログ・写真そのものは本作業では未閲覧。`RA266.md:28` の「2MB」は機体記録であり、今回の VRAM 実測ではない。BAR0 の全 00 もリニア窓無効の証明ではない (`T:104–108`)。
- Enable→無信号→Disable→98 表示は切替操作の有力な証拠。ただし v5 の R1(b') は「テスト画が出た往復」(`T:494` 付近、§5 の注記) なので、まだ完了にしない。欠け・白点も残っている。
- v6 の段 2 は **HW-BASIC 探索**とする。恒久ドライバの R2〜R4 実装を先に要求する v5 §5 の順を変更し、段 3 の入口で満たす。HW-BASIC の停止時 undo なし (`H:68`) と今回の「止まっても構わない」承認を採用する。許可リストは設けない。
- メーカー資料: [NEC LCD172VXM/192VXM 取扱説明書 p.20](https://www.sharp-nec-displays.com/support/webdl/dl_service/data/display_j/manual/manu_LCD172_192VXM_No01.pdf) の工場プリセット・分離同期を参照。Trident 社「ProVidia 9682, 64-Bit UMA Win95 and Video Accelerator」[PDF 所在](https://datasheet.datasheetarchive.com/originals/library/Datasheets-AS2/DSAAXSA0005239.pdf) は取得失敗、レジスタ仕様書は未入手。以下のレジスタ根拠はメーカー仕様でなく MIT 実装であると明示する。
- FreeBSD は [trident(4)](https://man.freebsd.org/cgi/man.cgi?apropos=0&manpath=FreeBSD+12.2-RELEASE+and+Ports&query=trident&sektion=4) が ProVidia 9682 を列挙。別のカーネル実装を仮定せず、FreeBSD ports 系と同じ X.Org を手本とする (`T:233–234`)。GPL と docs/archive/ は参照しない。

## 2. 段 2 の HW-BASIC 版

`userland/tests/hw/trident_mode.hwb` → `/test/hw/trident_mode.hwb`。mode=0 は識別/容量採取、mode=1 は下表の全列、mode=2 は S の Disable と同じ単独復帰、mode=3 は mode=1 と同じ列で順3 の GR06 だけ元値 (段 2b の比較用)。include は無いので S の protect/dac/Enable/Disable を出典・許諾付きで同ファイル内の label に展開し、末尾 end を return にする。
`v=BAR1`、`f=BAR0`。VGA 論理番号はすべて `v + 番号` の MMIO。実 I/O は既存列の 68h/6Ah/FACh のみ。BAR/Command/Bus Master/ROM は書き換えない。レジスタ書込みの自動 readback はしない。
以下の SR/CR/GR は index をそれぞれ 3C4h/3D4h/3CEh へ write8、data を次バイトへ write8。AR は毎回 3DAh を read8 → 3C0h に index → 同じ 3C0h に data。8bit 書込みを基本にして順序を明示する。

| 順 | 手本・前提 | OS32 の命令列/値（左から順に実行） |
|---|---|---|
| 0 | `D:2525`, `D:2919–2975` SPR | BDF/ID/Command/BAR と SR09、GR06/CR3A の元値を記録（mode=0でも採取、mode=1でも変更前に保存）。CR1F&0F: 01=512KiB、02=6MiB、03=1MiB、04=8MiB、06=10MiB、07=2MiB、08=12MiB、0A=14MiB、0C=16MiB、0F=4MiB、その他=1MiB。0E は CR C1&11 が 00/01/10/11 の順に 20/24/28/32MiB。申告容量を隣接 BAR を越えて試さない |
| 0a | `D:616–692,3459–3479`, `R:302–325`, `V:1795–1806` | mode=1/3/4 で PC98TRIDENT96xxInit を ModeInit より先に実行。サブシステム有効化、SR0C bit4 による設定ポート分岐、AR10、CR11 unlock、FIFO/latency、MCLK 80MHz、lockを手本の順に実施。PC-98 PCI では MMIO 有効化を飛ばす (`D:519–521`)。特殊ポートも BAR1 MMIO。分岐値を記録 |
| 1 | `P:42–55` 基準発振、`A:256` New Mode | SR0B を選択して data read（New Mode）。CR C0 bit7 から NTSC/PAL の式を選ぶ。読値と計算済み PLL 値を記録。Ra266 の発振子の周波数を実測したとは扱わない |
| 2 | `D:1028–1034`, `V:574–604` | CR11 bit7 を落としてから `call protect_on`（S:123–132）。旧値を必要な変数に保存する |
| 3 | `V:845–872` 標準 VGA restore | 3C2←E3h、SR1..4←21,0F,00,0E（消画を保持するため SR1 の20hを追加）。CR11←下表値&7F、CR00..18 を下表順、GR00..08←00,00,00,00,00,40,05,0F,FF、AR00..0F←00..0F、AR10..14←41,00,0F,00,00。最後も消画状態を保つ。段2bの比較引数 `keepgr6=1` の回だけ GR06 は保存した元値を書き、既定値0では05hとする |
| 4 | `A:781–791` DAC hidden command | SR0B 選択/read → SR0E←C2。3C8 read → 3C6 を4回 read → 3C6←00 → 3C8 read。通常の palette mask とは別の hidden command を 8bpp/non-MUX にする |
| 5 | `A:793–810` 拡張 restore の順 | CR1E←80、CR21←20、SR0D←20、CR50←00、CR27←08、CR2B←00、CR29←旧値&CF、CR36←00（NoAccel、D:2069-2073 / X/dac:694-700）、CR2F←旧値\|10、CR2A←旧値\|40、CR3A←00（下記注）、CR38←00、CR39←(旧値&F9)\|06、CR55←1F、GR2F←旧値\|04、GR0F←(旧値&F0)\|02、CR13←50（640/8） |
| 6 | `A:871–907`, `P:60–135` | 9682 は非3D・NewClockCode=false (`D:2384,2525`)。v+43C8←PLL a、v+43C9←PLL b、v+3C2←EB（E3 の ClockSelect bits を08へ）、SR0E←C2。SR18/19 や新 PLL の式は使わない。MCLK は順0aで80MHzに設定済み |
| 6a | `D:1039–1040` | 順6の直後に `call protect_off`（SR00同期リセット解除・SR01消画解除）。直後に順10の Enable を呼び、以後の BAR0・パレット/作画中はリセットしない |
| 6b | `D:1036–1047`, `S:57–83` | 順6aの直後に S と同じ Enable を call（68←0E、6A←07,8F,06、protect_on、CR23&DF、CR29\|04、拡張DAC、GR23&FC、SR1&EF、protect_off、FAC←02）。拡張DACは83C8←4/83C6 read/83C8←4/83C6 write を3回、値は旧値\|06、旧値\|08、旧値\|01 |
| 7 | `D:2294,2313`, `A:649–658` | CR21=20 はリニア enable。BAR0=20000000 の bit20..25 は0、上位は PCI BAR が担当する候補。式は8bitへ明示縮小。高位 BAR での実際の応答は次の書込み試験で決める |
| 8 | OS32 探索（手本に容量の alias 試験なし） | f+0 と f+4AFFC を保存 → 異なる32bitパターン → 両方読取 → 逆パターン → 両方読取 → 保存値を復元。等値判定は if != で保存値を戻す共通 label へ進み、その後 Disable。アクセス自体の失敗は即停止なので§3の単独復帰へ。まず必要な 307200B の端点だけ。未確認の全 BAR を埋めない |
| 9 | palette は VGA DAC、`V:723–730` mask | 3C6←FF、3C8←00、3C9 に RGB の6bit値を順次書く。index 0..7 = 黒/赤/緑/黄/青/紫/水/白（各成分0または63）。f の 640×480、pitch640 に下記 mmiofill の行単位で色帯・四辺白枠・左右非対称マークを書き、代表点を読取 |
| 11 | `S:85–115` | `wait us 5000000` → Disable。先頭 FAC←00、protect_on、SR1\|10、拡張DAC&FE、GR23←(旧値&FC)\|01、拡張DAC&FD/&CF/&F7/&FB、CR29&FB、CR23\|20、protect_off、6A←07,8E,06、68←0F。end と終了値を記録 |

- `D:1064` の xnfcalloc で構造体はゼロ初期化される。`A:673–678,803` で9682に別値を設定しない CR3A も restore 値は00h。**CR3A/CR50とも手本どおり00hを書く**。CR3Aの変更前読値をログに残す。CR50=0 はカーソル無効、PCI Retry/GE は無効。CR39 は既定の PCI Burst を有効にする (`A:709–721`, `D:2079`)。
- `D:616–692` の PC98TRIDENT96xxInit を手本どおり ModeInit の前に呼ぶ。設定ポート、FIFO/latency と MCLK 80MHz も含む。Enable/Disable は `D:694–775` に沿う。
- `D:1374–1389` は TGUI に TRIDENTClockSelect を使わないと明記。PLL のあと MiscOut の bits3:2=10b を選ぶ `A:634–640,899–901` が今回の ClockSelect。
- CR21 の enable、43C8/43C9、83C8/83C6 の機能的配送は **探索対象**。3C0h 台の成功から一括して証明しない。PC98 Init の分岐と CR3A の変更前読値/00h書込みも実機ログに残す。

640×480、25.175MHz 目標、H=(640,656,752,800)、V=(480,490,492,525)、負/負分離同期、non-interlace。blank は表示端〜total。以下は `V:1240–1286,1391–1468` の式（KGA補正込み）、CR13 は `A:572–576,631` の8bpp値で上書き。市販 VGA mode13h の表を流用しない。

| CR index | 値（16進、index 順） | 意味 |
|---|---|---|
| 00–05 | 5F 4F 4F 80 52 1E | H total/display/blank/sync。KGA_ENABLE_ON_ZERO では HBlankEnd=HTotal=800、i=0 |
| 06–09 | 0B 3E 00 40 | V total/overflow/preset/maxscan |
| 0A–0F | 00 00 00 00 00 00 | text cursor/start address。表示開始0 |
| 10–14 | EA 2C DF 50 00 | V sync/display、pitch640、underline |
| 15–18 | DF 0B C3 FF | V blank end は20Ch→20BhのKGA補正、mode/line compare |

CR03/05の期待値は表の転記でなく `V:1400–1422` の式から計算する。初期blank endは(800>>3)-1=99、下位6bitは35、blank startは(640>>3)-1=79の下位6bit=15。`i-- > 15` が真かつend=totalなのでi=0、CR03=80h、CR05のbit7=0で1Eh（KGA_BE_TOT_DECの分岐ではない）。

PLL はホストの整数計算で定数化し、script 引数/分岐へ渡す（HW-BASIC に乗除算を足す必要なし）。`P:74–135` の旧式探索順 k→n→m、k=0..1,n=0..121,m=1..31 を維持する。
`f_kHz = floor(Fref_Hz*(n+8)/((m+2)*(1<<k))/1000)`、目標25175との差が最小の候補を採り、同差なら先を保持、初期許容差750kHzより小さいもの。Fref=14318180 または17734480 (`R:27–28`)。最大積は2,287,747,920でu32内、Cでは符号なし乗除算。
NTSC は n=101,m=29,k=1 → a=E5,b=1E、約25.172284MHz。PAL は n=36,m=29,k=0 → a=A4,b=0E、約25.171520MHz。a=((m&1)<<7)|n、b=((m&FE)>>1)|(k<<4)。水平約31.465kHz/垂直59.934Hzを OSD の丸め込みで照合する。
初期版はこの2定数を if で選ぶ。未識別状態を25MHzの成功に丸めない。PLL lock poll のレジスタ/時間は手本に無いので発明しない。

HW-BASIC 文法の実例（部品。上表の順に label を組み立てた完成 script を段2aで作る）:
```text
device d = bdf 0:8.0
window v = bar d 1
window f = bar d 0
mmiowrite8 v + 0x3D4 0x21
mmiowrite8 v + 0x3D5 0x20
let off = 0
let row = 0
label paint
let color = (row >> 6) & 7
let word = color | (color << 8) | (color << 16) | (color << 24)
mmiofill32 f + off 160 word
let off = off + 640
let row = row + 1
if row != 480 goto paint
```
- 枠は先頭/最終行を `mmiofill8` のcount=640、各中間行の左右をcount=1で白(7)にする。左右マークは位置/大きさの異なる小矩形を行ごとのfillで描く。水平色帯の高さ64、最終色帯32。乗算はpitchの累積加算で不要。
- 新命令は **`mmiofillW address count value`（W=8/16/32）**。countはバイト数でなく書込み回数（1以上のu32）、valueは幅内、addressは既存のphys/bar/window。countは単一数値/変数/括弧式、valueは行末までの式。自然整列と最終バイトの32bit越境を最初の書込み前に検査し、幅ごとに番地を進めて同じ値を書く。特定PCI装置への制限・BARサイズの推定・自動readbackは加えない。
- fillはshell内で既存WRITEを繰り返す。BAR解決はfillの開始時に1回、wire/KAPIは変更しない。各要素の前に既存POINTで中止を確認し、失敗/中止で即停止、書込みのundo/再送なし。ログはfill全体でbegin/endの1組（開始番地・幅・count・値、成功した回数・rc）とし、途中失敗の位置は開始番地+成功回数×幅から求める。固まった場合はend欠落として扱う。
- 現行の毎命令ログ (`userland/shell/hwb_exec.c:366,379`) ではr1の約54万命令が約107万行になるため、行fillへ置換する。色帯は480 fill、枠/小矩形も行単位でログ量を抑える。98側表示中に作画を終え、切替後の5秒に描画時間を含めない。32変数/64ラベル/64KiB/深さ8 (`H:47`) 内に収め、scratch変数を再利用する。
- VRAM容量は SPR の申告・実アクセス可能範囲・BAR decode 幅を別欄に記録する。段3前には BAR サイズを Memory Enable の一時停止→BAR 保存/測定/復元→Command16復元の別 script で測り、隣接装置との非重複を確認する（PCI status を巻き込むDWORD RMWは禁止）。容量境界の保存/異値/alias/復元試験は、その範囲内で別回に行う。段2で二面分を要求しない。

## 3. 保存・復元の範囲と実機での判定

mode=1/3/4 は `TRIDENTSave` (`D:1084–1104,3471`) を PC98 Init の前に実行し、通常終了・alias 検出・VRAM probe の終了とも Disable の後に `TRIDENTRestore` (`D:1111–1136,3343–3349`) を実行する。mode=0 は採取だけ、mode=2 は保存値を持たない単独の Disable だけである。前の run の配列を復元に流用しない。

保存順は palette → VGA mode → Trident、復元順は protect → Trident → VGA mode → palette → unprotect。`dim save 852` に次を保存する。標準 VGA の CRTC は保存 MiscOut bit0 に従い mono (3B4h/3BAh) または color (3D4h/3DAh) を使う。Trident の復元は実行中の color CRTC、標準 VGA 復元以後は保存した側を使う。

| 配列位置 | 保存・復元する値 | 手本 |
|---|---|---|
| 0–767 | DAC 256色の RGB 全成分。最初の6成分を読み、index1の3成分を反転・読取検査・元へ戻す。読めれば残りを保存し、読取不能なら手本の defaultDAC 全768成分を使用 | `V:90–155,1051–1140,876–892` |
| 768–827 | MiscOut、CR00–18、AR00–14、GR00–08、SR01–04 | `V:1016–1049,845–873` |
| 828–844,851 | SR0E、CR13/21/1E/27/2B/2F/2A/3A/29/38/36/39/55/50、GR0F/2F、SR0D | `A:912–1008,1039–1042,781–810,908–909` |
| 845–848 | hidden DAC command、クロック用 MiscOut、43C8/43C9 | `A:1012–1041,781–791,887–901` |
| 849–850 | 条件付き MCLK 43C6/43C7 の予約。既定 `savemclk=0` ではアクセスしない | `D:3046–3051`, `A:1033–1035,891–894` |

9682 は progClock=true、非3D・非Cyber、NewClockCode=false、TVChipset=0 と評価する。Protection、Cyber/3D/TV 専用の分岐は適用しない。MCLK オプションの既定は0なので、その条件は偽である。PC98 Init の80MHz設定はこのオプションと独立し、手本も既定では以前の MCLK を復元しない。SR0E の復元値には手本どおり XOR 02h を適用する。DAC mask は保存値でなく FFh へ設定し、SR00 は最後に03h、SR01 bit5 は最後に0とする（手本の protect/unprotect）。

フォント・テキストの面は保存・復元しない。`D:1095,1131` の IsPrimaryCard と text mode (AR10 bit0=0) の条件は、この機械では primary が偽である（PM 決定、ユーザー承認 2026-11-05）。primary は起動時の固定 VGA I/O を受け持つ装置であるが、Ra266 の Trident は PCI Command=0002h、I/O 応答が無効で表示は98側 (`TASK_TRIDENT_DRIVER §2-1`)。`D:3474–3478` も PC-9821 では VGA BIOS が初期化しないと記す。AR10 の値によらず font/text 条件全体は偽になる。

1. 冷間起動で98の固定文字列とカメラ画像、BDF/config、台本の版・ハッシュ、SR09/SPR/基準発振選択、シリアル記録先を保存する。内蔵装置を取り外さない。
2. `hw run /test/hw/trident_mode.hwb mode=0`、次に mode=1。GR06比較は mode=3、VRAM probe は mode=4。保存・復元を含む全列の begin/end、最後のseq、rcを採る。全成分の命令ログにより転送量が増えるので、記録の完了まで待つ。書込み済み命令を通信timeoutで再送しない。
3. 成功は640×480の全四辺・8色・左右マーク、OSD概ね31.5kHz/60Hz、5秒後の98固定文字への復帰、復元完了と end/終了値0。容量・窓も別に照合する。無信号や黒画面の往復だけでは R1 を更新しない。
4. 無信号なら PLL/CRTC/同期/ルート、同期あり黒なら DAC/blank/VRAM/表示開始、周期的な重なりなら pitch/alias を調べる。1回の列は途中で変更せず、失敗から単一原因を断定しない。
5. アクセス失敗・poll TIMEOUT・CTRL+STOP は即停止し、自動復元しない。シリアルが動けば mode=2 を1回実行する。この単独 Disable は失った保存配列を復元できない。CPU/バス停止や戻らない場合はユーザーがリセットし、最後のseqと復旧結果を記録する。
6. 正常往復と乱れの有無を別判定し、写真/シリアル/冷間か再入場か/リセット有無を T §9 の形式に残す。ホスト試験は保存が Init より前、復元が Disable より後で、手本の全アクセス順と値を満たすことを確認する。NP21/W は文法・停止・非搭載時の動作だけで、映像の合格には使わない。

## 4. 段 3（ドライバ）への移し方

| ファイル案（drivers/video/trident/ 以下） | 単一の役割 |
|---|---|
| probe.c | BDF/ID/SR09/SPRと資源の識別。pci bind の所有権と表示 ACTIVE を分ける |
| regs.c | BAR1 MMIO の8bit/indexedアクセス、AR/DACの順序。物理ポインタ変換を共通写像へ集約 [C5] |
| mode_640.c | 段2で確定した標準/拡張/PLL書込み列。各ブロックにscript版と出典を付ける |
| route_ra266.c | Enable/Disable の操作列だけ。正常とpanicが同じ復帰列を使う |
| present.c / palette.c | CPU矩形転送 / DAC設定（1機能1ファイル）。EGC/GRCG/GDC描画・GE無し [HW1] |
| backend_trident.c | HALと上の部品を接続。GFX=trident強制だけ、失敗時は既存PC98へ |

境目の擬似C（API名は案、公開KAPIの変更ではない）:
```c
/* master CR3 / live AS=0 の準備境界で、全写像成功後 */
trident_prepare_resources(&dev);       /* 検証→予約→supervisor PCD写像 */
gfx_arm_emergency(trident_restore98, &resident); /* 最初の装置変更より前 */
if (!trident_mode_640(&dev)) {
    trident_restore98(&resident);     /* IF=0可、割当/tick/GE待ち無し */
    return GFX_INIT_FAILED;
}
trident_present(&dev, test_surface);   /* 完成した面だけ公開 */
trident_route_enable(&resident);
```
- 実装では prepare 失敗を必ず返し armしない。R2/R3/R4 (`T:287–289`) を段3で実装し、復帰関数・データ・写像は shutdown後も保持する。一般例外/#PFはアプリfault分類後・TVRAM出力前、所有者終了/CTRL+STOP/初期化失敗から呼ぶ。子アプリfaultでは戻さない。
- 段2の一時 MMIO 窓を恒久ポインタに転用しない。`T:432–453` の資源台帳と私有leaseに接続し、表示面とMMIOをユーザーへ無条件公開しない。8bpp、整数のみ、SDK ABIは変更しない。
- 起動FDには Trident本体・試験script を載せない方針。ディスク/CD側からロードする部品を想定し、arm後は復帰コードも含め常駐・unload不可とする。ロード接続は段3実装時に現行module APIへ合わせる。
- 設計予算（未実測）: 本体 text/rodata 8〜16KiB、状態1〜2KiB、kernel共通の復帰/接続1〜2KiB、動的PT 4KiB×必要PDE（LFBとMMIOで候補2枚）。300KiBのBBは像/BSSに埋めない。scriptは64KiB以下。`T:465–473` の古い残量13KiBは現在値として使わず、同構成で image/text/BSS/__bss_end と常駐RAMを前後計測する。

## 5. 98 へ戻った直後の欠け・白点

| 原因候補 | 確かめ方（毎回冷間対照、1条件だけ変更） |
|---|---|
| レガシー窓の重なり | GR06=05hのA0000–AFFFF窓はPC-98のTVRAM/GVRAMと重なる。Memory Enable=1なのでI/O無効だけでは除外できない。mode=0のGR06元値を記録し、段2bで元値維持/05hの2回を冷間起動から他条件を同一にして比較。復帰後の欠け・白点とTrident出力中のシリアル行が98画面に出ない現象 (`T:112`) を別々に記録。応答の有無は未確認であり原因と断定しない |
| 98消画・6Aの切替中にもコンソールがTVRAMを更新した/表示状態だけが変わった | 固定文字を表示し、同じ量のログだけの対照とEnable/Disable回を比較。文字/属性メモリの前後比較はログ表示自身の書換えを避けた領域で行う |
| 98側の拡張表示F/F・DAC・CR23/29等が期待状態へ戻っていない | SのDisableの順と全操作ログを照合。復帰直後/再描画後の写真を分ける。再描画で消えれば修復結果であり、原因確定ではない |
| MMIOの幅/配送、83Cxh のアクセスが想定と違う | Sの16bit index/data書込みと8bit二操作を別試験で比較。MMIOをPIOへ置換しない。BAR1の通常/拡張DAC操作を段階ごとに記録 |
| VRAM共有/誤窓/alias | 既存症状はBAR0へ作画する前にも発生しているのでBAR0だけに帰因させない。段2はLFB書込み無し/端点だけ/全画面を別回で比較 |

通常のHAL再描画で戻るかを最後に調べ、初回写真を消さない。段2探索は続けられるが、段3の「98へ正常復帰」受入は乱れを再現・解消または復帰時再描画の必要条件として確定するまで未達。

## 6. ファイルごとの変更一覧・段分け・受入 → 試験と変異

| 段 | 変更するファイル（本票では変更しない） | 受入 → 試験 / 一意な変異の例 |
|---|---|---|
| 2a | docs/decisions/realhw/HW_BASIC_DESIGN.md、userland/shell/hwb.h・hwb_parse.c・hwb_exec.c・hwb_log.c | script作成前に汎用fillの文法/幅/回数/停止/ログを正典に追記して実装。既存tools/tests/test_hwb_scripts.pyへ実行器の偽bus試験を追加：8/16/32bitの連続値・終端、count=0/桁あふれ/不整列/幅外値は書込み0、途中失敗/中止の成功回数・後続停止、begin/endは1組 → 番地更新/回数/中止点/ログ集約を各別変異でRED |
| 2a | userland/tests/hw/trident_mode.hwb、userland/deploy.yaml | 任意PCI窓で実行可能、全構文・上限・mode=2単独実行 → host偽busで本番script実行 / FAC=0を2へ変更して復帰traceがRED |
| 2a | 既存 tools/tests/test_hwb_scripts.py（CHECKに新script/実行器/ログ等のinputsを追加し、script一覧をCHECKの先頭7件から分離） | 本番scriptを実行する偽busモデルを追加。CRTCはタイミングとKGA式から期待値を算出、総幅/pitch/PLL旧式/AR/hidden DAC/CR3A=00、順6→protect_off→Enable→BAR0→palette→paint、fill画像/ログ量を検証 / CR03=80→82、CR05=1E→9E、protect_off後退、CR3A書込み削除、CR13/PLL/ARの変異を各別に実行しRED |
| 2b | 同script、T §2-1/§9、必要なら docs/tasks/DEFERRED_TESTS.md | 実機のテスト画→5秒→98、LFB/容量結果、乱れの記録。冷間と再起動の証拠。写真はhost testで代用不可。GR06の元値維持（mode=3: mode=1 と同じ列で順3 の GR06 だけ元値を書く）/05h（mode=1）を毎回冷間起動から比較し、GR06/CR3A元値・画・復帰後の乱れ・シリアル行の表示を記録。元値が05hなら同条件で差を判別不能と記録 |
| 2c | userland/tests/hw/trident_vram.hwb、同manifest/test | BAR幅・SPR・aliasを区別、全保存値復元 → 偽VRAMのwrap/非応答モデル / alias判定と復元を別々に除去しRED |
| 3a | 上記drivers/video/trident各ファイル、部品のbuild規則/manifest | 確定scriptとCのbus traceが一致、CPU present/palette → 描画端点・pitchの試験 / 最終行転送を削除 |
| 3b | gfxの復帰管理/選択、両例外handler、所有者回収、paging/資源台帳の接続 | T §9 P2-a〜f/P3：arm前無書込み、IF=0復帰、写像寿命、子fault非復帰、10往復 / arm順序・片方handler接続・子判定を各別変異 |
| 3c | gfxmode/system.cfgの選択口と受入記録 | 実機GUI/正常復帰・NP21/WでPC98へfallback、像の増分計測。T §9 P1/P4/P5を証拠で閉じる |

- 実装時は依頼BASEでpreflight、触った像のビルド、check-changed MUTATE=0、追加/変更試験だけ --mutate、run.py --lint を当該依頼のROLES手順で実施。変異は正常対照GREEN→同じ環境で実行時RED、anchorは一意。全変異とlandingの全体buildはPM担当。
- 今回は指定ファイルだけの設計作業。リポジトリを変更するpreflight --fix-fonts、build/check/ゲスト/実機は未実行。整数PLLの候補探索はr1でホストPython照合済み。r2ではF1〜F6の該当箇所のみ照合し、CR03/05の整数式と差分を確認。メーカーのチップ仕様、拡張窓、容量、実機映像、常駐部品サイズは未確認を保持する。

## 7. 未決（ユーザー判断が要るものだけ）

**0 件**。2026-11-05「全て承認」を適用。推奨は上記の順で段2a→実機→容量確定→段3、整数/8bpp/CPU描画/FD外/強制選択。資料入手・拡張窓の配送・復帰時の乱れ・module接続の確認は技術作業であり、再承認の項目にはしない。

## 反論

なし（0 件）。F1〜F6 と PM 決定をすべて反映。

## 確定 (2026-11-05、Opus 第 2 回、PM が反映)
- N1 (P2): 未定義の初期変数 keepgr6 は ARG で止まる (hwb_parse.c:84-92、初期変数は mode だけ) → **mode=3 = mode=1 と同じ列で GR06 だけ元値**に変えた。引数は mode だけ。
- N2 (P3, 実装の依頼に書く): 色帯は 64 行ぶんの連続領域を 1 回ずつ (8 回) fill、左右の枠は「行末+次の行頭」を mmiofill8 count=2 (約 480 回)。writes は書いた要素の数。tools/rshell_hwb.py と test_rshell_hwb.py を段 2a に加え、fill の count・done を検査する (今は未知の op を素通し rshell_hwb.py:52-62)。
- N3 (P3, 実装の依頼に書く): 票の行番号は main で引用し直す (T:252→253、§4 の R2〜R4 は T:295 付近)。
