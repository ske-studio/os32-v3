# GeForce 6200 段 4c (画面モード) の立会いの台本と承認の資料

> 正典: 確定設計 [画面モード設定の契約](../../docs/design/hwdev/gf_load_state.md)。前の台本は [gf_main.md](gf_main.md)・[gf_a4.md](gf_a4.md)。本ファイルは受入の台本で、確定設計を変えない。
> 出所: ~/os32-tmp/run/gf-4c-accept/gf_c.md (astra 起草・Opus レビュー確定可・PM が「当日ユーザーがすること」と P3 の 3 件を反映、2026-10-29)。**書込みを伴う段 (A1・A2・main ごと・C) はユーザーの承認を取る。**


OS32 は単一利用者・単一タスク・協調切替で攻撃者を想定しない。対象はハードウェアの誤動作とバグへの頑丈さ。新しい防御・復帰列は足さない。
本資料は確定設計 §9 段4の台本であり、実施承認・実機合格ではない。各書込み段は直前の結果と影響を提示してユーザーが個別承認する（(記録はアーカイブ)、M:4）。
根拠の `略号:行` は次表のファイルに解決する。略号以外の相対パスも `/home/hight/os32-v3/` 基準。推測・未測定は実測と区別する。

| 略号 | ファイル |
|---|---|
| D | docs/design/hwdev/gf_load_state.md |
| V | docs/design/kdebug/gfinit_decode.md |
| M / A / S | tools/accept/gf_main.md / tools/accept/gf_a4.md / tools/accept/gf_s8.md |
| W / G | tools/rshell_recover.py / kdebug/gfinit.c |
| B / K | tools/tests/gfinit_budget_host.c / kdebug/kpeek_allow.c |


## 当日ユーザーがすること (PM がまとめた要約、2026-10-29)

1. **承認は 8 回**: A1 (BAR の大きさ)・A2 (prepare --mode-c)・main 0・1・2・3・4・C (modeset)。それぞれ、直前の結果と影響を PM が示してから「進めてよい」と答える。書込みの前に止めたいときは、そう言えば止める。
2. **modeset の後、モニタが GeForce の画面に切り替わる** (0FACh=01、98 の画面はモニタから消える)。**モニタの入力は変えない**。
3. **模様を見る**: 外周に幅 8 ドットの白枠、左上に 8×8 ドットの白黒の市松 (左上のマスは白)、残りは幅 80 ドットの 8 色の縦帯 (左から 黒・赤・緑・青・白・シアン・マゼンタ・黄)。**色・位置・静止しているか**と、**モニタの表示 (OSD) が「無信号」「範囲外」「同期あり」のどれか**を PM に伝える。写真があれば助かる。
4. **GeForce の画面の間は、98 のキーボードに触れない**。ノートのシリアルから abort を送って 98 の画面に戻す (届かなければ recover now)。
5. **98 の画面が戻ったら**、キーを打って画面が更新されることを確かめて伝える。
6. **98 の画面が戻らない・固まったとき**は、電源を入れ直す (S0 か OS32 の再起動で GeForce は元に戻る、固まったときだけ電源)。
7. 最後に OS32 を再起動する (GeForce のリセット)。kpeek で既定値に戻ったことは PM とノートが確かめる。

## 当日の前に済ませたこと (2026-10-29、PM)
- NP21/W (np21x64w.ini、main 24b00c7 = コードは 87b6499d): ゲストの試験 21/21、gfinit・recover の入口は固まらずに断る — ~/os32-tmp/evidence/2026-10-29/gf4c-np21w/RESULT.md。
- 実 ROM の 25.175MHz の係数: 単段 26/7・P=2、25,071kHz (0.41%) — 設計の末尾。
- 実機 Ra266 を main **87b6499d** に更新 (CI build 成功、ノートで hsync boot → 全体 → sys): ver Commit 87b6499・Image CRC 9a835bff・SERVICES_READY、部品 9 本 rc=0 pair=9a835bff、mem leftover=0・audit_fail=0、db_test 9/9、GeForce の既定値 68050Ch=4・680508h=00051104・680578h=00000E02・680580h=01F0001F・PMC 200h=03010000 — ノートの ~/os32-tmp/evidence/2026-10-29/realhw-87b6499/。VPLL の診断版の部品は通常の版に戻った。

