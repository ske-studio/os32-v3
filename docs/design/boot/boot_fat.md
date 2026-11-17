# boot/boot_fat.asm — FAT12 フロッピーの IPL

> 状態: **現行 (2026-10-31)**

ブートセクタから FAT12 をたどり第2段ローダーを読み込む。
2HD と 1.44MB の媒体定数を切り替える。

## 落とし穴

- **セクタ長と配置**: 1.44MB では IPL の読込が 512B なので、末尾まで収まらないコードは実行できない。
  `SECT_SZ` に合わせてパディングし、溢れをアセンブル時に拒否する (`boot/boot_fat.asm`, `boot/boot_fat.asm`)。
  ルートは 192 エントリとし、バッファが下向きのスタックに重ならないようにする (`boot/boot_fat.asm`, `boot/boot_fat.asm`, `boot/boot_fat.asm`)。
  両媒体の起動確認が必要であり、これらの直接試験は未確認。

- **CHS と FAT 長**: 18 SPT は 2 の冪ではなく、シフトでの LBA 変換は誤る。
  SPT で除算し、媒体ごとの `FAT_SECTS` をすべて読む (`boot/boot_fat.asm`, `boot/boot_fat.asm`)。
  変更時は 2HD と 1.44MB の双方を確認する。直接試験は未確認。
