# kdebug/gfinit_resources/scan.inc — 設計
> 状態: **現行 (2026-10-31)** — 出所: BARLEN_DESIGN.md・REALHW_RECOVER_ADDENDUM.md (D1 で移した)
ソース: `kdebug/gfinit_resources/scan.inc`

## BARLEN_DESIGN

| 実物と設計上の扱い |
| --- |
| bus 0 の 32 device × 8 function を生読み。boot cache は使わない |

> [要確認] 現行の scan.inc は resource_function_count で function 数を決める。原文の 32 device × 8 function の固定走査とは異なる。

- 資源収集中は yield せず、装置の追加・再割当てもしない。PCI config の index/data 排他は既存 API に任せる。

```c
/* gfinit_resources のcount=0設定後、既存の除外収集(第2走査)より前 */
struct resource_bounds bounds = {0};
if (resource_collect_bounds(&bounds)) return -1;
/* gfinit_resources.c:81 のループ内、従来の無条件拒否を置換 */
if (raw && !(raw & PCI_BAR_SPACE_IO)) {
    if (resource_memory_bar(exclude, &used, &bounds, id, dev, fn, header, bar, raw))
        return -1;
}
/* gfinit.c:238 の収集呼出しと同:222 の recover_bar_probe 呼出しは変更なし。 */
```

ROM enable、

## REALHW_RECOVER_ADDENDUM

- 他装置・bridge 窓照合の採取担当は4a (A1)、判定担当はrecover。4aはPCI走査でGPU自身を除く装置の有効なmemory BAR範囲と、GPUの上位経路外のbridgeの有効なmemory窓を排他範囲表へ、上位経路の各bridgeのGPU BAR種別に対応する転送窓を包含範囲表へ収める。上位窓は重複禁止範囲には入れない。

- 4aはA1のprobe開始前に完全な走査結果を用意し、全probe終了まで表とPCI割当てを不変に保つ。窓欠落・走査/長さ取得失敗・32bit範囲で表現不能・件数超過は省略せずA1失敗とし、候補/cacheを無効化する。単一タスク前提で再走査・ロックは不要。

- A1の資源採取はGPU直結bus0の全functionを生configで読み
