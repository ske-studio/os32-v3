> 状態: **現行 (2026-10-21)** — 段 4a S2 (decode) の引数配置の確定設計。r2 + レビュー。
> 出所: `~/os32-tmp/run/gf6200_4a_s2/S2_LAYOUT.final.md` の確定版を 2026-10-21 に写した。往復の版 r1〜rN は ~/os32-tmp に残る。中身は変えず、リポジトリ内の文書への参照だけリンクに直した。

## r1→r2 の変更
F1：Kの引数を[I(length)]だけに固定し、値の配列はplan.rom_crcで担保する。
F2：RMW ruleをREC_EXPRへ移行し、既存制約を保持してREAD/IMM/IMMの共有式を登録する。
F3：0x1700のRW・read_safe targetを追加する。
F4：PLL入口Cのsubstepを0x11101に固定し、nや合流の有無によるlayoutキー衝突を防ぐ。
F5：命令内captureのSAVED番号をp->count+i（3件）に固定し、bind/実行の参照先を分岐する。
F6：範囲外Cと動的防御試験を省き、静的拒否だけにしてnode概算を更新する。

# 4a S2 引数配置 r1（2026-10-21）
前提は [DESIGN.final](GF6200_4A_DESIGN.md) §4–6/8/9、[ADDENDUM.final](REALHW_RECOVER_ADDENDUM.md)（末尾のS1/T1b決定を含む）。N1・上限・承認順は変更しない。実装前の設計表であり実機許可ではない。
参照：`/home/hight/os32-v3/{kdebug/recover.h,kdebug/recover_gf.c,tools/tests/gfinit_fixture.h}`、`gf6200_4a/nvbios/init_scripts.txt`、MIT nouveau `nvkm/subdev/bios/init.c` の各該当handler。x86コードは扱わない。

## 1. layout の記法と番号（この規則から静的 C 配列を生成する）
|記号|固定契約|
|---|---|
|I(x), R(t), S(q)|それぞれ IMM（復号済み値）、READ（target ID）、SAVED（同sessionのslot）。列の要素数がargc。targetも先頭I(t)で渡す|
|L(t,v), X(t,i), D(t,v)|`[I(t),I(v)]/WRITE`、`[I(t),I(i)]/SELECT`、`[I(t),I(v)]/DATA`。Xのiはtarget.index|
|Q(t)|`[I(t)]/READ`。条件用読取り・一時採取用。read_safeとindexed選択成立が必要|
|M(t,m,v;dir)|`[I(t),R(t),I(m),I(v)]/dir`、登録EXPR `(old & m) OR v`。幅mask適用。dir=WRITEまたはDATA。S2で6E/52/78/65のRMW ruleのvalue.modeをREC_EXPRへ変更（mask/bits/min/maxは保持）し、kinds=(READ,IMM,IMM)の式 `(old & m) OR v` を1件登録して共有する|
|C番号(引数列)|方向NONE、rule_id=expr_id=NONE。表内の引数は明記したkindだけ。無引数は `[]`|
|e|`gf_effects` 全体の0始まりordinal。W/SELECT/DATAのsubstep=eを維持（pc内連番ではない）。同一opcodeの各eを個別登録|
|補助substep|Q=0x10000+j、通常C=0x11000+j（jは命令内で0始まり、表の出現順）。繰返す通常C/Qはその都度jを増やす。通常Cは0x11000–0x110ff内に限定し、PLL入口Cはn・k・合流の有無によらず固定substep=0x11101（分岐先ごとに同じargc=3のlayoutを共有）。0x11100は予約のみで範囲外Cを生成しない。既存eと衝突しない|
|K（skip形）|substep=`0x20000+pc`、NONE、`[I(length)]`（argc=1）。値の配列を含むROM内容はplan.rom_crcで担保する。実pcごとにargc固定のlayoutを登録。rule/expr=NONE|
|通常辺F|next[0]=next[1]=列の次node。末尾は次命令、終了はNONE。異なるhead/skipだけ状態別nodeを作り、同一状態に合流|
|条件辺B|next[0]=偽（skip=1）、next[1]=真（skipを維持）。skip到達はKで読み0、skip維持のF|
|選択辺J|Cのnext[0]=次の比較、next[1]=対応W列。countが到達選択範囲を覆うことを静的検証し、覆わないROMは拒否。最終偽は到達不能（辺の格納先は次命令）、各W末尾は次命令へ合流。範囲外Cは生成しない|
EXPRのRは同W substep内でrecoverが採取・評価する（別Qの値をRで流用しない）。layout.dirは要求Wの方向、式評価の内部READを別のW権限と解釈しない。
全非実行形もROM境界・参照表・shiftを検査する。Wが存在するのにrule_id=NONEの実行nodeをbind可能にしない。