## 前提と当日の manifest
| 欄 | 当日記入・確認する内容 |
|---|---|
| 版 | 読取確認した main は `b848317074a1872c8affa6b723a650c9db7f51ba`。当日採用 SHA=［ ］、`ver` の Build=［ ］・Image CRC=［8桁］・bytes/loader=［ ］。起草時には実機値未取得。 |
| 成果物の組 | 同じビルドのカーネル像・`kdebug.bin`・`hwdev.mod` の版、各 CRC、ビルドフラグ、配備先を記録［ ］。C op13 と対応する shell/monitor を使用。旧 VPLL 診断版の混在を避ける（(記録はアーカイブ)、docs/design/build/README.md、G:407–435）。 |
| 実機へ入れる条件 | ビルド rc=0（既定と診断版があれば両方）、既定成果物と基点の一致、直接試験 MUTATE=0 の記録［ ］。全候補・変異・取り込み検査の終了待ちは条件に足さない。固まれば電源再投入、必要なら前の版の hsync。根拠：docs/tasks/agents/ROLES.md:17「検査」の2026-10-28決定。 |
| 退行の記録 | NP21/W の入口、非Cの既存 prepare/verify、recover/S0 復帰、CLI/peek 門の確認結果・版・証拠［ ］。未実施は未実施と記す。NP21/W を GeForce の映像の代替にしない(記録はアーカイブ)。 |
| 前回と配線 | 4a 合格の証拠［ ］。Ra266・GeForce VGA端子→98 RGB-IN、head A/ANALOG OR0。DCB entry0 は heads0/1・CONN0 VGA。OR1比較は今回不要（docs/design/kdebug/gfinit.md）。 |
| ROMと資源 | 当日 inspect の ROM長/CRC/chip/match、ID/BAR/Command［ ］。照合用既知値：63488 B・E9A60DE3・chip44、BAR0=24000000h/16MiB、BAR1=30000008h/256MiB窓、BAR2=25000000h/16MiB、A1後Command=0102h（M:19–20）。窓容量を実装VRAM容量と呼ばない。 |
| mode | 640×480・8bpp・pitch640・25,175kHz・負H/負V、H=640/656/752/800、V=480/490/492/525。**実 ROM (2026-10-29、ノート)**: 単段 N1/M1=26/7・P=2、actual 25,071kHz (0.41%)、680508h=00021A07h・680578h=8000011Fh・580 bit8=1、同期の見込み 約 31.34kHz/59.7Hz (D の末尾)。 |

## 段ごとの操作・承認・期待
`i`=inspectに渡した現在sid、`a`=A1で発行されたsid、`c`=A2で発行されたsid。数値は当日の受信値に置換する。A1/A2のmonitorはsession=0・別の新state、A2以降は同一state/log/CRC/sidを引き継ぐ（S:16–33、M:207）。

