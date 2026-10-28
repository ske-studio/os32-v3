# GF6200_4C — GeForce 6200 段 4c (画面モードと映像出力) — 設計

> 状態: **現行 (2026-10-28)** — 確定 (astra 起草・Opus 2 往復、手本は nouveau の modeset ([HW3])、P3 の文言 2 件は PM)。段の状態は票 TASK_GEFORCE6200 が持つ。
> 出所: ~/os32-tmp/run/gf-4c/4C_DESIGN.md (r2 + PM の文言) を 2026-10-28 に写した。往復の版は同じディレクトリの 4C_DESIGN.r1.md・r2.md、レビューの要約は opus_r1.md、直しの一覧は astra_r2_response.md。参照に足した nouveau の nvkm/subdev/pci と drivers/gpu/drm/drm_crtc_helper.c は ~/os32-tmp/refs/geforce6200/INDEX.md。

モデル ID は確認できないため、依頼で指定された gpt-6-astra として書く。
参照略号：`O/=/home/hight/os32-v3/`（main `3afe0196`）、`D/=O/docs/decisions/realhw/`、`T/=O/docs/tasks/realhw/`、`R/=/home/hight/os32-tmp/refs/geforce6200/`。
`N/=R/nouveau/drivers/gpu/drm/nouveau/`、`V/=N/dispnv04/`、`E/=R/envytools/`、`X/=R/xf86-video-nv/src/`。各 `file:line` はこの接頭辞で解決する。
[HW3]：主手本は nouveau c03c1a1 の MIT 部（`V/crtc.c:1–24`, `V/hw.c:1–23`）、補助は envytools f102b82・xf86-video-nv 8234bce（`R/INDEX.md:6–9`）。メーカーの NV44A modeset 公開仕様は未取得。GPL 本体のコード・構造、ROM x86 コードは参照しない。
OS32 は単一利用者・単一タスク・協調切替。対象は誤動作とバグへの頑丈さのみ。本票とr2修正一覧だけを書き、実装・ビルド・NP21/W・実機操作を行わない。

## 1. 目的と範囲
推奨は head A、ANALOG OR0、640×480 progressive・8bpp indexed・pitch 640 B、25,175 kHz、負H/負V、約31.469 kHz/59.94 Hz。H=640/656/752/800、V=480/490/492/525（表示/同期開始/終了/総数）。
白い外枠と黒・赤・緑・青・白・cyan・magenta・黄の静止縦帯、左上だけ白黒市松を表示し、色・幅・上下左右・静止を目視する。8bpp は VRAM 転送量を抑える案；32bpp は色形式の比較が必要になった後へ送る。
合格は RGB-IN で模様が見え、出力中の98テキストセル往復と、終端後の98表示・応答が成立すること。レジスタ一致だけでは映像合格にしない。
4a 合格を前提に、新しい同一試行の A2→main 0〜4→C を行う。旧 A4 は終端するので間に挟まない（`O/kdebug/gfinit.c:388–390`）。今回の VRAM 試験範囲は表示領域だけで、4a の容量試験を再実施したとはしない。
4b は畳み、1850h/cfg50=0・1854h/cfg54=0 を維持する（`T/TASK_GEFORCE6200.md:49`）。24kHz、GUI/HAL製品化、加速、割込み、head B表示、TMDS/TV、EDID自動選択は後段。