## 2. 27 opcode（番号は16進、長さはbyte）
表のWには実際のeを付す。Q/通常Cは上記j、PLL入口Cは予約substepで一意化。明記のない辺はF、skipはK、force無し。tはhead込みで解決したtarget ID。
|op / 名前|長さ・復号配置（+offset）|実行substep順・args/kind・方向|辺・展開／skip・force|
|---|---|---|---|
|32 IO_RESTRICT_PROG|11+4n：p16@1,i8@3,m8@4,s8@5,n8@6,d32@7,値32[]@11|X(t,i); Q(t); 各kにC`[I(m),I(s),I(k),I(n)]`; L(d,値[k])|J、比較はQの選択値を使用。全n選択肢、同じ値でも各W=eを保持|
|33 REPEAT|2：n8@1|入口C`[I(n),I(end_pc)]`; 各反復入口C`[I(k)]`; 出口C`[I(n)]`|n回静的展開、新ctx、各回END_REPEATへ。skipでも構造を展開、n=0拒否|
|36 END_REPEAT|1|C`[I(repeat_pc),I(k),I(n)]`|Fで次反復入口／最終回は33出口、親ctxへ。skipでも構造を実行|
|37 COPY|11：src32@1,s8@5,sm8@6,p16@7,i8@9,m8@10|X(dst,i); EXPR e=`[I(dst),R(dst),R(src),I(s),I(sm),I(m)]`/DATA|F。式は§3。W側でdst→src順に採取|
|38 NOT|1|C`[]`|F、skip反転（skip中も実行）|
|39 IO_FLAG_CONDITION|2：条件番号8@1→9B表|X(t,i); Q(t); C`[I(cond),I(m),I(s),I(data_pc),I(dm),I(v)]`|B、ROM[data+((Q&m)>>s)]&dm==v。全indexのROM範囲を検査|
|4A IO_RESTRICT_PLL2|11+4n：32と同配置（値はkHz）|X(t,i); Q(t); 各kにC`[I(m),I(s),I(k),I(n)]`; PLL入口C`[I(reg),I(kHz),I(limits_pc)]`|J。PLL内部101候補はS4、§4。実物の同一周波数は同一PLL入口へ合流可能|
|4B PLL2|9：reg32@1,kHz32@5|PLL入口C`[I(reg),I(kHz),I(limits_pc)]`|F→S4内部列。未実装を成功NONEにしない|
|51 CR_INDEX_ADDRESS_LATCHED|5+n：a0@1,a1@2,base@3,n@4,byte[]@5|最初X(t0,a0); Q(t0); 各kでD(t0,base+k), X(t1,a1), D(t1,data[k]); 次回前X(t0,a0); 最終X(t0,a0), `[I(t0),S(q)]`/DATA|最初のXは既存先頭eを兼用。n=7/3、最終e=410/454。採取と再生は§3。base加算はu8範囲確認|
|52 CR|4：i8@1,m8@2,v8@3|X(t,i); M(t,m,v;DATA)|F|
|53 ZM_CR|3：i8@1,v8@2|X(t,i); D(t,v)|F、CR44は下記head規則|
|54 ZM_CR_GROUP|2+2n：n8@1,{i8,v8}[]@2|各要素X(t,i); D(t,v)|各要素をe別nodeに展開、head変化は次要素にも伝播|
|58 ZM_REG_SEQUENCE|6+4n：base32@1,n8@5,v32[]@6|各kにL(base+4k,v[k])|実n=3/4/4、11 W。checked番地計算|
|5B CALL / SUB_DIRECT|3：絶対pc16@1|入口C`[I(dst_pc),I(pc+3)]`; 復帰C`[I(pc+3)]`|呼出箇所別展開、新ctx→DONE→親ctx復帰。skipはKのみ（非呼出し）|
|5F COPY_NV_REG|22：src32@1,s8@5,sm32@6,xor32@10,dst32@14,dm32@18|EXPR e=`[I(dst),R(src),R(dst),I(s),I(sm),I(xor),I(dm)]`/WRITE|F。src→dst順に採取。式は§3|
|62 ZM_INDEX_IO|5：p16@1,i8@3,v8@4|X(t,i); D(t,v)|F|
|63 COMPUTE_MEM|1|C`[]`（NOOP_NV4A）|force、skip保持、HWアクセス0、容量設定無し|
|65 RESET|13：reg32@1,d1@5,d2@9|M(184C,FFFFF0FF,0;WRITE); L(reg,d1); C`[I(10)]`; L(reg,d2); `[I(184C),S(q)]`/WRITE; M(1850,FFFFFFFE,0;WRITE)|e=14,15,16,17,18。Cは10µs待ち、最初のM内部READを採取。skipでも全列FORCE、終了後skip保持|
|6B SUB|2：番号8@1|入口C`[I(index),I(dst_pc),I(pc+2)]`; 復帰C`[I(pc+2)]`|5B同様。script表の範囲・入口を検査|
|6E NV_REG|13：reg32@1,m32@5,v32@9|M(t,m,v;WRITE)|F、keep=全1/value=0も省略しない|
|6F MACRO|2：index8@1→表8B|L(t,v)|1組だけ展開、表のindex×8を検査。反復時e再利用・ctx別|
|71 DONE|1|C`[]`|callなら復帰node、mainなら正常終端（入口順の次mainは別承認）。skipでも構造実行|
|72 RESUME|1|C`[]`|F、skip=0（skip中も実行）|
|74 TIME|3：µs16@1|C`[I(us)]`|F、skipはKで待ち0。待ち累計/動的予算へ加算|
|75 CONDITION|2：条件番号8@1→12B表|Q(t); C`[I(cond),I(mask),I(value)]`|B、(Q&mask)==value|
|78 INDEX_IO|6：p16@1,i8@3,m8@4,v8@5|X(t,i); M(t,m,v;DATA)|F、E81EのDATAだけ既定N1 guard/依存を保持|
|7A ZM_REG|9：reg32@1,v32@5|L(t, reg==200h ? v OR 1 : v)|F、200h補正はこのopcodeだけ（RESET値へ適用しない）|
CR44へのSELECT/DATAはhead0のtarget、DATA後は値3ならhead1、それ以外head0。skip中もnouveauのCR44処理によるhead変化を制御状態に反映するがアクセスしない。動的値からhead未解決なら拒否。
SUB後のhead/skipは共有、main入口はhead0/skip0。ctxはphase/main根・呼出箇所・反復番号で一意。nextはNONE又は大きいnode番号、両辺を検査する。
Qの読値は実行器の当該命令内の条件計算にだけ保持する。CのIMMへ読値や比較結果を詰めない。leaveは辺だけ検査し条件再評価を加えない。

