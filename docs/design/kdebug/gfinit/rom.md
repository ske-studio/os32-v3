# kdebug/gfinit/rom.inc — 設計
> 状態: **現行 (2026-10-31)** — 出所: GF6200_4A_DESIGN.md・GF6200_S7_SHELL.md (D1 で移した)
ソース: `kdebug/gfinit/rom.inc`

## GF6200_4A_DESIGN

## 3. BAR測定と G1、実行準備

| 順序 | 操作・検査 | 失敗時 |
| --- | --- | --- |
| A0 読取 | Commandを生読みして記録のみ（起動直後の0106hを許容）。BM=0は承認済みA1で0102hへ移した後に要求し、それまではRUN禁止。ID・PMC・straps・cfg・他PCI装置とブリッジ窓を段0と比較。PROMを2回読み一致・55AA/PCIR・長さ・和を検査し、部品内の上限128KiBバッファに保存。下記A3の静的検査もここで先に完了する | 書く前に止める |

trial の外の INSPECT は例外として通常の 1 行と終わりの行を出す

## GF6200_S7_SHELL

- `kdebug/gfinit.c:117-147,174-203` のINSPECTはROM二重読取・PCIR・checksum・decode。表示を足すだけでsnapshot経路は作らない。

- D2：INSPECTは通常行 `GF inspect sid= rc= rom_len= crc= chip= expect= match=` と `GF inspect-end sid= rc=` だけ。RECのsid/seqを進めない。

- hwdev側で実ROMから長さ/CRC/version.chipを表示し、既存importのkprintfを使う。新import・契約・可変長wireは追加しない。chipの位置/期待値は部品内の根拠に揃える。

- 数値はsid/rc/rom_len/matchが十進、crc/chip/expectが16進。未取得値はunknown、matchは1/0/unknownとし、取得失敗・不一致を合格にしない。

`GF inspect` 行は hwdev が出す (取れなかった欄は unknown)

- version.chip は既存二重読取ROM内の BIT `i` (version 1) のデータ先頭+2から読む（MIT envytools f102b82381f3f11cee113d16374c87091db039d9、nvbios/bios.c:198,230・bit.c:75–78,124–128・info.c:49–52）；期待値44は REALHW_RECOVER_ADDENDUM のS4取り込み事項に揃え、欠落・範囲外・不一致はinspect失敗とする。

- 二重読取とROM長の検査を通った時点でrom_lenと全ROMのIEEE CRC32を取得済みとし、PCIR/checksum/decode失敗でも表示する；それ以前はunknown、expectは常に44、matchはchip取得時だけ1/0とする。
