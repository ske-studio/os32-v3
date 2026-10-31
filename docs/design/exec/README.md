# exec/ — 設計
> 状態: **現行 (2026-10-31)** — 出所: MEMMAP_V3・SLIM_KERNEL_IMPORTS (D1 で移した)
ソース: `exec/`

回収・確保・lease の付け外しを許す場所 = **カーネルが自分の状態 (台帳・owner・lease の順序) を中途に持っていない点**:

| 通常文脈 | どこ | 備考 |
|---|---|---|
| syscall の入口 / wrap の中 | `ring3_syscall_dispatch` (`exec.c:1440-1452`) と KAPI の wrap | IF=1 (`int80_stub` が sti 済み)。`ring3_abort_check` はここで畳んでよい |
| exec_run の復帰点 | longjmp の着地、IF=1 に戻した後 | R1 の (b) 回収はここ |
| WM のハンドラ | gshell の OP_WAIT からの `exec_kill(宛先)` | CPL=0 だが syscall の中 (`ring3_wm_depth`)、通常文脈 |
| 起動時・モジュール読み込み | `kernel.c` の初期化列 | live AS = 0 |

**通常文脈でないもの**: IRQ ハンドラ本体 (EOI まで)、割り込みフレームの上で走る `ring3_abort_check` (R1 の (a) 移譲だけ)、V86 の反射中。

### appmem

**カーネルの口は `mem_map` / `mem_unmap` の 2 つだけ** — `brk` 相当の KAPI は足さず、ヒープの伸長は libc が「ヒープ末尾に続けて map」で行う。
**大きな塊の閾値は 64KB 固定** (GB 級はターゲット外)。

| 時点 | 誰が | 何を |
|---|---|---|
| アプリの実行中 (syscall) | exec (通常文脈) | `mem_map` / `mem_unmap` (R2、D23)。ヒープの伸長も libc がこれで行う。失敗は NULL |

**実行中の追加は通常文脈 (syscall) でカーネルに借りる。カーネルの口は `mem_map(size)` / `mem_unmap(ptr)` の 2 つだけ**
**失敗は NULL / −1 (ENOMEM)** で、スワップが無いのでアプリが扱う (縮小版で開く・断る)。**失敗の条件は 2 つ: 池の物理が足りない、または (map の場合) アプリ帯の仮想に連続した空きが無い**

> [要確認] 現行 exec/appmem.h の内部口は appmem_map(...bytes, hint, map_flags, ...) と appmem_unmap(...base, bytes)。原文の mem_map(size) / mem_unmap(ptr) は現在の正確な引数宣言ではない。

アプリのメモリは物理連続を要求しない
**ページ単位の割り当てには外部断片化が起きない**。
**ただし仮想の外部断片化は 1 本の長寿命アプリの中では起こる**
**`mem_map` は物理の空きが足りても仮想の連続領域が足りなければ ENOMEM を返す**

### os32x_hdr

**入口へ飛ぶ前の検査は常駐シェルも例外にしない**
**未知の形式版を「既知版以上だから可」としない**

### v86_call

V86 は CUI 専用 (`exec.c:1743-1747`)。CPL=3 の `v86` コマンドが同期 KAPI を呼ぶため、**呼出元 AS は保存・停止したまま保持する例外**とし、他の通常 AS (parked・親 AS を含む) が存在すれば入場を拒否する。セッション中は呼出元を実行せず、終了時に低位状態と呼出元 CR3 を復元してから KAPI へ戻る。

### appmem_map

物理は池から owner=その AS、仮想はヒープ末尾に続ける
アプリ帯の中 (exec_heap とスタックの間、または帯の上側) に仮想を予約し、物理を池から owner=その AS で張る。

### appmem_unmap

(ii) の塊は `mem_unmap` した時点で**池へ即返す** (owner の台帳から外し、その AS の PTE を NP に、TLB を無効化)。
アプリの終了時は owner ごと全部回収 (R5)。

### shell_service

shell_service_wm_mark
wm_enterや*_userのカーネル実体から呼ぶ。移植先へ展開しない

### system_surface

**probe の前に BB の量を決める**ので、識別 (機種 / PCI ID) で決まる backend の量 (planar 128KB / PEGC 300KB / Cirrus 0) を取り、識別できない・probe に失敗して別の backend へ落ちたときも**確保済みの BB をそのまま使う**
`rep movs` に要るのは仮想の連続だが、gshell は CPL=0・恒等で使うので**物理連続のまま持つ**のが最も単純。スプラッシュには同じ面を lease し、終了で lease だけ返る。

| 時点 | 誰が | 何を |
|---|---|---|
| GUI の開始と終了 | gshell | BB を boot から受け取る (永続 owner)。GUI を終えても返さず保持 |

BB は起動時 (§4-5 の識別 + 予約 + BB 確保の段、live AS = 0、D33 — probe より前) に池から連続確保 (§2-2)。**池からの新規の連続確保はこれとモジュール本体 (読み込み時) だけ** — 断片化で後から取れなくなるのを避ける。

BB は owner=boot → gshell (永続) の移譲 1 回だけで、GUI 終了 → CUI → GUI 再起動でも保持して再利用する (§2-2)。

| 何 | なぜ連続 | 対処 |
|---|---|---|
| BB (planar 128KB / PEGC 300KB) | gshell が CPL=0・恒等で `rep movs` | 起動時 (live AS = 0) に確保し gshell が永続 owner で保持 (R4・R5) |

ブート時に台帳から連続確保 → gshell へ移譲 (§2-2)。アプリへは lease 窓に別名写像

## 索引

- [exec/appslot.c](appslot.md)
- [exec/exec.c](exec.md)
- [exec/exec_heap.c](exec_heap.md)
- [exec/lease.c](lease.md)
- [exec/ring3_str.c](ring3_str.md)
- [surface_query.md](surface_query.md)
