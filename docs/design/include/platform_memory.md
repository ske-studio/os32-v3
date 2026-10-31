# include/platform_memory.h — 設計
> 状態: **現行 (2026-10-31)** — 出所: DEP_DIR_C_DESIGN (D1 で移した)
ソース: `include/platform_memory.h`

内部専用、全区間は半開区間

```c
struct platform_memory_geometry {
    u32 legacy_end, system_first, system_end, high_first, ram_ceiling;
};
/* geometryの実体は下記の固定名ヘッダで定義 */
u32 platform_memory_probe_high(void); /* 確認済み高位RAMの終端PFN、無し=0 */
struct platform_memory_span { u32 first, end; };
struct platform_memory_attributes {
    struct platform_memory_span native_uc[3];
    struct platform_memory_span bios_rom;
    struct platform_memory_span low_device; /* 台帳FIXED/UC、PTEとは別 */
};
#include "../platform/pc98/platform_memory_desc.h" /* 型定義後、guard付き */
```

- 実装では3をヘッダの配列長定数に集約（新規番地定数は作らない）。PWT=0を維持し、WB/UCの再解釈をしない。
- 固定名ヘッダはplatform_io.hと同じ寄せ方で正典ヘッダを内部includeし、include/platform_memory.h経由で参照する。
.cに外部実体は置かない。
