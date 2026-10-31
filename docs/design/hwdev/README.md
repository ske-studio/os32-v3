# hwdev の契約

kdebug・hwdev が読むヘッダには enum を足さず `#define` で書く。`tools/gen_hwdev_contract.py` が列挙子を契約に数えるため。

## 移し替えた設計


### owner

```c
struct hwdev_lease { u32 slot, generation, session; };
```

> [要確認] 現行 owner.h は slot でなく owner_generation と active を持つ。

- v1 の所有権は BDF/platform device ID・I/O/MMIO/IRQ/DMA 範囲・世代の構造体の形と拒否判定だけ。root IDE・LAN/SerialFS・表示 driver の所有中または未分類なら拒否
- v1 claim は「既に停止・非所有」だけ成功。透過的 driver 中断は作らない。

> [要確認] 本番 GF 使用中の拒否条件は現行の判定門と照合する。

- v1 の所有入力は `hwdev/owner.h` の BDF/platform ID・I/O/MMIO/IRQ/DMA 範囲・所有世代と分類。実装済み本番 GF driver がない固定 0:13.0 だけを停止・非所有と分類し、IDE・LAN/SerialFS・表示所有中・未分類は試験で注入して拒否する。

- lease は常駐 GF control と同じ boot body に置き、単調増加する claim 世代・所有世代・session を作業領域の世代と照合する。

### hwdev_main

- KMOD export の `initialize` は `int (__cdecl *)(const struct hwdev_manifest **manifest, const struct hwdev_ops **ops)`。slot 充填後に一度呼び、静的な RO の2ポインタを返すだけ。登録・確保・I/O・core 呼出しをしない。失敗時は公開しない。manifest/ops の全 span が当該 body の RO 内、call/release_work が text 内であることを facade が checked 範囲検査する。

- M3a の body は `gfinit_call` と `gfinit_recover_end` を call/release_work に型照合して結ぶ。

- 一つの `/boot/hwdev.mod`、初版は GF のみ、boot 中交換不可。