## 2. nouveau の modeset の経路
**順序訂正**：BRIEF の「ramdac・vga・ext」に対し取得版 `nouveau_hw_load_state` は protect→**ramdac→ext→palette→vga**→unprotect（`V/hw.c:827–835`）。以下は取得版を正とする。prepareはQ→P（encoder→CRTC、`R/nouveau/drivers/gpu/drm/drm_crtc_helper.c:371,376`、PM追加の同版・寛容許諾の順序参照）。
| 順 | 関数・書込み群と前提（すべて head A、明記した OR1 以外） |
|---|---|
| Q | `nv04_dac_prepare`→DACCLK off→`nv04_dfp_disable`：FP_TG が動作中なら OFF と50ms、stateのLCD route解除（`V/dac.c:364–373,426–472`, `V/dfp.c:93–110`）。Q→P後、mode設定の冒頭でCR21 shadowを解錠（`V/crtc.c:659–660`, `V/hw.h:287–305`）。 |
| P | `nv_crtc_prepare`：owner=0、DPMS OFF、blank、PCRTC_CONFIG=NON_VGA、RAMDAC900 bit16解除（`V/crtc.c:708–728`）。DPMS の SR00 reset→SR01 bit5→10ms→CR17 bit7→reset解除→CR1A sync mask は同:175–234。 |
| S | `nv_crtc_mode_set_vga` は timing・SR/GR/AR・pitchをstate化（`V/crtc.c:237–449`）。Curie はSEL_CLKをPLL計算前にも書く（同:662–667）；`nv_crtc_save` の初期値は saved_SEL_CLK & ~00050000h（同:685–689）。 |
| S | `nv_crtc_mode_set_regs` は出力OFFを前提にCRTC拡張・cursor・RAMDACを生成（`V/crtc.c:452–606`）；CR45と634は計算段でも書く（同:63–88,534,600）。`nv_crtc_calc_state_ext` がPLL limits→単段判定→solver→50Ch stateを生成（同:117–162）。 |
| S | `nv04_cursor_set_offset` のCR30→31→2FとCurieの位置再書込み（`V/cursor.c:38–59`, `V/hw.h:341–349`）はcursor非表示でも実施する。 |
| D | `nv04_dac_mode_set`：選択DACCLK=head<<8\|1、他ANALOGを反対headへ、NV44AのTEST_CONTROL=00100000h（`V/dac.c:375–409`）。OR番号はhead番号ではない；OR0 offset=0、OR1 offset=2000h（同:41–51）。 |
| L1 | `nv_crtc_commit`→load（`V/crtc.c:731–749`）。`NVVgaProtect(true)` のSR00=1・SR01 blank・AR gate（`V/hw.h:194–219,238–255`）の後、`nv_load_state_ramdac` がCURSYNC→PLL→50Ch→SEL_CLK→GENERAL→630→634→TV→FP→8C0→A20/A24/A34→CTV（`V/hw.c:468–536`）。 |
| PLL | `nv40_clk_new` のpll_prog=`nv04_clk_pll_prog`（`N/nvkm/subdev/clk/nv40.c:229`, `nv04.c:49–62`）→`setPLL_double_highregs`：C040切離し→必要時580→578→508→C040復元（`N/nvkm/subdev/devinit/nv04.c:197–272`）。50Chはその後。510hの書込みはこのload経路に無い。 |
| L2 | `nv_load_state_ext`：ENGINE_CTRLはCR33より先、PVIDEO/PBUS→PCRTC→900→CONFIG→拡張CR→cursor位置再書込み→残りCR→START（`V/hw.c:665–772`）。NV44Aには旧世代用の垂直帰線待ち:746–757を入れない。 |
| L3 | palette mask/index/data を順に書く（`V/hw.c:794–807`）。`nv_load_state_vga`：MISC→SR0..4→CR11解錠→CR0..18→CR11施錠→GR0..8→AR0..14→AR gate（同:564–586）。 |
| C | unprotect（`V/hw.h:250–255`）→`nv04_crtc_do_mode_set_base` の形式/pitch/START/arbitration（`V/crtc.c:863–903`）→CRTC DPMS ON（同:749）→DAC commit/DPMS ON（`V/dac.c:412–419`）。 |
ANALOG OR0 は初回の仮選択。DCB 96ACh v3・CONN 97DAh にANALOG 2本・TMDS/TVがあるが、RGB-INとORの対応は未確認（`T/TASK_GEFORCE6200.md:67`, `T/GEFORCE6200_VBIOS.md:295–299`）。ROMデータ表のhead mask/connector番号を実装前に照合し、OR0→head A不可なら固定案を訂正する。
差分：PC-98ではVGA実I/Oを使わずBAR0の8bit別名を使い、I/O/BM=0・Mem=1を保つ（`D/GF6200_4A_DESIGN.md:65,72,89`）。nouveauに98共存処理は無い。
差分：VBIOS POST済み保存値に依存しない。4a後の採取値をsaved相当とするが、nouveau自身も既存状態依存を明記するため、この置換で全状態が十分とは未証明（`V/disp.c:107–118`）。
差分：1850=1は通常列に無い。INIT_RESETはbit0を落とす（`N/nvkm/subdev/bios/init.c:1494`）；shadow復帰はPROM取得用（同`shadowrom.c:55–82`, `N/nvkm/subdev/pci/base.c:66–73`）。envytoolsの「0でvideo停止」（`E/docs/hw/bus/pci.rst:50–58`）との食違いは§7で比較する。
補助照合：envytoolsのNV4:G80の50Ch bit9/28（`E/rnndb/display/nv3_pramdac.xml:59–85`）。xf86-video-nvは50Ch→508の順（`X/nv_hw.c:1494–1505`）なので混ぜず、nouveauの順を採る。envytoolsのPCRTC説明にも未記述が残る（`E/docs/hw/display/nv3/pcrtc.rst:9–25`）。

