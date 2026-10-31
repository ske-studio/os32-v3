# kdebug/gfinit_resources/ — 設計
> 状態: **現行 (2026-10-31)** — 出所: BARLEN_DESIGN.md・REALHW_RECOVER_ADDENDUM.md (D1 で移した)
ソース: `kdebug/gfinit_resources/`

## 索引

- [kdebug/gfinit_resources/bars.inc](bars.md)
- [kdebug/gfinit_resources/scan.inc](scan.md)

### bounds.inc

ソース: `kdebug/gfinit_resources/bounds.inc`

memory32 の非零baseに対し `align_cap = base & (0U-base)`。長さは2冪かつbaseを整列させるので、実長 <= align_cap。

- `next` はbaseより大きい最小の割当済みmemory BAR/ROMのbase、`cap = min(align_cap, next-base)`。隣がなければalign_capを使う。 隣候補は同一装置の別BARも含む。Command Mem・prefetch・ROM enableに関係なく非零割当を採る。I/Oは除外、属性ビットは除去する。

> [要確認] 後段の C1 は隣候補を Command.Mem 有効の非零 memory32 BAR に限定する。無効 ROM も候補にする本文とは異なる。

- 第1走査は既存と同じbus0全function、header別BAR本数とROM位置で候補を収集する。GPUは除外、非零memory32とROM baseを重複除去して保存、256件超は `memory_bar_count`。64bit BARは上位も読み飛ばして候補にせず、範囲外headerは既存どおり拒否。 第2走査は既存のMem有効判定を維持。2走査間にyield/割当変更なし。第1走査のdisabled ROM候補採用と、第2走査のenabled ROM拒否は別の処理。

- trace は `GF A1 reject=%s bdf=0:%u.%u reg=%x raw=%x` を維持 (`gfinit_resources.c:30`)。

- 上記新 reason でも reg は `PCI_CFG_BAR0 + BAR_STRIDE*bar`、raw は元の32bit値。部分配列は count=0 で無効化する。

- 成功 trace の追加は行わない。既存 host の kprintf mock は reason 引数前提なので不要な書式追加を避ける。

§2 の「隣の候補」は **Command の Mem が有効な装置の、非零の memory32 BAR だけ** に絞る。Mem 無効の装置の BAR・無効の ROM の番地は候補にしない (BIOS の割当の前提の外)。

別の装置・別の BAR の間で base が等しければ `memory_bar_alias` で拒否する。§3 の重複除去は同じレジスタの重複だけにする。それ以外の alias の検出はしない。

- S6修正: A1の長さ不明memory BAR/ROM・header・bridge窓・GPU不一致/不在は拒否理由とbus0 BDF・reg・rawを開始前にログへ出す

### windows.inc

ソース: `kdebug/gfinit_resources/windows.inc`

bridge window の type/high/range 検査は `gfinit_resources.c:37`、同:87 のまま保持する。

off-path bridgeは正確な転送窓全体で子装置を包含して排他する（TI PCI2050B SCPS076G §§4.20–4.25、FreeBSD releng/14.3 `sys/dev/pci/pcireg.h`）；有効なendpoint memory BAR/ROMの長さ不明は既定どおり書込み前に拒否し、隣接割当・baseの整列から実長を推測しない。
