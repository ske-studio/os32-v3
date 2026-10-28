# GF6200_VPLL_PROBE — VIDEO PLL の係数が効かない件の調べ — 設計

> 状態: **現行 (2026-10-28)** — 確定 (astra 起草・Opus 2 往復・r3 は PM が決めた 3 点の反映で往復を閉じた)。段の状態は票 TASK_HWDEV が持つ。
> 出所: ~/os32-tmp/run/gf-vpll/VPLL_PROBE.md (r3) を 2026-10-28 に写した。往復の版は同じディレクトリの VPLL_PROBE.r1.md・r2.md、レビューは opus_r1.md・opus_r2.md、回答は astra_r2_response.md・astra_r3_response.md。前の調べは ~/os32-tmp/run/gf-a4-followup/VPLL.md。
> 段の順 (PM 決定 2026-10-28): 段 0 (ホストの solver の道具を実装 → ノートで実 ROM に回す) → 結果を見て段 1 (記録だけの読み戻し、GF_VPLL_PROBE=1) → 実機の観測 → 段 2 (単段の比較、個別承認)。


## 1. 目的と範囲
4b の前に head A の要求係数・段選択・保持・C040復元を切り分ける。読戻し一致はlock／実pixel clockの証明ではない。
OS32 は単一利用者・単一タスク・協調切替、攻撃者なし。固まらない・壊さない・戻せる範囲だけを扱う。
O=`/home/hight/os32-v3`、F=`/home/hight/os32-tmp/run/gf-a4-followup`。以下の接頭辞と行番号は file:line。
N=`/home/hight/os32-tmp/run/gf-main0-fail/ref/nouveau/nvkm/subdev/devinit/nv04.c`（MIT:1–25）、I=そのsubdev配下の`bios/init.c`。
D=`/home/hight/os32-tmp/refs/geforce6200/nouveau/drivers/gpu/drm/nouveau/dispnv04`（crtc.c:1–24、hw.c:1–23 はMIT）、E=`/home/hight/os32-tmp/run/gf-main0-fail/ref/envytools/rnndb/display/nv3_pramdac.xml`。
本作業は本票とレビュー回答だけを更新。Oは読取りのみ、実機・NP21/W・ビルド・試験の実行なし。GPL実装の転用・ベンダバイナリ解析なし。

## 2. 既知の観測と仮説
旧578はold=00000E02h→要求80001003h→読戻し80000E02h。final後508=00051104h、580=01F0001Fh（F/REC4.md:11–12）。4aは後のREC修正で合格済み（O/docs/tasks/realhw/TASK_HWDEV.md:46）。
578はhead A第2段でbit31はenable、580[8]=0は二段要求（N:185–227）。最終508を有効係数と仮定すると27MHz基準で第2段16/3なら19.125MHz、14/2なら約25.102MHz、36MHzではない（F/VPLL.md:28–31）。
| 仮説 | 次の観測による区別（いずれも未確定） |
|---|---|
| H1 制御条件不足 | C040の一時値と580の直後値が要求どおりなら要求未反映の説明を絞れる。50C前後値も記録するが、未知ゲートは残る。 |
| H2 固有ラッチ／保存値・実行列の不一致 | 5本のold/new/readbackとcaptureを突合し、列・復元元の誤りを除外。固有ラッチは残る。 |
| H3 読出し像／後続上書き | 578直後から旧NMなら後続上書きだけでは説明不能。直後一致→final不一致なら後続書込み・自律変化を分ける。 |
| H4 lock待ち不足 | 直後・finalとも旧NMなら単なる短い読戻し遅延説は弱まる。lock未達そのものは判定不能。 |
| H5 表示前は未設定でよい | 4aの扱いであり物理原因ではない。VRAM成功も4bでの自動解消もVPLL成立の根拠にしない（O/docs/decisions/realhw/GF6200_4A_DESIGN.md:259–266）。 |
| H6 低クロックでの段選択不適合 | D/crtc.c:133–147 はchipset>40hかつdot_clock≤vco1.max/2でvco2を消し単段計算。36MHzが条件内か段0で判定し、単段比較で保持が変わるか調べる。NM2更新抑制の説明は推測。 |
modesetのH6をdevinitの必須条件とはしない。INIT_PLL2(4Bh)はI:985–998→I:348→N:355–388でlimitsをそのまま計算へ渡し、低周波単段化を挟まないためvco2有効なら二段を解く。
CurieのmodesetはUSE_VPLL2_TRUEも立てる（D/crtc.c:155–162）。単段選択とは別条件で、H6比較では50C[2,9]を同時変更しない。
現行順C040→580→578→508→C040はN:257–272に沿う（O/kdebug/gfinit_pll.c:205–210）。逆順・任意delay・全面verify復活の根拠はない。

