# include/ — 設計
> 状態: **現行 (2026-10-31)** — 出所: DEP_DIR_A_DESIGN・MEMMAP_V3・DEP_DIR_B_DESIGN・DEP_DIR_D2_DESIGN・DEP_DIR_D3_DESIGN・DEP_DIR_D_DESIGN・SLIM_KERNEL_DESIGN・VER_INFO_DESIGN・DESIGN (D1 で移した)
ソース: `include/`

### dma_contract

DMA_XFER_MIN/MAX_BYTES、DMA_BANK_SIZE/MASK/SHIFT、DMA_PHYS_LIMIT と純粋判定の宣言
8237のポート/チャネル/待ち定数は drivers に残す。

```c
/* dma_pool.h / dma8237.h は dma_contract.h を読む */
```

DMA プールは 64KB 整列、ISA の 16MB 未満・境界は割当条件 (`dma_alloc(size, align, limit)`)

### pcm_contract

`include/pcm_contract.h`
利用側が要る型/公開状態値/関数宣言だけ
ハードレジスタ・PCMシーケンサ内部構造は移さない。

pcm_present/reclaim/open等、

### fm_contract

`fm_contract.h`
利用側が要る型/公開状態値/関数宣言だけ

FMの常駐facade宣言を共通契約から供給。

### lan_contract

`lan_contract.h`
利用側が要る型/公開状態値/関数宣言だけ

E82557状態値・診断型は名前と値を維持してlan契約へ。

### ide_contract

**96BのIdeInfo** とその寸法定数/既存エラー/選択番号定数
ドライバ・platform・生成KAPIが共用。

### platform_host

```c
i32 platform_host_open(const char *req, u32 len, int owner);
i32 platform_host_status(i32 h, u32 *status, u32 *length, int owner);
i32 platform_host_read_stage(i32 h, u32 cap, const u8 **out, int owner);
i32 platform_host_write(i32 h, const void *buf, u32 len, int owner);
i32 platform_host_close(i32 h, int owner);
void platform_host_owner_exit(int owner);
```

### kcg_contract

KCGの共通宣言を `include/kcg_contract.h` に分離
読み出しKAPIの寸法定数も同契約へ。

### pci_dev_contract

`include/pci_dev_contract.h` (struct pci_dev と PCI_DEV_STRUCT_SIZE などの公開定数だけ、drivers/pci/pci.h が読む)

### pci_bind_contract

`include/pci_bind_contract.h` (struct pci_driver・struct pci_bind_info などの bind と診断の型、drivers/net/e82557/lan_module.c・platform/pc98 の機種の配線・kapi の診断が読む)

### platform_sys

内部、リンク時固定、状態なし

```c
void platform_reset(void);       /* reset I/O の3書込みだけ、戻る場合あり */
void platform_buzzer_on(void);   /* BSR_BUZ_ON の1書込み */
void platform_buzzer_off(void);  /* BSR_BUZ_OFF の1書込み */
```

- 呼出文脈・IF は呼出元のまま。reset/beep に CLI/STI、待ち、再試行、tick 依存、PIT 再設定を加えない。

### platform_irq

kernel が必要とする状態/操作だけ

```c
int platform_irq_slave_last_in_service(void); /* ISR bit7、読後は IRR に戻す */
void platform_irq_eoi_master(void);           /* master だけ EOI */
int platform_irq_timer_pending(void);         /* master IRR bit0、L13 が使用 */
```

### platform_timer

一時値、継続状態ではない

```c
struct platform_clock_value { unsigned long hz; u8 is_8mhz; };
struct platform_timer_sample { unsigned int count; int p1, p2; };
void platform_clock_read(struct platform_clock_value *out);
void platform_timer_program(unsigned int reload); /* mode2→LSB→MSB */
void platform_timer_sample(struct platform_timer_sample *out); /* 呼出側 IF=0 */
```

### pointer_service

```c
void pointer_register(const PointerOps *ops);
typedef struct PointerOps {
    void (*poll)(MouseState *);
    int (*available)(void);
    void (*bounds)(i16, i16, i16, i16);
    void (*cursor_mode)(int), (*cursor_show)(void), (*cursor_hide)(void);
} PointerOps;
```

- `MouseState`（i16×4+u8×2=10B）、ボタン・モード・カーソルの公開値は `include/pointer_service.h` へ。ドライバ内部のバスマウス/NP21操作は移さない。

### port_service

