# GeForce 6200 の main 0〜3 (A3) 立会いの台本と承認の資料

> 正典: 票 TASK_HWDEV §3-1 の 6、確定設計 [GF6200_4A_DESIGN](../../docs/decisions/realhw/GF6200_4A_DESIGN.md) §7 (実機 A3-0〜4)・U2・N1。前の段の台本は [gf_s8.md](gf_s8.md)、次の A4 は [gf_a4.md](gf_a4.md)。本ファイルは受入の台本で、確定設計を変えない。
> 出所: ~/os32-tmp/run/s9-main/MAIN_PLAN.final.md (astra 起草・Opus 2 往復・PM が条件 N1〜N3 を反映、2026-10-26)。末尾の「確定」が本文の該当の行より優先する。**書込みを伴う段 (A1・A2・main ごと・A4) はユーザーの承認を取る。**


P2-1：main単体とtrial全体の60秒枠を区別し、A2・各mainの経過記録を追加。
P2-2：host timeoutをceil(60+Tlog)と明示し、自動abort／command省略のrecover now／電源の経路へ修正。
P2-3：A2後の組3記録をmain 0承認の前提にし、既存口の制約と必要な読取観測処理を明記。
P2-4：RUN_ONE失敗時の自動cleanup終端FAIL・S0 verified条件と、abortを重ねず電源へ進む手順を追加。
P2-5：RESET・strap・PRAMIN・DRAMの変更結果、98画面への影響と電源による戻し方を追記。
P2-6：同じ立会いでA4を続けるかを推奨付き未決1件として提示。
P3：当日のCRC、main 4後に終了する場合のabort→電源、N1の根拠を明記。

# GeForce 4a main 0〜3：次回 Ra266 立会いの承認資料

対象は単一利用者・単一タスク・協調切替の OS32。ハードウェア異常・バグで停止したときに止めて戻すための資料で、設計・実装は変更しない。
根拠の略記：D=`docs/decisions/realhw/GF6200_4A_DESIGN.md`、S=`docs/decisions/realhw/GF6200_S7_SHELL.md`、R=`docs/decisions/realhw/REALHW_RECOVER_DESIGN.md`、T=`kdebug/generated/recover_gf_tables.inc`。以下の `T:行` 等はすべて `/home/hight/os32-v3/` からの file:line。
観測前提（ユーザー提供、2026-10-26）：397ad51／Image CRC 6a8ac396、S8 合格。inspect は rom_len=63488／crc=E9A60DE3／chip=44／match=1。
同前提：BAR0=24000000h・16MB、BAR1=30000008h・256MB prefetch、BAR2=25000000h・16MB、Command=0102h・BM=0。
同前提：A2 PAUSED まで PBUS1854/1850・PMC140=0、PMC.ENABLE=03010100h→03010101h、head0 CR44=0、CR1F unlock rb=03h、CR11=20h。98 テキスト表示・更新を目視。abort PARTIAL→電源再投入→route-test 後も98表示正常。
この観測は次回の承認を代替しない。A1→A2→各 main→A4 は個別承認（D:217）。main 0〜3 の実機結果はまだ未確認。

| 段 | 次回の操作と承認境界 |
|---|---|
| 起動・inspect | 電源を入れ直す→ver・CRC・IDLE・新baseline→inspect を1回。上記像・ROM同定を照合（S:45–53、tools/accept/gf_s8.md:38–41）。 |
| A1 | ユーザー承認→size-bars。BAR長・復元・Command=0102h/BM=0・REC final OK verified=1を確認（D:81–82、tools/accept/gf_s8.md:17–23）。 |
| A2 | 結果を見せて承認→prepare→新sidでPAUSED。G1・別名応答・98更新を確認。**A2後・main 0前にPSTRAPS読値から組=3を記録済み**を承認前提とする。今回提示されたS8結果にはその記録がないため、下記P2-3の観測手段が整うまでmain 0を承認しない（D:185、tools/accept/gf_s8.md:35）。 |
| main 0→1→2→3 | **A2と同じ起動・同じsessionのPAUSEDの続き**。各表を示し、その1本だけ承認→RUN_ONE→PAUSED→RECと98画面を確認。間にabort・再起動・補助コマンドを挟まない（S:41,53、kdebug/gfinit.c:373–378、kdebug/recover.c:1230–1255）。 |
| main 4→停止 | DONEのみを静的確認し `run-one <sid> 4 --allow-write`→PAUSED。GPU書込みなし。**A4の前で承認待ち、未承認のverifyを送らない**。同じ立会いで続けない場合は同sidでabort→期待PARTIAL/S0/verified=1→電源再投入。次回A4には全mainの再実行が必要（D:186–187、kdebug/gfinit.c:379–381、tools/accept/gf_s8.md:32–41）。 |