## 3. 観測の設計
実機より先に§7段0で要求値を確定し、保存済みmain 2生RECのpc=E623h/substep=12040h～12044hと突合する（O/tools/accept/gf_main.md:156–157）。生RECは今回未入手。
現行verify=0のEND.readback=FFFFFFFFhは未採取であり実測ではない（O/kdebug/recover.c:2015–2032）。診断版の識別を台本・証跡に残す。
| DWORD（BAR0相対） | 非破壊読取りの根拠と許可 |
|---|---|
| 680508h | N:205,207の保存読取り。係数定義E:10–14,41。HWDEV read_safe=1（O/kdebug/generated/recover_gf_tables.inc:739）、kpeek可。 |
| 680578h | N:206,208の保存読取り。E:26–29,88はNV31:NV34限定なのでNV44Aへの根拠はN。read_safe=1（同:740）、kpeek可。 |
| 680580h | N:220–224のNV41以降の保存読取り。read_safe=1（同:615）、kpeek可。 |
| 00C040h | N:257–259,272の保存→変更→復元。read_safe=1（同:612）、kpeek不可なので下記1行を追加。 |
| 68050Ch | D/hw.c:392–404の通常状態保存で読む（MIT）。E:59–80も源選択制御。既存kpeek PRAMDAC帯で可、HWDEV行は不要。 |
これらは通常制御の保存読取りを非破壊性の根拠とする。NV44Aマニュアルによる保証ではなく、追加MMIO readによるposted write排出・時間差は残る。
C040はO/kdebug/kpeek_allow.c:12–20に `NV_ROW(0x00c040U,4U,KPEEK_W4)` 相当を定数化して1行追加。PMC帯全体を広げない。
| 時点 | 最小の採取と意味 |
|---|---|
| main前 | A1後・A2前の許可された時点でkpeekにより50Cを1回（既存台本O/tools/accept/gf_main.md:22–30）。main 2列直前の値とは呼ばない。 |
| 列前／各書込み前 | 既存captureとgf_bus.oldで508/578/580/C040を回収。C040復元直前oldも保持し、保存Gと区別する。 |
| 5書込みの各直後 | 対象番地だけを各1回読み、既存gf_bus END.readbackへ記録。順にC040一時値、580、578、508、C040復元値。 |
| C040復元後 | 最後のENDはC040のみ実測。508/578/580は各直後の観測値を参照するが、復元後の再実測と扱わない。 |
| main 2終了 | 段0で後続書込みなしを確認できれば専用採取を省く。「最後の直接観測」と「終了までソフト書込みなし」を分けて記す。 |
| final後 | final/EOT後・電源再投入前にkpeekで508/578/580/C040/50Cを各1回。50Cは前と合わせて2値だけ。 |
有効化はビルド定義 `GF_VPLL_PROBE=1` のみ（既定0、追加経路は条件コンパイルで除外）；有効版のGF profileに専用version=56504C03hを割り当て整合検査も対応、台本・証跡にフラグ・version・成果物CRCを残し、hostで無指定／0の経路・traceを現行と比較する。
記録専用readは診断有効かつmain=2/pc=E623h/substep=12040h～12044hの5規則に限定し、verify_mask=0を維持。不一致は止めず、readのアクセス失敗・送信失敗は既存の失敗／S0復帰へ。
O/kdebug/recover.c:2015–2032の読取りと不一致判定を分離し、read_safe確認後にlogged_read（同:943–956）を使う。RUNNINGの5本でaccess BEGIN/ENDが10行、call_pointが5回増える（ops->read直呼びはしない）。
前例は同じVIDEO PLL 5規則の旧verify非零版（O/docs/decisions/realhw/GF6200_4A_DESIGN.md:261–262）；同じverify読戻し経路（O/kdebug/recover.c:2016–2020,943–956）が各到達規則で同じaccess行を出していた。
SAMPLE・固定RAM・新しいREC形式は不要。既存gf_bus ENDへ全32bitを記録し、増分は上記access 10行。
hostは既存metadataのversion（O/kdebug/recover.c:448–449、O/tools/rshell_recover.py:41,122–123）=56504C03hで診断版を識別し、対象5規則だけverify=0でも実測として扱い、それ以外の版でこの5規則のreadback≠NONE（FFFFFFFFh）が出たら誤りとする（他のverify非零規則は従来どおり）。
「後続書込みなし」は他レジスタの作用・ハード自律変化の否定ではない。直後とfinalに差が出た場合だけ、該当区間の末尾読取りを次票で追加する。
PAUSED中kpeek禁止を維持（O/kdebug/kdebug_main.c:130–143）。BAR0/Mem有効を確認し、例BAR0=24000000hなら508は24680508h、C040は2400C040h（O/kdebug/kpeek_allow.c:33–46）。
1584hは対象外（N:112–139）。各RECとkpeek記録に版・sid・時点・採取順を付け、観測していない同時値を補間しない。