```c
void port_register(const PortOps *ops);
typedef struct PortOps {
    void (*init)(u32), (*puts_kapi)(const char *);
    int (*getchar_kapi)(void), (*putchar_kapi)(char);
    int (*trygetchar)(void), (*is_initialized)(void), (*init_vfast)(u32);
    int (*get_status)(u32 *, u32 *, u32 *);
    void (*diag)(u32 *, u32 *, u32 *, u32 *);
} PortOps;
```

### kbd

```c
/* include/kbd.h: 共通キー値・SHIFT_*・RAW_*のみ。driverをincludeしない。 */
```

### bootinfo

```c
void bootinfo_disk_register(const struct boot_disk_ops *ops);
```

### platform_emergency

登録関数・構造体なし。

```c
void platform_boot_puts(int x, int y, const char *s, u8 attr);
void platform_fault_cell(u32 pos, u16 code, u8 attr);
```

- 停止ループ、DFのpolled serial、V86の停止前診断とカウンタ、例外時の表示行・0x07/TATTR_REDは一切変更しない。
`kernel.c`の全tvram_print呼出しは同じ名前で固定口に到達。

### console_attr

```c
u8 con_backend_printf_attr(u8 input); /* 純粋変換、通常raw属性とは別 */
```

- raw_attrは無変換。kprintfだけ従来どおり変換してからsink等へ渡す。shell入力や障害属性を二重変換しない。

属性の口の宣言は **include/console_attr.h** に置く
lib/kprintf_attr.c は `#include "console_attr.h"`。
kernel/console/console_backend.h は L16b の grid・カーソルの口だけを持ち、属性の口は include/console_attr.h を読む。

### platform_display

KCGのハード操作だけ

```c
void platform_display_kcg_code(void);
```

### platform_splash

登録/opsなし、起動前景専用。

```c
int  platform_splash_begin(void); /* 0=描画可、失敗はshutdown済み */
void platform_splash_clear(u8 color);
void platform_splash_fill_rect(int x, int y, int w, int h, u8 color);
void platform_splash_clear_pages(void);
```

### kbd_inject

drop 観測変数の既存実体はカーネルに残し、宣言を `include/kbd_inject.h` に置いて GETTER/SETTER を生成する。

### config

| 候補 | 決定と理由 |
|---|---|
| include/config.h の SYS_VERSION | 採用。機械が読む唯一の製品版。確定値は `3.0-dev`（§8） |
| VERSIONS.md | 系列・リリース判断の説明。製品版文字列の抽出元にせず、SYS_VERSION を参照すると明記 |
| ビルド生成 | 製品版については不要。config.h への通常の依存でカーネルを再構築 |
| git タグ／describe | リリース照合には使えるが、浅い clone・タグなしで表示が変わるため値の供給元にしない |

版の表記は **`3.0-dev`**

### wab_xe10

- 制御は 2 段階 I/O: ボード型は **0FA2h (INDEX) / 0FA3h (DATA)**、内蔵型は **0FAAh / 0FABh**。
  レジスタ `00h` = ID 読み出し (内蔵: Xe=58h, Cb=59h, Cf=5Ah, **Xe10=5Bh**, Cb2=5Ch, Cx2=5Dh,
  Be 系=50h; ボード: PC-9801-96=60h など)、`01h`/`02h` = VRAM 窓 / MMIO 窓アドレス
  (80h=F20000h, A0h=F00000h, C0h=F40000h, E0h=F60000h; MMIO は F80000h〜F98000h)、
  **`03h` bit1 = 映像出力リレー (1 = アクセラレータ出力, 0 = 本体スルー)**、bit0 = MMIO 有効。

4. **定数は [C4] の三層方式**: ポート・ID・窓アドレスはボードごとのヘッダに集め、
   資料 (`C:\WATCOM\docs\undocumented\io_wab.md` 等) の出典をコメントで示す。

## 索引

- [include/clock_service.h](clock_service.md)
- [include/console_output.h](console_output.md)
- [gf_production.md](gf_production.md)
- [include/gfx_format.h](gfx_format.md)
- [include/gfx_hal.h](gfx_hal.md)
- [include/gfx_mode.h](gfx_mode.md)
- [include/hwdev_private.h](hwdev_private.md)
- [include/input_service.h](input_service.md)
- [io.md](io.md)
- [include/kselftest_probe.h](kselftest_probe.md)
- [include/memmap.h](memmap.md)
- [include/pegc.h](pegc.md)
- [include/platform_device.h](platform_device.md)
- [include/platform_memory.h](platform_memory.md)
- [video_service.md](video_service.md)
