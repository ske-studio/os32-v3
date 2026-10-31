# kdebug/gfinit_decode.c — 設計
> 状態: **現行 (2026-10-30)** — 出所: GF_DRIVER_DESIGN (2026-10-29 確定、D0 で移した)
ソース: `kdebug/gfinit_decode.c`

**解読は現行の switch と `.inc` をそのまま共有する**。実 ROM で確認済みの解読を 27 本へ配り直さない（F3、`O/kdebug/gfinit_decode.c:18–55`）。
OS32 固有差分も保持：PMC_ENABLE bit0 を立てる、RESET 後 1850 は 0、CR3C の組を確認する。根拠は `O/kdebug/gfinit_op/pll2_zm_reg_decode.inc:3`、`reset_expand.inc:10`、`D/GF6200_4A_DESIGN.md:263`。nouveau 全機種対応への拡張はしない。
下層 A の契約は `int gf_op_<name>_run(struct gf_init_context *, const struct gf_instruction *)`。解読は現行 `decode_instruction` と `.inc` の一つの実体を共有し、命令別 decode API は増やさない。
switch と既存 include 構成を保って共有。recover 型からの切離しは薄い接続だけ。27 本へ再分配しない。

## GF6200_4A_DESIGN

| 順序 | 操作・検査 | 失敗時 |
| --- | --- | --- |
| A3 静的確認 | 全 main 5本と SUB/CALL先を検査。193命令・27種・9本、表位置と全副作用を照合。承認対象のmanifestを確定 | 未知命令・N1のE81E以外の表示有効化・W行不足なら実機RUNを拒否 |

## 4. 解釈器 — 実物27種の照合済み仕様

- `nvbios/init_scripts.txt` の静的総数は193命令・27種・9本（main0〜4、SUB6/7/8、CALL D336）。REPEATの動的反復は件数に含めない。5DFA/5E34/5E6Eの各行はNV_REGを4命令として数え、main4の同一行DONEも含めた。

- 全読取りはLEの checked u8/u16/u32。`pc+length`、可変件数×要素幅、表index、ROM内参照先、shift、MMIOの整列を**実行可否によらず**検査する。マスク式の `m` は保持マスク。

| opcode / 名称 | 件数 | 意味・OS32実装 | MIT根拠 |
| --- | --- | --- | --- |
| 36 END_REPEAT | 1 | 1B、終端直後を保存し反復フレームへ復帰 | /home/hight/os32-tmp/refs/geforce6200/nouveau/drivers/gpu/drm/nouveau/nvkm/subdev/bios/init.c:724–733 |
| 38 NOT | 2 | 1B、条件skip反転 | /home/hight/os32-tmp/refs/geforce6200/nouveau/drivers/gpu/drm/nouveau/nvkm/subdev/bios/init.c:767–772 |
| 54 ZM_CR_GROUP | 3 | 2+2count B、{index,data}のCR直書き | /home/hight/os32-tmp/refs/geforce6200/nouveau/drivers/gpu/drm/nouveau/nvkm/subdev/bios/init.c:1213–1230 |
| 5B CALL（SUB_DIRECT） | 1 | 3B、ROM内u16絶対先へcall、DONEでpc+3へ。skip中は呼ばない | /home/hight/os32-tmp/refs/geforce6200/nouveau/drivers/gpu/drm/nouveau/nvkm/subdev/bios/init.c:1344–1363 |
| 63 COMPUTE_MEM | 1 | 1B、force。NV44AはNOOP_NV4A（D2）、容量を設定しない | /home/hight/os32-tmp/refs/geforce6200/nouveau/drivers/gpu/drm/nouveau/nvkm/subdev/bios/init.c:1459–1469 |
| 71 DONE | 9 | 1B、現在フレーム終了、callなら呼出元、mainなら要求終了 | /home/hight/os32-tmp/refs/geforce6200/nouveau/drivers/gpu/drm/nouveau/nvkm/subdev/bios/init.c:610–614 |
| 72 RESUME | 6 | 1B、条件skipクリア | /home/hight/os32-tmp/refs/geforce6200/nouveau/drivers/gpu/drm/nouveau/nvkm/subdev/bios/init.c:1751–1756 |
| 74 TIME | 13 | 3B、u16 µs。実行許可時だけ待つ。§5の時計で最低指定時間を保証 | /home/hight/os32-tmp/refs/geforce6200/nouveau/drivers/gpu/drm/nouveau/nvkm/subdev/bios/init.c:1781–1795 |