| 段 | 操作と承認する範囲 | 期待・採る証拠／戻し方 |
|---|---|---|
| 新しい起動 | OS32 `reboot`、既定値のkpeek→`ver`・`recover status`。固まっていれば電源再投入。kpeek が既定値と不一致・読取り不能 (DENY など) なら電源再投入。 | 新baseline・IDLE/sid0。50C/508/578/PMCを後述の既定値と照合。不一致なら電源再投入。rebootで試行前の値へ戻す運用はV:174,179–182、(記録はアーカイブ)。 |
| inspect | `gfinit inspect i`（読むだけ）。 | `GF inspect … chip=44 expect=44 match=1`、`GF inspect-end … rc=0`、同定・BAR保存。違えば進めない（S:17）。 |
| A1 | 個別承認→`gfinit size-bars i --allow-write`。BARサイズ探索の書込み・復元、Command設定を承認。 | 新sid=a、BAR長・元値復元・Command=0102/BM=0、final OK verified=1・EOT。拒否なら記録して終了（S:18–23）。 |
| 組=3 | A1後・A2前、`peek -m -w4 0x24101000 4` をserial経由で1回。 | PSTRAPSのxと `(x>>2)&7=3` を記録。DENY/WINDOW・組違いならA2へ進まない（M:221）。 |
| A2 | 結果提示→個別承認→`gfinit prepare a --mode-c --allow-write`。PBUS1854/1850・PMC140/ENABLE・CR44/1F/11の準備書込みと既存復帰を承認。 | 新sid=cでPAUSED。1850/1854/cfg50/54/PMC140=0、Command0102、head0・unlock、98表示/更新。C付きplan/CRCを固定。失敗・中止はS0→reboot（S:29–34、M:209、docs/design/kdebug/gfinit.md）。 |
| main 0 | 個別承認→`gfinit run-one c 0 --allow-write`。RESET・PMC・strap・CORE/MEM PLL・PFB、両head blank。 | 同sid PAUSED、REC main0の対・rc・CR3C組3、98更新。GPU内部の初期値はS0では戻らずreboot（M:41–87）。 |
| main 1 | 個別承認→`gfinit run-one c 1 --allow-write`。PFB/メモリ・MACRO列。 | 同sid PAUSED、REC main1の対・rc・98更新。PFB状態はrebootで戻す（M:89–122）。 |
| main 2 | 個別承認→`gfinit run-one c 2 --allow-write`。旧VIDEO PLL 36MHz・CR/latched列。 | 同sid PAUSED、REC main2の対・rc・98更新。この係数をCの25.175MHzへ流用しない（M:124–155、docs/design/hwdev/gf/display/pll_program.md）。戻しはS0→reboot。 |
| main 3 | 個別承認→`gfinit run-one c 3 --allow-write`。DRAM・PRAMIN・head0 E81Eのblank解除。 | 同sid PAUSED、REC main3の対・rc・98更新。DRAM/VRAM内容の巻戻しなし、S0→reboot（M:200–205）。 |
| main 4 | DONEのみの列を確認→`gfinit run-one c 4 --allow-write`。GPU書込みなし。 | 同sid PAUSED・next_main=5。旧A4の `verify` は挟まない。C未承認ならabortして終了（docs/design/kdebug/gfinit.md、G:431–435）。 |
| C modeset | 全結果と次表を提示し個別承認→`gfinit modeset c --allow-write`（op13）。mode/VRAM/route01と終了時のS0書込みを承認。 | C入口/出口CHECK・VRAM一致・route01・セル往復、PAUSED/EOT、monitorのC判定。失敗は既存cleanup、途中再開しない（docs/design/kdebug/gfinit.md）。 |
| 映像の目視 | PAUSEDのままユーザーがRGB-INの画面を観察。追加peek/poke/statusや別のroute-testを挟まない。 | 外枠・市松・縦帯の色、位置、静止、OSD状態/周波数、時刻を記録。一本のモニタなので98表示はこの間見えない(記録はアーカイブ)。 |
| abort・98の目視・final | 同じc/state/logで `gfinit abort c --allow-write`。既存local→quiesce→screen/S0により0FACh=00へ戻す。 | 98に戻る瞬間と入力更新をユーザーが確認、final/EOTを保存。**98復帰目視はabortに伴うS0の後**であり、abort前に戻す独立操作はない(記録はアーカイブ)。 |
| 終端後の採取 | final/EOT確認→下記kpeek。CR要所はC出口CHECKを併録。 | sid・時刻・採取順・値を保存。S0後の値を出力中の値で代用しない(記録はアーカイブ)。 |
| reboot比較 | 証拠保存→OS32 `reboot`→ver/status/ID/BAR・同じkpeek。 | 既定値への復帰を確認。不一致/読取不能/停止なら電源再投入。再試行は新inspectから(記録はアーカイブ)。 |

## Cで承認する書込みの列・影響・戻し方
BAR0相対hex、MMIO32bit・indexed/port8bit。sはmain4後Q前、rは直前読値。詳細の列とmaskはD:41–58、係数は当日のmanifest。表内の各群を順に実行し、C内では追加承認待ちを挟まない。