## 3. OS32 の列
番地はBAR0相対hex、MMIOは32bit、CR/SR/GR/ARは8bit。`s(x)` はmain4後・Q前に採る値、`r(x)` はその操作直前値。CR=6013D4/D5、SR=C03C4/C5、GR=C03CE/CF、AR=6013C0/C1・FF reset=6013DA（`V/nvreg.h:129–139,177–187,298`）。
| 順 | 番地・値（省略した接頭辞は同一行内のみ） | 根拠・差分 |
|---|---|---|
| 0 | 入口CHECK：ID/BAR/Command=0102、cfg50/54と1850/1854=0、route&3=0、main4完了。s(1C/33/3C/4B/52/60081C/600850/680524)を採取 | 既存4a＋`V/crtc.c:494,532,542–565,685–689`。1854の再書込みは不要。 |
| Q | 68052C=r&~1；680848がONなら20000022と50ms；state.FP=20000022、CR33=s&~3B | §2 Q。head Bはmain0のblankを維持し、新たに表示しない。 |
| P | CR1F=57、CR44=0；SR00=1→SR01=r\|20→10ms→CR17=r&7F→SR00=3→CR1A=r\|C0；blank；600804=1、680900=r&~10000 | A2解錠を確認して再利用（`D/GF6200_4A_DESIGN.md:84–91`）、§2 P/`V/hw.c:85–124`。 |
| S | CR21=r\|FA→680524=s&~00050000；CR45=0；680634=0；CR30/31/2F=80/00/00→680300=r | §2 S。cursorの領域割当ては省き、offset=0・非表示（CR31 bit0=0）のみ。 |
| D | 68052C=1→68252C=(r&~100)\|100→680608=00100000 | §2 D。TEST_CONTROL上位senseは期待値に含めない（`D/GF6200_VPLL_PROBE.md:162`）。 |
| L1a | protect→680404=02000000→C040=r&~00030000→680580=(r&~100)\|single*100→680578=(r&7FFF0000)\|80000000\|NM2\|(single?011F:0)→680508=(r&FFF80000)\|(P<<16)\|NM1→C040=退避値 | §2 PLL。既存4aの5書込みsequenceを再利用するので同値時も実施、580の条件省略も4aとの差を増やさない（`O/kdebug/gfinit_pll.c:196–210`）。 |
| L1b | 68050C=10000204→680524=s&~00050000→680600=20100130→680630=0→680634=0→680700=0→680848=20000022→6808C0=100→680A20=0→680A24=000FFFFF→680A34=1 | `V/hw.c:481–531`, `V/crtc.c:589–606`。head A単独の50Chはbit2/9/28。診断の204hを流用しない。 |
| L2a | 600860=10→8704=1→8140=0→1588=0→600810=04011100→600830=1DD→600834=1DF→60081C=s→600850=s→680900=r\|10000→600804=2 | `V/hw.c:680–710`, `V/crtc.c:496–568`。overlay停止、cursor非表示。 |
| L2b | CR19=00→1A=04→25=10→28=01→33=s&~3B→2D=00→1C=s&~20→1B=04→20=C0→47=00→9F=01→49=00→30/31/2F=80/00/00→680300=r→CR39=FF | `V/hw.c:712–734`, `V/crtc.c:388–413,494,555,578`, `V/arb.c:239–264`。FIFOはNV44A分岐のburst=4/lwm=C0をstateに先置き。 |
| L2c | CR3B=22→3C=s→41=00→45=00→4B=s\|80→52=(s+4)&FF→42=00→53=00→54=00→CR57/58[i=0..15]=i/00→59=00→5B=00→85=FF→86=01→600800=0 | `V/hw.c:736–772`, `V/crtc.c:519–575`。未計算CR49/CR58/5Bはzero state相当（`V/disp.c:223`）、POST代替の妥当性は未確認。 |
| L3a | 6813C6=FF→6813C8=00→6813C9にRGB×256。index0..7=黒/赤/緑/青/白/cyan/magenta/黄（成分00/FF）、8..255=黒 | `V/hw.c:794–807`。DAC dataは自動増分、old/readbackを付けない。paletteの値は本試験専用。 |
| L3b | C03C2=E3；SR00..04=00 21 0F 00 0E；CR11 bit7解除→CR00..18=`5F 4F 4F 83 53 9F 0B 3E 00 40 00 00 00 00 00 00 E9 2B DF 50 00 DF 0C 43 FF`→CR11 bit7設定 | `V/crtc.c:245–256,305–381`, `V/hw.c:570–578`。単なる標準VGA表の転記ではなくnouveauの+1/-1補正を適用。 |
| L3c | GR00..08=`00 00 00 00 00 40 05 0F FF`；AR00..0F=00..0F、AR10..14=`01 00 0F 00 00`、各index/data前にFF reset；AR gate復帰→unprotect | `V/crtc.c:418–449`, `V/hw.c:580–586`, `V/hw.h:194–255`。VGA実portには書かない。 |
| C | CR28=01→680600=20100130→CR19=00→13=50→42=00→600800=0→CR1B/20/47=04/C0/00；SR00=1→SR01=01→10ms→CR17=C3→SR00=3→CR1A=04→68052C=r\|1 | §2 C。CR11最終AB、CR21解錠維持、CR44=0。S0まで旧timingを再生しない。 |
| R | 出力中の98セル往復→0FACh←01h（8bit、mask03hで01）→出口CHECK→PAUSED | nouveauに無い。RGB-IN bit0優先を採用する案（`O/docs/hw/undocumented/io_wab.md:345–366`、`T/TASK_GEFORCE6200.md:73–76`）；Ra266での映像による確認は§5。 |
差分：analog専用なのでL1のTV timing/CTV・FP timing/dither/debug/marginの再ロード、L2のPVIDEO offset/limitは省く（`V/hw.c:495–516,519–523,533–535,684–689`）。TV_SETUP=0・FP_TG=OFF・PVIDEO_STOP=1の停止値を先に成立させ、停止中の未使用経路を設定しないため。
manifestはH/V入力→`V/crtc.c:245–256`の中間値（HD/HS/HE/HT/HBS/HBE=4F/53/5F/5F/4F/63、VD/VS/VE/VT/VBS/VBE=1DF/1E9/1EB/20B/1DF/20C）→CR00..18・25/2D/41の各式と算出値を並べ、§3の手書き表と独立に突合する。CR05=((HBE>>5)&1)<<7 | (HE&1F)=9F（`V/crtc.c:350–408`, `V/nvreg.h:197–198`, `V/hw.h:34`）；同じ表から期待traceを複製するだけでは不可。
PLLは4aのparser/整数solverを**再利用して25,175 kHzを解き直す**。limitsのvco1.max/2以下ならvco2=0、単段N2=M2=1（`V/crtc.c:129–150`, `O/kdebug/gfinit_pll.c:78–118,132–155`）；NM2の011Fへの合成は上表。36MHz係数は使わない。
§9 の 1 の中で `O/tools/tests/gfinit_romfile_host.c:40–57,60–104` を拡張する手順：ノート上で実ROMを既存GF_ROM_HEADERへ渡し、ROM CRC・VIDEO limits/refを出力→limitsのコピーに25,175 kHzで§3の単段判定を適用→既存solve/sequenceからN1/M1/N2/M2/P・actual/error kHz・誤差率・5書込みを出力→manifestへ保存し±0.5%を判定する。元の36MHz診断は保持。本作業では拡張・実行せず、手計算の係数を使う場合は「推定」と明記し実ROMの結果と区別する。
実装時のmanifestにlimits・N/M/P・actual kHz・誤差を数値で固定し、案の許容誤差±0.5%以内を確認する（この許容値はOS32の試験方針）。係数書込み直後の像は判定せず、50Ch選択後に508 mask=0007FFFF・578 mask=8000FFFFを比較；510はread_safe対象へ追加して同時に読むだけ（定義 `E/rnndb/display/nv3_pramdac.xml:82`、既存の無停止読取実測 `D/GF6200_VPLL_PROBE.md:163,180–182`）。
VRAMはQ→Pで停止後・Dより前にBAR1+[0,4B000h)の307,200 Bを予約してCPU転送。1行640 Bの固定試験データを480行、全行書込み後に全域比較、posted write排出は既存の安全なGPU読取り。VRAM容量/窓の下限と書込範囲をplanで確定（`D/GF6200_4A_DESIGN.md:192–194`）。
[HW1]の通常描画はbackbuffer→gfx_present/dirtyのHAL経路（`O/docs/CONSTRAINTS.md:73–81`）。本票は既存A4と同じ専有VRAMへの診断パターン転送として扱う明示差分で、CUIのHALを切り替えない。白枠は外周8px、市松は内側左上64×64px、残りは80px幅8帯；通常アプリ描画への流用は後段。

