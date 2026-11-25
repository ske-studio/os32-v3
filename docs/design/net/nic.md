# sys/net/nic.c — 1 枚の NIC を link と LAN 版 SFS で共用する (選択・寿命・受信の振り分け)

> 状態: **現行 (2026-11-18)** — 出所: NET_HOSTSVC_DESIGN・TASK_NET_HOSTSVC (要約して移した)
ソース: `sys/net/nic.c`・`sys/net/nic.h`・`sys/net/nic_rx.c`・`sys/net/nic_rx.h`・`sys/net/rx_work.c`・`sys/net/rx_work.h`・`sys/dev/net/ne2000/nic_lgy98.c`・`sys/dev/net/ne2000/lgy98.c`・`sys/dev/net/e82557/nic_e82557.c`・`sys/dev/net/e82557/lan_module.c`・`sys/dev/net/e82557/lan_module.h`・`tools/lan_bridge.py`

全体の形は `docs/design/net/README.md` の「Host Services の LAN の経路」。

## 何をするか

- NIC を受信するのはここだけ。link (`sys/net/link.c`) と LAN 版 SFS (`sys/fs/serialfs/sfs_lan.c`) は NIC を直接読まず、`net_rx_take()` で自分の待ち行列から取る。
- 送信は `net_tx()` (Ethernet のフレーム全体、FCS なし)。待たない。0 = 受理、`AGAIN` = 後でもう一度、未 bind は `HOSTDOWN`。

## 装置の選択と寿命 (`sys/net/nic.c` が持つ)

| 口 | 契約 |
|---|---|
| `net_adapter_register(id, adapter)` | 検出した装置だけが登録する。LGY-98 は `lgy98.c` (vmkernel、`nic_lgy98.c` も常にビルドし、使うかは実行時に LGY が見つかったかで決まる)。82557 は `lan_module.c` (lan.mod)。同じ id に別の adapter は `BUSY` |
| `net_start(choice, peer)` | `NET_NIC_AUTO` は 82557 が登録されていれば 82557、無ければ LGY-98。明示した装置が無ければ `NOSYS`。`acquire` は task で呼び、82557 は `lan_module_session_acquire()` で lan.mod の参照を握る。同じ装置・同じ peer での 2 回目は既存のハンドシェイクを保って 0、違えば `BUSY`。peer=NULL なら相手の選別は各プロトコルに任せる |
| `net_bind(ops)` | 起動中に 1 回。`link_init()` をここで呼び、tick (`net_jiffy`) は常駐で 1 回だけ登録する。`link_init` 自身は tick を登録しない (二重登録で link が初期化されなくなるため) |
| `net_unbind()` | 受信の処理中なら遅らせる。`bound` を下ろしてから両方の待ち行列を捨て、最後に task で `release`。tick は登録したまま `bound` で門を閉じる |

- 起動時の選択の上書き (82557 と LGY-98 を設定で選ぶ) は実装していない。結線 (`sys/platform/pc98/kernel_boot_memory.inc` の `kernel_boot_hostsvc`) は `NET_NIC_AUTO` だけを使う。
- vmkernel は lan.mod の記号とリンクできない。82557 の adapter と netcd の登録口は `LanModuleApi` の表の欄 (`adapter`・`netcd_register`) を通す。表を変えたら `LAN_MODULE_API_SLOTS`・`LAN_MODULE_OPS_VERSION` を上げる。

## 受信の振り分け (`sys/net/nic_rx.c`)

- 待ち行列はプロトコルごとに 4 枠 × 1514B (FCS なしのフレーム全体) を静的に持つ (`NET_RX_SFS`・`NET_RX_LINK`、EtherType ではない論理の番号)。
- `net_rx_demux_budget()` の判定の順: Ethernet の長さ → EtherType 0x88B5 → 固定 peer なら送り手 → 宛先 (自分宛て、または broadcast) → payload の先頭 3 バイト。
- **SFS かどうかは 3 バイト全体 (`05 53 46`) で決める。** link の EOF (op=5) と SFS の ENQ (0x05) は同じ値なので、先頭 1 バイトで判定してはいけない。ホスト側の `tools/lan_bridge.py` も同じ 3 バイトで SFS を link へ通さない。
- broadcast を受けるのは link の HELLO だけ。SFS は自分宛てだけ。
- link の待ち行列が満杯の間はフレームを NIC に残す (link は毎 tick で取り出すので、ACK や応答の連続を捨てない)。SFS の待ち行列は読み手がいないと満杯のままになり得るので、満杯なら新着を捨てて `net_rx_sfs_dropped` に数える。
- `net_rx_take()`: 空なら 1 回だけ受信を進めてから取る。0 / 空 `AGAIN` / 容量不足 `NOSPC` (`*len` に要る長さ、消費しない) / 不明なプロトコル `INVAL`。`link_tick()` は呼ばない。
- `net_rx_reset(protocol)` は指定した行列だけを捨てる (受信の処理中なら終わってから)。

## 受信を誰がいつ進めるか

- 主は前景の協調点: `io_work` の口 `IO_SVC_NET_RX` (`net_rx_idle`) が、時間の予算 (`NET_RX_WORK_LIMIT_US`) と件数 (`NET_RX_SCAN_MAX`=8) の範囲で `poll` → 振り分け → `link_tick()` を進める。
- IRQ0 の `net_jiffy()` は `net_tick()` で poll と受信の予約をし、`link_tick()` を回す。前景が 2 jiffy 以上止まっていて IRQ0 の予算が残っている時だけ、IRQ の中でも振り分けを進める (救い)。
- 再入は `ticking` で防ぐ。処理中に来た要求は `poll_due`・`link_due` に残し、終わりの `rx_finish()` でまとめて 1 回だけ行う。82557 の回復の tick を共通の poll で二重に行わない。
- 前景の予算と Tier の扱いは `docs/tasks/v3/TASK_IO_TIER.md` と `docs/tasks/network/TASK_LAN_BULK.md` の側で変わる。

## 確かめ

- `tools/tests/test_hostsvc_nic.py`・`test_hostsvc_nic_rx.py` (3 バイトの判定、EOF=5 を SFS にしない、片方の満杯が他方を止めない)。
- `test_hostsvc_link_core.py`・`test_hostsvc_lgy98.py`・`test_hostsvc_lgy98_attach.py`・`test_hostsvc_e82557.py` (装置の登録と選択、LGY の無い機械で 82557 の lan.mod の輸入が解決する)。
- 共通の土台は `tools/tests/hostsvc_fixture.h` (libc なし ILP32、偽の adapter・時計・IF)。
