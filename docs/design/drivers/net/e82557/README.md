# drivers/net/e82557/README — 設計
> 状態: **現行 (2026-10-31)** — 出所: DEP_DIR_A_DESIGN (D1 で移した)
ソース: `drivers/net/e82557/README`

### lan_module

```c
/* lan_start_module: candidate検査完了後、現位置で実行 */
const struct pci_driver *const drivers[] = { platform_lan_driver() };
pci_bound = 1;
pci_bind_all(drivers, sizeof(drivers) / sizeof(drivers[0]));
```

- lan_module_probe の状態検査→REGISTRATION参照→TICK参照→candidate.probe→DECLINE時の回収/隔離を不変にする。表からmod内アドレスを直接指さない。

| ファイル | 登録 |
|---|---|
| `drivers/net/e82557/lan_module.c` | `lan_module_load()` の先頭、同様に e82557 façade を登録 |

- tick_register が失敗したときの load の戻り値は各部品の既存の初期化失敗の値 (FM/PCM/LAN の MODULE_INIT_FAILED 相当、今の load の失敗の経路と同じ)。