| 群 | 書込みの要約と影響 | 戻し方 |
|---|---|---|
| Q→P | 68052C DACCLK off、必要時680848 FP off+50ms、CR1F/44解錠/head0、SR00/01・CR17/1A DPMS off+10ms、600804=1、680900 bit16解除。headBはblankのまま（docs/design/hwdev/gf/display/output.md）。 | 旧timingを再生せずS0→reboot。 |
| VRAM | BAR1+[0,4B000h)＝307200Bを640B×480行で全域転送→全域比較、posted write排出。元内容は失われる（docs/design/kdebug/gfinit_plan_c.md）。 | 内容の書戻しなし。S0→rebootでGPU状態を戻す。 |
| S→D | CR21解錠、680524=s&~50000、CR45/680634=0、CR30/31/2F=80/00/00・cursor位置再書込み。68052C=1・68252Cのhead束縛・680608=00100000でOR0選択（docs/design/hwdev/gf/display/cursor.md）。 | S0→reboot。 |
| L1a | protect、680404=02000000、C040切離し→680580→680578→680508→C040退避値復元。25.175MHzのsolver/5書込み列。係数直後のreadbackでは判定しない（docs/design/hwdev/gf/display/pll_program.md）。 | C040は列内復元、PLLはS0で巻き戻さずreboot。 |
| L1b | 68050C=10000204、524=s&~50000、600=20100130、630/634/700=0、848=20000022、8C0=100、A20/A24/A34=0/FFFFF/1（680接頭辞）。源・形式・停止経路設定。510には書かない（docs/design/hwdev/gf/display/output.md）。 | S0→reboot。 |
| L2 | ENGINE_CTRL→overlay/PBUS→PCRTC、900・CONFIG=2、拡張CR・cursor・残りCR・START=0。CR19/1A/25/28=00/04/10/01、FIFO=04/C0/00、CR57/58の16組等（docs/design/hwdev/gf/display/extended.md）。 | S0→reboot。 |
| L3 | DAC mask/index/data=FF/00/RGB×256（先頭8色、残り黒）。MISC=E3、SR/CR timing→GR→AR→gate/unprotect。CR11の解錠/施錠を含む。DAC/ARへ一律readbackは付けない（docs/design/hwdev/gf/display/palette.md）。 | 局所index/lockは既存cleanup、palette/modeはreboot。 |
| C→R | 形式/pitch/START/FIFO再設定→DPMS ON（10ms、CR11最終AB・CR17=C3・SR01=01）→DACCLK ON→98予約セル往復→0FACh=01（mask03）→出口CHECK（docs/design/hwdev/gf/display/dac_format.md）。**98画面はモニタから消えRGB-INへ切り替わる**。 | abortの既存S0で0FACh=00、98画面復帰。停止なら電源再投入。 |
| cleanup | local→quiesce（1850=0→1854=0→PMC140=0・照合）→screen。PCI同定不能ならGF MMIOを省きFAIL、独立な98復帰を試みる。decode解放未確認時はA0000hに触れない（docs/design/kdebug/recover_common/s0.md）。 | S0は98復帰まで。mode/DAC/PLL/PFB/VRAMを逆順再生しない。reboot、固まれば電源。 |

## ノートのREC採取・時間の見込み
2026-10-29 の回 (結果): C は 4 回目で完走し、映像を目視 (8 色の縦帯・市松・白枠、位置のずれはモニタ側)。1 回目 50C bit28、2 回目 680A24、3 回目 CR1A bit3/5 の読み返しの食い違いで止まり、そのつど照合から外して版を入れ直した (作業木 gf-c-50c、ed924ee9→f5953909→debd2ad5)。4 回目は monitor が 50C の値を mask なしで比べて誤って断り、状態のファイルに誤判定が残って abort も断ったので、`gfinit abort 2 --allow-write` を rshell_serial で直接送った (final PARTIAL target=S0 verified=1 unknown=1 trial=1 lost_lines=0 txd=0)。終端後の kpeek は C の値のまま (50C=204・508=00021A07・578=8000011F・1850/1854/PMC140=0)、reboot 後は既定値 (50C=4・508=00051104・578=00000E02・200=03010000)。証拠はノートの ~/os32-tmp/evidence/2026-10-29/gf-c-run4/ ほか (gf4c・gf-c-50c・gf-c-run3)。次の回は `--verify required` (記録だけ) で回せる (作業木 gf-c-noverify)。

A1は新 `a1.json/a1.raw`、A2は新 `c.json/c.raw`。以降同じファイルへ追記し、受信者は1つ。生REC、monitor出力、開始/PAUSED/final時刻、行数・経過秒、sid/seq・version/profile_crc/plan_hash/snapshot_crcを保存（M:193,207–208、W:41,533–550）。
共通形：`python3 tools/rshell_recover.py --port /dev/ttyUSB0 --baud 9600 --session <sid> --state <state> --image-crc <当日CRC> --log <raw> --timeout 900 --cleanup-timeout 180 --command '<表のコマンド>'`。
A1/A2開始だけ `<sid>=0`、コマンド内のsidはそれぞれi/a。A2後は両方c。既定240秒に任せず、A1/A2/mainは既存台本の900秒を保つ（S:19,27、M:222、W:39–40）。