表の読み方：番地はBAR0からのoffset、MMIOは32bit。CR/SRは8bitでindex選択も書込みに含む（選択値は名称末尾、例CR3Cなら3Ch）。CR index/data=6013D4/6013D5h+head×2000h、SR=C03C4/C03C5h+head×2000h（D:145–147、T:587–740）。
`R(w,b)` は「実機で決まる」旧値を用いる `(old & ~w) OR b`（幅内）。wは変更可能mask、bは設定bits。保持maskとの取り違えに注意（kdebug/recover_gf.c:152–156、kdebug/recover.c:1850–1867）。
`選択[...]` は `(head0.CR3C & 70h)>>4` の0〜7番を1つ選ぶ。全8値を順に書く意味ではない。許可表は全8組を通し、COPYのverify FFhも計算値との一致だけで組3を強制しない。組違いでもmain 0は別組のPFBを書いて走りきりうるため、A2後の組3記録が承認の前提（D:131,146、T:91–152,581、kdebug/recover.c:1925）。
verify欄の `0` は**実装に書込み直後のreadback照合なし**、一致済みではない。非零Vは `(actual XOR new)&V=0`。値は左欄のnew／退避値／計算結果（kdebug/recover.c:1916–1930）。
書込み数はRUN phaseの**静的許可行数**（index・全選択肢・PLL内部・SUB/CALL込み、REPEAT未展開）：0/1/2/3=141/229/123/51。実行回数は分岐・反復で異なる。T:38–584のmain_id列を集計（列定義 kdebug/recover_gf.c:213–218）。
PLL表記は「実機で決まる」：CR3C選択とPLL limits、N/M/P探索、実機から退避した制御値でnewを算出。CORE 200MHz・MEM 400MHz・VIDEO 36MHzが期待、解なしなら無書込み停止。係数=`(N2<<24)|(M2<<16)|(N1<<8)|M1`、補助値=`(saved&keep)|bits`（D:150–158、kdebug/gfinit_pll.c:196–244）。
PLL gate・制御・mirror・powerの書込み順とmaskは `kdebug/gfinit_pll.c:206–242` の列を使用。計算値のreadbackでありPLL lockやVRAM成功とは呼ばない（D:159）。
COPYは「実機で決まる」：CR3CはPSTRAPSのshift/mask結果を旧CR3Cの保持部分へOR（期待3xh）、1700hは10020Ch読値をshift・mask・xorし旧1700hの保持部分へOR。引数は検査済みplan、式は `kdebug/recover_gf.c:164–175`、生成は `kdebug/gfinit_decode.c:596–604`（D:116,146）。

## main 0：承認してから1本だけ

含むもの：SUB6(E728 CORE PLL)・SUB7(E754 MEM PLL)、RESET（184Ch退避→0F00hクリア→PMC2段書込み・10µs→184Ch復元→1850h=0）。CALL/REPEATなし。SR01 bit5をhead1→head0で立てる。98表示は変化しないはずだが、PMC/strap/PLLの変更で異常は起こりうる（D:119,132,136、T:43–55,531–532,551–571,580、kdebug/gfinit_decode.c:606–616）。