## 3. S1からの式と一時値（5件）
|対象／W substep|参照式・採取位置|実装条件|
|---|---|---|
|DFAF COPY / e37|`((old_CR3C & 0x0f) OR ((src101000 << 2) & 0xf0)) & 0xff`、e36 SELECT後、e37内R(dst),R(src)|一般式は表のsを右shift（s<80h）／左(100h−s)。shift量<32。Cの符号付きshift禁止、u32ビット演算|
|E7FB COPY_NV_REG / e492|`(old1700 & dm) OR ((shift(src10020C,s) & sm) XOR xor)`、e492内で採取。実物dm=0,s=0,sm=FFFFFFFF,xor=0|10020Cを読取元としてread_safe分類追加、書込先扱い禁止。0x1700はR(dst)用にRW・read_safeのtargetを追加する。旧dstを省く最適化は採取順の試験後。E80B表記は採用しない|
|DF10 RESET / e17|identity(saved184C)、**e14内でclear前に読んだ32bit全値**を採取（0F00だけではない）|同命令・同ctx・head・sessionの採取成立が必須。e17はRUN+FORCE EXPR、COLDの終了復元には使わない|
|E67F CR57 head0 / e410|identity(savedCR57)、先頭e381 SELECT後Q0で採取、最初のe382 DATAより前|最終e409 SELECTを要求。8bit、RUN EXPR|
|E6C0 CR57 head1 / e454|identity(savedCR57)、先頭e441 SELECT後Q0で採取、最初のe442 DATAより前|最終e453 SELECTを要求。head0のslotと別、RUN EXPR|
現行recoverのSAVEDは開始snapshot/local_cleanup用であり、この3件をそのまま通せない。S2で「命令内snapshot」の最小接続を設計・host実装し、S5でbus接続する（新arg kindや任意式は作らない）。
具体形：planと一緒にCRC/hash対象の固定capture表 `{slot,producer_node,consumer_node,target,read_ordinal}` 3件を渡す。REC_ARG_SAVEDの番号は開始snapshotを0≤slot<p->count、命令内captureをslot=p->count+i（i=0:RESET184C、1:CR57 head0、2:CR57 head1の3件）に固定し、bindの範囲検査と実行時の参照先をこの境界で分岐して後者をcapture値/validへ接続する。profile版更新・型追加は共有側変更としてレビューする。
recover所有値/validは当該producerの成功したREADでのみ設定、全経路の先行をbind検査。consumerは指定session/phase/main/pc/ctx/head/targetに完全一致、使い終わり・abort・session終了で無効化。普通のrequires成立bitをQ成功と偽装しない。
RESETはe14内READ ordinal0、CR57はQ0 ordinal0を指定。SAVEDのidentity式にはslotのみ渡す。未接続時は3行をDENYのままとし、RESTORE/LOCALへ偽装しない。捕捉値はcleanupから参照不可。
COPY2件も登録EXPR＋読取元分類＋既存mask/range検査が揃うまでDENY。mainの未知exprをhost専用合成式で許可しない。

