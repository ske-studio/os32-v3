# kdebug/kdebug_main.c — 設計
> 状態: **現行 (2026-10-31)** — 出所: GF6200_4A_DESIGN.md・GF6200_RECLOG_DESIGN.md・GF6200_S7_SHELL.md・HWDEV_CORE_API.md・HWDEV_DESIGN.md・RECOVER_ROUTE_TEST.md・RECOVER_T4B_INJECT.md (D1 で移した)
ソース: `kdebug/kdebug_main.c`

## GF6200_4A_DESIGN

| 本設計での扱い |
| --- |
| 既存 KAPI を使う専用 op を追加。新しい KAPI スロットは不要 |

- shell `gfinit` → `kdebug_call(op,&req,sizeof req)` → dbg_call の振り分け → gfinit。`peek` の op・構造・読み取り専用契約は維持する。

## GF6200_RECLOG_DESIGN

```c
/* kdebug_main.c: recovery_tx_ready/put は生 rc をそのまま返す（現行通り）。
 * recovery_transport.uart.wait_frame = recover_hal_wait_frame の接続も維持。
 * recovery_tx_end は flush の記録後にも必ず serial_checked_end() を呼ぶ。 */
```

## GF6200_S7_SHELL

- `kdebug/kdebug_main.c:105-123` はGFを一般PAUSED門より先にhwdevへ渡す。GF自身のsession・状態・main順序検査を維持する。

## HWDEV_CORE_API

| 境界 | 契約 |
| --- | --- |
| kdebug import 268 | `const struct hwdev_ops *hwdev_module_ops(void)` (`kernel/module/hwdev_module.h`)、S5 の末尾追加。既存 0〜267 は不変、private import 契約 CRC は生成器で更新 |
| op 5〜10 | 公開済み ops を取得し、未読み込み・非公開なら `-1`、要求を書き換えず recovery_attach も実行しない。取得後は既存の attach と 6 引数を維持して `ops->call` |
| op 3/4 | S0 の attach・recover と結果を維持。op 4 の recover が成功または状態遷移した後だけ、公開済みなら `ops->release_work`。部品なしでは解放呼出しなし |

## HWDEV_DESIGN

| 実物・根拠 (file:line) | 確認したこと |
| --- | --- |
| `kdebug/kdebug_main.c:100` | dbg_call が recover と gfinit を振り分け、外部 recover 後に GF 所有領域を解放 (:115)。単なる表の移動では済まない |

## RECOVER_ROUTE_TEST

- `kdebug_main.c` に `recovery_route_write(void *, u32)` を追加しrecovery_opsへ接続；値がRECOVER_ROUTE_98以外なら拒否。

- outpはこのcallbackだけに置き、`outp(RECOVER_ROUTE_PORT, RECOVER_ROUTE_98)` を1回。read-modify-writeや上位bitの再生はしない。

- op 12 の session・IDLE・接続済み ops の検査は attach より前に行い、未接続時だけ既存 attach で実機 S0 ops を接続する。拒否時は detach/release_work も行わない。

## RECOVER_T4B_INJECT

| 実物 | 確認と今回埋める境界 |
| --- | --- |
| `kernel/module/kdebug_call.c:8`、`kdebug/kdebug_main.c:102,110`、`userland/shell/cmd_recover.c:9` | kernel→既存dbg_call。GF分岐後にPAUSED門、shellはstatus/now（op 3/4）のみ |

- PAUSED 門は kdebug_main.c:110 の位置のまま。既存 now 例外に `op==11 && action∈{step,end} && 仮想owner` だけ追加（payload のサイズ・版を先に照合）。GF 5〜10 は従来どおり :105 で hwdev へ渡し、GF_OP_ABORT 等の許可表を kdebug に持ち込まない。

- GF_OP_ABORT は既存 GF 分岐・許可判定を維持し、ops->call の戻り直後に recover_virtual_detach を呼ぶ。仮想 trial が終端なら即座に借用を解除し、拒否・非終端なら借用を維持する。

- PAUSED の例外は 40B・版・reserved の検証後、仮想借用中の ops・session と step/end の一致で許可する。
