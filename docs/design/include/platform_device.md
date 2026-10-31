# include/platform_device.h — 設計
> 状態: **現行 (2026-10-31)** — 出所: DEP_DIR_A_DESIGN (D1 で移した)
ソース: `include/platform_device.h`

```c
/* include/platform_device.h: 以下は代表、全宣言は元と同じ型で列挙 */
void platform_ide_init(void);
int platform_ide_read_sector(int drv, u32 lba, void *buf);
int platform_ext2_format_at(int drv, u32 start_lba, u32 length);
/* include/platform_device.h、slot・型・NULL出力の意味は元と同一 */
int platform_loop_dev_get_geometry(int slot, u16 *c, u8 *h, u8 *s, u16 *bps, u32 *total);
int platform_loop_dev_read_chs(int slot, u16 c, u8 h, u8 s, void *buf);
int platform_loop_dev_read_lba(int slot, u32 lba, u32 count, void *buf);
int platform_loop_dev_attach(const char *path, int slot);
void platform_loop_dev_detach(int slot);
int platform_loop_dev_status(int slot, u32 *total, int *bps);
struct platform_disk_geometry {
    u32 total_sectors;
    u16 cylinders, heads, sectors, phys_sector_size;
};
int platform_ide_drive_present(int drv);
int platform_ide_range_ok(int drv, u32 lba, u32 count);
int platform_disk_geometry_get(int drv, struct platform_disk_geometry *out);
int platform_ext2_format(int drv, u32 sectors);
int platform_serialfs_session_begin(void);
int platform_serialfs_session_end(int exit_code);
/* 登録は従来のVFS登録表のみ。上の関数は登録せず直接リンクする。 */
/* L08: include/platform_device.h + pci_dev_contract.h / pci_bind_contract.h */
int platform_pci_count(void);
int platform_pci_get(u32 idx, void *out);
u32 platform_pci_cfg_read32(u32 bus, u32 dev, u32 fn, u32 reg);
int platform_pci_bind_info_get(int idx, struct pci_bind_info *out);
```