| 書込み先（幅） | 値／決め方（同じ先は列記順、条件skipあり） | verify mask | 根拠（規則；target） |
|---|---|---|---|
| PCRTC 600818h（32bit） | R(0h,0h)；R(0h,0h) | 0h | T:38,41；T:600 |
| PCRTC 60081Ch（32bit） | R(11010h,11000h)；R(22020h,0h) | 0h | T:39,42；T:601 |
| PCRTC 600858h、PCRTC 602858h（32bit） | R(1FFFh,1FFFh) | 0h | T:40,550；T:602,731 |
| h0 CR44（8bit） | index選択＋data：3h；0h | 0h | T:43–44,47–48；T:597 |
| h1 SR01、h0 SR01（8bit） | index選択＋data：R(20h,20h) | 0h | T:45–46,49–50；T:603–604 |
| PBUS 00184Ch（32bit） | R(F00h,0h)→退避値（実機で決まる） | 0h/FFFFFFFFh | T:51,580；T:605 |
| PMC.ENABLE 000200h（32bit） | 1100000h→5110111h；R(2100000h,2100000h)。RESETでA2の03010101hを上書きしエンジン状態を変更。98維持を期待するが停止・崩れは中止、元値再生で戻さず電源再投入（D:132,170–171） | 0h | T:52–53,81；T:592 |
| PBUS 001850h、MMIO 00C020h、PFB 100210h、MMIO 001508h、PMC割込み 000140h（32bit） | 0h | 0h | T:54,59–60,73,78；T:589,591,609–610,619 |
| PSTRAPS 101000h（32bit） | R(80000C00h,80000000h)。bit31を立てbits10–11を落とし冷間strapを変更。98維持を期待するが異常時は中止、冷間値への復帰は電源再投入（docs/tasks/realhw/GEFORCE6200_VBIOS.md:126,215） | 0h | T:55；T:606 |
| MMIO 10100Ch（32bit） | R(0h,0h) | 0h | T:56；T:607 |
| MMIO 00C000h（32bit） | 1h；R(0h,0h) | 0h | T:57–58；T:608 |
| MMIO 00402Ch（32bit） | C1C11002h | 0h | T:61；T:611 |
| PLL gate 00C040h（32bit） | 30000000h；3C0BC003h；PLL補助（実機で決まる）→PLL補助（実機で決まる）；PLL補助（実機で決まる）→PLL補助（実機で決まる） | 0h/FFFFFFFFh | T:62,89,553,556,562,569；T:612 |
| CORE制御 004000h（32bit） | C000001Ch；PLL補助（実機で決まる）→PLL補助（実機で決まる）→PLL補助（実機で決まる） | 0h/FFFFFFFFh | T:63,551–552,555；T:613 |
| MMIO 004008h（32bit） | E000001Ch | 0h | T:64；T:614 |
| VPLL段制御 680580h（32bit） | 1C0001Ch；1F0001Fh | 0h | T:65,75；T:615 |
| MEM制御 004020h（32bit） | 2000001Ch；PLL補助（実機で決まる）→PLL補助（実機で決まる）→PLL補助（実機で決まる）→PLL補助（実機で決まる）→PLL補助（実機で決まる）→PLL補助（実機で決まる） | 0h/FFFFFFFFh | T:66,558–560,565–566,570；T:616 |
| MMIO 680510h（32bit） | 1F0001Fh | 0h | T:67；T:617 |
| h0 CR3C（8bit） | index選択＋data：1h；0h；COPY（実機で決まる） | 0h/FFh | T:68–72,90,99,108,117,126,135,144,531–532,581；T:618 |
| MMIO 001584h（32bit） | R(FFh,11h) | 0h | T:74；T:620 |
| MMIO 001580h（32bit） | R(30000000h,30000000h) | 0h | T:76；T:621 |
| MMIO 00C044h（32bit） | 1010h | 0h | T:77；T:622 |
| MMIO 0015B4h（32bit） | R(FC000000h,8000000h) | 0h | T:79；T:623 |
| MMIO 0015B8h（32bit） | 94800000h | 0h | T:80；T:624 |
| MMIO 680608h、MMIO 682608h（32bit） | 100000h | 0h | T:82–83；T:625–626 |
| PFB 100080h（32bit） | 47170020h | 0h | T:84；T:627 |
| MMIO 001084h（32bit） | 205749h | 0h | T:85；T:628 |
| PFB 1003ACh、PFB 1003B0h（32bit） | 80808080h | 0h | T:86–87；T:629–630 |
| MMIO 004028h（32bit） | 80000AA8h | 0h | T:88；T:631 |
| PFB 100220h（32bit） | 選択[50B1110h,50B1110h,50B1110h,3080E0Bh,50A120Fh,4090F0Dh,50A120Fh,4090F0Dh] | 0h | T:91–98；T:632 |
| PFB 100224h（32bit） | 選択[6010406h,6010406h,6010406h,5010405h,5010406h,5010405h,5010406h,5010405h] | 0h | T:100–107；T:633 |
| PFB 100228h（32bit） | 選択[20230204h,20230204h,20230204h,20220303h,20220205h,20220304h,20220205h,20230204h] | 0h | T:109–116；T:634 |
| PFB 10022Ch（32bit） | 選択[42098203h,42098203h,42098203h,41088102h,420A9203h,41088102h,420A9203h,41088102h] | 0h | T:118–125；T:635 |
| PFB 100230h（32bit） | 選択[D1h,D1h,D1h,D1h,D1h,D1h,D1h,D1h] | 0h | T:127–134；T:636 |
| PFB 100234h（32bit） | 選択[266633h,266633h,266633h,266622h,B6FF44h,266622h,266622h,266622h] | 0h | T:136–143；T:637 |
| PFB 100238h（32bit） | 選択[140200h,140200h,140200h,140200h,3F0200h,140200h,140200h,140200h] | 0h | T:145–152；T:638 |
| CORE係数 004004h、MEM係数 004024h、MEM係数mirror 00403Ch（32bit） | PLL係数（実機で決まる） | FFFFFFFFh | T:554,563–564；T:734–735,737 |
| MEM電源補助 004600h（32bit） | PLL補助（実機で決まる）→PLL補助（実機で決まる） | FFFFFFFFh | T:557,568；T:738 |
| MEM制御mirror 004038h（32bit） | PLL補助（実機で決まる）→PLL補助（実機で決まる）→PLL補助（実機で決まる） | FFFFFFFFh | T:561,567,571；T:736 |

ノート：承認後 `gfinit run-one <sid> 0 --allow-write` を下記monitorで1回。RECのmain=0・gf_pre/gf_bus BEGIN/END・rc・PAUSED・同じsid・連番欠落なしを確認し、差分と直後の98表示/更新をユーザーへ提示（S:38,41,53、D:163–168）。

## main 1：承認してから1本だけ

含むもの：PFB・メモリ関連設定とMACRO3/1（1002D8h/1002D4h←1）。SUB/CALL/REPEATなし。CR3Cは選択用indexだけでSR01・E81E・CR dataの表示変更なし。98表示は変化しないはず（T:153–381、D:136）。

