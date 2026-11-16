# PERF_PROFILE_PLAN — 性能の段 (互換モードと Ra266 基準モード) のリファクタリング計画

> 状態: **計画 (2026-11-16)** — 未着手。ユーザーの依頼 (2026-11-16)「設計的に遅いデバイス基準で動いていますよね？ そこは互換モードとして機能させて ra266 程度の能力を基準として動くモードなどを用意するリファクタリング計画をドキュメント化しておいて」。設計の往復はまだで、着手のときに確定設計を `docs/decisions/` に起こす。
> 関係: [IO_TIER_DESIGN](../../decisions/IO_TIER_DESIGN.md) (予算)、HSYNC_MANIFEST_DESIGN (往復の数、`docs/decisions/network/`、main 入りの待ち)、[LAN_BULK_DESIGN](../../decisions/network/LAN_BULK_DESIGN.md) (pipelining、HAL 中心期は着手しない)、[RA266](../../decisions/realhw/RA266.md) (機体の構成)。

## 1. 背景: 今の上限は遅い装置に合わせてある
LAN と I/O の上限の多くは、いちばん遅い経路に合わせた 1 つの定数です。一番遅い経路は、C バスの NE2000 (PIO)、シリアル SFS、P100 級の CPU です。Ra266 (Celeron 266MHz・64MB・PCI の 82557 はバスマスタの DMA) でも同じ値で動いています。

| 定数 (main 2026-11-16) | 値 | 何に合わせた値か |
|---|---|---|
| `net/rx_work.h:5` NET_RX_WORK_LIMIT_US | 120µs | NE2000 の PIO の受信の 1 回の上限 (IO_TIER の X2)。PIO で 1514B を読むと 1 枚で越え得る |
| `net/nic.h:53` NET_RX_SCAN_MAX | 8 | 1 回の振り分けで NIC から取り出す枚数 |
| `net/nic_rx.c:11` RX_SLOTS | 4 | link・SFS の列の枠。2026-11-16 に満杯で捨てていた (f61ad39f で link は NIC に残す形へ) |
| `platform/powermac/nic_gem.c:16` GEM_RX_SLOTS | 4 | GEM の受け取りの枠。link の WINDOW の credit もこの 4 枠だけを数える |
| `drivers/net/e82557/e82557_regs.h:53` E82557_RFD_COUNT | 8 | 82557 の受信の記述子 (DMA の列) |
| `net/link.h:109` LINK_WINDOW_LEN | 2 | link の窓 |
| `net/link.h:112・116` LINK_RTO_TICKS・LINK_TRIES | 200ms・5 回 | 10ms の tick 基準の再送 |
| `include/remote_control.h:21` REMOTE_SFS_CALL_MAX | 1044B | 1 回の SFS の呼び出し (LAN の 1KB) |
| `fs/serialfs/sfs_proto.h:40・43` SFS_MAX_PAYLOAD・SFS_LAN_MAX_PAYLOAD | 512B・1028B | シリアルと LAN の 1 回の読み。1 本ずつ返事を待つ (stop-and-wait) |
| `kernel/io_work.h` IO_PASS_US | 500µs | I/O の Tier の 1 巡の上限 |
| `kernel/io_policy.c` の既定の予算 | 120µs ([推]) | NE2K・NIC・TSC の 1 回の予算 |
| `userland/system/hsync.c:106・107` FILE_BUF・CMP_BUF | 64KB・32KB | hsync の読み書きの単位 |

計測で見えたこと:
- hsync (x86 の NP21/W、シリアル SFS) は時間のほぼ 100% が往復の待ちでした。ふだんの量で、差分なし 34 秒、全部の日時が変わると 67 分です。往復の数は HSYNC_MANIFEST で減らします。
- PowerPC の link の 64KiB の写しは約 21 秒 (1KB の CALL の往復の積み重ね) でした。
- I/O の Tier の D3・D4 のレビューで、120µs だと NE2000 の PIO では 1 回に 1 枚ほどしか回収できないと分かりました。