## 4. 一条件の比較試験の候補
まず現行書込み列・値の観測版をbaselineにする。cold起動→inspect→A1→組確認→A2→main 0/1/2を既存の段別承認で行う設計（O/tools/accept/gf_main.md:22–30）。
元版との差は5回のreadだけ。結果が変われば排出／時間差の影響を残し、原因確定としない。
比較の第一候補はH6の「同じ36MHzを単段で解く」。段0の実ROM limitsで36MHz≤vco1.max/2かつ単段解が有効な場合に限る。50C bit9比較は後順位で今回の書込み候補から外す。
一条件は段選択方式。整合に必要な580・578・508は一組で変えるため、単独bitの寄与を分離する実験ではない。順・C040・50C・待ち・ROM・組・観測方式は同じにする。
列前保存をA=508、B=578、T=580、G=C040とする。二段baselineは§7のsolverから算出したN1/M1/N2/M2/Pを使う。
単段はlimitsのコピーのvco2を0にして再計算しN2=M2=1とする（O/kdebug/gfinit_pll.c:138–154）。N1/M1/Pも再計算し、旧二段値を流用しない。
単段の5要求は順に `G & FFFCFFFFh`、`T | 00000100h`、`(B & 7FFF0000h) | 8000011Fh`、`(A & FFF80000h) | (P<<16) | (N1<<8) | M1`、`G`（同:203–210、N:210–227）。
N2=M2=1は論理解であり、578の下位実書込み011Fhは単段用符号化。実レジスタのN/M=1/31と論理解を混同しない。
段0の数値とその回の保存値から5本の完全なhex要求を記した比較票を作り、ユーザーが個別承認してから実施。本票作成は実機書込み承認ではない。
影響は表示PLLの段・周波数経路の変更、GPU停止の可能性。表示・legacy decodeを追加有効化しない。単段でNM読戻しが変わっても実周波数の正常は未証明。
停止条件は98コンソール停止、アクセス失敗、期限、REC欠落、予定外制御変化、C040復元不一致。NM不一致自体は記録して通常終端へ。
通常・異常とも既存S0復帰。PLLはcold-requiredで逆順再生しない。C040は列末にGへ戻し、不能時は追加GPU書込みで救済しない。
比較各回・再試行は電源断／再投入と新baselineで戻す（O/tools/accept/gf_a4.md:92）。ソフト復帰だけでPLL復元済みとはしない。

## 5. 受入条件
段0の入力同定・行選択・limits・解・要求値が揃わなければ実機比較へ進まない。旧finalの508をsolver要求値の代用にしない。
5本のold/new/readbackを対応付け、G・G&FFFCFFFFh・復元Gを照合。係数不一致でも後続ENDとS0まで記録できること。
二段578下位16bitは「その回の要求（旧試行1003h）／旧値（旧試行0E02h）／その他」、単段は011Fhとの一致を分類する。
508 bits18:0を要求と比較し、580[8]・578[31]を別記。C040直後値と50C前後の全値・[9,2,0]を併記し、同時採取とは記さない。
直後とfinalが一致ならその2時点の保持、差があれば区間未解決。後続計画・実RECに書込みがない場合も自律変化／読出し像は残す。
観測の成立はsid/seq、BEGIN/END、diag/final/EOTが完全、trial=0/lost_lines=0/txd=0、S0 verified=1。cold-requiredの正常PARTIALをFAILとしない（O/docs/tasks/realhw/TASK_HWDEV.md:46）。
源選択・保持が説明できなければ4b保留。説明できても設定観測までで、pixel clock・同期周期・lockは別の4b試験で確認する（F/VPLL.md:68–69）。

