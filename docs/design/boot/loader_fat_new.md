# boot/loader_fat_new.asm — FAT12 の第2段ローダー

> 状態: **現行 (2026-10-31)**

FAT12 からカーネル像を読み、検査して起動する。
媒体ごとの定数でルートと FAT チェーンをたどる。

## 落とし穴

- **ロード番地と org**: DS=0 の絶対参照に `org 0` を使うとデータ参照がずれる。
  IPL のロード先と一致する `org MEM_LOADER_START` を保つ (`boot/loader_fat_new.asm:80`, `boot/boot_fat.asm:62`)。
  この契約は FAT ローダーのものであり、他の形式のローダーへ一律に適用しない。
  像の検査には `make check-vk32-crc-host` があるが、org 自体の直接試験は未確認。

- **媒体ごとの FAT 長**: 1.44MB の FAT は 9 セクタで、2 セクタだけ読むと後方のクラスタが壊れる。
  512B・18 SPT と 1024B・8 SPT の定数を選び、`FAT_SECTS` 全体を読む (`boot/loader_fat_new.asm:45`, `boot/loader_fat_new.asm:181`)。
  変更時は両媒体の起動を確認する。媒体別 FAT 長の直接試験は未確認。
