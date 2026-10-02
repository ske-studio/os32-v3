# TASK_HAL_WIRING_RESULTS — 完了した段の記録

> 状態: **完了記録 (2026-10-02 切り出し)**。元票: [TASK_HAL_WIRING.md](../../tasks/v3/TASK_HAL_WIRING.md)。節番号・記述時点は原文のまま。本文中の節参照は元票を指す。未確認事項の受入完了を意味しない。

### 3-1. 受入の記録 (PM、2026-09-23、NP21/W + 実機 Ra266 の W0)

| ID | 結果 |
|---|---|
| W0 | NP21/W: 合格 (tick 99.3Hz、kselftest)。**実機 (Ra266、2.4576MHz、Build Sep 23 10:20 = API v61): 合格** — Ubuntu ノートのシリアル越しに `uptime` を 2 回: ホスト単調時計 1790133068.362 → 1790133188.648 (**120.29 秒**) のあいだに `up 64 min 34 sec` → `up 66 min 34 sec` (**120 秒**、表示は秒単位)。比 0.998 (許容 ±0.5%)。修正前の 8.125ms tick なら +148 秒と出る。`serial` は `mode=compat baud=9600 FIFO=yes`。**起動画面 (写真、CI ビルド 527255b)**: `CPU...480K PIT 2.4576M` = クロック判定と `loops_per_tick` 480K (Celeron 266)。`cpu_delay_us × 1000` の実機値は未 (シェルから読む口が無い) |
| W1 | 合格: ホスト試験 9 本 (dma8237 8 / dma_pool 9 / pci_bind 10 / irq_math 8 / time_math 8 / pit_clock 5 / ring3_str の変異すべて RED)、`make check` 全通過 |
| W2 | 合格: FD 起動、`[dma] 0439h ff -> ff state=UNREADABLE`、FD へ書いた 5,151B を読み戻して md5 一致。タイムアウト → abort → 再試行の経路は未観測 (NP21/W では起きない) |
| W3 | 部分: kselftest の `int $0x23` で登録規則・2 巡・DEFERRED・登録数マスク・解除・ストーム 200/201・隔離 sticky を毎起動確認 (187/187)。**実 IRQ (master 5、LGY-98) を確認**: `[lgy98] base 0x10d0 irq 5` で `irq_register` され、`POST /api/net/inject` で ARP を 1 フレーム入れると `irq_lines[5].tick_hits` = 1 / `tick_stamp` 更新、`irq_unexpected` 不変 (= アダプタが HANDLED)、PIC の ISR/IRR は空に戻り IMR 不変。実 IRQ の**共有 (2 装置)**、slave 側 (9)、V86 中は未 (`/api/pic` は読み取り専用で人工のエッジは作れない) |
| W4 | 合格 (NP21/W): 1 万回で逆行 0 (クランプ 0 回)、CPL=3 の `time_test` で NULL / 範囲交差 (差 0〜3) が負 + 出力不変、差 4 は成功、heap / stack 出力は成功、2000 回で逆行なし。実 PIT の位相待ちは p1=1 の分岐だけ踏めた (0/0 と再試行は未検証)。**位相試験の直後にクランプが 1,002 回**入った = p1=1 (IRR 先) で 1 周期ぶん先に出た値を、続く 1,000 回の読みが追い越すまで押さえた (NP21/W の非原子性、実機での回数は要記録)。**実機 (Ra266、2.4576MHz、CI ビルド 527255b = Build Sep 23 06:09:44、API v61、2026-09-23 16:38)**: CPL=3 の `timetest` が **TIME PASS (0 failure)** — 1 stack out rc=0 (lo=217558231)、2 monotonic 2000 reads、3a/3b NULL → -9 で出力不変、4 gap 0〜3 → -9 / gap 4 → 0、5 heap out rc=0。**起動画面 (写真)**: `[selftest] time: clamped 0 of 10000 reads` (**実機ではクランプ 0 回** = p1/p2 の判定だけで足りた。NP21/W の 1,002 回は模擬の非原子性)、`real-PIT t0=0 t1=1 retry=0 (unhit = UNVERIFIED)` (NP21/W と同じく p1=1 の分岐だけ)、`1000 reads 9432 us (9 us/call)` (`sys_time_now` の CPL=0 の単価)、**`[selftest] 186/186 passed`** (NP21/W は 187。条件つきの検査が 1 つ少ない、内訳は未確認)。IRQ3 のストーム → 隔離 → `irq_test_reset_line` の黄色い行も実機で出た (= 実 8259 でも `int $0x23` 経路が同じ)。**実機の W4 は合格** |
| W5 | 合格: カーネル 468KB 中 459.8KB (残り 8.2KB)。増分の内訳は A/B/修正の各報告 (製品コード ≒ 3.6KB + 4.5KB、kselftest は圧縮後 +2.6KB) |
| W6 | 残件: 82557 の実 IRQ での**共有** (実機の `lspci`: 0:11.0 8086:1229 **irq 3 pin A**、bar1 io 0x6000、Command 0x0147 = IO/MEM/BM 有効 → 動的 IRQ 3 で `irq_register` できる番号。TASK_LAN_82557 §6)、W7 キャッシュ整合、NE2000 の ISR 内 reset (L-C)、既存出力 KAPI の RO 穴 (TASK_KAPI_OUTPUT_GUARD)、**io_wait() 連打で FD 読みが古くなる (POLICY_DEBUG §4-55、原因未特定)**、p1=1 で 1 周期先に出る値の扱い (クランプで単調だが 10ms 止まる。判定に count を併用する案) |
| W7 | 未 (実機。FDC の書き→読み戻しは W2 で NP21/W のみ) |
| 実機の起動画面のその他 (2026-09-23) | `[dma] 0439h b4 -> b0 state=VERIFIED` (実機では読み戻せる = VERIFIED。NP21/W は UNREADABLE)、`[fdc] FDC rc=0 st0=20`、`IDE...mech#1 ok`、`[pci] 6 devices` / `bind: 0 bound, 0 quarantined of 6`、`[ide] drive0 identify=0` + `[pc98pt] C=16382 H=16 S=63 total=16514063` (8GB HDD が見えている。`[fatfs] mount failed pdrv=1 err=13` は未フォーマットなので想定内)、`[KCG] kernel-init load failed: -1` (FD に font が無い、想定内)、**`[pcm] CS4231 v=101 irq 10 dma 1 fmt 0x5B`** (TASK_PCM_CS4231 E6 の検出まで) |