## 6. 既存の利用者の退行
診断無効ではmain 0～4の書込み・capture・verify・RECとread回数を現状維持。有効時も対象5本以外の読取りを変えない。
CR3CのFFh verify、main_id=NONEの非零31本、A4のVRAM／容量試験、main 4 DONE、S0のroute/decode/screen・cfg復帰を保持（O/docs/decisions/realhw/GF6200_4A_DESIGN.md:263–266）。
診断有効のRUNはlogged_read 5本分のaccess BEGIN/END=10行・call_point=5回を加算。O/kdebug/gfinit.c:273–286のrowsに10を足し、ΔB=10×(RECOVER_ROW_MAX+1)=3850Bを保守上限としてbytes・log_limit・transport.max_pollsを再算出；ENDも最大桁で384B枠を検証。
RUN時間はBにΔB、実行msに有限5read分を足し、GF6200_RECLOG_DESIGN（O/docs/decisions/realhw/GF6200_RECLOG_DESIGN.md:114–122）のT(B)で再算出（9600bps・1042µs/Bなら追加送信≤4.012秒）；poll枠も追加byte/wait・5call_point・busy余裕を計上し、固定18秒据置きを避け、cleanupのP/S・diag/final予約は維持する。
hostでは診断無指定／0で現行経路・trace同一、有効時5read・10行・5call_point増、専用version識別／他版の対象readback拒否、旧NM固定でも継続、read失敗／送信失敗時S0、CR3C不一致停止、予算境界・END解析を確認する計画。今回は未実行。

## 7. 実装の要否と段
段0（軽～中、実機前必須）: 保存ROMのデータ表を使うhost診断を設計。fixture.h:1–4,34–45は転記未検証・PLL entry_size不明、O/tools/tests/gfinit_host.c:29–38のlimitsは合成なので実ROMの代用不可。
入力は保存済みRa266 ROMのPLL表とE623hの転記・取得元識別／CRC。既存O/tools/tests/gfinit_romfile_host.c:30–46を拡張する案とし、データ表だけを読む。未入手なら実ROM数値は未確定のまま止める。
手順1: 5BC8hのversion/header/entry length/countと全行keyを出す。680508h一致行、なければ先頭行を選び、行番号・offset・key・fallback理由を出す（O/kdebug/gfinit_pll.c:78–97）。
手順2: 選択行のvco1/vco2各min/max周波数・入力周波数（kHz）・N/M範囲、ref、max_p、bias_pを出す（同:98–122）。E623hの要求00008CA0h=36000kHzも出す（O/tools/tests/gfinit_fixture.h:184）。
手順3: 現行gfinit_pll_solveで解く。二段はmax2からPを選びM1→N1→M2の昇順、丸めN2、入力/VCO/倍率4～10条件を検査し最小誤差・同率先着（同:125–190）。N1/M1/N2/M2/P・actual・error・各中間周波数を出す。
手順4: gfinit_pll_sequenceで5本のsource/keep/valueを出し、保存A/B/T/Gを合成して要求508/578/580/C040を出す。508=(A&FFF80000h)|(P<<16)|(N1<<8)|M1、578=(B&7FFF0000h)|80000000h|(N2<<8)|M2、580=T&FFFFFEFFh（二段、同:203–210）。
保存値未入手なら完全値を捏造せず上式と可変部を出す。次にvco2=0のコピーで同じsolverを実行し、§4単段要求を出して並べる。実ROM limits・解の手計算結果は今回は添えない。
手順5: 同ROMのmain 2列後～4の展開済み計画（CALL/REPEAT・全選択肢を含む）とcleanupの書込み先を列挙し、508/578/580/C040/50Cの後続書込みの有無を示す。生RECでも照合し、ないことを今回の実行済み成果とはしない。
段1（中）: O/kdebug/recover.cと必要な内部ヘッダへ対象5規則の記録専用read、kpeek_allow.c/hへC040行、tools/rshell_recover.pyへ診断版の解釈。HWDEV target追加・SAMPLE不要、gfinit_pll.cのbaseline列は不変。
予定試験: tools/tests/gfinit_pll_host.c・gfinit_exec_host.c（両解と列／保存値）、recover_gf_host.c（限定read・継続・失敗復帰）、kpeek_host.c、test_rshell_recover.py。gf_main.md/gf_a4.mdに採取・判定を追記。
段2（保留、中）: 実ROM数値と観測が揃った後に単段用limitsコピーと比較値をレビュー、個別承認後に実施。今は文書のみで、ビルド・host試験も後続実装者の段とする。
## 8. 未確認
実ROM PLL limitsとsolver要求値、生RECのcapture／要求508、50C前後全値は今回未入手。合成fixtureの成功から実ROMの成立を結論しない。
H6はmodesetの選択方針を根拠とする候補であり、devinit二段がNV44Aで禁止との証拠ではない。USE_VPLL2・源選択の因果も未確認。
read追加の時間的影響、実クロックと読出し像の一致、係数更新ゲート、lock、末尾までの自律変化は未確認。
参照コピーの版の独立確認、実装後のREC最大長・時間予算は残る。本票は原因確定でも4b実施承認でもない。