## 4. 実行の形
`gfinit prepare <sid> --mode-c --allow-write` のflagsにMODE_C=2を追加、初回はMODE_Cだけを実装し、BUS50_ONE=4（MODE_C必須）は§7の比較を実装する回に追加する。新opは**13の `gfinit modeset <sid> --allow-write` 1つ**。終了は既存abort。40B/version1・op5〜12・KAPI slotは維持（`O/kdebug/gfinit_wire.h:4–17`, `O/kdebug/kdebug_main.c:115–123`）。
flagsはprepare時にownerへ保存しplan/hash/profile_crcへ反映。再送cacheはprepareのflagsも比較する（現状はop/mainのみ、`O/kdebug/gfinit.c:365–375`）。途中のflag変更や旧prepareからmodesetを受け付けない。
INSPECT→A1→組3確認→prepare MODE_C→main0〜4は現行台本（`O/tools/accept/gf_main.md:193–204`）。C実施時は同sid/next_main=5/PAUSEDを確認；旧verifyはMODE_Cでは使わず、通常prepareのA4は従来どおり。
HWDEV側 `gfinit_plan` に A2+main+固定C節を最初からbind。Cは入口CHECK→Q→P→VRAM→S/D→L1/2/3→C→R→出口CHECK→NONE、成功はPAUSED。失敗は既存trial_end、途中再開なし（`O/kdebug/gfinit.c:298–357`）。
許可表は `recover_gf` のC専用phaseに§3各先の幅・値式・順・verify maskを登録。PLL・indexed対象は既存targetを共有、mode dataはCOLD。SR/CR/GR/AR・DAC dataの副作用は型で扱い、領域一括MMIO許可にしない（`D/REALHW_RECOVER_DESIGN.md:115–120`）。
必須CHECKは50C全bit、524の計算値、580 bit8、C040復元値、CONFIG&3/START、標準CR/形式/pitch/blank/sync・DACCLK&101、cfg/route。TEST_CONTROLはbit20だけ比較しsenseを除外、未知・未使用scratchは記録のみ。ARはFF操作付き専用列、DACは書込み専用列としread_safeな通常MMIOへ偽装しない。
0FAChは現行target検査で禁止（`O/kdebug/recover.c:605–608`）。Cの固定route節だけ既存route callbackを使う専用操作を結線し、01/読戻しmask03、終端00を定義。任意I/O opは足さない。S0の98セル部分を出力中CHECKでも再利用する（§5）。
307,200 BをDWORDごとのplan/RECへ展開しない。固定640 B行の有限転送/比較を内部操作として追加し、方向ごとにgf_bus BEGIN/ENDを1組（転送全域で1組・比較全域で1組、先頭・長さ・方向、最初の不一致番地/値だけ記録）、途中の既存safe point・窓復元を持つ。DAC768 byteも固定有限列。汎用bulk APIにはしない。
新しい要約ログ行は設けず、入口/出口CHECKとgf_busで読む。C出口は50C/510/508/578/580/524、CONFIG/START、CR00..18・19/1A/25/28/33/44/47、DACCLK/TEST、cfg50/54/Command/route、セル検査結果を保持する。
セル検査は `recover_profile.c:35–45` のread/write部分を共有化し、RUNのsafe point/予算で呼ぶ。restore/redrawやRESTORING専用の時計は呼ばず、試験値書込みが失敗した場合も既存どおり元セルの復元を一度試みる。
REC予算は転送/比較の計2組4行（480行ずつの処理は内部反復）・DAC768アクセス・全レジスタ操作/待ち/失敗分岐の最悪行数から開始前に算定し、既存gf_budgetのnode/arg式へ反復分を加える（`O/kdebug/gfinit.c:259–295`）。trial全体の送信除外60秒・PAUSED積算外は維持。
cleanup予算は既存のlocal→quiesce→screenを保ち、Cで増えるlocal保存対象を含む実対象数でP/Qを再計数、S=max(4096,Q)・byte/time/poll予約とCRC不変を維持（`D/GF6200_RECLOG_DESIGN.md:150–193`）。hostは算定Tlog/cleanup_secondsで待ちを設定し、9600bpsの既定60秒を流用しない。