## 2. 方針: 2 つのモード
| モード | 対象 | 方針 |
|---|---|---|
| **互換モード** (今の値) | C バスの NE2000 (PIO)・シリアル SFS・P100 級の CPU・32MB・NP21/W の既定 | 今の定数をそのまま使う。正しさと止まらないことを優先する |
| **Ra266 基準モード** (新設) | PCI のバスマスタ DMA の NIC (82557・GEM)・Celeron 266MHz 級以上・64MB | 枠・予算・1 回の量・窓を広げ、往復とコピーを減らす |

- 選び方 (案): 起動時に自動で決め、設定で上書きできる。
  - 自動の判定は、bind した NIC が DMA (adapter の能力の印) か、CPU の段 (`arch_cpu_has_mmx` などの既存の判定)、メモリの量で行う。
  - 上書きは `/etc/system.cfg` に `PERF=compat|ra266|auto`。
- 1 つの NIC の上で、モードを途中で替えることはしない (bind のときに決める)。
- ホストとの量 (SFS の 1 回の量・link の窓) は、HELLO で互いの能力を伝えて小さい方に合わせる。古いホストは互換の値のまま。

## 3. リファクタリングの形
1. **値を 1 か所に集める**: 上の表の定数を、`include/perf_profile.h` の構造体 (例 `struct perf_profile { u32 rx_work_us, rx_scan_max, rx_slots, link_window, sfs_payload, ...; }`) と、2 つの既定の表 (`perf_profile_compat`・`perf_profile_ra266`) に移す。[C4] の 3 層管理に合わせ、値の正は 1 つにする。
2. **読む側を表に替える**: `#define` で配列の大きさを決めている所 (RX_SLOTS・GEM_RX_SLOTS・RFD_COUNT) は、Ra266 の値で静的に確保し、互換モードでは使う枠を少なくする (大きい方で確保して、使う数を表で決める)。
3. **予算**: I/O の Tier の io_policy の既定の表を、モードごとに 2 面にする (KAPI の io_policy_set の形は変えない)。
4. **SFS・link の量**: HELLO に能力の欄を足し、1 回の読みの量と窓を両者の小さい方にする。プロトコルの版を上げる。古いホストは互換の値のまま (HSYNC_MANIFEST と同じく、無ければ今の経路)。
5. **hsync の単位**: FILE_BUF・CMP_BUF をモードで変える (64KB・32KB → 例 256KB)。
6. **試験**: 2 つのモードの両方で、host の試験を回す (表を差し替えて同じ試験を 2 回)。

## 4. 段の案 (着手のときに設計の往復で確定)
| 段 | 中身 | 依存 |
|---|---|---|
| P0 | 計測: Ra266 の実機で、82557 の LAN 版 SFS の 1 往復・64KiB の写し・hsync を測る (今の互換の値で) | なし |
| P1 | `perf_profile` の口と 2 つの表、判定と上書き (値は今のまま、動作は変えない) | P0 |
| P2 | NIC の受信の枠・振り分け・予算を表から読む (NE2K・82557・GEM) | P1、I/O の Tier の D 系 (済) |
| P3 | HELLO の能力の欄と、SFS の 1 回の量・link の窓の取り決め (ホストの 3 つの経路も) | P1、HSYNC_MANIFEST |
| P4 | hsync・install の単位をモードで変える | P1 |
| P5 | Ra266 の値を入れて実機で計測し、P0 と比べる | P2〜P4 |
| (P6) | 返事を待たずに何本も出す (LAN_BULK の pipelining) を Ra266 基準モードだけで使う | ユーザーの判断 (HAL 中心期は着手しない) |

## 5. 決めていただくこと (着手の前に)
1. Ra266 基準モードを作ることと、互換モードを残すことの確認。
   - これまでの目標は「P100/32MB で超快適」で、それ以上の性能は提案しない方針でした。互換モードがその目標を受け持ち、Ra266 基準モードは上に足す形でよいか。
2. 選び方を自動 + 設定の上書きにするか。
3. P6 (pipelining) を、Ra266 基準モードに限って HAL 中心期の後に回すか。
4. 着手の時期 (I/O の Tier と hsync の後、など)。
