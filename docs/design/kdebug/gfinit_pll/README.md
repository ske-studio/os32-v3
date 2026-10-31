# kdebug/gfinit_pll/ — 設計
> 状態: **現行 (2026-10-31)** — 出所: GF6200_4A_DESIGN.md・GF6200_4C_DESIGN.md・REALHW_RECOVER_ADDENDUM.md (D1 で移した)
ソース: `kdebug/gfinit_pll/`

### limits.inc

ソース: `kdebug/gfinit_pll/limits.inc`

`/home/hight/os32-tmp/refs/geforce6200/nouveau/drivers/gpu/drm/nouveau/nvkm/subdev/bios/pll.c:301–333` のVCO1/2上下限・入力周波数・/home/hight/os32-tmp/refs/geforce6200/nouveau/drivers/gpu/drm/nouveau/nvkm/M範囲・max_p/bias_pをchecked読取。ref_clk=0は27,000kHz（同:386–398）。

### parse.inc

ソース: `kdebug/gfinit_pll/parse.inc`

- PLL limits: 表0x5BC8、版0x21、4件。対象レジスタのキー (NV40 の対応 CORE 0x004000・MEMORY 0x004020・VPLL0 0x680508) が一致するエントリ、無ければ表の最初のエントリ (`/home/hight/os32-tmp/refs/geforce6200/nouveau/drivers/gpu/drm/nouveau/nvkm/subdev/bios/pll.c:162–175`)。キーの重複は断る。Ra266 の ROM はヘッダ 5・エントリ 35 バイト、キーは 0・0x4020・0x4008・0x402C で、CORE と VIDEO は最初のエントリを使う。

- PLL0x21は4件・entry長31 byte以上をchecked読取し、指定reg（低位はreg−4）を一意に検索する；ref=0は27000kHz、非零は27000だけ、N/M=1..255、max_p/bias_p=0..7とし、探索のmax_p_usable=6・段別丸め・0.5%補正・fixedgain例外はMIT pllnv04.cのNV4x仕様を維持する。

> [要確認] 4A の後段訂正では一致キーが無い場合は表の先頭へ fallback する。原文の一意検索だけでは現行の選択規則を表せない。

### solve.inc

ソース: `kdebug/gfinit_pll/solve.inc`

- 周波数はkHz整数、`f≈27000×N1×N2/(M1×M2×2^P)`。単段はN2=M2=1。中間積はu32、nouveauの段別演算順を維持し、一括積の式へ置換しない。/home/hight/os32-tmp/refs/geforce6200/nouveau/drivers/gpu/drm/nouveau/nvkm/M<=255、ref=27000を検査し、`_Static_assert(27000U <= UINT32_MAX / 255U / 255U, "PLL product")`で二段積上限1,755,675,000を確認する。clkP×M+丸め項も乗算前に `(UINT32_MAX-add)/M` と比較し、shift・加算・分母を検査して超過は拒否。pllnv04.c:99/107/171/184/195の演算をhostで比較し、32bitリンクで__udivdi3等の64bit除算支援が不要なことを確認する。

- solverは `/home/hight/os32-tmp/refs/geforce6200/nouveau/drivers/gpu/drm/nouveau/nvkm/subdev/clk/pllnv04.c:28–120,127–247` の探索順・丸め・単段/二段選択・NV4xの段間倍率制約を再現。解無しなら書かない。返した係数・実周波数・誤差をログ化する。

### sequence.inc

ソース: `kdebug/gfinit_pll/sequence.inc`

- 4004h/4024hは低位PLLのNMNM。limits検索はreg−4（`/home/hight/os32-tmp/refs/geforce6200/nouveau/drivers/gpu/drm/nouveau/nvkm/subdev/devinit/nv04.c:366`）、4000h/4020hの制御と対で扱う。680508hは高位PLL。

- 低位書込は同:277–353を基準に、4000/4004、4020/4024、4038/403C、4600、C040の順序・保存/復元・MPLL biasを再現。裸のM/N/P書込で済ませない。

- 高位は同:200–273、680508/680578・680580・1584・C040を含めチップ分岐で使用する補助レジスタを許可。402Ch=C1C11002hは生レジスタ値として扱い周波数と混同しない。

- r(x) は既存 REC_ARG_READ でその操作直前に評価し、PLL は既存 parser/solver/sequence で 25,175 kHz を bind 時に解く；C040 の末尾だけは先頭の C040 読取りを capture して復元し、係数直後 verify=0 は維持する（nouveau nvkm/subdev/devinit/nv04.c:197–272、§3 の 4a 再利用差分）。

- NV44Aの内部列は4004hが6書込み、4024hが15書込み、680508hが5書込み；S1の101候補は76件を26本の順序依存つきruleに対応付け、4004h側のMPLL専用3レジスタ×8候補と高位1584hの計25件はチップ分岐で不要として解消し、未解決PLLは0件とする。

- 同値なら省く参照実装の最適化は採らず、承認済みの同じ有限列を毎回実行する；680580hも同値書込みを含める。係数と補助レジスタはreadbackし、lock検出とは呼ばない。元値の読取りは内部列の先頭へまとめ、S5の実accessorや中断後のCOLD復元を追加しない。

- capture上限は3から32（valid bitmapの幅内）にし、PLLの一時値は既存3件に続く23件の単一consumer slotで扱う；4038hの設定元は4020hなのでconsumer宛先とsource targetの同一条件だけ外し、同pc/ctx/head・先行read・全経路の到達・一回消費・許可ruleを維持する。

> [要確認] 現行は診断も hwdev/gf/runner.c を使い、main 境界の 5 node を生成する。原文の静的展開・ordinal・verify 規則を現行実装と同一とは扱えない。