| 書込み先（幅） | 値／決め方（同じ先は列記順、条件skipあり） | verify mask | 根拠（規則；target） |
|---|---|---|---|
| h0 CR3C（8bit） | index選択のみ（data書込みなし） | 0h | T:153,162,171,180,194,210,219,229,238,247,256,265,274,283,292,302,311,320,329,339,348,357,362,371；T:618 |
| PFB 100350h（32bit） | 選択[440Ah,440Ah,440Ah,440Ah,440Ah,440Ah,440Ah,440Ah] | 0h | T:154–161；T:639 |
| PFB 1003F4h、PFB 1003F8h、MMIO 001080h、PFB 100458h（32bit） | 選択[0h,0h,0h,0h,0h,0h,0h,0h] | 0h | T:163–170,172–179,181–188,230–237；T:640–642,658 |
| PFB 100210h（32bit） | 0h；選択[0h,0h,0h,0h,0h,0h,0h,0h] | 0h | T:189,330–337；T:610 |
| MMIO 001220h、MMIO 001228h、PFB 100090h（32bit） | 0h | 0h | T:190–191,381；T:643–644,675 |
| MMIO 001210h、PFB 10021Ch、PFB 1002D8h、PFB 1002D4h（32bit） | 1h | 0h | T:192,301,360–361；T:645,666,672–673 |
| MMIO 001214h（32bit） | 4770101h | 0h | T:193；T:646 |
| MMIO 001218h（32bit） | 選択[F0000101h,F0000101h,F0000101h,F0000101h,F0000101h,F0000101h,F0000101h,F0000101h] | 0h | T:195–202；T:647 |
| MMIO 001230h、MMIO 001234h、MMIO 001240h、MMIO 001244h（32bit） | BFBFBFBFh | 0h | T:203–204,206–207；T:648–649,651–652 |
| MMIO 001238h、MMIO 001248h（32bit） | BFBFBEBFh | 0h | T:205,208；T:650,653 |
| MMIO 00124Ch（32bit） | AA11AA11h | 0h | T:209；T:654 |
| PFB 100360h（32bit） | 選択[8B303A98h,8B303A98h,8B303A98h,8B303A98h,8B303A98h,8B303A98h,8B303A98h,8B303A98h] | 0h | T:211–218；T:655 |
| PFB 100450h、PFB 100470h（32bit） | 選択[100h,100h,100h,100h,100h,100h,100h,100h] | 0h | T:220–227,248–255；T:656,660 |
| PFB 100454h（32bit） | 11h | 0h | T:228；T:657 |
| PFB 10045Ch（32bit） | 選択[1011h,1011h,1011h,1011h,1011h,1011h,1011h,1011h] | 0h | T:239–246；T:659 |
| PFB 100474h（32bit） | 選択[11110101h,11110101h,11110101h,11110101h,11110101h,11110101h,11110101h,11110101h] | 0h | T:257–264；T:661 |
| PFB 100490h（32bit） | 選択[2h,2h,2h,2h,2h,2h,2h,2h] | 0h | T:266–273；T:662 |
| PFB 100480h（32bit） | 選択[77777777h,77777777h,77777777h,77777777h,77777777h,77777777h,77777777h,77777777h] | 0h | T:275–282；T:663 |
| PFB 100460h（32bit） | 選択[88888888h,88888888h,88888888h,AAAAAAAAh,88888888h,77777777h,AAAAAAAAh,88888888h] | 0h | T:284–291；T:664 |
| PFB 100218h（32bit） | 選択[100h,100h,100h,100h,100h,100h,100h,100h]；選択[100h,100h,100h,100h,100h,100h,100h,100h]；選択[101h,101h,101h,101h,101h,101h,101h,101h] | 0h | T:293–300,340–347,349–356；T:665 |
| PFB 100200h（32bit） | 選択[3003202h,3003202h,3003202h,3003202h,3003202h,3003202h,3003202h,3003202h]；選択[3003200h,3003200h,3003200h,3003200h,3003200h,3003200h,3003200h,3003200h] | 0h | T:303–310,312–319；T:667 |
| PFB 100204h（32bit） | 選択[449020h,449020h,559020h,559020h,559020h,559020h,559020h,559020h] | 0h | T:321–328；T:668 |
| PFB 100214h（32bit） | B0h | 0h | T:338；T:669 |
| PFB 1002C4h（32bit） | A7h；選択[100000h,100000h,100000h,100000h,100000h,100000h,100000h,100000h] | 0h | T:358,363–370；T:670 |
| PFB 1002CCh（32bit） | A7h；選択[100000h,100000h,100000h,100000h,100000h,100000h,100000h,100000h] | 0h | T:359,372–379；T:671 |
| PFB 100084h（32bit） | 1Dh | 0h | T:380；T:674 |

ノート：承認後 `gfinit run-one <sid> 1 --allow-write` を下記monitorで1回。RECのmain=1・gf_pre/gf_bus BEGIN/END・rc・PAUSED・同じsid・連番欠落なしを確認し、差分と直後の98表示/更新をユーザーへ提示（S:38,41,53、D:163–168）。

## main 2：承認してから1本だけ

