# sys/platform/pc98/video/cirrus/cirrus_identify.c — 設計

> 状態: **現行 (2026-11-18)** — 出所: TASK_S5 (§6 の R2 の観測) の契約を今のソースと照らして要約
ソース: `sys/platform/pc98/video/cirrus/cirrus_identify.c` (`cirrus_win_usable`・`cirrus_identify`)・`sys/platform/pc98/wab_xe10.h`

ポートを叩かずに、Cirrus (Xe10) の窓を張ってよいかを決める。Cirrus は NP21/W 互換のためだけに持つ。

## 窓を張ってよいか (`cirrus_win_usable`)

- 窓 `[base, base + size)` にかかるページを 1 枚も落とさずに、ブート時に凍結した物理地図 (`pgalloc_range_has_ram`) へ問い合わせる。RAM が 1 ページでもあれば張らない (自分の RAM を隠す)。
- **RAM の上端 (`sys_get_mem_kb`) では決めない**。15MB 機 + 高位 RAM (NP21/W `ExMemory 16`) の上端は 17,408KB で、15-16MB の穴の中のバンク窓 F60000h まで RAM と誤判定する。
- 大きさ 0 と、32bit の物理空間の末尾を越える窓は拒む。4GB ちょうどで終わる窓は可 (末尾は inclusive で持ち、桁あふれを作らない)。
- pgalloc が未初期化なら RAM あり扱い (張らない)。

## 候補か (`cirrus_identify`)

1. バンク窓 (`WAB_XE10_WIN_BASE`、F60000h) が張れること。
2. リニア窓 (`WAB_XE10_LINEARWIN_BASE`) が張れること。張れないと CPU 描画面を出せないので PEGC・9801 に譲る。
3. リニア窓の先頭がページ境界で、使う VRAM (表示面 + 非表示面 + パターン) が窓に収まること。

## リニア窓の置き場 (`sys/platform/pc98/wab_xe10.h`)

- 番地は v3 のデバイス窓の帯 (`sys/include/memmap.h` の `MEM_DEVICE_APERTURE_*`、物理地図で MMIO) の先頭。`WAB_XE10_LINEARWIN_SEL` = 帯の先頭 >> 24 = FEh で、窓は FE000000h。使うのは 2MB、窓として出る 4MB (`WAB_XE10_LINEARWIN_DECODE`) が帯と帯の静的 PT に収まることは `sys/platform/pc98/video/cirrus/wab_glue_xe10.c` の STATIC_ASSERT が見る。
- 旧番地 01000000h は 16MB を超える RAM と重なるので使わない。

## 検査

`test_cirrus_win.py (旧版 c6379f3b83c7)` (本体 `cirrus_win_host.c (旧版 4807cc191f12)`、実物の `cirrus_identify.c`・`backend_cirrus.c` を include し、物理地図を贋物にする)。見る構成:

| 構成 | 期待 |
|---|---|
| 15MB + 高位 1MB (上端 17,408KB) | バンク窓 F60000h も PEGC の窓 F00000h も張れる |
| NP21/W 17MB・実機 64MB | 旧番地 01000000h は拒み、FE000000h は張れる。窓の末尾 1 ページだけの RAM でも拒み、窓の直前・直後の RAM では拒まない |
| 15-16MB が RAM | バンク窓は拒む (先頭 1 ページだけでも) |
| 8MB 機 | 両方張れる |
| pgalloc 未初期化 | 張らない |
| 大きさ 0・末尾越え・回り込み | 拒む |

`--mutate` は旧判定 (RAM の上端) に戻す・物理地図を見ない・問い合わせの末尾ページを落とす・窓の前の 1 ページまで問い合わせる・末尾越えを見ない・リニア窓を 01000000h へ戻す、などがすべて落ちることを見る。