## 5. 受入条件
| 観測 | 合格条件（同一試行の証拠） |
|---|---|
| シリアル：C前 | 当日版/ROM CRC/BAR/組3、A2と全main成功、C入口CHECK成立、書込値/PLL解/範囲/予算を保存。 |
| シリアル：C後 | gf_busのBEGIN/END閉鎖・rc=0、全域VRAM比較一致、50C=10000204、524=s&~50000、選択後PLL係数一致、CR列は§3の最終値（11=AB、17=C3、SR01=01）、START=0・CONFIG=2・DACCLK bit0=1/head=0。 |
| 510h | bit9=1時の実値を記録。未知なので0でも不合格にせず、PLL lockや実周波数をこの値で判定しない。 |
| 出力中の98 | cfg54/1854=0・Command I/O=0を確認してからS0予約A0000hセルの元値→異なる値→読戻し→元値復元→再照合を実行。画面全体復帰は呼ばない（`D/REALHW_RECOVER_DESIGN.md:60,86`, `O/kdebug/recover_profile.c:25–49`）。到達性の証拠であり表示継続の証明ではない。 |
| 目視：RGB-IN | route&3=1のPAUSEDで外枠4辺・8帯・左上の市松が静止し、色/位置が期待どおり。モニタOSDに周波数が出れば併記。1本なのでこの間の98表示は目視不能。 |
| 目視＋serial：戻し | abort後route&3=0、98テキスト正常・入力で更新、停止CHECKとS0予約セル成功、final PARTIAL target=S0 verified=1 unknown=1 trial=1 lost_lines=0 txd=0、diag.fn=NONE・EOT。意図したabortのtrial=1を失敗と混同しない。 |
| kpeek：終端後 | reboot前に50C/510/508/578/580/524・1850/1854・PMC140とrouteを保存。PAUSED中に一般peekを開けない。indexed CRTCはC出口CHECKを証拠とし、S0後の像で出力中を代用しない（`O/tools/accept/gf_a4.md:111–113`）。 |
機械項目のみ成立なら「機械項目合格・映像保留」。REC欠落・final不着・目視なしを4c合格にしない。失敗終端はFAILとして別記、汎用monitorのrc=1だけで判定しない。

## 6. 影響・戻し方・止める条件
VRAM表示領域・PLL・CRTCが変わり、0FACh切替中は98が見えない。モニタ1本の接続/切替の目視と実機書込みは従来の段別立会い（`T/TASK_GEFORCE6200.md:10,23,50`）。本起草は実施承認を求める作業ではない。
通常終了も中止も `gfinit abort <sid> --allow-write` / `recover now <sid>`、RUN中は既存abort frame。cleanupは既存の**local→quiesce→screen**を保つ（`O/kdebug/recover.c:1091–1109`）。②のS0復帰は元から0FACh→98が先頭（`D/REALHW_RECOVER_DESIGN.md:85`）で、C専用の順序変更はしない。
quiesceは既存1850=0→1854=0→PMC140=0と既存照合を維持（`O/kdebug/recover_gf.c:124–127`）。この位置の追加DACCLK解除・DPMS OFF列はnouveauに無いため設けない。mode/DAC/PLLはcold-requiredとしてS0後のrebootに任せる（`D/GF6200_VPLL_PROBE.md:145`）。
1850=0を映像停止の証明とはしない。局所窓/index/lockの解放は既存処理を使い、旧timingやmode/DACを追加再生しない；S0復帰とGFの試行前状態への復帰は別に判定する。
PCI照合不能ならGF MMIOを省いてFAIL、独立な0FACh→98は行う。decode解放未確認ならA0000hに触れない。mode/PLL/PFB/VRAMは巻き戻さずcold-required・S0まで（`D/REALHW_RECOVER_DESIGN.md:81,120`）。
アクセス/読戻しCHECK/送信/期限/セル復元失敗、98応答停止、モニタ同期外れ・乱れは後続を止めて同じ出口へ。未知510やTEST_CONTROL sense上位を停止理由にしない。固まって出口へ戻れなければ電源再投入。
S0→証拠採取→OS32 reboot→50C=4、508=00051104、578=00000E02、PMC=03010000およびID/BARを基準照合→新inspect。これはkpeek2/段2の実測による運用更新で、PCI RST#や全内部冷間一致の保証ではない（`D/GF6200_VPLL_PROBE.md:145,174,179–182`）。不一致/読取不能なら電源再投入。

## 7. 映像が出ないときの切り分け
各比較はS0→reboot・基準照合→新しい同じinit/modeで行い、変更条件・REC・目視を対に保存する。PAUSEDへ任意pokeして重ねない。
1. 書込みを増やさず入口/出口、PLL解/50C bit9/28、CRTC、VRAM比較、0FACh readbackとモニタ入力を照合。モニタOSDを必ず「無信号／範囲外／同期ありで黒」で記録（無信号は1850・OR・route、同期ありの黒はpalette・VRAM・START・DACCLKを照合、範囲外はtiming/PLL）。機械項目が不成立ならその故障を直し、出力比較に進めない。
2. **1850h←1だけ**をC最終ON後・R前に追加する比較（prepareのBUS50_ONE）。直前1854/cfg54=0、直後1850/cfg50=1・54=0、出力中98セル往復・目視、abortで1850=0とS0まで確認。PROMはinspect取得済みを使い再読しない（`T/TASK_GEFORCE6200.md:49`, `E/docs/hw/bus/pci.rst:50–58`）。
初回には入れず、この比較の回にBUS50_ONE flagと例外を追加する。比較用ruleはREC_Cの当該1書込みだけに値1を許し、RUN/N1の4先zero guardは維持；C出口だけ期待50=1、cleanupは0。`recover.c`の1850非零拒否・zero_hold判定もこの固定節の条件に限って対応する。
3. 1850は既定0へ戻し、DCB/CONNと実配線を照合してANALOG OR1へ出力選択だけ変更（DACCLK/TESTのoffsetと相手head束縛を一組の条件とする）。head A・mode/PLL・0FAChは同じ。
4. RGB-INの実割当てが疑わしければ0FAChを01→03だけ比較（bit1の影響、UNDOCUMENTED上のRa266適用は推測）。次にPOST既存状態との差をMITの保存/生成値と比較；根拠無しの510再書込みやTMDS列追加はしない。

