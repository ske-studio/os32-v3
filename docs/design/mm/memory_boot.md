# mm/memory_boot.c — 設計
> 状態: **現行 (2026-10-31)** — 出所: DEP_DIR_C_DESIGN (D1 で移した)
ソース: `mm/memory_boot.c`

|状態・値|所有者|文脈|
|---|---|---|
|boot_high_end / boot_ram_kb / boot_memory / meta境界|mm/memory_boot.c、従来のstaticを維持|detect/initの既存位置|

```c
/* mm/memory_boot.c: 公開名と戻り値契約を保存 */
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
