# tools/tests/ — 設計
> 状態: **現行 (2026-10-31)** — 出所: BARLEN_DESIGN・CRT31K_DESIGN・GF6200_VPLL_PROBE・SLIM_HWDEV_DESIGN (D1 で移した)
ソース: `tools/tests/`

- 6本の既存host試験は寿命を持つgfinit_planバッファへ1回展開し、以後そのprofileを使う。部分コピーするlayouts/targetsもその配列を借りる。

- host の include 階層は translation unit ごとに 1 個の展開済み fixture を共有し、各実行入口で初期化する；旧静的配列への直接参照も移し、ROM 診断ツールは既存の合成 ROM fixture で通す。

### gfinit_resources_ra266

- fixture は E の7台の pcidump から転記し、それ以外の function は vendor=ffffh。raw attribute bit を保持する。

### test_np21w_ini

- test_np21w_ini.py: on/off往復、大小文字・CP932・CRLF・コメント・無関係バイト保存、欠落/重複/不正値拒否、旧fixtureで既存操作成功。

### test_np21w_ini_live

- test_np21w_ini_live.py: preview無変更、apply/readback/receipt/restore一致、off≠restore、既存排他・停止確認・差替え検知の回帰。

### gfinit_romfile_host

- 選ばれた行の番号は、道具が `#include` した gfinit_pll.c の定数 (GF_PLL_ENT_REG・pll_le・表の header/entry/count) で各行の key を照合して出す (一致の数・行番号・offset、一致 0 なら先頭の行への fallback)。製品の gfinit_pll_parse は変えない。照合で選んだ行の欄から組んだ limits が gfinit_pll_parse の結果と全欄一致することを道具の中で確かめ、食い違えば rc≠0 (選び方の二重実装のずれを検出する)。

### recover_tables_host

- 抽出は別 translation unit の旧表／製品表を C で評価し、全欄を table・row・field・slot・value の名前付きレコードとして出力する；参照は NULL／シンボル名と全要素、式は eval の名前と前処理済み関数本文、profile は CRC も照合し、--extract で schema=1 の JSON を後続生成器へ渡す。

### FatFs 保持と既存回帰

- vfs_fd_path_host.c の allocator は4個の4KiBスロットとし、Ext2Ctx と保持状態を含む FatFsCtx が収まることを静的に確認する。Ext2Ctx のサイズを全 FS の確保上限としない。
- fdc_track_host.c の font_replay は製品 fatfs_vfs_read_cache.inc の rc_read を実 FDC 模型と FatFs に接続し、解除前に破棄する。test_fdc_track.py は VFS と同じ helper を通す配線と、変異の写しにも製品 .inc を含むことを維持する。データ一致と配置から導くシーク・まとめ読み上限は保つ。

## 索引

- [FatFs 読み取り保持の実 FatFs 試験](fatfs_read_cache_host.md)
- [FatFs 読み取り保持の試験入口](test_fatfs_read_cache.md)
- [FatFs VFS の境界回帰](fatfs_stat_host.md)
- [tools/tests/recover_tables_oracle](recover_tables_oracle.md)