## 8. 既存の利用者の退行
| 利用者 | 維持し、実装時に確認すること |
|---|---|
| 4a/診断 | 通常prepare→main→verifyの値/順/N1・旧PLL solver呼出し・A4即時終端・gf_verifyを維持。MODE_CなしでC節や1850=1が出ない。VPLL診断列を通常Cへ混ぜない。 |
| recover/S0 | 非Cのroute-test/now・local cleanup・cold/FAILを維持。Cの途中失敗/PAUSED abort/外部recover/二重終端・lease返却・既存cleanupの各失敗点から98へ戻る。 |
| CLI/HWDEV/peek | op5〜12・40B・NOSYS・PAUSED門・read-only peek、起動FD除外を維持。op13だけ明示振分け、核/部品の契約CRCを生成で追随（`T/TASK_HWDEV.md:39–44`）。 |
| 98/他装置 | CUI HAL・31kHz・Trident/LAN資源は既存のまま。セル往復は予約セルだけ、変更後は必ず元値照合。一般描画経路へ診断転送を流用しない。 |

## 9. 実装の段と重さ
1. 中〜重（1〜2日）：`gfinit_mode.c/.h`相当のstate計算、DCBのデータ確認、PLL再計算、§3の独立期待traceと許可表を固定。MIT表示保持。省略した停止経路・POST依存をレビューする。
2. 重（1〜2日）：`gfinit_plan/bus/recover_gf/recover`へC節・固定行転送/DAC列・route/セルCHECKを結線し既存cleanupへ接続。現行2048 nodes等の容量、arg/target/rule/entry・部品mem上限とREC最悪量を算出、実効秒数を報告する。
3. 中（0.5〜1日）：`gfinit_wire/gfinit/kdebug_main`・`kernel/kdebug_call`・shell・HWDEV契約・`rshell_recover.py`を1op/flagsへ追随。順序/係数/色形式/失敗出口と§8の既存host試験を確認；関連チェック・buildはこの将来の実装段で実施。
4. 実機の回：`tools/accept/gf_c.md`相当の台本に当日manifest・C承認・§5判定・S0/reboot比較を掲載。目視結果は票へ。NP21/Wは入口/復帰退行の確認に限り、GeForce映像の代替にしない。
容量基準：既存 `O/build/out/{kdebug.bin,hwdev.mod}` のheaderを読み取ったimage/memは125,892/139,264 Bと93,696/90,112 B（鮮度・mainとの一致は未保証）。欄は `O/tools/module_schema.py:7–17`、image/mem上限はKDEBUG 262,144/262,144・HWDEV 196,608/196,608 B（同:55,180）。旧239,908 B・92%は分離前（`T/TASK_HWDEV.md:9`）。
推定増分：mode計算・固定列・許可表・転送はHWDEV（`O/build/hwdev.mk:1–9`）でcode/表/reloc計12〜24 KiB、核のdispatch/共通接続は1〜2 KiB。大きい側でimageはhwdev=118,272 B（上限まで78,336 B）、kdebug=127,940 B（上限まで134,204 B、M5目標192 KiBまでは68,668 B）。係数solverは再利用、307,200 Bの絵を像に埋め込まない。
mem増分はHWDEVの640 B行buffer・state・追加表/コード・丸めを含め最大32 KiB、核は4 KiBと推定：122,880/143,360 B、各上限との差73,728/118,784 B。ownerのplan等は別計数（`T/TASK_HWDEV.md:20`）。実装段のビルドでimage/mem/load spanとowner頁数を実測して置換し、現時点で枠内を保証しない。

## 10. 未確認
OR0/OR1と実コネクタ/RGB-INの対応、DCBのhead mask、Ra266の0FACh=01による映像選択は未確認；§7の一条件比較で解く。
25,175 kHzの実ROM limitsでの係数/誤差、PLL lock/実周波数、510 bit9=1時の像、POST無しで§3のstateが十分かは未確認。実装前のmanifestと実機の映像を区別する。
初版はanalog専用の省略列を採った。FP/TV停止値、未計算CRのzero state、読戻しmaskは実装時に各targetの資料と照合し、読出し副作用のあるAR/DACに一律verifyを付けない。
方向単位ログのREC時間/容量と既存cleanupの実対象数による秒数、C出力中の98セル・S0・reboot後の値は未実測。設計作成完了を4cの実機合格とは呼ばない。