含むもの：VIDEO PLLとhead0/1 CR設定・CR57/58 latched列。SUB/CALL/REPEATなし。CR44でhead1へ切替え最後はhead0へ戻る。SR01 bit5解除・E81Eなし。GF側表示設定は変化しうるが98表示は変化しないはず（T:382–497,572–576,582–583、D:132,145–147）。

| 書込み先（幅） | 値／決め方（同じ先は列記順、条件skipあり） | verify mask | 根拠（規則；target） |
|---|---|---|---|
| MMIO 001088h（32bit） | 11F0h | 0h | T:382；T:676 |
| MMIO 00108Ch（32bit） | D1h | 0h | T:383；T:677 |
| MMIO 001530h（32bit） | R(60Fh,407h) | 0h | T:384；T:678 |
| MMIO 680880h（32bit） | R(10000000h,10000000h) | 0h | T:385；T:679 |
| MMIO 680610h（32bit） | 0h | 0h | T:386；T:680 |
| h0 CR38、h0 CR28、h0 CR33、h0 CR2C、h0 CR3A、h0 CR3B、h0 CR4A、h0 CR4B、h0 CR4C、h1 CR2C、h1 CR3A、h1 CR3B、h1 CR3C、h1 CR4A、h1 CR4B、h1 CR4C（8bit） | index選択＋data：0h | 0h | T:387–388,391–394,399–410,456–469；T:681,683–684,687–692,702–708 |
| h0 CR1A（8bit） | index選択＋data：3Fh | 0h | T:389–390；T:682 |
| h0 CR21、h1 CR21（8bit） | index選択＋data：FAh | 0h | T:395–396,448–449；T:685,698 |
| h0 CR2B（8bit） | index選択＋data：R(DFh,0h) | 0h | T:397–398；T:686 |
| h0 CR57（8bit） | index選択＋data：0h→1h→2h→3h→4h→5h→6h→退避値（実機で決まる） | 0h/FFh | T:411–412,415–416,419–420,423–424,427–428,431–432,435–436,439,582；T:693 |
| h0 CR58（8bit） | index選択＋data：7Fh→0h→0h→0h→0h→0h→0h | 0h | T:413–414,417–418,421–422,425–426,429–430,433–434,437–438；T:694 |
| h0 CR43、h1 CR43（8bit） | index選択＋data：1h | 0h | T:440–441,483–484；T:695,711 |
| h0 CR85、h1 CR85（8bit） | index選択＋data：R(FFh,FFh) | 0h | T:442–443,450–451；T:696,699 |
| h0 CR86、h1 CR86（8bit） | index選択＋data：R(FFh,1h) | 0h | T:444–445,452–453；T:697,700 |
| h0 CR44（8bit） | index選択＋data：3h；0h | 0h | T:446–447,485–486；T:597 |
| h1 CR2B（8bit） | index選択＋data：R(EFh,0h) | 0h | T:454–455；T:701 |
| h1 CR57（8bit） | index選択＋data：0h→1h→2h→退避値（実機で決まる） | 0h/FFh | T:470–471,474–475,478–479,482,583；T:709 |
| h1 CR58（8bit） | index選択＋data：7Fh→0h→0h | 0h | T:472–473,476–477,480–481；T:710 |
| h0 CR52（8bit） | index選択＋data：4h | 0h | T:487–488；T:712 |
| h0 CR53（8bit） | index選択＋data：40h | 0h | T:489–490；T:713 |
| MMIO 001584h（32bit） | R(FFFFFh,0h) | 0h | T:491；T:620 |
| PFB 100210h（32bit） | 80000000h | 0h | T:492；T:610 |
| MMIO 68052Ch（32bit） | R(101h,1h) | 0h | T:493；T:714 |
| MMIO 68252Ch（32bit） | R(101h,101h) | 0h | T:494；T:715 |
| MMIO 680848h、MMIO 682848h（32bit） | R(100000h,100000h) | 0h | T:495–496；T:716–717 |
| MMIO 001200h（32bit） | 3103Ch | 0h | T:497；T:718 |
| PLL gate 00C040h（32bit） | PLL補助（実機で決まる）→PLL補助（実機で決まる） | FFFFFFFFh | T:572,576；T:612 |
| VPLL段制御 680580h、VPLL係数 680578h、VPLL 680508h（32bit） | PLL補助（実機で決まる） | FFFFFFFFh | T:573–575；T:615,739–740 |

ノート：承認後 `gfinit run-one <sid> 2 --allow-write` を下記monitorで1回。RECのmain=2・gf_pre/gf_bus BEGIN/END・rc・PAUSED・同じsid・連番欠落なしを確認し、差分と直後の98表示/更新をユーザーへ提示（S:38,41,53、D:163–168）。

## main 3：承認してから1本だけ

含むもの：MACRO0（1002D4h←1）→REPEAT 10×MACRO2（1002D0h←1）とTIME→SUB8→100210h=80000000h→COMPUTE_MEM(NOOP_NV4A)→PRAMIN→容量写し→E81E→CALL D336。省略してA4へ進めない（D:118,132、T:498–549,584）。