- 合計193。実物に無い34/56/69/76/79等の候補は許可集合から外す。4C〜50・5E・8E・5C・77・73・6D・47・48も無い。I2C・GPIOを含め表外opcodeは静的拒否する。

- 32/4Aは全てCR3C、mask=70h、shift=4、count=8でs=0..7を収容する。全到達値とcountを静的照合し、変異で範囲外が可能なら実行前に拒否。実行時の防御でも範囲外はnouveauどおり書かずRECへ記録し続行する。

- 9本の境界とopcode位置を先に作り、CALLが命令途中・表領域を指す、REPEAT不整合、未知命令、整数溢れは書く前に拒否。実行中のHW異常も即時停止し成功扱いにしない。

- BIT Iの追加unknown script（`/home/hight/os32-tmp/refs/geforce6200/nouveau/drivers/gpu/drm/nouveau/nvkm/subdev/bios/init.c:430,2344`）が非零なら5本だけで完了とせず、manifestへ取り込みレビューするまで拒否。encoded MMIO番地のhead/OR補正（`/home/hight/os32-tmp/refs/geforce6200/nouveau/drivers/gpu/drm/nouveau/nvkm/subdev/bios/init.c:144–179`）も展開後に許可検査し、出力依存で解決不能なら停止する。

> [要確認] 現行は診断も hwdev/gf/runner.c を使い、main 境界の 5 node を生成する。原文の静的展開・ordinal・verify 規則を現行実装と同一とは扱えない。

## GF6200_4A_S2_LAYOUT

参照：`/home/hight/os32-v3/{kdebug/recover.h,kdebug/recover_gf.c,tools/tests/gfinit_fixture.h}`、`gf6200_4a/nvbios/init_scripts.txt`、MIT nouveau `nvkm/subdev/bios/init.c` の各該当handler。x86コードは扱わない。

| 記号 | 固定契約 |
| --- | --- |
|e|`gf_effects` 全体の0始まりordinal。W/SELECT/DATAのsubstep=eを維持（pc内連番ではない）。同一opcodeの各eを個別登録|
|補助substep|Q=0x10000+j、通常C=0x11000+j（jは命令内で0始まり、表の出現順）。繰返す通常C/Qはその都度jを増やす。通常Cは0x11000–0x110ff内に限定し、PLL入口Cはn・k・合流の有無によらず固定substep=0x11101（分岐先ごとに同じargc=3のlayoutを共有）。0x11100は予約のみで範囲外Cを生成しない。既存eと衝突しない|
|K（skip形）|substep=`0x20000+pc`、NONE、`[I(length)]`（argc=1）。値の配列を含むROM内容はplan.rom_crcで担保する。実pcごとにargc固定のlayoutを登録。rule/expr=NONE|
|通常辺F|next[0]=next[1]=列の次node。末尾は次命令、終了はNONE。異なるhead/skipだけ状態別nodeを作り、同一状態に合流|
|条件辺B|next[0]=偽（skip=1）、next[1]=真（skipを維持）。skip到達はKで読み0、skip維持のF|
|選択辺J|Cのnext[0]=次の比較、next[1]=対応W列。countが到達選択範囲を覆うことを静的検証し、覆わないROMは拒否。最終偽は到達不能（辺の格納先は次命令）、各W末尾は次命令へ合流。範囲外Cは生成しない|

## 2. 27 opcode（番号は16進、長さはbyte）

表のWには実際のeを付す。Q/通常Cは上記j、PLL入口Cは予約substepで一意化。明記のない辺はF、skipはK、force無し。tはhead込みで解決したtarget ID。

