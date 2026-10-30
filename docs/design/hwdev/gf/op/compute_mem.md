# hwdev/gf/op/compute_mem.c — 設計
> 状態: **現行 (2026-10-30)** — 出所: GF_DRIVER_DESIGN (2026-10-29 確定、D0 で移した)
ソース: `hwdev/gf/op/compute_mem.c`

| opcode・命令名（最終ファイルの語幹） | nouveau の対応 |
|---|---|
| 63 COMPUTE_MEM | I:1459 `init_compute_mem`、NV44A では下記の no-op |

COMPUTE_MEM の no-op は省略ではなく NV44A の仕様分岐。`N/nvkm/engine/device/base.c:677`→`nvkm/subdev/devinit/nv1a.c:29` に meminit が無い（`D/GF6200_4A_DESIGN.md:49`）。