| 書込み先（幅） | 値／決め方（同じ先は列記順、条件skipあり） | verify mask | 根拠（規則；target） |
|---|---|---|---|
| h0 CR3C（8bit） | index選択のみ（data書込みなし） | 0h | T:498,507,535,540,545；T:618 |
| PFB 1002C0h（32bit） | 選択[141h,141h,141h,131h,141h,131h,141h,131h]；R(100h,100h)；R(100h,0h) | 0h | T:499–506,547,549；T:719 |
| PFB 1002C8h（32bit） | 選択[141h,141h,141h,131h,141h,131h,141h,131h]；R(100h,100h)；R(100h,0h) | 0h | T:508–515,546,548；T:720 |
| PFB 1002D4h、PFB 1002D0h（32bit） | 1h | 0h | T:516–517；T:673,721 |
| PFB 100210h（32bit） | 80000000h | 0h | T:518；T:610 |
| PRAMIN 70FFF0h（32bit） | 0h。未確認のVRAMに対応するPRAMINを書き換える。98への直接描画ではないが表示異常・停止なら中止、内容保全を保証せず電源再投入（D:132,170–171、T:722） | 0h | T:519；T:722 |
| PFB 100200h、MMIO 001310h（32bit） | R(0h,0h) | 0h | T:520,530；T:667,730 |
| h0 SR01（8bit） | index選択＋data：R(20h,0h) | 0h | T:521–522；T:604 |
| MMIO 011014h（32bit） | R(8000h,8000h) | 0h | T:523；T:723 |
| MMIO 001304h（32bit） | 800h | 0h | T:524；T:724 |
| MMIO 001400h（32bit） | A9A81DA2h | 0h | T:525；T:725 |
| MMIO 001404h（32bit） | 7FB71E23h | 0h | T:526；T:726 |
| MMIO 001408h（32bit） | C9C825D7h | 0h | T:527；T:727 |
| MMIO 00140Ch（32bit） | 7F27C216h | 0h | T:528；T:728 |
| MMIO 001098h（32bit） | R(8h,0h) | 0h | T:529；T:729 |
| h0 CR44（8bit） | index選択＋data：R(FFh,0h) | 0h | T:533–534；T:597 |
| PFB 1002CCh（32bit） | R(1h,1h)；R(1h,0h)；R(40h,40h)；R(40h,0h) | 0h | T:536,538,541,543；T:671 |
| PFB 1002C4h（32bit） | R(1h,1h)；R(1h,0h)；R(40h,40h)；R(40h,0h) | 0h | T:537,539,542,544；T:670 |
| 容量写し 001700h（32bit） | COPY（実機で決まる） | FFFFFFFFh | T:584；T:732 |

ノート：承認後 `gfinit run-one <sid> 3 --allow-write` を下記monitorで1回。RECのmain=3・gf_pre/gf_bus BEGIN/END・rc・PAUSED・同じsid・連番欠落なしを確認し、差分と直後の98表示/更新をユーザーへ提示（S:38,41,53、D:163–168）。

## 実行期限・停止・ノートの共通手順

