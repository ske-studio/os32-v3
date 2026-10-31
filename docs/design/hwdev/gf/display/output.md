# hwdev/gf/display/output.c — 設計
> 状態: **現行 (2026-10-30)** — 出所: GF_DRIVER_DESIGN (2026-10-29 確定、D0 で移した)
ソース: `hwdev/gf/display/output.c`

| 機能・ファイル語幹 | 中身・初版の境界 | nouveau（`N/` 以下） | VBE 対応 |
|---|---|---|---|
| GF 出力選択 `output` | head A/OR0、DACCLK・TEST、他経路を停止 | `dispnv04/dac.c:375–409`、`dfp.c:93–110` | Core の独立機能無し |

## 移し替えた設計

stateのLCD route解除

選択DACCLK=head<<8\|1、他ANALOGを反対headへ、NV44AのTEST_CONTROL=00100000h

state.FP=20000022、CR33=s&~3B

68052C=1→68252C=(r&~100)\|100→680608=00100000

68050C=10000204→680524=s&~00050000→680600=20100130