## 4. 送付項目・規模・計算量
|項目|S2での決定／出口|
|---|---|
|runtime index記録|ポート別にする。キーは検査済み実効index port（space/bar/offset/head込み）、値は選択targetとphase/main/ctx。同portの別SELECTで失効、別portは保持。bindとの同値性を優先し、SEQ→GFX→SEQ DATAを受理|
|COLD index保存|index portの保存可否をdataのCOLD分類と分離する。安全性の分類済みindexだけ局所採取、dataを保存・終端再生しない。S2は型/host検査、S5は実bus/cleanup接続|
|PLL101候補|内部eを予約してS4へ渡す。S2は引数/limits参照・選択辺・DENYを検証、係数/内部EXPR/layoutは創作しない。S4まで全ROM RUN bind成功を合格条件にしない|
|静的基数|実fixtureを集計：193命令、619 effects＝定数等257＋選択256＋PLL101＋COPY2＋一時再生3。CALL/SUB先は各1回、REPEATでMACROとENDが各9個増える（動的命令基数211）|
|node概算|619 W候補＋REPEAT追加9 W＋選択比較272（34×8）＋条件/選択Q/C約50＋構造/TIME約60＋skip複製等約60＝約1070。固定列・PLL内部READ/分岐の余裕を見て約1300–1600。2048内の見込みであり確定実測ではない|
|合流の条件|32は各選択Wを保持。4Aは実物の8周波数が全同値なので比較後に同一PLL処理へ合流、8倍複製しない。将来異なる周波数なら別展開し再計数。経路履歴ごとの全suffix複製は禁止|
|最終件数|S2 decodeでmain別/合計のnodes,args,ctx,deps,entries、最大経路steps/待ち/深さを報告。args8192等も独立検査。S4完成時にPLLを含め再計数、未実装列込みで収容を証明したとは言わない|
|超過時|既定通りbind拒否。上限を勝手に増やさず、集合をW nodeと合流点だけに持つ案を設計者へ戻す。これはflow_bits圧縮案でありnode上限の自動解除ではない。疎な集合の添字と実行nodeの添字を分離する変更も別レビュー|
|O(N²·R)|現行の線形layout/rule探索を残した場合N=1300,R≈544で約9.2億、N=2048で約22.8億の比較規模。時間秒へ推測換算しない。layout数も測定値に併記|
|時間の合格条件|fake access/ログ除外でbindとenter/access/leaveを別計測、N=実数/2048・rule/layout最大付近のhost計測値と探索回数を報告。実機tickは接続段で測る。秒数の新上限は設けないが未測定を合格にしない|
|計算量の対処|bind時にnode→layout/ruleを一度解決して固定長cache、indexed port IDも事前解決（profile/plan不変期間のみ有効）。依存検査内の線形探索を除きO(N²＋N·R)へ。前向き1回・合流交差・512KiB契約を維持|

