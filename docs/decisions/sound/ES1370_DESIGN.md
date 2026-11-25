# ES1370 を共通 PCM 出力につなぐ設計 r3
2026-11-24／OS32 main 3497386e7。以下の台本・変更量・受入値は実装案。ゲストでの採取は後続段で行う。

## r2→r3 の変更

- P1-1　段1・2で停止証拠、先読み上限 F、初期位相を採取し、確定できない場合の保留条件を定めた。
- P1-2　毎回の start で DMA 公開後に Master を設定し、読戻しを開始条件にした。
- P1-3　消費下限と転送位置を同時に HAL に渡し、補充完了までの到達範囲を除外した。
- P1-4　末尾無音の計数規則と、DRAIN 完了時に同一 epoch の結果を保存する試験口を定めた。
- P2-1　PC-98 の BAR 窓経路と、ページ確認から読取りまでの装置排他を指定した。

## 1. 対象と出典

目的は mac99 に PCI カードを追加し、PowerPC 上のアプリから共通 PCM を経て WAV に届くまでを CI で測ること。
実機 G4 の内蔵音源は別装置である。先行調査 ppc-sound-survey/OUT.md の内蔵音源の受入条件はそのまま使える。

|POLICY_DEV §1 の判断|今回の結論|
|---|---|
|同じ役目はどこにあるか|CS4231 と同じ PCM_OUT。装置一覧、所有者、位置、drain は既存の HAL 設計に従う。|
|HAL で一つにできるか|ES1370 の状態と操作列は drivers/sound に一つ。両機種が同じ後端を登録する。|
|計画に合うか|TASK_AV_HAL の PCM 一本を TICK 段 B の上で試す。デバイス専用 KAPI やミキサは増やさない。|