## 段 0 の実装で PM が決めたこと (2026-10-28)
- 選ばれた行の番号は、道具が `#include` した gfinit_pll.c の定数 (GF_PLL_ENT_REG・pll_le・表の header/entry/count) で各行の key を照合して出す (一致の数・行番号・offset、一致 0 なら先頭の行への fallback)。製品の gfinit_pll_parse は変えない。照合で選んだ行の欄から組んだ limits が gfinit_pll_parse の結果と全欄一致することを道具の中で確かめ、食い違えば rc≠0 (選び方の二重実装のずれを検出する)。

## 段 0 の前に分かったこと (2026-10-28、ノートの生 REC の抜き出し)
- 証拠: ノートの `~/os32-tmp/evidence/2026-10-28/vpll-rec/EXTRACT.md` (b1ff7f28 と 0ebe3a30 の回、5 番地の行は両回で同じ)。
- main 2 の 5 本 (pc=E623h、substep 12040h〜12044h): C040h 3C0BC003h→3C08C003h、680580h 01F0001Fh→01F0001Fh (同値)、680578h 00000E02h→80001003h、**680508h 00051104h→00040802h** (P=4・N1=8・M1=2、二段 16/3 と合わせて 36MHz)、C040h を 3C0BC003h へ戻す。END の readback はどれも未採取 (verify 0)。書込みの前に 4 番地の access (読取り) が 4 行。
- main 3・main 4・cleanup (RESTORING 以降) には 5 番地への書込みも restore も無い。68050Ch は両回とも 1 行も無い。main 0 にも C040h・680580h の書込み (680580h は 01F0001Fh→01C0001Ch→01F0001Fh)。
- final 後の kpeek は **680508h=00051104h (書く前の値)**・680578h=80000E02h・680580h=01F0001Fh。→ 第 2 段だけでなく **第 1 段の係数も反映されていない** (578h の bit31 だけが残る)。H6 (第 2 段の NM だけが効かない) では 508h を説明できない。残るのは「係数レジスタへの書込みが抑えられる (H1/H2)」と「書いた後に初期値へ戻る (H3、ただし 5 番地への後続書込みは無い)」— 区別は段 1 の直後の読み戻しでつく。§2 の VPLL.md 由来の「final の 508 を有効係数と仮定した 19.125MHz/25.1MHz」の計算は、508 が書く前の値なので意味を失う。

## 段 0 の結果 (2026-10-28、ノートで実 ROM、03aa4033)
- 証拠: ノートの `~/os32-tmp/evidence/2026-10-28/vpll-rom/DECODE.txt` (ROM 63488 B、sha256 db7457d8…、rom_crc=3919973859、decode rc=0)。
- PLL 表 5BC8h: 版 21h・header 5・entry 35・4 行、key は 0・4020h・4008h・402Ch。VIDEO は一致 0 で先頭の行 (key=0) へ fallback。limits: vco1 100〜405MHz・入力 3〜25MHz・N/M 1〜255、vco2 400〜1000MHz・入力 35〜100MHz・N/M 1〜31、ref 27MHz・max_p 7・bias_p 0。
- 要求は計画の pc=E623h から 36000kHz。二段の解 N1/M1=8/2・N2/M2=16/3・P=4 (vco1 108MHz、vco2 の入力 36MHz・出力 576MHz、誤差 0) は実機の REC の new (680508h=00040802h、680578h=80001003h) と一致 — **係数の計算と書いた値は正しい**。単段の解は N1/M1=16/3・P=2 (vco1 144MHz、誤差 0、580h に 100h、578h=8000011Fh、508h=00021003h)。後続の書込み (main 2 の残り〜main 4) は 0。
- 帰結 (PM): 原因は値ではなく、**書いた係数が 508h・578h の両方で残らない**こと (bit31 だけ残る)。H6 (単段) は 36MHz≤vco1.max/2 で nouveau の modeset なら単段を選ぶ条件には当たるが、第 1 段の 508h が残らないことを説明しないので順位を下げる。次は段 1 (直後の読み戻し) で「書いた瞬間から残らない (書込みの抑え)」と「後で戻る」を分ける。§4 の単段の比較は段 1 の結果を見てから要否を決め直す。