## 段 1 の前に PM が埋めたこと (2026-10-28、terra の設計の欠けの報告 — §2 の DCB の照合)
- 出所: ノートの `~/os32-tmp/evidence/2026-10-28/gf-dcb/DCB.txt` (10/21 の envytools nvbios の出力 s3_nvbios.txt から、ROM のデータの表のデコードした欄だけ。生のバイトは出さない)。
- DCB 0x96ac 版 3.0 の 4 エントリ: **0 = ANALOG・I2C 0・heads 0 1・CONN 0・OR 0・LOCAL・maxfreq 400000kHz**、1 = ANALOG・I2C 1・heads 0 1・CONN 1・OR 1、2 = TMDS・I2C 1・heads 0 1・CONN 1・OR 0、3 = TV・heads 0 1・CONN 2・OR 1。CONN 0x97da: 0 VGA・1 DVI_I (HPD_1)・2 COMPOSITE・3 S_VIDEO・4 RGB。
- 帰結: エントリ 0 は head A (0) を許し OR 0・CONN 0 (VGA) なので、**§1・§2 の「head A・ANALOG OR0」の仮の選択は ROM の表と合う** (確定)。RGB-IN に繋がるのが VGA の端子 (CONN 0) か DVI-I のアナログ (CONN 1・OR 1) かは配線の確認 (§10 の未確認) のまま — 映像が出なければ §7 の切り分けに OR 1 を足す。
- 配線 (ユーザー 2026-10-28): **GeForce は VGA の端子で 98 の切替回路 (RGB-IN) に繋いでいる** → DCB のエントリ 0 (ANALOG・OR 0・CONN 0 VGA) が RGB-IN の経路で確定。ボードには DVI の端子もある (ユーザーは DVI-D と言う、ROM の CONN 1 は DVI_I・TMDS OR 0 と ANALOG OR 1 が載る) — 4c では使わない。§7 の切り分けの「OR 1」は外してよい。

## 段 2 の前に PM が決めたこと (2026-10-28、terra の設計の欠けの報告 — §3 の市松)
- 市松は **1 マス 8×8 px** (64×64 px の中に 8×8 マス)、**左上のマスは白**で白・黒を交互 (palette の番号は段 1 の kdebug/gfinit_mode.c の表で白=4・黒=0)。位置は外周の白枠 (8 px) の内側の左上 x=8〜71・y=8〜71。
- 重なりの優先は **白枠 > 市松 > 縦の帯** (帯は全幅 640 px に 80 px ずつ 8 色を敷き、市松と枠が上書きする)。行のデータは y ごとにこの規則で決まる 640 B の固定の列 (480 行で 3 種類: 枠の行 (y<8・y>=472)・市松の行 (8<=y<72)・帯の行)。
- 理由: 目視で同期のずれ (16 dot など) と色の取り違えが一目で分かることを優先 — 8 px のマスは文字 1 字分で数えやすい。

## 容量の扱い (ユーザー決定 2026-10-28)
- 「特に物理的な（フロッピーディスクに入らない）などがなければ拡張していいですよ」— owner の作業領域 (HWDEV_CORE_API §9 の 267 頁) と計画の表の大きさは、物理的な制約 (起動 FD・部品の像の上限・番地の配置) に当たらなければ広げてよい。段 2a で astra が 267 頁に収めるため GF_PLAN_TARGETS を 768→608 に減らした件は、今の 4a・S0 の利用が足りるかをレビューで確かめ、足りなければ減らさずに枠を広げる。

## 段 2a の実装で設計者が決めたこと
- MODE_C=2 は prepare だけが受理し owner に保存する；ALLOW_WRITE は承認の条件、MODE_C は C 節の即値と専用 profile の対象・規則・layout に反映して plan hash/profile_crc を分け、再送 cache は flags 全体の一致を要する。
- A2 と main 0〜4 は旧 builder と同じ prefix を使い、REC_RUN の書込み前の読取りは 4a の read_safe を規則に保持して判定する（C の phase 9 だけ C の read_safe を使う）；C entry (phase=9、main/pc=NONE) と全 C 節を初回に bind する；旧 A4 を含めず、MODE_C の旧 verify は拒否し、main4 後は PAUSED で C 入口を待つ（op13 は段 3、旧 prepare に C は後付けしない）。
- s(x) の 8 値は main4 後・入口 CHECK の後・Q 前に READ/SELECT で採取し、既存 recover capture を消費先ごとに割り当てる；CR52 は (s+4)&FF、CR1C/33/4B と SEL_CLK の変換は §3 の式のまま（nouveau crtc.c:494,532,542–565,685–689、保存時刻だけ OS32 の cold 起動に合わせる）。
- r(x) は既存 REC_ARG_READ でその操作直前に評価し、PLL は既存 parser/solver/sequence で 25,175 kHz を bind 時に解く；C040 の末尾だけは先頭の C040 読取りを capture して復元し、係数直後 verify=0 は維持する（nouveau nvkm/subdev/devinit/nv04.c:197–272、§3 の 4a 再利用差分）。
- CR/SR/GR は既存 target を共有し、各 READ/DATA の直前に同じ ctx/phase の SELECT を置く；C の DATA は共有 RW key でも local 保存を増やさず COLD とし、従来の index/lock 保存は保つ（nouveau hw.c:712–772,564–586、旧 timing を再生しない §4 の契約）。
- MISC は専用 VGA_DIRECT target とし、ログの書込み先 C03C2 を保ちつつ bus の読取りだけ C03CC 別名へ写す（nouveau dispnv04/nvreg.h:129,137）；AR/DAC をこの型や通常 MMIO に置換しない。
- Q の FP_ON 条件は (r(680848)&10000011)!=0、真の枝だけ OFF 書込み→50ms を置き、P/C の SR01 の後に各10msを置く（nouveau dfp.c:93–110、crtc.c:175–234）；待ちは専用実行部が未実装なので段 2a では未結線とする。
- 2b の VRAM と AR/DAC、2c の route・98 セル・入口/出口 CHECK（cleanup と REC 予算を含む）は、種類・206 行表の全欄を即値にした REC_LAYOUT_UNBOUND 節として hash に含める；bind は通るが enter/leave は必ず拒否し、成功 NOP にしない（段の分割による OS32 の実装契約、nouveau に相当なし）。
- 完成時の C の出口は CHECK→NONE→trial_step の PAUSED であり trial_end は呼ばない；段 2a で C 全体の実行成功はまだ提供せず、試験だけが明示的に未結線を飛ばして後続の規則・値・順を検査する。
- 許可表の row ID 578〜783 を保って書込み 178 行を規則化し、未結線の型付き 28 行と区別する；同じ target への別操作も固有 substep を保ち、旧 phase/rule/trace は変更しない。
- target 608 は今の 4a の最大 471〜472・C の 234 に足りるので 768 から減らした（余り 136）。layout 枠は 1056→1280、capture 枠は 32→48（valid は64bit）とし、最大 BAR1 の旧 A4 と C をそれぞれ検査する。足りなくなれば既存の利用を減らさず、物理的な制約に当たらない範囲で owner の頁数を広げる（ユーザー決定 2026-10-28、HWDEV_CORE_API §9）；絵全体の埋込みは行わない。

