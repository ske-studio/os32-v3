# drivers/ata/README — 設計
> 状態: **現行 (2026-10-31)** — 出所: MEMMAP_V3 (D1 で移した)
ソース: `drivers/ata/README`

### cd_module

3. ATAPI/iso9660はFD/HDD root上からPOST_ROOTで読み、cdinst・USER_READYより前に登録する。MINIMALでもCD読取を残す。登録後に `/cd0` 自動mountとloop/hd上ISOのiso9660試行も実行する。

- [ide.md](ide.md)
- [ide_pio.md](ide_pio.md)
- [ide_request.md](ide_request.md)