| 項目 | 承認時・実行後に確認する内容 |
|---|---|
| main 0/1/2/3 共通の時間見込み | 実測秒数は未確認。**trial全体（A2＋各main＋続けるA4）の送信除外実行時間の合計60秒**と、各RUN_ONEの60秒の2枠がある。spentは各callをまたいで積算し、PAUSED待ちは除外。各mainの待ち累計5秒・動的4096上限は別。A2と各main直後にノートで開始→PAUSEDの壁時計経過・累計を記録し、UART込みの観測値をゲストspentと同一視しない（kdebug/recover.c:740–751,1084–1090,1169、kdebug/recover_profile.c:85–87、kdebug/recover.h:90、kdebug/gfinit_exec.c:5–24、kdebug/gfinit.c:109–114）。 |
| UART込みの期限 | 9600bpsではログが支配しうる。B=(128+12×plan.node_count+4×plan.arg_count)×(RECOVER_ROW_MAX+1)、Tlog=2×ceil(B×frame_bits/actual_baud)+3秒。**hostの `--timeout` は ceil(60+Tlog) 秒を明示**し、各呼出しに同じ保守的上限を使う。既定60秒は使わない。B/Tlogの当日plan根拠をノートへ渡せない場合は推測で開始せず、算定値の記録を用意する。host壁時計枠を延ばしてもゲストtrialの残り実行時間は増えない（kdebug/gfinit.c:261–286、tools/rshell_recover.py:95–108,197）。 |
| 1命令・内部操作ごとの確認 | safe pointのabort/期限、expected node・phase/main・head、許可先/幅/値式/変更mask、BEGIN送信、実アクセス、非零verify maskの比較、END、正しい次edge。未知命令・未知書込み・予定外の順序・readback不一致・bus/log失敗は後続停止（kdebug/gfinit_exec.c:80–126、kdebug/recover.c:1850–1867,1904–1934）。 |
| RUN_ONE自身が失敗した終端 | 読戻し不一致・未知書込み等はfail→cleanupを自動実行。期待は `result=FAIL target=S0 unknown=1`、S0復帰成功時だけ `verified=1`、失敗なら0。trialには原因、lost_linesも保存。終端を受けたらabortを重ねず98を目視して電源再投入へ。終端不着は下記応答なし経路（kdebug/recover.c:369–376,1048–1063,1249–1251）。 |
| N1・全mainの保持 | cfg50/54・PBUS1850/1854は32bit全maskで0。各RUN境界にもzero guard、E81E前後は4先を照合。E81EのSR01自身はverify mask=0なので「解除readback合格」と報告しない。安全の根拠はcfg50h=0でGF出力を止め、4a中は0FAChが98表示のままであること（D:219、kdebug/recover_gf.c:15–19、kdebug/recover.c:1676–1692,1994–2011、T:521–522）。 |
| main 0の追加確認 | A2後に記録したPSTRAPSから組3を確認し、COPY直後CR3C=3xhも確認する。COPY照合は組3を保証せず、実行後の組違い発見では既にPFB書込み済み。組3の100220=03080E0Bh・100224=05010405h・100228=20220303h・10022C=41088102h・100234=00266622hは予定値であり、verify=0を照合済みと扱わない（D:146,189、T:91–143,581）。 |
| P2-3：組3を読み取りだけで記録する手段 | PSTRAPSの32bit読値x・導出組=`(x>>2)&7`・同sid/起動をRECに残す（docs/tasks/realhw/GEFORCE6200_VBIOS.md:248–249）。既存kpeekにはPSTRAPSの読取行があるがPAUSED門で拒否、A2 snapshotはBARのみ、prepare固定列にもPSTRAPS読取はないため、**現行の口ではA2後の必要記録を取得できない**（kdebug/kpeek_allow.c:16、kdebug/kdebug_main.c:130–142、kdebug/recover_gf.c:119–123,305–315）。必要なのはA2前処理完了後・PAUSED帰還前に、recover管理下でBAR0+101000hを32bit読取りし値と導出組をRECへ残す観測処理。実装・検証は別作業、今回は追加しない。過去の冷間値やA2前のpeekで代替せず、記録なしならmain 0を開始しない。 |
| main 3のDRAM詳細 | SUB8はCR44を0へ戻し、条件により1002CC/2C4のbit0をset→clear、bit6をset→clear、1002C8/2C0のbit8をset→clear。組・条件は実機CR3Cから決まる。DRAMの状態と内容が変わり、失敗時は98が維持されてもS1復元不可、電源再投入で戻す（D:170–171）。10020Chには書かず、COMPUTE_MEMは容量を生成しない（T:533–549,584、D:118,132,190）。 |
| 画面の判定 | 全main直後、98テキストが表示され更新されることをユーザーが目視。0は両head screen-off、2はCR、3はhead0だけE81Eでscreen-off解除とPRAMIN変更あり。GF内部の変化はありうるが98の消失・崩れ・更新停止は受け入れず止める（D:132,169,186,219）。 |
| PAUSEDで止める | 同じstate/log/sidで `gfinit abort <sid> --allow-write`。期待はPARTIAL target=S0 verified=1 unknown=1 trial=1 lost_lines=0、利用者中止reason=1。自動的に次mainへ進めない（tools/accept/gf_s8.md:32–35）。 |
| RUN中・応答なし | ①既存monitorは指定timeout到達時に当該sidのabort frameを自動送信しcleanup終端を待つ。Ctrl-Cはabortを送らずポートを閉じるので中止手段にしない。②monitor終了後、通信可能なら同じ `--session/--state/--image-crc/--log/--timeout` で**`--command`を省略**し `recover now <sid>`。③それも応答しないCPU/バス停止はユーザーが電源断・再投入。受信者を重複起動しない（tools/rshell_recover.py:90–108,197–210,229–237、D:167）。 |
| 復帰の判定 | S0=98 CUI・legacy解放・予約テキストセル復元検証と目視、S1=安全な保存項目だけ。PARTIALはGPU内部まで元に戻った意味ではない。FAIL/verified=0・REC欠落・timeoutのみを成功にしない（R:60–65,120,143、tools/accept/gf_s8.md:34）。 |
| abort後 | PLL/PFB/DRAM・VRAM内容・PMC.ENABLE等はcold-required。逆順snapshot再生なし。電源を入れ直し新baselineからinspect→A1→A2。新sidやCPUリセットだけではGPU残留状態を除去できない（D:168,170–171、R:77,120、tools/accept/gf_s8.md:41）。 |

