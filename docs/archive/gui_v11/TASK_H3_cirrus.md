# H3: Cirrus GD54xx チップドライバ + Xe10 グルー

> 発行: PM (2026-09-05) / 状態: **受入完了 (2026-09-06)**
> レーン: H / 前提: H2 完了 + ai-debug screenshot 拡張 (本票 §0)
> 親: [TASKS.md](../../tasks/gui/TASKS.md) / 設計: [DESIGN.md](../../tasks/gui/DESIGN.md) §3.2, §4, §6, §7, §8 (Cirrus), §10-3
> 排他: `drivers/wab_cirrus.{c,h}` `drivers/wab_glue_xe10.{c,h}` (新規)、`gfx/backend_cirrus.c` (新規)
> 位置づけ: v1.2。ゲート G5 の後半。**着手は PM の指示を待つ** (screenshot 拡張と ini 承認が先)

## 0. 前提作業 (PM / np21w-src 側)

- NP21/W ai-debug の `/api/screenshot` は 98 側 VRAM しか拾わない (DESIGN §4)。WAB 出力を
  含める拡張を `~/np21w-src` で先に行う (`scrnsave` に WAB フレームバッファの
  経路を足す)。これが無いと H3 は目視検証できない。
- `np21x64w.ini` の **`[NekoProject21]` セクション** に `USEGD5430=true` / `GD5430TYPE=91` (10 進。PFTYPE_UINT16 は 16 進表記を受け付けない。91 = 5Bh = Xe10 固定、AUTO 禁止) を入れる。**[D2] 承認事項** (2026-09-06 承認済み、切替は H3b 完成後に PM)。

## ゴール

DESIGN §6 の二層で、Xe10 内蔵 Cirrus をバックエンド表の 3 枚目にする。**初めて
`GFX_CAP_HW_FILL | HW_BLT` を立て**、`gfx_hw_fill_rect` / `gfx_hw_blit` が NOSYS 以外を返す。

## 作業

1. **グルー `wab_glue_xe10`** (100 行前後): ポート翻訳表 (VGA 論理レジスタ 3C0h 系 →
   0904h / 0CA0h〜0CAFh / 0DA4h / 0DA5h / 0DAAh / FF82h)、制御レジスタ (2 段 I/O
   0FAAh / 0FABh: 00h = ID、03h bit1 = 表示リレー)、MMIO / リニア FB の有効化。
   `probe` は ID = 5Bh のときだけ 1。出典は `docs/hw/undocumented/io_wab.md`。
   **40E1h / 42E1h / 46E8h / 51E1h / 5BE1h は触らない** (メルコ / I-O DATA の C バス
   ボード用。DESIGN §3.2 と §4)。
2. **チップドライバ `wab_cirrus`**: レジスタ体系 (SR / GR / CR 拡張)、リニア FB の設定、
   BitBLT (塗り・矩形転送・色展開)、ビジー待ち。**PC-98 のポート番号を一切含めず**
   `glue->out(reg, val)` / `glue->in(reg)` で書く。BLT レジスタは MMIO で設定する
   (DESIGN §8: OUT より速い)。
3. **バックエンド `backend_cirrus`**: クライアント面はカード VRAM の**非表示領域** (表示面の
   後ろ。1MB VRAM なら 640×480×1B の面がもう 1 枚以上入る) に置き、`bb_base` はそこを指す
   (主記憶バックバッファ無し)。`fill_rect` / `blit` はエンジン、十数ピクセル角以下は
   CPU 直書きへ切り替える閾値をカウンタで決める。**`present_rect` = 非表示面から表示面への
   エンジン BLT** (損傷矩形単位) + 完了待ち。これで契約 G4 の「commit 前の描画は画面に
   出ない」をソフトウェアバックエンドと同じに保つ (2026-09-05 改訂。表示面へ直接描く案は
   取り下げ)。`enter` / `leave` でリレー切替。
4. **能力ビット**: `HW_FILL | HW_BLT`、`TEXT_OVERLAY` は 0 (リレー中は 98 側テキストが
   見えない)、画面は 640×480 / 8bpp から。

## 鉄則 (DESIGN §7 の 7 則をそのまま)

- 層を越えない: チップにポート番号を書かない、グルーにチップの知識を書かない。
- [HW1] は適用外 (アクセラレータは 2D エンジン可、B3)。
- 小さい操作の設定コストと非同期待ちが律速 (DESIGN §8)。操作は矩形単位にまとめる。

## 完了条件

- Xe10 設定で `hal_test` が HW_FILL|HW_BLT を表示、`gfx_hw_fill_rect` が 0 を返す。
- `gdi_test` が同じ絵を出し、`gfx_stats().hw_ops` が塗り・転送・commit の BLT の回数だけ増え、
  `present_bytes` が 0 (CPU がピクセルを運ばない)。commit 前に途中経過が表示面に出ない
  (描画途中で screenshot を撮っても前フレームのまま)。
- 9801 / PEGC 設定で回帰なし。
