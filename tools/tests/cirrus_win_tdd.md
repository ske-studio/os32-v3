# cirrus_win — Cirrus の窓の可否を物理地図で決める (ホスト TDD の記録)

教訓: [docs/POLICY_DEBUG.md](../../docs/POLICY_DEBUG.md) §4-34 (デバイス窓は物理地図で判定、RAM の上端で判定しない)
観測: [docs/archive/settings/TASK_S5.md](../../docs/archive/settings/TASK_S5.md) §6 R2 (Cirrus)
実装: `gfx/backend_cirrus.c` の `cirrus_win_usable()`
試験: `tools/tests/cirrus_win_host.c` + `tools/tests/test_cirrus_win.py` (`make check-cirrus-win-host`)

## 欠陥

`cirrus_win_usable()` は窓を張ってよいかを `sys_get_mem_kb() > base / 1024` (RAM の**上端**) で決めていた。
K6-RAM 以後、15MB 機 + 高位 RAM (NP21/W `ExMemory 16`) の上端は 17,408KB で、15-16MB の穴の中にある
バンク窓 F60000h (15,744KB) まで「RAM が届いている」と判定され、`cirrus_probe()` はボードの ID 判定の前に 0 を返す。
PEGC で直した §4-34 と同じ形。

## 様式

`test_pgalloc_range.py` と同じ形: `cirrus_win_host.c` が実物の `gfx/backend_cirrus.c` を `#include` し
(1 行も写さない)、周りだけを贋物にしてホスト ILP32 GNU89 (`gcc -m32 -nostdlib`、`int 0x80`) で走らせる。
同じソースを `i386-elf-gcc -Werror` でもコンパイルする ([C1])。

- `pgalloc_range_has_ram` = **贋の物理地図** (RAM の span 表)。実物と同じく範囲異常・未初期化は 1。
- `sys_get_mem_kb` = 構成の上端 (旧判定を RED にするため)。
- `wab_glue_xe10` = 贋グルー (実物の窓の番地 `include/wab_xe10.h` を入れ、`probe` の到達を数える)。

## 2 段目: リニア窓の移設と NP21/W の門 (A2、ユーザー決定 2026-09-29)

1 段目 (上の修正) の後も、リニア窓 01000000h は 16MB 超の RAM と本当に重なるので probe は 0 のままだった。
ユーザー決定: **Cirrus は NP21/W 互換のためだけ**。リニア窓を v3 のデバイス窓の帯 `[0xFE000000, 0xFF000000)`
(`include/memmap.h` `MEM_DEVICE_APERTURE_*`) の先頭へ移す。

- `include/memmap.h`: 帯と `MEM_PHYS_RAM_CEILING` を定義。`kernel/memory_boot.c` は `[MEM_PHYS_RAM_CEILING, 4GB)` を MMIO にする。
- `include/wab_xe10.h`: `WAB_XE10_LINEARWIN_SEL` = 帯の先頭 >> 24 = FEh (NP21/W は FFh を捨てる)。窓として出る 4MB を `WAB_XE10_LINEARWIN_DECODE` に。
  帯と静的 PT に収まることは `drivers/wab_glue_xe10.c` の STATIC_ASSERT。
- `kernel/paging.c`: 帯の先頭 4MB の PT を静的に 1 枚 (+4KB BSS)。PDE は paging_init で present。
- `gfx/backend_cirrus.c`: 窓の上限を旧 PAGING_MAP_SIZE (32MB) から 32bit 空間の末尾へ。
  **段 2 の門**: auto では `np2_detect()` が真のときだけボードの ID を読む。`GFX=cirrus` の明示時は読む。
  auto の probe は 1 回ごとに np2_detect の検出通信 (07EFh へ `"NP2"` の OUT 3 回 + 応答の IN 最大 7 回) を 1 組**追加で**行う。
  叩くポートはマウスの初期化 (`drivers/mouse.c`) が毎回の起動で既に使っている 07EFh だけで新しいポートは無いが、
  実機でもこの回数ぶん I/O は増える (ボードの ID 0FAAh/0FABh を読まなくなる代わり)。

## 見ること

`cirrus_win_host.c` (実物の backend_cirrus.c):