## 段 2b1 の結線
- VRAM の未結線節だけを、BAR1+[0,307200) の転送・比較の 2 node に置換する。`gfinit_mode_row` の 640 B 行を480行、A4と同じDWORD窓アクセスで処理し、窓/PTE/IRQ復元後の行間に既存safe pointを置く。全行転送後は既存read-safeなPFB容量レジスタの読取りでposted writeを排出する。
- `recover_plan_access` が固定範囲・方向の型を扱い、`recover_ops.mode_vram` へ渡す。正常時はgf_busが方向ごとにBEGIN/ENDの計4行（bar/off/len/dir）。比較は不一致後も全域を読み、最初のbyteのBAR1相対off/期待値/実値だけをENDに保持し、既存のREC_VERIFY失敗出口へ進む。
- 未結線は35→34節。AR/DAC・route/98セル・入口/出口CHECK・待ち・cleanup/REC予算とop13は後段のまま。旧phase/許可表の番号は変えず、共有callback/型の契約CRC・layoutは生成器で追随する。

## 段 2b2 の結線
- AR の 21 行と gate 3 行は専用の C 操作とし、各行で 6013DA を読んで FF を reset、AR は 6013C0 に index→data、gate は同 port に 00 または 20 を書く。値・順は段 1 の生成表と mode state を使う。
- DAC の mask/index/data 3 行は固定 1 操作にまとめ、6813C6=FF→6813C8=00→6813C9 に `gfinit_mode_palette` の RGB 768 byte を書く。操作ごとに gf_bus BEGIN/END 1 組、失敗は既存 trial_end に渡す。通常の read-safe MMIO target や汎用 bulk は追加しない。
- 未結線は 34→7 節。route・98 セル・入口/出口 CHECK・待ち・cleanup/REC 予算と op13 は後段のまま。旧 phase/許可表の番号は維持し、専用 callback の HWDEV 契約 CRC・layout は生成器で追随する。

## 段 2c1 の結線
- 入口 CHECK は既存 token の identity、保存した BAR 3 本、Command=0102、cfg50/54・1850/1854=0、route&3=0 を比較する。main4 後の C entry から Q 前の採取へ進む。出口 CHECK は §§3–4 の必須値と 510 の読取りを保持し、係数は 50C 選択後だけ比較する。CR11 は最終 AB、CR17 は C3、SR01 は 01 を比較する。
- CHECK の node は source/key/mask/expected/keep/result-slot を hash に含める。`recover.c` の `mode_check_access` に読取り・mask 比較・項目ごとの CHECK REC・不一致時の既存 REC_VERIFY 失敗出口をまとめる。524・CR33・C040 は既存 single-use capture を使い、結果は `gfinit_plan.c_checks` に保持する。510 は mask=0、TEST は bit20 のみで sense を除外する。
- 待ち 3 本は専用の型と `recover_ops.mode_wait` から既存 `recover_delay_ms` の 1ms 分割・safe point へ結ぶ。手本の順序は nouveau c03c1a1 MIT `dispnv04/dfp.c:93–110` (FP_ON 時の OFF→50ms)、`crtc.c:175–234` (P/C の SR01→10ms→CR17)。実行せず leave する CHECK/待ちは拒否する。共有 callback/型の契約 CRC・layout は生成器で追随する。
- 出口の route とセル結果は `GF_C_EXIT_ROUTE_SLOT` / `GF_C_EXIT_CELL_SLOT` を未成立のまま予約し、対応する未結線 node に slot を記す。R の route/セルも未結線のままなので C 全体の成功はまだ提供しない。cleanup 接続・REC 予算の再計数は 2c3、公開 op13 は段 3。

## 作りの大きさ (ユーザー決定 2026-10-28)
- 段 1〜2c1 で製品のコードが約 1,260 行 (gfinit_plan_c.c 464・recover.c +227・gfinit_mode.c/.h 281・gfinit_bus.c +88 ほか、生成の表と試験は別) — 大半は 4a と同じ「戻せる試行」の枠組み (許可表・capture・書く前の読取り・REC・safe point・S0) への載せ替えで、nouveau の load_state そのものは 200〜300 行。ユーザー「もっと細かくシンプルに実装したほうが良いのでは？」→ 選択肢 3 (折衷) を決定: **4c の実機の回までは今の作りで残り (2c2・2c3・op 13) を小さく仕上げ、映像が出ることを確かめた後、本番の画面ドライバは nouveau の load_state に近い簡素な作り** (許可表と行ごとの capture を使わず、戻し方は OS32 の reboot での GeForce のリセット (GF6200_VPLL_PROBE の kpeek2) に任せる案) で書く。以後の段は細かくシンプルに切る。

## 実機の台本 (2026-10-29)
- 段 1〜3b は main (b8483170)。段 4 の台本と承認の資料は `tools/accept/gf_c.md` (astra 起草・Opus レビュー確定可・PM が「当日ユーザーがすること」を冒頭に)。立会いの回はユーザーの承認と目視が要る。