## 5. S2の実装段と受入・変異
|段|合格条件|変異行（拒否又は期待trace）|
|---|---|---|
|decode|9本/193命令/27種、全長/参照/作用が独立fixtureと一致。REPEAT10、SUB6/7/8、CALL D336、main4 DONEを含む。出力に未解決の番地/headなし|未知op、切詰め、可変長溢れ、shift32以上、表index外、CALL命令途中/表内、再帰、REPEAT0/不整合/深さ超過を書込前拒否|
|表検証|eとrule対応一意、args/kinds/方向一致、全辺位相順、skip/force/head/ctx/依存の交差を確認。S1固定列と合成scriptをreal recoverへbind、PREPARE最終CR11成功|重複layout、argc/kind/target/方向差替、back edge、片側依存欠落、別call/前反復の成功流用、同port再SELECT後のDATAを拒否|
|式/採取|COPY2式の期待整数値、一時採取3件のproducer/consumer固定、途中abort後の再生0。未接続本番行はDENYとして受入|dm/sm/xor/shift差替、slot/head/ctx/session差替、採取なし経路、e14 clear後採取、CR57書換後採取を拒否|
|合成fixture|27種のdecode/全経路表、32/4Aのn変更（0・1・8・9）でlayoutキー/argc整合と静的拒否、32の8択、条件偽/真・NOT/RESUME、skipのread/write/TIME=0、RESET force列、COMPUTE_MEMアクセス0|32/4Aの到達選択範囲をcountが覆わないROMは静的拒否。範囲外C・異常選択値注入による無書込続行試験は設けず、静的拒否だけを検証|
|head/index|CR44 head0→head1/0、PRMVIO stride、SUBからの共有head/skip、別port保持と同port失効が静的/runtimeで一致|SEQ→GFX→SEQ DATAは成功、head取り違え・SELECT省略・合流片側だけSELECTは拒否。COLD dataのcleanup再生0|
|N1/終端|E81Eのみ既存許可、4 guard前後、PREPARE停止2件・SELECT依存、失敗後access0/cleanup1回。main順・PAUSED保持・終端破棄を回帰|cfg50/54/PBUS1850/1854を各々前/後で不一致にし停止。abort後cursor再利用・辺飛越し・未分類PLLを拒否|
|規模/予算|nodes等の実数・推計との差、各main最大動的経路、host時間/探索回数を提出。静的分岐node総数を実行stepsに足さない|2049 nodes、8193 args、深さ/4096命令/5秒超過、checked加算乗算溢れは開始前拒否|
変異の設計行はここで固定する。実行は既定のPMレビュー後、通常MUTATE=0。S3の完全executor/実機をS2で完了したと報告しない。

## 6. sol向け注意・未決
表の制御引数はROMに裏付いた値だけ。READの再採取と過去値SAVEDを混同しない。SELECTにもruleが必要、Q/Cはruleなし、必要な追加SELECTを勝手にfixture ordinalへ割り込ませない。
51の最初のSELECTを採取と最初のDATAで共用し既存e列を維持する。snapshot採取を目的に新しいWを暗黙追加しない。RMW式をliteralに縮約してread_safe/選択検査を消さない。
現行型への追加点は命令内capture/slotとport別選択記録・index保存分類（計算量cacheは内部実装）。併せて既存rule設定ではRMWのvalue.modeをREC_EXPRへ移行して共有式を登録し、0x1700のRW・read_safe targetを追加する。S2で仕様とhostを固め、共有側の変更はレビュー対象、未実装は拒否継続。
ユーザー判断が要る未決：0件（推奨：既定分担で進める）。S4の内部PLL列とS2の計数・計測は実装成果でありユーザー判断にしない。
確定設計との食い違い：0件。未提供のPLL101内部layoutはS4へ留保し、本表の入口を実行許可としない。

反論：なし（0件）。

## 確定 (2026-10-21、Opus 第 2 回の条件付き承認、PM が反映)
- G1 (P2): RMW を REC_EXPR に替えるのは **65 の RMW のうち e18 (zero_target 1850、出力停止の 0 書き) を除く**。e18 は REC_LITERAL (bits 0) のまま残す (出力停止は 0 を書くだけで、決まった値で縛るのがいちばん簡単で安全 — PM 決定)。S2 の作業に次を含める: tools/tests/recover_gf_host.c の trace_plan (現在 N1 の E81E の 78 を 2 引数の layout `two` で bind) を M の形 `[I(t),R(t),I(m),I(v)]` に書き直し、fake bus の SR01 の読み値と SELECT を先に成立させる。recover_gf_host.c:124 の 1850 の rule の `mode==REC_LITERAL` の確認はそのまま通る。
- G2 (P3): L23 の「ROM の内容は plan.rom_crc で担保」を次に読み替える — 「decode が ROM を gf_scripts の crc32 (S1 の fixture の期待値、ノートの実 ROM と 9 本一致済み) と照合してから plan.rom_crc を設定する。recover は rom_crc を plan の hash に入れるだけで ROM と照合しない。K はアクセスしないので安全上の穴ではない」。
- G3 (P3): 通常 C の substep 0x11000–0x110ff に収めるため、**j>0xff になる REPEAT (n≥255) は decode で拒否する** (実 ROM は n=10)。
