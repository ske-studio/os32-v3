# platform/pc98/platform_memory_desc.h — 設計
> 状態: **現行 (2026-10-31)** — 出所: DEP_DIR_C_DESIGN・MEMMAP_V3 (D1 で移した)
ソース: `platform/pc98/platform_memory_desc.h`

|geometry / attributes|platform/pc98/platform_memory_desc.h の static const、確保なし|登録前から参照可能、IRQ操作なし|

```c
static const struct platform_memory_geometry platform_memory_geometry = {
    MEM_SYSTEM_SPACE_BASE, MEM_SYSTEM_SPACE_BASE, MEM_SYSTEM_SPACE_END,
    MEM_HIGH_RAM_BASE, MEM_PHYS_RAM_CEILING
};
static const struct platform_memory_attributes platform_memory_attributes = {
    { {TVRAM_CHAR_BASE, TVRAM_CG_WINDOW},
      {GVRAM_PLANE_B, GVRAM_BRG_END}, {GVRAM_PLANE_I, GVRAM_I_END} },
    {MEM_BIOS_ROM_START, MEM_BIOS_ROM_END + 1},
    {MEM_CONV_END, MEM_BIOS_ROM_END + 1} /* bios_rom.endと同じ */
};
```

15〜16MB は既定で予約

### 3-3. 15〜16MB (D6)

既定で予約。セットアップで切り離した機 (資料 `io_mem.md`) でも RAM として配らない。地図検査は「登録の無い部分は NP、MMIO 登録 (PEGC リニア窓) は sup+PCD」で見る (§2-3 ⑥)。