| op / 名前 | 長さ・復号配置（+offset） |
| --- | --- |
| 36 END_REPEAT | 1 |
| 38 NOT | 1 |
| 54 ZM_CR_GROUP | 2+2n：n8@1,{i8,v8}[]@2 |
| 5B CALL / SUB_DIRECT | 3：絶対pc16@1 |
| 63 COMPUTE_MEM | 1 |
| 71 DONE | 1 |
| 72 RESUME | 1 |
| 74 TIME | 3：µs16@1 |

動的値からhead未解決なら拒否。

SUB後のhead/skipは共有、main入口はhead0/skip0。ctxはphase/main根・呼出箇所・反復番号で一意。nextはNONE又は大きいnode番号、両辺を検査する。

| 項目 | S2での決定／出口 |
| --- | --- |
|合流の条件|32は各選択Wを保持。4Aは実物の8周波数が全同値なので比較後に同一PLL処理へ合流、8倍複製しない。将来異なる周波数なら別展開し再計数。経路履歴ごとの全suffix複製は禁止|

表の制御引数はROMに裏付いた値だけ。READの再採取と過去値SAVEDを混同しない。SELECTにもruleが必要、Q/Cはruleなし、必要な追加SELECTを勝手にfixture ordinalへ割り込ませない。

51の最初のSELECTを採取と最初のDATAで共用し既存e列を維持する。snapshot採取を目的に新しいWを暗黙追加しない。RMW式をliteralに縮約してread_safe/選択検査を消さない。

decode が ROM を gf_scripts の crc32 (S1 の fixture の期待値、ノートの実 ROM と 9 本一致済み) と照合してから plan.rom_crc を設定する。recover は rom_crc を plan の hash に入れるだけで ROM と照合しない。K はアクセスしないので安全上の穴ではない

通常 C の substep 0x11000–0x110ff に収めるため、**j>0xff になる REPEAT (n≥255) は decode で拒否する** (実 ROM は n=10)。

## HWDEV_DESIGN

ROM データ復号

## REALHW_RECOVER_ADDENDUM

## §2 追加：分岐・skip・呼出し・反復と全経路検査

- decodeはSUB/CALLを呼出箇所ごと、REPEATを既知count回だけ静的展開する。呼出し/反復の入口と出口も元opcodeのnodeとして残す。ctxは親ctxと呼出箇所またはREPEAT箇所/0始まり反復番号で一意に採番し、復帰は親ctxへ戻る。phase/mainの根ctxも別ID。再帰・count=0・不整合終端・深さ超過は4a §4どおり拒否する。

- nodeは展開後の命令/substepと到達時のhead・skipを表す。同じ位置に異なるhead/skipで到達する場合だけnodeを分ける。`next[0/1]`は通常同一、条件は偽/真の後続、終了はNONE。呼出し先と復帰先、反復の次回/終了先は展開列の辺となり、後退辺は不要。入口間の正常遷移も検査対象とする。

- skip中の通常W/読取/TIMEは非実行の制御node（rule_id=NONE）として残す。NOT/RESUME・条件のskip保持・SUB/CALLの非呼出し・force・共有head/条件状態は4a §4の27種だけで遷移を検査する。IO_RESTRICTの選択肢と範囲外無書込み、PLL等の内部分岐も辺へ展開し、未知/未解決の制御・番地・headは拒否する。

- decodeは9本のscript CRC照合後にROM全体CRCをplanへ置き、S1の命令位置とeffect ordinalだけを本番側の範囲表に持つ；ROMバイト列・x86コードは持たない。ホストの支持表は合成データで閉じる。

- CALL/REPEATは先にctx別の命令列へ展開し、各位置のhead/skip状態を合流させる；最長経路steps/待ちは後ろから計算し、分岐全体のnode数を実行予算へ足さない。

SUBの表入口は既知pcだけでなくscript IDとの対応も照合する。

- static最長経路のstepsはnodeとEXPR内READを数え、実行器と同じ4096枠に統一する