## 段 1 の設計の欠けの埋め (2026-10-28、astra の GAP1・PM の決定)
- 出所: ~/os32-tmp/run/gf-vpll/GAP1.md (astra)。実装者の停止の報告は ~/os32-tmp/run/vpll1/impl.log。
- **PM 決定 (§6 の「実行 ms に有限 5 read 分を足す」を差し替える)**: 5 回の読取りに新しい時間の上限は作らない。既存の verify の読取り (kdebug/recover.c:2016-2020・943-956) と同じく、送信の時間は tx_ticks、読取りの時間は試行の spent に数え (同:312-338・808-818)、期限の検査 (同:1168-1183) で超えたら REC_TIMEOUT。trial_limit (60 秒、recover_profile.c:86-88) は変えない。
- 予算: 増える 10 行・最大 3850B は RUNNING だけ。B'=B+3850 で log_limit と transport.max_polls を算出し直す (gfinit.c:273-288)。cleanup の restore_limit・P/S の予約・diag/final は変えない。試験の値: Δrows=10、ΔB=3850、read +5、call_point +5、restore_limit/P/S 不変。
- 入口: `make GF_VPLL_PROBE=1 gf-vpll-probe` → `$(BUILD_OUT)/gf-vpll-probe/{kdebug.bin,hwdev.mod}`。診断の object・ELF・map・pair.h はその下に分け、-DGF_VPLL_PROBE=1 は診断の対象のレシピだけ。既定の all と成果物は変えない。診断の core は既定の vmkernel.lz4 に対して pack し、gen_hwdev_pair.py で組を作る (カーネルは組み直さない)。世代は据置き、GF profile の version=56504C03h と profile CRC・image/pair の CRC は既存の生成器で出す (build/kdebug.mk:34-58、build/hwdev.mk:11-38、tools/gen_hwdev_pair.py:11-26)。
- 配るもの: 診断の kdebug.bin・hwdev.mod (同じ vmkernel.lz4 の組)、対応する tools/rshell_recover.py。組の一覧 (フラグ・版・各 CRC・元の commit) を証拠に残す。

## 段 1 の実機の結果 (2026-10-28、Ra266)
- 版: カーネル・ユーザーランドは CI main 03aa4033、kdebug.bin・hwdev.mod だけ wt/vpll1 70ca0630 の診断版 (pair=896f9a6f、GF profile version 56504C03h)。WSL から scp で渡し、名札の 2 行を書き換えて hsync。証拠はノートの `~/os32-tmp/evidence/2026-10-28/vpll-probe/` (b_summary.txt・a2.raw・main2_vpll.txt・kpeek_*.txt)。
- 電源の入れ直し → inspect match=1 → A1 OK → PSTRAPS 0x234D9E8F (組=3、ノートの報告の 16 進の誤りを訂正) → A2 PAUSED → main 0/1/2 PAUSED (216/251/200 秒、REC の連番の抜け 0、各本の後 ver 応答) → abort は PARTIAL target=S0 verified=1 trial=1 lost_lines=0 txd=0 (設計どおり)。
- 5 本 (old → new → 直後の readback): C040h 3C0BC003→3C08C003→**3C08C003**、680580h 01F0001F→01F0001F→01F0001F、680578h 00000E02→80001003→**80000E02**、680508h 00051104→00040802→**00051104**、C040h 3C08C003→3C0BC003→3C0BC003。68050Ch は A1 の後と abort の後とも 00000004。abort の後の kpeek も 508h=00051104・578h=80000E02・580h=01F0001F・C040h=3C0BC003。
- 帰結 (PM): **係数は書いた直後から残らない** — H3 (後で戻る)・H4 (読み戻しの遅れ) は外れ、H1/H2 (書込みの抑え) に絞れた。同じ列の C040h は書いたとおりに残るので、MMIO の書込み自体は届いている。抑えているのは 508h の全体と 578h の bit31 以外。

