# kdebug/gfinit_plan.c — 設計
> 状態: **現行 (2026-10-31)** — 出所: GF6200_4A_DESIGN.md・HWDEV_DESIGN.md・REALHW_RECOVER_ADDENDUM.md・REALHW_RECOVER_DESIGN.md・SLIM_HWDEV_DESIGN.md (D1 で移した)
ソース: `kdebug/gfinit_plan.c`

## GF6200_4A_DESIGN

- PFB組3の必須比較: 100220=03080E0B、100224=05010405、100228=20220303、10022C=41088102、100234=00266622（票:56）。

- 10020Chは上位容量フィールドを記録・解釈し、0/不合理値なら容量不明を記録し、容量照合（alias試験）だけを省く。予約済みBAR1先頭1KiBの最小試験は10020Chに依存せず実行し、容量未検証を別記してtrial_end(normal)へ進む（§9 N2）。全script・macro・sequenceに10020Chへの書込みは無く、COPY_NV_REGは読むだけなので、初期値0から容量が得られる保証は無い。BAR1サイズを実装VRAM量とは呼ばない。PMC.ENABLEも実行traceから導いた最終値と比較する。

- PLLは4000/4004・4020/4024・4038/403C・402C・680508/680578/680580、および変更した補助レジスタをreadback。レジスタ毎の読出可能マスクで比較し、WO/W1Cへ検証用の追加RMWをしない。

> [要確認] 旧 A4 の PLL 比較規則は後段の verify=0 訂正と異なる。現行の A4 と共有 runner の検証箇所を照合する必要がある。

- VRAM最小試験: planで破棄可能と明記した予約範囲（BAR1先頭1KiBを予約できた場合）に交互55AA55AA/AA55AA55を**全て書いてから全て読む**、反転でも繰返す（Haiku `nv_general.c:1394–1422` の test_ram相当）。書く前の内容は退避するが初期化前の保全は保証しない。

- 容量照合: 10020Chの容量フィールドが非零かつ合理的な場合だけ、`min(BAR1窓,PFB申告量)` 内の2冪境界・先頭・最終DWORDへ異なる番地署名をまとめて書き、全点読戻しでaliasを検出。範囲外へは書かず、窓が小さければ全容量未検証と記す。

- PCD写像とvolatileを使い、GPUの同じ安全な読取でposted writeを排出。BAR1書込範囲と試験patternも承認資料に明記。疎な試験の成功を全VRAMセルの健全性とはしない。

## HWDEV_DESIGN

A4 等の plan 合成

## REALHW_RECOVER_ADDENDUM

- A4の読戻しは専用layout flagの読取専用CHECK_EXPR nodeへ8語ずつまとめる；recoverに登録した純整数式とread_safeな非indexed targetだけを許し、Wの1node/1rule・既存件数上限・全経路照合を変えず、式未実行のleaveを拒否する。

- A4先頭1KiBの元値は最初のpattern書込み式の許可済READで部品所有RAMへ一度だけ退避する；破棄予約なので終端で再生せず、2パターンとも全書込み後に全読戻しを行う。

- PFB10020Chの容量はMIT nouveau `subdev/fb/ramnv44.c:30` とenvytools `memory/nv10_pfb.xml:213` のMEM_AMOUNTにより上位8bitをbyte量として読む；16MiB以上の2冪だけをこの段の合理的形式とし、0・他形式は容量未検証と記録して最小試験だけを行い、実容量無しとは主張しない。

- 容量照合は実測BAR1長までの2冪境界・終端DWORD・先頭の固定候補をplanへ収め、PFBとの小さい方に入る点だけを前向き分岐で選ぶ；GPU専有の破棄予約をその候補範囲へ広げるが、W許可は固定候補だけとし、最小試験・容量確認・窓による部分確認とS0復帰の結果を分けてRECへ残す。

## REALHW_RECOVER_DESIGN

容量不明でも BAR1 先頭 1KiB の最小試験を行う。

## SLIM_HWDEV_DESIGN

gfinit_plan.c:109–113のコピーは同じ配列への自己代入となり、そのままでよい。

- gfinit_plan.c:83,102–103,167とVRAM用追記、decode/bus/exec内の表参照も従来型の直接参照のまま。