1. 15MB + 高位 1MB (上端 17,408KB): バンク窓 F60000h は張れる。物理地図に問い合わせている。PEGC の窓 F00000h も同じ答え。
2. リニア窓の置き場: 番地は帯の先頭 (SEL = FEh)、4MB の窓が帯に収まる。NP21/W 17MB と実機 64MB のどちらでも
   旧番地 01000000h は拒み、新番地 FE000000h は張れる。窓の末尾 1 ページだけの RAM でも拒む。直前・直後の RAM では拒まない。
3. 15-16MB を RAM にした構成: バンク窓は拒む。窓の先頭 1 ページだけでも拒む。
4. 8MB 機: 両方張れる。
5. pgalloc 未初期化: 張らない。
6. 大きさ 0・32bit 空間の末尾越え・先頭側へ回り込む窓は拒む。4GB ちょうどで終わる窓と 32MB より上の窓は物理地図で決める。
7. probe の段: 17MB でも NP21/W なら ID 判定まで進む (本件の症状が消える)。auto で NP21/W でなければ
   NP21/W 判定を 1 回読むだけで ID は読まない。`GFX=cirrus` なら進む。バンク窓・リニア窓が RAM に当たる構成では
   NP21/W 判定も ID も読まない。

`app_band_pde_host.c` の H (実物の paging.c): 帯の PT が静的に在り PDE が present・全 PTE が Not-Present、
アプリ AS が居る間でも窓を張れる (対照: 静的 PT の無い次の PDE は張れない)、窓を張った後の AS は PDE ごと写し、
クライアント面だけが USER + PCD、表示面と master / 他のアプリの PDE は USER にならない、剥がしても PT と PDE は残る。
`memory_boot_host.c`: 帯は物理地図で MMIO、`pgalloc_range_has_ram` は「RAM 無し」。

## RED → GREEN

- RED (修正前の `gfx/backend_cirrus.c`、2026-09-29):
  `ASSERT FAIL: cirrus_win_usable(BANK, BANK_N)` / `EXIT cirrus_win_host=1` (1 の最初の確認で落ちる)。
- GREEN (1 段目の修正後): 7 段とも PASS、`EXIT cirrus_win_host=0`、`TARGET i386-elf GNU89 -Werror COMPILE PASS`。

## RED → GREEN (2 段目)

- app_band_pde の H: paging_init の PDE 登録を外すと `FAIL: page_directory[pdi] == ((u32)page_tables[pdi] | PAGE_RW)` (手で確認、元に戻した)。
- 変異 (下) は 9 本すべて RED。

## 否定側 (`--mutate`)

写しの木 (`mutpar`) で壊し、すべて RED になることを見る。

| 変異 | 壊し方 | 落ちた所 |
|---|---|---|
| `top_of_ram` | 旧判定 (`sys_get_mem_kb() > base / 1024`) に戻す | 1 (バンク窓) |
| `no_map` | 物理地図を見ない (常に張れる) | 1 (問い合わせの有無) |
| `end_short` | 問い合わせの末尾ページを落とす | 2 (末尾 1 ページの RAM) |
| `first_early` | 窓の前の 1 ページまで問い合わせる | 1 (F00000h、直前が RAM) |
| `no_wrap_check` | 32bit 空間の末尾越えを見ない | 6 (回り込む窓) |
| `no_np2_gate` | auto でも NP21/W 判定をせずに ID を読む | 7 |
| `gate_ignores_forced` | `GFX=cirrus` でも NP21/W でなければ試さない | 7 |
| `np2_before_window` | 窓の判定より前に NP21/W 判定を読む | 7 (RAM に当たる構成で判定を読む) |
| `old_linear_sel` (`include/wab_xe10.h`) | リニア窓を 01000000h へ戻す | 2 (番地)。カーネルでは glue の STATIC_ASSERT でも止まる |

## 範囲の外 (未検証)

- NP21/W 上での確認 (Cirrus 有効 ini、RAM 17,408KB) は未実施 (PM が手配)。FE000000h の窓が NP21/W で
  読み書きできることはソースを読んだだけ (`i386c/cpumem.c` の slow 経路が RAM より先に窓を判定、
  `[CPU_EXTLIMIT, 4GB)` は MMIO 登録)。
- 実機の Xe10 がレジスタ 02h に応えるかは資料に記述なし (Cirrus は NP21/W 互換のためだけ)。
