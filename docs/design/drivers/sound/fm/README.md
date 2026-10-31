# drivers/sound/fm/README — 設計
> 状態: **現行 (2026-10-31)** — 出所: DEP_DIR_D1_DESIGN (D1 で移した)
ソース: `drivers/sound/fm/README`

### fm_module

| ファイル | 登録 |
|---|---|
| `drivers/sound/fm/fm_module.c` | `fm_module_load()` の先頭で `tick_register(PC98_TICK_SND, snd_tick)`。失敗なら既存の負値経路で load を中止 |

- tick_register が失敗したときの load の戻り値は各部品の既存の初期化失敗の値 (FM/PCM/LAN の MODULE_INIT_FAILED 相当、今の load の失敗の経路と同じ)。
