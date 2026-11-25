# sys/kern/mm/memory_boot.c — 設計
> 状態: **現行 (2026-10-31)** — 出所: DEP_DIR_C_DESIGN (D1 で移した)
ソース: `sys/kern/mm/memory_boot.c`

|状態・値|所有者|文脈|
|---|---|---|
|boot_high_end / boot_ram_kb / boot_memory / meta境界|sys/kern/mm/memory_boot.c、従来のstaticを維持|detect/initの既存位置|

```c
/* sys/kern/mm/memory_boot.c: 公開名と戻り値契約を保存 */
u32 memory_boot_detect(u32 mem_kb)
{
    u32 end = platform_memory_probe_high();
    if (!end) return mem_kb; /* boot_high_endを新たに消さない */
    boot_high_end = end;
    return end * (PAGE_SIZE / KIB);
}
```

- 穴を全RAM供給源の一律拒否規則に変えない。高位attestationなしにローダ値だけからRAMを追加しない。UNKNOWNとRESERVED/MMIOを混同しない。

- kernel側でMEM_BIOS_ROM_END + 1 == KERNEL_LOAD_ADDRをSTATIC_ASSERTする（Cの定数式として正典マクロを用いる）。

- low_deviceはmemory_boot_fixedの機種行を置換。既存の低位WB行→機種UC行→残り固定領域の登録順を維持し、行数・型・owner・PFNを固定する。
- 台帳の背景2区間はL14のgeometry、cache/type/ownerは従来値。低位台帳UCをPTEへ流用しない。

## 16MB 超の RAM — 人為的な上限を置かない

> 出所: TASK_K6_ram_ceiling の契約を今のソースと照らして要約 (2026-11-18)

- 起動の順は `memory_boot_detect` → `paging_init` → `memory_boot_init` (`sys/platform/pc98/kernel_boot_memory.inc`)。検出が恒等写像の範囲と表の大きさを決めるので、この順を崩さない。`memory_boot_init` が失敗したら fail-stop で、`shm_init`・自己試験・`shlib_init` はその後。
- 高位 RAM (`MEM_HIGH_RAM_BASE` = 16MB より上) の量は `memory_boot_detect` が確かめた量 (`boot_high_end`) だけ。ローダの申告 `mem_kb` はヒントで、それだけでは 1 ページも高位 RAM に上げない (`mem_kb = 0xFFFFFFFF` でも昇格しない)。`physmem_add_trusted` を呼ぶ製品のソースは `sys/kern/mm/memory_boot.c` の 1 か所 (`PHYSMEM_SOURCE_MACHINE`) だけ。
- 登録の上限は `MEM_PHYS_RAM_CEILING` (2GB) だけで、それ以外に RAM の量を切り詰める定数を置かない。引くのは名前の付いた予約だけ:
  - `[MEM_SYSTEM_SPACE_BASE, MEM_SYSTEM_SPACE_END)` (15〜16MB のシステム空間) → RESERVED。RAM として 1 ページも配らない。
  - `[MEM_PHYS_RAM_CEILING, 4GB)` → MMIO (デバイス窓の帯 `MEM_DEVICE_APERTURE_*` と最上位の ROM・PCI の帯 `[MEM_PHYS_MMIO_TOP, 4GB)` を含む)。
- 表 (L1 の bitmap・owner・pin・恒等 PT と窓の PT の workspace) の大きさは検出量から出す (`memory_boot_table_pages`)。置ける場所は `[MEM_PHYS_WORKSPACE_FLOOR, 低位のアリーナの上端)` の低位 RAM だけで、`memory_boot_high_fit` は入る量まで PDE 1 枚 (`PTE_COUNT` ページ) 刻みで丸める:
  - 32MB・128MB 相当は丸めずに通す。
  - 丸めた量は置き場を超えず、PDE 1 枚ぶん増やすと超える (目一杯まで取る)。上限いっぱいの申告では 2GB 未満に収まる。
  - 置き場が無い構成は 0 を返し、高位 RAM を登録しない (量をでっち上げない)。
- 高位 RAM があっても低位の連続アリーナ (`physmem_legacy_end`、exec のレイアウト) は変わらない。`sys_usable_mem_end()` は workspace の下端のまま。workspace と metadata は `[MEM_PHYS_WORKSPACE_FLOOR, MEM_SYSTEM_SPACE_BASE)` の内側。ブート窓より上の恒等 PT は全部 workspace から取る。
- 検出量の最終ページまで確保・解放できる (`pgalloc_limit_pfn()` は検出量ちょうど、2GB を超える申告では丸めた量)。
- `memory_boot_ram_kb()` は登録した span の合計 (低位 + 高位)。上端 `sys_mem_kb` とは別で、高位 RAM のある構成では差が 15〜16MB の穴の 1MB になる。8MB 構成では一致する。
- デバイス窓を張れるかは「RAM の上端」でなく「窓に RAM が登録されているか」(`pgalloc_range_has_ram`、`docs/design/mm/pgalloc.md`) で決める。15MB・32MB 構成では上端が窓より上でも、システム空間は穴なので窓を張れる。

検査: `tools/tests/test_memory_boot.py` (`memory_boot_host.c`)・`tools/tests/test_physmem.py` (`test_high_ram_has_no_artificial_ceiling`)・`tools/tests/test_pgalloc_range.py`。`memory_boot_detect` の実ハードの動き (0594h の読み・書き込みの確認) はホストで確かめられない。