| 区間 | 行数・秒数の期待と扱い |
|---|---|
| inspect/A1/A2/main | inspectは2行。A1/A2/mainの実行REC数は分岐・反復依存で当日数を保存、固定成功行数を創作しない。旧診断版main0/1/2の216/251/212秒は参考実測でC版の保証ではない（S:17、M:36,193、(記録はアーカイブ)）。 |
| C | 段2c3の上限2338行、900130B=2338×385、9600bps・10bit/frameでceil(900130×10/9600)=938秒の送信見込み。VRAM4・DAC2・AR48・WAIT6・CHECK66・CELL4・ROUTE2行を含む（B:195–200、W:44–48）。実機所要時間は未測定。 |
| Cのhost待ち | 現行monitorはmodesetを認識し `max(指定値,60+938+cleanup_timeout+5)` に拡張する。上の180秒なら**1183秒以上**。Cに旧900秒を上限として適用しない（W:496–498,542–546）。 |
| ゲストの時間 | trial全体の送信除外60秒、PAUSEDの承認待ちも積算外。A2/main/C合計の枠でありC専用60秒ではない。壁時計とspentを混同しない（docs/design/kdebug/README.md、M:193）。 |
| cleanup | 段2c3のhost試験では23秒、S0予約4096B。実機はRESTORINGのcleanup_secondsを記録、180秒がその値+5以上か確認。monitorの警告・追加待ちを保存（B:185–190、W:424–434）。 |

## 合否（機械と目視を分ける）
| 区分 | 合格条件・出力 |
|---|---|
| Cの機械項目 | 同sid、CRC不変、連番欠落なし、BEGIN/END閉鎖・rc=0、VRAM dir=[1,0]/範囲307200/不一致なし、CHECK66、route&3=1、セルstep=[2,7]復元成功。monitorは `MODE_C 68050C/680508/680578 mask=… expected=… actual=…` の3行、`MODE_C CHECK=66 route=01 cell=OK VRAM=OK`、`MODE_C: 機械項目合格・映像保留`、`PAUSED`（W:275–321,394–408）。 |
| 必須レジスタ | 50C=10000204（全bit）、508 mask0007FFFF・578 mask8000FFFFで当日解一致。524=s&~50000、580 bit8、C040復元、CONFIG=2/START=0、CR00..18・19/1A/25/28/33/44/47、DACCLK&101=1、TEST bit20、Command0102・cfg50/54=0。出口CHECKを保存（docs/design/kdebug/recover_gf_stage/c.md）。 |
| 記録のみ | 680510の実値（bit9=1の選択下）、TEST sense上位。未知510が0でも落とさず、PLL lock/実周波数の証明にしない(記録はアーカイブ)。 |
| RGB-IN目視 | 外周白枠8pxの四辺、80px幅8色（黒/赤/緑/青/白/cyan/magenta/黄）、x/y=8〜71の64×64px市松（1マス8×8px・左上白）。優先は白枠>市松>帯。色・幅・上下左右・静止をユーザーが確認、OSD周波数があれば記録（docs/design/hwdev/gfinit_mode.md）。 |
| S0復帰 | abort後route&3=0・停止CHECK/予約セル成功、98テキスト正常かつ入力で更新。final `result=PARTIAL target=S0 verified=1 unknown=1 trial=1 lost_lines=0 txd=0`、diag.fn=NONE・EOT。trial=1は意図したabort。汎用successはOK専用なのでrc=1だけで失敗としない（(記録はアーカイブ)、W:323–331,409）。 |
| 全体判定 | 機械項目とRGB-IN/98復帰の目視、終端、reboot比較が揃って4c合格。目視なしなら「機械項目は合格・映像は保留」。REC欠け/final不着は機械合格にも数えず、FAILは別記(記録はアーカイブ)。 |

## 終端後のkpeekとreboot比較
final/EOT後、ノートで `python3 tools/rshell_serial.py --port /dev/ttyUSB0 cmd 'peek -m -w4 <物理番地> 4'` を各先1回。BAR0が24000000hの当日値と一致した場合の番地は次表。違えば実BAR0+offsetで算出する（A:111、K:19–34,41–68）。

