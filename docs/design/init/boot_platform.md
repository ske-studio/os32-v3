# init/boot_platform.c — 設計
> 状態: **現行 (2026-10-31)** — 出所: DEP_DIR_D3_DESIGN (D1 で移した)
ソース: `init/boot_platform.c`

```c
/* init/boot_platform.h: 内部専用。列挙値は下表の境目に1個ずつ。 */
struct boot_context { u32 drive; const char *root_dev, *root_fs; };
typedef void (*boot_step_fn)(const struct boot_context *);
struct boot_platform { boot_step_fn step[BOOT_POINT_COUNT]; };
void boot_platform_register(const struct boot_platform *ops);
void boot_platform_run(enum boot_point point, const struct boot_context *ctx);
/* boot_platform.c が const 表ポインタを所有。表本体は platform 常駐。 */
```

- 登録は起動時・IRQ 公開前、同じ const 表の再登録のみ許可。run は登録済み・全必須スロット有効を前提に bind の host 試験で検証。装置不在は各 callback が従来どおり扱う。稼働中の差し替え/解除はない。
- run は **IRQ handler 内ではない**。IF を共通 dispatcher で変えない。DMA は `_enable` 前、kbd 以後は現行の IF を引き継ぐ。FDC wrapper 内の `_enable()` を残し、IRQ11 は D1 の FDC 登録に任せる。
- 表の添字順に一括実行しない。kernel.c の元の各位置で一つずつ run し、その間の処理を残す。

- boot_context は kernel_main の生存期間全体で有効、他部品は保持しない。bootinfo の BIOS 写し `s_bootinfo` は引き続き bootinfo.c 所有。破損/欠落時も root を勝手に変えない。
