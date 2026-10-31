# hwdev/gf/op/compute_mem.c — 設計
> 状態: **現行 (2026-10-30)** — 出所: GF_DRIVER_DESIGN (2026-10-29 確定、D0 で移した)
ソース: `hwdev/gf/op/compute_mem.c`

| opcode・命令名（最終ファイルの語幹） | nouveau の対応 |
|---|---|
| 63 COMPUTE_MEM | I:1459 `init_compute_mem`、NV44A では下記の no-op |

COMPUTE_MEM の no-op は省略ではなく NV44A の仕様分岐。`N/nvkm/engine/device/base.c:677`→`nvkm/subdev/devinit/nv1a.c:29` に meminit が無い。

## 移し替えた設計

| opcode / 名称 | 意味・OS32実装 | MIT根拠 |
|---|---|---|
| D2 | NV44A の 0x63 は明示の no-op。容量は PFB・BAR1・独立の VRAM 試験で照合。nv20 の容量探索を移植しない |

| 63 COMPUTE_MEM | 1B、force。NV44AはNOOP_NV4A（D2）、容量を設定しない | I:1459–1469 |


|op / 名前|長さ・復号配置（+offset）|実行substep順・args/kind・方向|辺・展開／skip・force|
|---|---|---|---|
| 63 COMPUTE_MEM | 1 | C`[]`（NOOP_NV4A） | force、skip保持、HWアクセス0、容量設定無し |