ノート共通形：`rshell_recover.py --port /dev/ttyUSB0 --session <A2のsid> --state a2.json --image-crc <当日のverのCRC> --log a2.raw --timeout <ceil(60+Tlog)を計算した秒数> --command 'gfinit run-one <A2のsid> <main:0-4> --allow-write'`（各山括弧は実値に置換）。同一起動のA2から同じstate/log/CRCを使用、受信者は1つ。A1/A2開始のみmonitor session=0と別state（S:38,51–53、tools/accept/gf_s8.md:11,17–28、tools/rshell_recover.py:197,225–226）。
PAUSEDは正常な途中帰還でfinal OKを要求しない。RECを保存し、最後のBEGINでpc/内部先を追えることを確認。abort時はhost rc=1でも上記PARTIAL終端・EOT・連番・S0を個別判定する（D:163,167、tools/accept/gf_s8.md:34）。
復旧で使用する名前：REC_GF_R_QUIESCE_PBUS1850/1854/PMC_IRQ。A2はREC_GF_R_PREPARE_PBUS1854/PBUS1850/PMC_IRQ/PMC_ENABLE/CR44_SELECT/CR44_DATA/CR1F_SELECT/CR1F_DATA/CR11_SELECT/CR11_UNLOCK（kdebug/recover_gf.h:27–30,31–33,48–57、kdebug/recover_gf.c:305–315）。main許可行には個別REC_GF_R_*名がなく数値id、表のT行で照合する。

## 未確認・ユーザー判断

立会い運用の未決は**1件：A4を同じ立会い・同じsessionで続けるか**。推奨：別途A4の承認資料を用意し、main 4後のPAUSEDを保って結果確認→ユーザーの個別承認後に続ける。今回はその承認を含まない。終了を選ぶならabort PARTIAL→電源再投入、次回はinspect→A1→A2→main 0〜4を再実行・再承認してからA4（D:168,187,217、kdebug/gfinit.c:373–381）。実機の分岐・PLL係数・所要時間・回数・98表示は観測項目であり推測で合格にしない。
判断は上記A4の立会い運用とU2の各回の実施承認。推奨：表・REC・98目視とA2後の組3記録が揃った段だけ次を承認。P2-3の観測手段・Tlog記録は開始前に解消する実装／証拠の不足であり、未確認のまま進む選択肢とはしない。verify=0を含む範囲も示し、追加readbackや再試行が必要ならabortして設計へ戻す。
本資料作成ではソース読取りと本ファイル作成のみ。ビルド・試験・NP21/W・実機操作は未実施。A4のVRAM書込みは別承認で、`gfinit_plan_verify`を呼ぶverifyは今回送らない（D:187、kdebug/gfinit_plan.c:233–255）。

反論：0件。

## 確定 (2026-10-26、Opus 5.5 第 2 回の条件付き承認、PM が反映 — 本文の該当の行より優先する)

- **N1 (組=3 の記録)**: 新しい観測の処理は作らない。**同じ起動で A1 の後・A2 の前**に、読み取りだけで `rshell_serial.py --port /dev/ttyUSB0 cmd 'peek -m -w4 0x24101000 4'` (PSTRAPS 101000h = BAR0 0x24000000 + 101000h、kpeek の許可表 kdebug/kpeek_allow.c:16) を 1 回読み、値 x と `(x>>2)&7` を記録する。**`(x>>2)&7 = 3` を main 0 の承認の前提**にする (3 でなければ A2 に進まずに止めて PM に戻す)。main 0 より前に 101000h へ書く段は無く (A2 が書くのは PBUS・PMC・CR44・CR1F・CR11 だけ、最初の書き込みは main 0 の rule 55 で mask 80000C00h — bits 2-5 は変えない)、COPY が読む値と同じものが得られる (根拠 GEFORCE6200_VBIOS.md:248、冷えた値 23409E8Fh で組 3)。kpeek は試験とは別の一時の窓で読み (kdebug/kpeek.c の kpeek_mmio)、A1 の後は Command=0x0102 (Mem=1) なので読める見込み — 読めなければ (DENY・WINDOW) A2 に進まずに止める。本文 195 行・24 行・210 行の「新しい観測処理を作るまで main 0 を承認しない」は取り消す。
- **N2 (--timeout)**: ノートの `--timeout` は**大きめの固定値 900** (小さくしない)。ノートの値は自動で abort を送るまでの上限でしかなく、時間の上限はゲストの中の期限 (main ごとの GF_INIT_EXEC_US と試行全体の 60 秒) が先に効く。本文 190 行・203 行の「Tlog を算定して記録できなければ開始しない」は取り消す。
- **N3 (A4)**: 立会いの前に A4 の資料が揃っていなければ、**main 4 の DONE の確認の後は abort (期待 PARTIAL) → 電源の入れ直しで終わる**。A4 は資料を別に用意して個別に承認したときだけ同じ試行で続ける (cold-required なので、続けなければ次回は電源 → A1 → A2 → main 0〜4 のやり直し)。
- 補足 (第 2 回): ノートのタイムアウトで自動 abort した場合の終端は reason=REC_ABORT で `result=PARTIAL` (FAIL ではない、recover.c:1219)。RUN_ONE が自分で止まったとき (読み戻しの不一致など) は `result=FAIL target=S0 unknown=1` (recover.c:369-372) で、abort を重ねずに電源の入れ直しへ進む。
- ユーザーに上げる未決は **A4 を同じ立会い・同じ試行で続けるか** の 1 件だけ (推奨: A4 の資料を別に用意し、個別に承認したときだけ続ける。今回は用意していないので main 4 の後に終わる)。
