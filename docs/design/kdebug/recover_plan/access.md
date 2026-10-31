# kdebug/recover_plan/access.inc — 設計
> 状態: **現行 (2026-10-31)** — 出所: BARLEN_DESIGN.md・GF6200_4A_DESIGN.md・GF6200_4A_S2_LAYOUT.md・GF6200_VPLL_PROBE.md・REALHW_RECOVER_ADDENDUM.md・REALHW_RECOVER_ADDENDUM_R2.md (D1 で移した)
ソース: `kdebug/recover_plan/access.inc`

## BARLEN_DESIGN

- 重なりの失敗は既存 recover の access_error と cleanup 経路を使う。架空の resource_reject 発生を試験で期待しない。

## GF6200_4A_DESIGN

- G1（`/home/hight/os32-tmp/run/gf6200/PLAN.final.md:88`）は「4bの前」の下限。本設計では初期化中の legacy decode も防ぐため4aの先頭で満たす。`/home/hight/os32-tmp/refs/geforce6200/envytools/docs/hw/bus/pci.rst:50–60,78` が根拠。

- [A0000h,C0000h) を GF の試験に写像・アクセスしない。GPU の I/O decode は常時0。1854h を非零、1850h bit0 を1にする script は事前拒否し、黙って書換えない。

- 各script終了でも cfg54/PBUS1854=0、cfg50/PBUS1850=0を確認。RESETの1850 bit0クリアは許可する。終了・ABORT時も54h=0を保持し、4bも同じ禁止条件を守るが、4a終了のGPU状態を残さず、承認されたinitを含む単一sessionを別途設計する。

- 54h=0は安全側に保持する例外。中断時に1へ戻してlegacy窓を再取得させない。4bの承認前に1850h=1は書かない。

## GF6200_4A_S2_LAYOUT

EXPRのRは同W substep内でrecoverが採取・評価する（別Qの値をRで流用しない）。layout.dirは要求Wの方向、式評価の内部READを別のW権限と解釈しない。

全非実行形もROM境界・参照表・shiftを検査する。Wが存在するのにrule_id=NONEの実行nodeをbind可能にしない。

| 対象／W substep | 参照式・採取位置 | 実装条件 |
| --- | --- | --- |
|DF10 RESET / e17|identity(saved184C)、**e14内でclear前に読んだ32bit全値**を採取（0F00だけではない）|同命令・同ctx・head・sessionの採取成立が必須。e17はRUN+FORCE EXPR、COLDの終了復元には使わない|
|E67F CR57 head0 / e410|identity(savedCR57)、先頭e381 SELECT後Q0で採取、最初のe382 DATAより前|最終e409 SELECTを要求。8bit、RUN EXPR|
|E6C0 CR57 head1 / e454|identity(savedCR57)、先頭e441 SELECT後Q0で採取、最初のe442 DATAより前|最終e453 SELECTを要求。head0のslotと別、RUN EXPR|

ポート別にする。キーは検査済み実効index port（space/bar/offset/head込み）、値は選択targetとphase/main/ctx。同portの別SELECTで失効、別portは保持。

## GF6200_VPLL_PROBE

記録専用readは診断有効かつmain=2/pc=E623h/substep=12040h～12044hの5規則に限定し、verify_mask=0を維持。不一致は止めず、readのアクセス失敗・送信失敗は既存の失敗／S0復帰へ。

kdebug/recover.c:2015–2032の読取りと不一致判定を分離し、read_safe確認後にlogged_read（同:943–956）を使う。RUNNINGの5本でaccess BEGIN/ENDが10行、call_pointが5回増える（ops->read直呼びはしない）。

前例は同じVIDEO PLL 5規則の旧verify非零版(記録はアーカイブ)；同じverify読戻し経路（kdebug/recover.c:2016–2020,943–956）が各到達規則で同じaccess行を出していた。

SAMPLE・固定RAM・新しいREC形式は不要。既存gf_bus ENDへ全32bitを記録し、増分は上記access 10行。

## REALHW_RECOVER_ADDENDUM

- bindでは引数の型/参照/範囲と全選択肢を検査する。runtimeでは同substepで共有accessorが採取した読値とsnapshotを使い同じ登録式をchecked整数演算で評価し、4aの要求値と比較した後r2のmask/SET/RANGE/write_maskも検査する。PLLは登録したsolver/手順に入力とlimitsを渡し、係数の自己申告で済ませない。未実装expr IDはDENY、

runtime でも選択済み target と照合する。

- 局所 snapshot は recover に32件の `{key,dir_substep,value}` を持ち、安全なRW indexedの初回 SELECT/DATA 直前に採取する。LOCAL+SAVED の RESTORE rule だけが対応値を使える。元indexはdataの値と区別し、元index復元のSELECTだけはvalue=target.indexの制約を外す。COLD data は採取・SAVED再生しない。

分類済み解錠→data→lock→元indexの列を recover_access(NONE,...) で実施後、PTE/IRQ/窓を解放する。operationは安全点/accessorの非0後に解放・アクセスをせず直ちにreturnする。

- cfg50/54・PBUS1850/1854は部分幅/途中offsetで迂回できない32bit tupleとし、非零Wを拒否する。N1の前後は必ず4個を別々に読み、PREPARE後のRUN入口/出口と終端でも確認する。Commandは16bit、SAVED禁止、Mem再開時は生BAR復元確認を要求する。

- snapshot専用regsはSELECTを要さないtargetに限定する。indexedの安全な保存は上記局所snapshotに収め、未承認のSELECTをsnapshot/INSPECTから暗黙に発行しない。

- skip中の53/54のK nodeはROM検証済みの次node.headへ論理headを進める（HWアクセス0）；RUN main入口はhead0へ戻す。通常nodeのhead不一致拒否は維持する。

- LOCAL/SAVEDのSELECTだけは明示分類済みindex portならCOLD targetにも認め、同portの保存indexを参照可能とする；DATAのCOLD再生は禁止のまま。

- CR1Fの局所SAVEDは保存読値0/3を命令値99h/57hへ写して承認し、同じLOCAL ruleでcleanup冒頭の解錠57hを許す；全CR1F DATAのverifyは命令値を読値0/3へ写し、順序をUNLOCK→CR11→CR44→LOCK→INDEXとする。

## REALHW_RECOVER_ADDENDUM_R2

index選択もWとし、条件読取も承認済み書きop内限定。直接data側や同一別名へのraw MMIOによる迂回を拒否する。

- N1 guard前にも生Command.Mem/BAR0を検査。不成立ならMMIOを読まずFAILED。0FAChはrecoverだけが扱い、54h=1での迂回や未承認ROUTE書込みは認めない。