| offset | 物理番地／記録する内容 |
|---|---|
| 68050C / 680510 / 680508 / 680578 | 2468050C / 24680510 / 24680508 / 24680578。C出口・S0後・reboot後を別欄にする。 |
| 680580 / 680524 | 24680580 / 24680524。段制御/SEL_CLKの終端値とreboot後値。 |
| 1850 / 1854 / 140 / 200 | 24001850 / 24001854 / 24000140 / 24000200。quiesce3先=0、PMC.ENABLEは別欄。 |
| CRの要所・route | CR00..18/19/1A/25/28/33/44/47、SR01は**C出口CHECK**、routeはC出口とS0のRECから採る。kpeekは選択済みCR index/data（246013D4/D5、幅1）しか読めず、任意CRを選択する口ではない。要所を採るためのindex書込みは足さない（docs/design/kdebug/gfinit_plan_c/README.md、K:33–34）。 |

reboot後の照合基準は50C=00000004、508=00051104、578=00000E02、PMC.ENABLE=03010000、ID/BARと新baseline。510/580/524/CRに根拠のない既定値を置かない。全内部状態の冷間一致やPCI RST#の保証ではなく、既存実測に基づく運用確認。不一致/読取不能なら電源再投入して再確認（(記録はアーカイブ)、(記録はアーカイブ)）。

## 映像が出ない場合・止める条件
| 状態 | 記録と次の一手(記録はアーカイブ) |
|---|---|
| OSD「無信号」 | モニタの入力は変えない (98 の入力のまま — 0FACh は 98 本体の中の切替回路)。配線、route01、1850/cfg50、OR0/DACCLKを既存RECと照合。VGA→RGB-INは確定済みなのでOR1へ勝手に変更しない（docs/decisions/realhw/GF6200_4C_DESIGN.md）。 |
| OSD「範囲外」 | OSD表示値を保存、timing/PLL解・50C選択と出口CHECKを照合。後続を止めS0へ。 |
| OSD「同期ありで黒」 | palette列、全域VRAM比較、START=0、DACCLKを照合。機械項目の失敗があれば先にその故障を解消。 |
| 比較が必要 | 初回は1850=0を維持。**1850h←1だけ**の比較は別承認・別の回、C最終ON後R前。BUS50_ONEは今回未実装で、その比較の回に足す。54=0・50=1・98セルと目視・abortで50=0/S0を確認。毎回S0→reboot→baseline→同じinit/modeで一条件比較。PAUSEDにpokeしない(記録はアーカイブ)。 |
| 次の候補 | 1850を0へ戻した別の回で必要なら0FACh=01→03のみ比較。Ra266へのbit1効果は推測。根拠なしの510書込み/TMDS追加はしない(記録はアーカイブ)。 |

98コンソール停止・シリアル無応答・REC欠け・CHECK不一致・アクセス/送信/期限/セル復元失敗・同期外れ/乱れなら後続を止める。PAUSEDなら同sid abort、RUN中は既存abort frame（monitor timeout時に自動送信）→cleanup。Cのroute01による意図した98画面消失とは区別する（(記録はアーカイブ)、W:365–377）。
FAIL終端を受信済みならabortを重ねない。final/EOT不着時はmonitor終了後、通信可能なら共通引数で `--command` を省略して `recover now c` を一度試す。Ctrl-Cをabortの代用にせず受信者を重ねない。出口へ戻れなければユーザーが電源再投入。復帰できたら証拠→reboot→baseline照合（M:196,203、docs/design/kdebug/recover_common/s0.md）。

## 当日前にノートで埋める未確認
25.175MHzの**実ROM**での係数は未確認。ノートの既存段0の私有ROMを `GF_ROM_HEADER` の `gf_private_rom` に渡し、当日版 `tools/tests/gfinit_romfile_host.c` を同じhostコンパイル手順で実行する（同:8,44–61,65–110,133–154、コンパイルの組立例 tools/tests/test_gfinit.py:130–139）。本起草では実行・ビルドしない。
保存するのはROM長/CRC、VIDEO limits/ref、`decode.rc=0`・`diagnostic.rc=0`、`mode.request_khz=25175`、single_stage、mode.n1/m1/n2/m2/p・actual/error・error_percent_hundredths・within_half_percent=1、mode.write0..4のreg/source/keep/value。単段判定を適用したmode欄を使い、旧36MHzのtwo/single欄と混ぜない。±0.5%以内を確認しmanifestに転載する。ROM生バイトは資料へ載せない（docs/design/hwdev/gf/display/pll_program.md、同host:104–112）。
実ROMの解が揃ってもPLL lock/実周波数、POSTなしのstateの十分性、0FACh=01の実映像、C実機所要時間は未確認のまま。実機で上の証拠を採る(記録はアーカイブ)。本作業は本ファイルの起草のみ、実機・NP21/W・ビルド・試験は未実施。