[A] メーカー一次資料は ENSONIQ CONCERT Specification Rev 2.3、1997-01-17、§3 Register Map。表の番地は PCI I/O BAR0 基準。[資料 PDF](https://ftp.kolibrios.org/users/Asper/sound/Ensoniq/ES1370.PDF)
[N] NetBSD sys/dev/pci/eap.c rev 1.103、eap_set_format、eap_trigger_output (980–1067)、eap_halt_output、eap1370_mixer_set_port。[固定版](https://cvsweb.netbsd.org/bsdweb.cgi/src/sys/dev/pci/eap.c?rev=1.103&content-type=text/plain)
[R] 同 eapreg.h rev 1.14、EAP_ICSC から EAP_SIC、191–213 の INC・CSR・ページ・サイズ・原時計。[固定版](https://cvsweb.netbsd.org/bsdweb.cgi/src/sys/dev/pci/eapreg.h?rev=1.14&content-type=text/plain)
[O] OpenBSD sys/dev/pci/eap.c rev 1.66、eap_trigger_output (841 以下) のリング、周期、開始の順を比較する。移植時は BSD 表示を保持する。[固定版](https://cvsweb.openbsd.org/src/sys/dev/pci/eap.c?rev=1.66&content-type=text/plain)
[Q] mac99 の公開文書と WAV バックエンドのオプションが確認範囲。手元の QEMU 8.2.2 で ES1370,help に audiodev がある。[機種](https://www.qemu.org/docs/master/system/ppc/powermac.html)・[起動オプション](https://www.qemu.org/docs/master/system/invocation.html)
WAV はホスト出力であり実カードのアナログ経路を測れない。codec の利得・DMA 先読み・IRQ の実機との一致を QEMU の保証に含めない。

|対象|番地と採用幅（bit）|採取・設定する意味|根拠|
|---|---|---|---|
|PCI 識別|構成 00/32|下位 vendor 1274、上位 device 5000。ES1371 を除く|PCI 構成ヘッダ、N の match|
|Command/Status|構成 04/32 読、04/16 書|Command bit0 は I/O、bit2 は bus master。Status の W1C を保護|PCI ヘッダ、pci_config_contract.h|
|class/revision、BAR0|構成 08,10/32|版と資源を記録。BAR0 bit0=1、下位2 bit を除いたバス上の先頭|PCI ヘッダ、N の attach|
|ICSC|00/32|DAC2 enable bit5、分周欄 [28:16]。無関係な bit を保存|A §3 IRQ/Chip Select、R EAP_ICSC|
|STATUS|04/32 読|bit31 割込み、bit1 DAC2 要因。読み取りで ACK しない|A 同 Status、R EAP_ICSS|
|MEMPAGE|0c/32|下位4 bit が 30–3f のページ。DAC 用は c|A Host Interface、R EAP_DAC_PAGE、N 1036|
|CODEC|10/16 書|上位バイトに AK4531 の番地、下位に値。台本からは触れない|A CODEC Interface、N の codec 書込|
|SIC|20/32|DAC2 stereo/16 bit は bit2/3、割込み許可 bit9。開始増分0、終了増分2|A Serial Interface、N 1007–1023|
|DAC2 CSR|28/32|下位16は周期 frame 数−1、上位16は現在の周期カウンタ|A Serial Interface、R 194–196、N 1041|
|DAC2 frame address|page c:38/32|先頭 PCI DMA アドレス|A Host Interface、R 198–206、N 1036–37|
|DAC2 frame size/count|page c:3c/32|下位16は最終 dword 番号、上位16はPCI 転送済み dword 数|A Host Interface、R 206–209、N 1038–40|

資料上のリセット値を起動後の期待値にしない。ファームウェアがカードを列挙した後で読む。
実チップの BAR0 は [15:6] が基底、64 byte 整列。QEMU 8.2.2 の停止起動で報告された資源長は256 byte。台本は先頭64 byte 内だけを使い、実際の割当長は毎回記録する。
codec の採用列と未照合値は §3 に示す。

## 2. HW-BASIC の調査票

置き場案は userland/tests/hw/es1370_probe.hwb。POLICY_PCIDEV §1 と hpt370_probe.hwb の device/pciread/print に合わせる。
実ファイルの先頭には A の版・節、N の版・関数を残す。オフセットの単位は byte。
台本の装置・構成・ポート書込みはすべて 0 回。BAR サイズ探索も禁じ、起動時の割当表で 64 byte の範囲を確認する。
提案する窓は 1274:5000 の一意な所有橋/BDF、Command.IO、割当済み BAR0 と資源長を確認。offset は下記7値だけ、自然整列で off+4 ≤ length を拡幅検査。PC-98 は base+off+3 ≤ 0xffff も要求。各命令は LE32 単発読取り、page 窓は専有中かつ page=c の時だけ許可。 両機種の PCI 資源管理側が橋/BDF ごとの排他を所有し、ドライバのページ選択・アクセスと共用する。台本の先の PAGE 表示に依存せず、38/3c 各読取りの直前に同じ排他内で PAGE を再読し、不一致・専有競合は窓を読まず失敗とする。

```text
device d = id 1274:5000
pciread32 d 0 -> id
print "ID" id
pciread32 d 4 -> cmd
print "COMMAND_STATUS" cmd
pciread32 d 8 -> rev
print "CLASS_REV" rev
pciread32 d 0x10 -> bar
print "BAR0" bar
if (cmd & 1) != 1 goto done
if (bar & 1) != 1 goto done
let base = bar & 0xfffffffc
if base == 0 goto done
if bar == 0xffffffff goto done
window regs = bar d 0
mmioread32 regs + 0 -> v
print "CONTROL" v
mmioread32 regs + 4 -> v
print "STATUS" v
mmioread32 regs + 0x0c -> p
print "PAGE" p
mmioread32 regs + 0x20 -> v
print "SERIAL" v
mmioread32 regs + 0x28 -> v
print "DAC2_PERIOD" v
if (p & 15) != 12 goto done
mmioread32 regs + 0x38 -> v
print "DAC2_ADDR" v
mmioread32 regs + 0x3c -> v
print "DAC2_SIZE_COUNT" v
label done
end
```

1. mac99 の既存 KDEBUG 有効受入像に台本を同梱し、既存の起動引数に次の二項を追加する。
   `-audiodev wav,id=a0,path=es1370.wav,out.frequency=44100,out.channels=2,out.format=s16 -device ES1370,bus=pci.1,addr=0x0f,audiodev=a0`
2. シリアルから `hw run /test/hw/es1370_probe.hwb`。QEMU 版、機種、BDF、橋、資源長、終了値と全ラベルを保存する。
3. 停止起動の qtree/info pci で pci.1（uni-north-internal-pci）配下 00:0f.0 を確認。OF 起動後の同じ橋のノード・assigned-addresses・I/O ranges と cfg BDF を採取し、子 I/O 区間全体が親窓内で一意に変換できることを照合する。
4. 段1合格には上記対応、ID・Command.IO=1・BAR0 の割当と窓読み成功がすべて必要。現行の HW_NOSYS は部分観測。
5. Command.IO 無効や未割当なら cfg のみと記録。正規の PCI 資源設定を行った起動像で再試行する。
6. PC-98 は PCI と ES1370 を提供するエミュレータがある場合に同じ手順。手元の標準機種一覧で PC-98 は確認できず。
   対応機で KDEBUG 像を起動し同じ hw run を使う。装置不在も記録し、PC/AT の結果で代用しない。
7. 合格記録には ICSC・STATUS・PAGE・SIC・CSR が必要。page≠c なら窓二語は対象外と明記し、ページを切り替えない。

## 3. PCM 後端

AV_HAL_DESIGN §3-7 の SndBackend に以下の関数を割り当てる。KAPI の番号や構造体配置は機種ごとに決める。
```c
int es_caps(void *ctx, SND2_Caps *o);
int es_open(void *ctx, const SND2_Fmt *f, u32 period, u8 **ring);
int es_start(void *ctx);
int es_pos(void *ctx, SndDmaPosition *out); /* 同一 epoch の累積 frame 下限 L と転送位置 T（各 u64） */
int es_ack(void *ctx);
int es_stop(void *ctx);
void es_release(void *ctx, int leak);
int es_volume(void *ctx, u32 l, u32 r);
```
登録値は abi=SND_HAL2_ABI(1)、DevInfo={size=64,dev=中核採番,kind=PCM_OUT,flags=PRESENT,name="ES1370 DAC2",driver="es1370"}。caps は size=256、同じ dev/kind、fmt 1件、period 256..2048 の2冪、ring_frames=4096、volume_steps=32、他の能力・予約欄0。
一致した PCI function ごとに ctx を持ち、snd2_register で PCM_OUT を公開。初版の能力は 44100 Hz、S16LE、左右各1。
DAC2 分周は整数 1411200 / 44100 − 2 = 30。要求外の形式は open 前に拒否する。DAC1、ADC、MIDI、DSP は公開しない。
PCI 発見は既存の列挙結果を使う。e82557_eeprom.c の pci_bar_kind/base と同じ検査を行う。
構成アクセスは sys/include/pci_config_contract.h 経由。platform/{pc98,powermac}/pci_config.c が橋へ配送する。
資源ハンドルは root・BDF・BAR・種別・base/length を保持し、LE32 読書きと codec 用 LE16 書込みを自然整列・範囲検査付きで提供。リング公開前と各補充後に DMA 用 release barrier、隔離済み割当は機種の池へ戻さない。
PC-98 はポート I/O、PowerMac は OF ranges で PCI I/O→CPU 窓へ変換し LE32 と順序保証を提供する。
音声ドライバに機種ヘッダを入れず、台本とドライバで同じ資源翻訳を使用する。
OHCI の hc->dma[].bus と同様に CPU のリング VA と装置に渡す bus address を別々に保持する。
dma_alloc で連続 16384 byte を 16384 整列で取得。変換後も 32 bit に収まり、先頭と末尾が同じ 64 KiB 区画か調べる。
末尾算術を拡幅する。失敗時は開始せず解放。PPC は coherent な割当か、各 HAL 書込み後に同期できる契約が必要。
初版は coherent なリングだけを返す。CPU の整数を直接置かず、左右順の LE 音声バイト列を保つ。
open は停止中に ICSC.CDC_EN を立てる。N の eap1370_write_codec に合わせ各 LE16 書込み前に STATUS & 0x100 が0になるまで最大1 ms（OS32 予算）待ち、超過は失敗。
eap_attach の AK_RESET=PD → PD|NRST → AK_CS=0 を採用。eap1370_mixer_set_port に従い OUT_MIXER1=0、OUT_MIXER2=VOICE_L|VOICE_R、MASTER/VOICE 左右は減衰コード0から開始。実カードでの0 dB 対応、reset 保持・解除後の時間、ready 最悪時間は AK4531 資料との照合待ち。
open は DAC2 停止中に形式、分周、codec を設定。codec busy には有界待ち、VOICE と MASTER の左右を開き VOICE を出力へ接続。
HAL がリングを満たした後、start は page c、DMA 番地、SIZE=4095、CSR=period−1 を設定。
SIC はループ再生、pause 解除、DAC2 IRQ 無効。初回・再 open・再同期とも DMA 公開 barrier 後、Command を16 bit RMW で Master=1 にし、IO と Master の読戻し成功後だけ DAC2_EN を立てる。失敗は共通停止へ進む。
pos は割込み禁止の短区間で page c を選び、3c を一回読む。S16LE stereo は 1 dword/frame。
段1の HW-BASIC で停止時 PAGE・生 count を記録。段2は専用リングに連番印を置き、enable 前後・最初の転送・折返しで count と観測可能な DMA 読みの番地・長さを突合し、次の未転送 frame T に換算する初期位相を決める。以後 count 差を mod 4096 で累積し、範囲外は失敗。
mac99 の固定構成で DMA 読みと DAC 消費の印を共通時刻で対応付け、起動・定常・折返し・停止直前の T−C を測る。観測分解能、バーストと取りこぼしを含め全許容位相で覆える上限 F < 256 を段2出口に記録する。WAV 単独や有限標本の最大値だけでは上限未確定とする。
hw_pos の一回の標本から HAL に L=max(0,T−F) と T を返す。HAL は消費・played に L を使う。次周に書く絶対 frame 区間 [a,b) は b−4096 ≤ L かつ a ≥ T+B の場合だけ補充する。B は採取から書込み公開 barrier 完了までの最大 DMA 前進量（消費速度×最悪所要時間を切上げ、先読み F と計時誤差を加算）。割込み遅延込みで上限を確保できない時は書かず停止・再同期。周回を跨ぐコピーも絶対位置で全体を検査する。
DRAIN 入口で同一 epoch の最終有効 frame の直後 E を固定し、以降のリングを意図的な無音で埋める。この無音は written・played・underrun の加算対象外。E より前の欠配は通常どおり数える。L ≥ E で完了し、公開 played は有効音声分まで。


期限は HAL が共有 fast slot を排他取得し notify を登録。競合なら既存所有者に触れず今回の open を逆順に戻す。arm(0) は期限だけ解除する。HAL/TICK に所有トークンを照合する解除口を追加し、登録 NULL は使わない。
DAC2 割込みは使わず es_ack は0、V86 用タイマ口は NULL。
IRQ は世代付き pending を立てるだけ。KAPI 復帰・協調 yield・idle の各配送点で advance、補充、再 arm。period=256 は約5805 µs、半周期で再観測。観測間隔が (4096−F)/44100 秒に達したら wrap を推定せず stop、成功時のみ epoch 更新・再充填。
停止・開始途中の失敗・再同期は同じ手順。DAC2_EN を落として readback、専有 function の Command.Master を16 bit で落とし readback、その後に機種の drain 証拠を要求する。
段2では DMA 読みが進行中の複数位相で停止を要求し、最終要求の完了と新規要求の遮断を識別できる橋の fence 完了または outstanding=0 を転送記録と照合する。対象 UniNorth／PC-98 の橋ごとに、停止時にも取得できる証拠とその読取り順を記録する。制御 bit の読戻しや count の静止だけでは成功にしない。
待ちは IRQ に依存しない単調時計で最大1 ms（設計予算）。証拠なし・期限超過なら即隔離し再開禁止。段2で遅延中の転送を残した模型で解放拒否を確認、保証を持つ機種だけ登録する。
停止の証拠を得られなければ es_stop は負値、es_release(leak=1) は dma_mark_leaked、HAL が FAULTED にする。
close は closing と世代更新、disarm、pending の無効化、停止、リング解放または隔離、所有 slot の返却まで。正常 close 後は同じ ctx で再 open。装置 ctx は snd2_unregister 成功時だけ解放し、FAULTED/BUSY なら保持する。
written・played、無音の扱い、pause、drain、v1 の 4096 frame 自動開始は HAL が持つ。
前景の切替で音を止めず、所有者の close・終了で回収。別所有者の open は BUSY。音量は左右の codec 減衰へ整数換算する。

## 4. 試験と所要

|試験|合格の観測|一行変異|
|---|---|---|
|host 資源模型|二つの橋に同一 I/O 番地を置き対象だけを読む。台本の write 数 0|最初の橋に固定|
|host 開始列|非恒等 DMA 翻訳、停止中の 38/3c/28、最後の enable を記録|38 に CPU の番地を書く| 再 open・再同期でも Master=0 から再設定し、読戻し失敗で enable しないことを確認。
|host 境界|bus=0x1e000 の 16 KiB リングを拒否し解放|境界検査を除去|
|host 位置|上位 count が 4094→2 なら経過4、CSR の値を変えても一定|pos の番地を 28 に変更| FIFO 遅延 0..F、折返し、コピー中の最大前進 B で既読の次周区間への補充を検出。
|host 期限|時計 IRQ 中の I/O・コピーは0、遅延一周で epoch 更新|notify 内で advance|
|host 解放|停止確認失敗で DMA 隔離、次の open は IO|leak を常に0にする|
|mac99 WAV|左右別の印と周期番号をすべて順に復号し欠け・重複0|左右の word を交換|
|mac99 形式|既知の符号付き標本の大小・符号が左右それぞれ一致|上位と下位 byte を交換|

公開 PCM の open/write/start/position/close(DRAIN) を通し、period 256・lead 3 で3秒送る。
入力 n=0..132299、k=n div 441、j=n mod 441、A=0x1234。L は j=0 で偶数 k なら +A、奇数なら −A、他は0。R は j=1..16 に k の bit(j−1) を下位から順に、1=+A、0=−A で置き、他は0。
最終 frame だけ (L,R)=(−2A,+A) に置換。ホスト側はこの式から独立生成。固定 unity 利得、各標本の許容差 ±1 LSB、先頭印より前と末尾印より後も同じ許容差内の無音に限る。
close(DRAIN) を呼ぶ前にゲスト試験が stream と epoch を指定して HAL の試験用保存枠を arm。HAL は L ≥ E の判定時、世代更新・teardown より前の同じ排他内で epoch、written、played、under、rep、resync を一括保存する。close 復帰後は保存枠を試験トークンで読み、停止失敗・epoch 不一致・記録欠落は不合格。
ゲストは played=written、under=rep=resync=0 と前景切替前後の所有者を照合して終了通知。ホストは QMP quit 後の正常終了、RIFF/data 長・PCM/44100/2/16・frame 境界、両印間の132300 frame 全標本を検査。左右交換、word の byte 交換、最終 frame 削除の各変異で不一致を確認。
CI 追加予算は起動 15–30 秒、準備・再生・終了 5–10 秒、比較 1 秒以内、計 21–41 秒。実測前の割付は 45 秒。
weights.json に登録し、同じ runner で初回3回の経過と WAV サイズ（音声部分約517 KiB）を記録して更新する。

## 5. ファイル別の変更案と実装順

|段・工数|新規／変更予定ファイルと概算行数|完了条件・依存|
|---|---|---|
|1a 半日|新 userland/tests/hw/es1370_probe.hwb +45、既存像の台本収録表 +1|cfg 採取、橋・I/O 資源・不足する読取り口を特定|
|1b 1日|変更 sys/platform/powermac/hwb_resource.c +40、hwb_window.c +35、新 platform/powermac/pci_io.c +90、新 platform/pc98/hwb_bar_window.c +60 と PC-98 HW_OP 配送の接続 +10|PC-98 は window bar → BDF 資源照合 → 窓 handle → mmioread32 → 検査済み base+off の inl。両機種で BAR 指定 I/O 窓で段1の値を観測。TICK 待ちなし|
|2a 半日|新 include/pci_io_contract.h +25、platform/pc98/pci_io.c +35、両機種の DMA アダプタ各 +35|範囲・LE・bus 翻訳・coherent 契約を模型で確認|
|2b 1日|新 drivers/sound/es1370/regs.h +65、pcm_hw.c +260、tools/tests/es1370_host.c +180|観測後に装置列を実装。HAL と時計は模型で代替可能| 段2出口で停止証拠・F・初期位相の観測記録を確認。一つでも確定できなければ当該構成の PCM 統合と段4受入を保留する。
|3 1日|変更 両機種の PCI bind 各 +15、sound/hal の期限配送・位置対・補充判定・DRAIN 保存口 +85、build/targets/ppc-powermac.mk と PC-98 の対象規則各 +10|TICK 段 B の残りと SndBackend 中核、PPC の期限配送・KAPI 結線後に実機像へ統合|
|4 半日〜1日|新 userland/tests/es1370_pcm.c +85、tools/tests/es1370_wav.py +100、ppc-qemu 起動規則 +20、tools/checks/weights.json +1|段3後に WAV とシリアルの同時合格|

段3の着手条件は TICK 段 B、AV HAL S1–S6、K1、E1。PPC 像の対象規則へ HAL 中核、KAPI 表、時計所有 API、協調配送、PCI bind、DMA アダプタ、共通後端を結線し、ゲストの open から期限配送まで確認する。
1–2 は進行可。段3で本線の所有者・解除・補充を再利用し、段4のゲスト試験まで別の暫定 PCM KAPI は設けない。

## 6. 像とメモリの見積もり

|対象|増分予算|計測方法|
|---|---|---|
|PowerMac の PCI 音声有効カーネル|text/rodata 約4–7 KiB、ctx 約0.25 KiB/台|同一設定のリンク map と size -A の差|
|PC-98 の PCI 音声有効像|共通部と小さい port アダプタで約3–5 KiB|CS4231 のみの像と別計測|
|PowerMac KDEBUG の I/O 読み口|約1–2 KiB、台本は試験 FS 約2 KiB|カーネルと kdebug の配置別に差を取る|
|再生中の RAM|DMA 16 KiB、HAL staging 8192×4=32 KiB|open 一回につき約48 KiB、隔離時は DMA 分を保持|
|CI 試験像|ゲスト試験約2–4 KiB、比較器・WAV は host のみ|梱包後の像長、録音ファイル長を別記|

HAL 共通化本体の増分は本線で計上する。カード不在時はリングを確保しない。上表は未ビルドの予算。

## 7. 未決

ユーザー判断は 0 件。初版の対象と互換の範囲は決定済み。codec 待ち時間の照合と I/O 窓・停止証拠の検証は実装担当が段1–2で行う。

## 確定 (2026-11-24、sol 第 3 回「3 点を反映すれば確定してよい」、PM が反映)

本節は §3・§5 の該当の記述より優先する。

- **R3-1 (P1・§3 補充ガードの適用範囲):** 稼働中のリングへの書き込みはすべて (次周の補充・初周の追補・pause のゼロ埋め) `a ≥ T+B` を満たす区間に限る。再利用する区間には、前の周の消費の確認も課す。初周は前の周の確認を省く。現行 HAL の `p+1` 判定は ES1370 の後端では使わない。
- **R3-2 (P1・§3 DRAIN の終端):** DRAIN の入口では `written` を論理の終端として固定する。最後の有効なフレームをリングへ置いた時点で、その epoch の中の DMA の座標 `E` を確定する。再同期 (epoch と DMA の座標が変わる) が起きたら `E` を無効にして、同じ手順で確定し直す。
- **R3-3 (P2・§5 BAR 窓の実装順):** 両機種の PCI の I/O の契約・資源の翻訳・窓の読みとドライバの排他は段 1b に置く。段 2a は DMA のアダプタだけを扱う。

状態: **設計まで (ユーザー 2026-11-24「実装はしないでね」)**。実装の段は起こさない。mac99 の DMA の停止の証拠・先読みの上限 F・初期位相は段 1・2 の観測で決め、採れない時は PCM の統合を保留する (PM 決定)。
