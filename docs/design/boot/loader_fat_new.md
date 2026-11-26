# boot/loader_fat_new.asm — FAT12 の第2段ローダー

> 状態: **現行 (2026-10-31)**

FAT12 からカーネル像を読み、検査して起動する。
媒体ごとの定数でルートと FAT チェーンをたどる。

## 落とし穴

- **ロード番地と org**: DS=0 の絶対参照に `org 0` を使うとデータ参照がずれる。
  IPL のロード先と一致する `org MEM_LOADER_START` を保つ (`boot/loader_fat_new.asm`, `boot/boot_fat.asm`)。
  この契約は FAT ローダーのものであり、他の形式のローダーへ一律に適用しない。
  像の検査には `make check-vk32-crc-host` があるが、org 自体の直接試験は未確認。

- **媒体ごとの FAT 長**: 1.44MB の FAT は 9 セクタで、2 セクタだけ読むと後方のクラスタが壊れる。
  512B・18 SPT と 1024B・8 SPT の定数を選び、`FAT_SECTS` 全体を読む (`boot/loader_fat_new.asm`, `boot/loader_fat_new.asm`)。
  変更時は両媒体の起動を確認する。媒体別 FAT 長の直接試験は未確認。

## 像の検査と FAT チェーンの検査

出所: TASK_SERIAL_HOSTFS 部品 A-4 (§1-v3「FD ローダ」) の契約を今のソースと照らして要約 (2026-11-18)。

- **像の検査** (`pm_vk32_boot`): HDD ローダの `boot/vk32_boot.c` と同じ順序・同じ `VK32_ERR_*` で VK32 v2 を検査し、CRC32
  (nibble 表) も見る。規則は `docs/design/boot/vk32_boot.md`。外れたら理由を画面に出して止まる。通ったら起動したイメージの
  CRC をブート情報域のイメージ欄に書く (`pm_entry32`)。
- **展開** (`pm_lz4_decode`): 入口で `cld`、入出力の境界検査、全出口を `ebp` から戻す (`docs/design/boot/lz4_mini.md`)。
- **FAT チェーン** (`fat_chain_check`): `VMKRNL.LZ4` を読む**前に**チェーン全体を検査する。結果 `FATCHK_*`:
  長さ 0 か `MAX_IMAGE_SIZE` 超 → `SIZE`、開始クラスタが [2, MAX_CLUSTER) の外 (0・1・予約・不良・EOC 0FF8h〜を含む) →
  `START` (文言 `bad start cluster`、途中の範囲外と別)、必要な数より前に EOC → `SHORT`、途中が範囲外 → `RANGE`、
  必要な数を辿った次が EOC でない (循環・長すぎる) → `LONG`。範囲外なのに欄が EOC の場合も開始と途中で区別する。
  EBX・EDX・ESI を保つ。上限ちょうど (508KiB) は通る。
- `fat_chain_check` と `fat12_next32` は 32 ビットのレジスタと番地だけで書く (実モードでは 66h/67h 前置で同じ意味、番地は
  6000h + 最大 4.3KB で 64KB を越えない)。ホスト試験は同じソース (`FAT_HOST_BEGIN`〜`END`) を bits 32 で組み、2HD と 1.44MB の
  両方の `MAX_CLUSTER` で回す。
- ローダは 1 クラスタを超えてよい (IPL がチェーンで読む)。images/ の FAT でチェーンが正しいことまでを試験が見る。
- 試験: `make check-vk32-crc-host` (`test_vk32_crc.py (旧版 787690e6f03c)` の `fat`・`corrupt`・`real`)。