## 段 1 の取り込みで送る項目 (2026-10-28)
- astra のレビューは取り込み可 (指摘なし)。残り: 診断の読み戻しの read の失敗・送信の失敗から S0 へ行く専用の失敗注入の試験、restore_limit/P/S が不変であることの専用の試験は無い (コードの経路は既存の失敗の出口に繋がることをレビューで確認)。診断版をもう一度使うときに足す。

## 段 1 の REC の照合 (2026-10-28、読むだけ、ノートの EXTRACT2.md)
- 読み解きの候補 (astra の ~/os32-tmp/run/gf-vpll/ANA1.md): ① 68050Ch の源の選択 (4h = bit0 XTAL・bit2 USE_VPLL2・bit9 VPLL_PROG=0) ② CR1F/CR44・head の取り違え ③ NV44 固有のラッチ ④ C040h[17:16] の切り離し ⑤ PMC で表示ブロックが無効。
- 照合: 5 本 (seq 7060〜7163) は CR44=0 の間 (main 0 seq 375 で 0 に戻してから main 2 seq 8659 まで書込み無し)、head B への書込み 38 件はすべて main 2 の CR44=3 の期間 (seq 8683〜9523) → **② は外れ**。PMC 200h は main 0 で 01100000h→05110111h→07110111h (bit24・25 PCRTC は 1、次の old で残っていることを確認) → ⑤ の単純な無効は弱い。68050Ch は回の全体で書込みも読取りも無い。
- CORE/MEM の PLL: 制御 4000h の bit28・4020h の bit23 は書いても残らない (前からの既知と同じ)、係数 4004h・4024h は 1 回だけ書いたので REC では残ったか分からない → kpeek で読む (4000h〜4007h・4020h〜4027h の許可を診断版に足す)。
- kpeek2 (2026-10-28、wt/vpll2 0d3ab280 の診断版、hsync boot → OS32 の reboot の後、電源の入れ直し無し、ノートの kpeek2.txt): 4000h=0001001Fh・4004h=01010C01h・4020h=2000011Ch・4024h=05010E01h・680508h=00051104h・680578h=00000E02h・680580h=01F0001Fh・C040h=00000000h・68050Ch=00000004h・200h=03010000h — **どれも試行の前の値** (PMC も A2 の前の値)。OS32 の reboot で GeForce がリセットされる (PCI のリセットと推測)。CORE/MEM の係数が残るかは、試行の後に reboot せずに kpeek で読む必要がある。

## 2 回目の実機の回 (2026-10-28、wt/vpll2 0d3ab280 の診断版、電源の入れ直し無し — OS32 の reboot で GeForce は試行の前の値、ユーザー承認)
- 証拠: ノートの `~/os32-tmp/evidence/2026-10-28/vpll-probe2/` (b_summary.txt・kpeek_vs_written.txt・main2_vpll.txt)。inspect match=1 → A1 OK → 68050Ch=4 → 組=3 → A2 PAUSED → main 0/1/2 PAUSED (216/252/201 秒) → abort PARTIAL target=S0 verified=1 trial=1 lost_lines=0。5 本の old/new/readback は 1 回目と同じ。
- **abort の後、reboot せずに kpeek**: 4000h=C002001Ch・4004h=14032809h・4020h=E041001Ch・4024h=14032809h・200h=07110111h・C040h=3C0BC003h・680580h=01F0001Fh — **書いた値のまま残る**。680508h=00051104h (書いたのは 00040802h)・680578h=80000E02h (80001003h) — **VIDEO PLL の係数だけ残らない**。68050Ch=00000004h (書込み無し)。
- 帰結 (PM): CORE・MEM の PLL の係数は同じ書き方で残る → PLL 全体に共通する抑え (候補 ③ のうち PLL 共通の部分) は外れ、**VIDEO PLL (表示側) に固有の条件**に絞れた。残る候補は ① 68050Ch の源の選択 (bit9 VPLL_PROG=0) と、表示側に固有の未知のラッチ。次は ① の一条件の比較 (68050Ch bit9) の設計を確定する。
