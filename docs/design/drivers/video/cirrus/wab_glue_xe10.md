# drivers/video/cirrus/wab_glue_xe10.c — 設計
> 状態: **現行 (2026-10-31)** — 出所: DESIGN (D1 で移した)
ソース: `drivers/video/cirrus/wab_glue_xe10.c`

- 内蔵 GD5428/5430 の VGA レジスタは 0904h、0CA0h〜0CAFh、0DA4h/0DA5h/0DAAh、FF82h に
  写像される。
- NP21/W の ID 定数 (`CIRRUS_98ID_*`) はこの資料の ID 値と一致する。

- **40E1h / 42E1h / 46E8h / 51E1h / 5BE1h は Xe10 のポートではない** (2026-09-04 に訂正)。
  これらはメルコ WAB-S / WSN-A2F / WSN と I-O DATA GA-98NB という **C バス製ボード**の
  ポートで、NP21/W は機種 ID が 0x100 以上 (サードパーティ) か AUTO のときだけ登録する。
  Xe10 モードでは NP21/W も資料どおり 0FAAh/0FABh と 0904h / 0CA0h〜 / 0DA4h 系を
  使うので、**OS32 のドライバは資料のポートだけ実装すればよい**。

- **ボードグルー**は「ポート翻訳表 + 制御レジスタ (ID / リレー / VRAM 窓 / MMIO 有効)」だけを
  持つ小さな 1 枚。NP21/W の `vga_convert_ioport()` と同じ役割。

- 表示リレーの切替はミリ秒単位 (メカニカルリレーまたはアナログスイッチ) + モニタ再同期。
  `enter` / `leave` のときだけ行う。
