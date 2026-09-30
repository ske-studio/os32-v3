# SURVEY_N1 — N1 で触れた / 見つけた CPU アーキテクチャ依存

発行: コーダー (2026-09-14、票 [TASK_N1](../../archive/network/TASK_N1.md) 段 7、ユーザー指示)。

対象は **N1 の実装で実際に触れた場所と、その周辺を grep して見つけた場所**だけ。
移植そのものは行っていないので、ここに書くのは「N1 で直した」「残っている (場所と理由)」
「移植時にやること」の 3 つに限る。推測で書いた行は無く、全部 `grep` した実在の行を挙げる。
行番号は基点 `af48990` + 本票の変更後のもの。

前提 (OS32 の現況): i386 protected mode 固定。little-endian、非アラインアクセスが
ハードウェアで許され、ポート I/O 空間があり、割り込みは 8259A + 8254 (PIT)、
システムコールは `int 0x80` + ユーザースタック上の cdecl 引数。ARM (AArch32 / AArch64) や
RISC-V へ移すと、この 5 つが全部変わる。

---

## (a) 直列化とアライメント

### N1 で直した

ワイヤ v2 (20B ヘッダ) は **構造体を使わず、バイト単位の LE アクセサだけ**で読み書きする。
`rid` が意図的に 4 バイト境界に載らない (@14) 配置なので、構造体にすると詰め物が入り、
非アラインアクセスの許される x86 でしか動かない形になる。

- `net/link.h:41-54` — ヘッダの各欄をオフセット定数 (`LINK_OFF_OP` … `LINK_OFF_SESS`) で持つ。
  v1 の `struct link_hdr`（16B、`net/link.h` の旧版）は**削除した**。
- `net/link.c:50-59` — `rd16` / `rd32` / `wr16` / `wr32`。全フレームがここを通る。
- `net/link.c:215-243` (`link_send_ex`) と `net/link.c:412-470` (`link_dispatch`) —
  送受信のどちらも 1 欄ずつアクセサ経由。
- `tools/host_agent.py:93-101` — Python 側も `struct.pack("<BBHIIHIH", ...)` (`<` で
  詰め物なし) の 1 か所に閉じ込めた。
- `tools/tests/test_host_agent.py` の贋 OS32 (`FakeOS32.frame`) は **Agent の直列化を
  借りずに欄ごとに組む**ので、片側だけレイアウトを変えたら試験が落ちる。

EtherType だけはワイヤ上 big-endian で、`net/link.c:227` が明示的に `>> 8` / `& 0xFF` で
組む (`p[12]` / `p[13]`)。ここはホストのバイト順に依存しない。

### 残っている (場所と理由)

> **2026-09-15 追記 (順序 4-a で解消)。** 下の一覧と「移植時にやること」の 1. / 2. は
> **済み**。`fs/ext2_*.c` / `drivers/kcg.c` / `lib/utf8.c` の直アクセス 163 か所は
> `include/endian_le.h` の `le16_rd` / `le16_wr` / `le32_rd` / `le32_wr` に置き換え、
> `fs/iso9660.c` の `iso_read_le16` / `iso_read_le32` もそこへ寄せた。4. の番人は
> `tools/check_le_access.py` (`make check`) — 文字列検査と `-Wcast-align=strict` の
> 2 段。以下は N1 時点 (基点 `af48990`) の記録としてそのまま残す。
> `fs/hostdrvfs_proto.h` の packed 構造体は経路ごと x86 前提なので**対象外のまま**。

**ext2 ドライバはディスク上の構造を `*(u16 *)&buf[off]` で読み書きしている。**
非アラインアクセスと LE の両方に依存する。実数で 80 行以上:

- `fs/ext2_inode.c:25-43` (inode の読み) / `:68-86` (書き) / `:241,253,257,283,307,317`
  (間接ブロック表)
- `fs/ext2_super.c:78-79, 101-106, 209-219, 247-252` (スーパーブロック / グループ記述子)
- `fs/ext2_dir.c:33-34, 80-81, 125-126, 136-144, 169-170, 208-220, 267-273, 313-314,
  388-389, 417-423` (ディレクトリエントリ)
- `fs/ext2_fmt.c:225, 317-341` (mkfs)
- `drivers/kcg.c:260-268` (フォントアーカイブのヘッダ `magic` / `payload_size` / `flags`)

alignment は**たまたま**合っていることが多い (inode は 128B 単位、ディレクトリエントリは
4B 整列) が、`fs/ext2_dir.c` の `pos` は `rec_len` の足し算で進むので、壊れた FS では
奇数オフセットになりうる。x86 では読めてしまい、ARM では `SIGBUS` 相当 (あるいは
`SCTLR.A` 次第で黙って回転した値) になる。

対照的に **iso9660 は既に専用アクセサを使っている** (`fs/iso9660.c:52-58` の
`iso_read_le32` / `iso_read_le16`、使用は `:148-149, 252-262, 327`)。ext2 もこの形にすれば
よい、という手本が同じツリーの中にある。

`fs/hostdrvfs_proto.h:130-366` は NP21/W の HostDrv プロトコル構造体を
`__attribute__((packed))` で定義している。packed は詰め物の問題は消すが**バイト順は消さない**
(この経路は「ホストが x86 Windows」という前提そのものなので、移植先では経路ごと無くなる)。

### 移植時にやること

1. `fs/ext2_*.c` の `*(uNN *)&` を `iso9660.c` と同じ LE アクセサ (`ext2_rd16` /
   `ext2_rd32` / `ext2_wr16` / `ext2_wr32`) に置き換える。置換は機械的で、
   `grep -n '\*(u16 \*)\|\*(u32 \*)' fs/` が 0 件になることが完了条件。
2. `drivers/kcg.c:260-268` も同じ。
3. `net/link.*` と `fs/iso9660.c` は**そのまま**移植できる (既にアクセサ経由)。
4. 新しいワイヤ / ディスク形式を足すときは N1 と同じ規約 (構造体キャスト禁止) を使う。
   `make check` に「`*(u16 *)&` を新規に増やさない」番人を足すのが安い。

---

## (b) 割込み制御 (`cli` / `sti` / `hlt` の直書き)

### N1 で直した

リンク層は **`cli` / `sti` を直書きしない**。`include/io.h` の `irq_save()` /
`irq_restore()` (IF を保存して復元する、ネストしても安全な対) を通す。

- `net/link.c:29-45` — `LINK_IRQ_SAVE()` / `LINK_IRQ_RESTORE()` / `LINK_IDLE()` の
  3 つのマクロに閉じ込め、カーネルでは `irq_save` / `irq_restore` / `sti;hlt` に、
  ホスト試験 (`LINK_HOST_TEST`) では贋物に切り替わる。**この 3 つが、リンク層が
  持つアーキテクチャ依存の全部**。
- `tools/tests/net_link_host.c:222-276` が贋物側 (IF=0 中のタイマ延期と、
  IF 復元直後の tick)。ケース `irq_if0_defers_the_timer_then_runs_it` が踏む。

### 残っている (場所と理由)

- `include/io.h:42-48, 70-72` — `_enable` / `_disable` / `_halt` と
  `irq_save` (`pushfl; popl; cli`) / `irq_restore` (`pushl; popfl`)。**ここが唯一の
  実装箇所**で、他は全部ここを呼ぶ、という形になっていない場所が下の 3 つ。
- `net/link.c:44` — `sti\n\thlt` を 1 命令対で書いている。**「IF を立ててから寝る」は
  x86 の `sti` が次の 1 命令だけ割り込みを遅らせる性質に依存する**
  (ARM の `cpsie i; wfi` / AArch64 の `msr daifclr; wfi` は同じ保証を持たない ―
  `wfi` は割り込みが保留なら即復帰するので実際には成立するが、根拠が違う)。
  他の `hlt` 直書きと同じ扱い。
- `kernel/kernel.c:271, 384, 597, 608, 617` / `kernel/sys.c:40, 45` /
  `drivers/kbd.c:423, 457, 478` / `drivers/serial.c:182, 207` — `hlt` の直書き
  (アイドル待ち)。`_halt()` を使っていない。
- `exec/exec.c:1736 (`086bda5` 時点。調査時は 1717)` — CPL=3 へ iret する直前のインライン asm の中の `cli`
  (セグメント復元と iret の間に IRQ を入れないため。x86 の特権復帰そのもの)。
- `kernel/ring3_entry.asm:47` (`sti`) / `:60-` (出口は IF=0 のまま iretd) —
  POLICY_DEBUG §4-19 の「CPL=3 KAPI は IF=1、出口は IF=0」がここ。

### 移植時にやること

1. `hlt` の直書き 12 か所 (`kernel.c:384` は `cli; hlt`)を `_halt()` (= HAL の 1 関数) に寄せる。ARM では `wfi`、
   RISC-V では `wfi` になる。
2. `net/link.c:44` の `LINK_IDLE()` は HAL の `hal_wait_irq()` に差し替える (1 行)。
   リンク層の他の行は触らなくてよい。
3. `irq_save` / `irq_restore` は HAL に移す。AArch64 なら `mrs x,daif` / `msr daif,x`。
   **意味 (ネストしても内側の restore が外側の禁止を壊さない) は同じにする** —
   `net/link.c` の cli 区間の正しさはこの性質に依存している。
4. 特権復帰 (`exec/exec.c:1736 (`086bda5` 時点。調査時は 1717)`、`kernel/ring3_entry.asm`) は移植ではなく書き直し。

---

## (c) ポート I/O (ARM では MMIO)

### N1 で直した

**リンク層はポート I/O を 1 か所も持たない。** `grep -n 'inp(\|outp(\|inpw(\|outpw(' net/`
は 0 件で、NIC との会話は `drivers/ne2000.h` の API (`ne2k_send` / `ne2k_recv` /
`ne2k_state` / `ne2k_is_busy` / `ne2k_rx_ring_free_pages` / `ne2k_rx_queue_free`) だけ。
ホスト試験がこの 6 本を贋物に差し替えられたのは、その境界が既に引けていたから。

### 残っている (場所と理由)

- `include/io.h:11-38` — `inp` / `outp` / `inpw` / `outpw` / `insw_rep` が
  `in` / `out` / `rep insw` のインライン asm。**x86 にしかない I/O 空間**。
- ポート I/O を使う .c は 22 ファイル (`grep -rl 'inp(\|outp(\|inpw(\|outpw(\|insw_rep('
  --include=*.c kernel/ drivers/ net/ fs/ gfx/ lib/ exec/` = 22)。
- `drivers/ne2000_io.asm:30, 51` — `rep insw` / `rep outsw` で NIC RAM を PIO 転送。
- `kernel/idt.c:214-216` — PIT の分周比を `outp` で書く。

### 移植時にやること

1. `inp` / `outp` を HAL の `hal_io_read8/16` / `hal_io_write8/16` にする。ARM には
   I/O 空間が無いので、実装は「ベース番地 + オフセットの MMIO」になる
   (PC-98 の周辺そのものが無いので、実際には**ドライバごと書き直し**)。
2. `net/link.c` は 1 行も変わらない。リンク層より下だけを書き直せばよい、という
   分割が既にできている ― これは N1 の成果として残しておく価値がある。

---

## (d) メモリ順序とキャッシュ (NIC リング / DMA / [HW2])

### N1 で直した

無し (リンク層は DMA を持たず、NIC RAM にも触らない)。ただし **リンク層と 100Hz
タイマ (IRQ0 文脈) の間の共有状態**が新しく増えたので、そこの前提を書いておく:

- `net/link.c` の `link_h[]` / リング / TX の印は **KAPI (CPL=0 の通常文脈) と
  `link_tick` (IRQ0 文脈) の 2 者**で共有する。排他は `irq_save` / `irq_restore` だけで、
  **メモリバリアは 1 つも置いていない**。単一 CPU + 割り込み禁止なので x86 では正しい。
- `net/link.c:844-920` (`link_host_open`) は「空きを探す cli 区間 → IF=1 で写し →
  再確認して積む cli 区間」の 3 段。**IF=1 の区間で触るのは FREE のハンドルだけ**
  (`link_tick` は FREE を触らない) という不変条件に依存している。

### 残っている (場所と理由)

- `drivers/ne2000.c:73-74` — `volatile u8 busy` / `volatile u8 irq_pending`。
  IRQ 文脈と通常文脈の共有を `volatile` だけで守っている。**`volatile` は順序も
  可視性も保証しない** (C89 の意味では「最適化で消すな」だけ)。x86 の強い順序と
  単一 CPU だから成立している。
- `drivers/ne2000_ring.c` の受信リング計算は純粋関数で、NIC RAM への実アクセスは
  `ne2k_pio_read/write` (PIO)。**DMA ではない**ので、キャッシュ一貫性の問題は今は無い。
- [HW2] (DMA バッファが 64KB 境界をまたがない) が効いているのは FDC:
  `drivers/fdc.c:27` (「DMAバンク(64KB)をまたがないように1セクタ分」)、
  および V86 BIOS の再現 `kernel/v86_bios.c:589, 757, 1079-1134`。
  これは **ISA DMA コントローラ (8237) の 64KB ページレジスタ**という PC-98/AT の
  ハード制約で、CPU アーキテクチャではなくバスの話。

### 移植時にやること

1. マルチコアか、弱い順序の CPU (ARM / RISC-V) へ行くなら、
   `net/link.c` の cli 区間は**そのままでは足りない**。`irq_save` が割り込みしか
   止めないので、他コアとの共有には `dmb` 相当のバリアかロックが要る。
   いまは単一 CPU 前提と明記しておく (`net/link.c:8-14` の見出しに書いた 3 本柱の
   (2) がその宣言)。
2. `drivers/ne2000.c` の `volatile` 共有は、移植時に「IRQ 文脈との共有」を明示する
   アクセサ (読み書きの前後にバリア) に置き換える。
3. 実 DMA を使う NIC / ディスクドライバを書くなら、キャッシュの clean / invalidate が
   要る (x86 はコヒーレント、ARM はそうとは限らない)。[HW2] の 64KB 制約は PC-98 の
   8237 固有なので、移植先では別の制約 (IOMMU / バーストアラインメント) に置き換わる。

---

## (e) システムコールの引数渡し

### N1 で触った

`kapi/kapi_host.c` は **ユーザー領域のポインタを 2 段で検査してから写す**、という
v50 と同じ形を踏襲した。この 2 段は「引数がユーザースタックに積まれている」ことを前提に
している:

- `kernel/ring3_entry.asm:31-60` (`int80_stub`) — `int 0x80` で入り、`pushad` で
  フレームを作り、`ring3_syscall_dispatch(frame*)` を呼ぶ。IF はゲートで 0 になり、
  `:47` の `sti` で戻し、出口は IF=0 のまま `iretd`。
- `exec/exec.c:1020-1090` (`ring3_syscall_dispatch`) — `frame[11]` = CPL=3 の ESP から
  `args_src = user_esp + 4` を作り、`kapi_argsize[slot]` バイトを引数とみなす。
  `kapi_argptr[slot]` のビットが立った引数 (= ポインタ) の**先頭番地**を `ring3_ptr_ok`
  で見て、帯外なら `ring3_fault_kill()`。
- `kernel/ring3_entry.asm:129-150` (`kapi_invoke`) — `rep movsb` で
  ユーザースタックからカーネルスタックへ引数を丸写しし、cdecl で `wrap_*` を呼ぶ。
- `kapi/kapi_host.c:30-37` (`host_range_ok`) — 先頭が帯内だった後の「どこまで読んで
  よいか」を `ring3_user_range_ok(p, len)` で見る。失敗は `OS32_ERR_INVAL`。
- 試験: `tools/tests/net_link_host.c` の
  `r3_B8_dispatcher_kill_and_wrapper_inval` が両方 (kill と INVAL) を踏む。
  早期検査のビットマスクは `kapi_generated.c` から `tools/tests/test_net_link.py`
  (`argptr_defines()`) が読んで `-D` で渡すので、生成表とずれたら試験が落ちる。

### 残っている (場所と理由)

この設計は **「引数が呼び手のスタック上に連続して並ぶ」= cdecl** に丸ごと依存している。
レジスタ渡しの ISA (AArch64 は x0〜x7、RISC-V は a0〜a7) では:

- `args_src = user_esp + 4` という式が成立しない (引数はトラップフレームのレジスタ)。
- `kapi_argsize[]` (バイト数) が意味を失う (**引数の個数**になる)。
- `kapi_invoke` の `rep movsb` による「丸写しして cdecl で呼ぶ」が成立しない。
- 8 本を超える引数はスタックへ溢れるので、**レジスタとスタックの両方**を見る必要がある。
  KAPI で引数が一番多いのは `launch_take` の 5 本なので、実際には全部レジスタに載る。

`KAPI_ADDR` (`sdk/include/os32/os32_kapi_shared.h:114`) が**固定番地の関数表**である
ことも、この経路の一部 (`exec/exec.c:1053` が `((const u32 *)KAPI_ADDR)[2 + slot]` で
wrap を引く)。番地の固定自体は (f) の話。

### 移植時にやること

1. `kapi_argsize[]` を「バイト数」から「引数の個数 + 各引数の種別」に変える
   (生成器 `sdk/gen_kapi.py` の 1 か所)。`kapi_argptr[]` は**そのまま使える**
   (bit k = 引数 k がポインタ、という意味は ISA に依らない)。
2. ディスパッチャの早期検査は「トラップフレームのレジスタ x[k] を見る」に書き直す。
   **`kapi/kapi_host.c` は 1 行も変わらない** — ラッパー側は `ring3_user_range_ok` しか
   知らないので。この分割は維持する価値がある。
3. `kapi_invoke` は ISA ごとのアセンブラ。

---

## (f) 物理番地の前提 (`memmap.h` 以外)

### N1 で直した / 足した

`net/link.c` と `kapi/kapi_host.c` は **絶対番地を 1 つも持たない**。
`grep -n '0x[0-9A-F]\{5,\}' net/link.c` が拾うのは `0xFFFF` / `0xFFFFFFFF`
(`net/link.c:290, 855` の epoch 周回と rid 枯渇の判定 = **番地ではなく値域の上限**) だけ。

### 残っている (場所と理由)

- `exec/exec.c:473` — `if (p >= 0xA0000UL && p < 0xC0000UL) return 1;` (VRAM 帯を
  `ring3_ptr_ok` が直値で持つ)。`exec/exec.c:1480` にも VRAM 0xA0000 / 0xA8000 の
  コメント付き直値。
- `sdk/include/os32/os32_kapi_shared.h:114` — `KAPI_ADDR` は `KHEAP_BASE + KHEAP_SIZE`
  の式なので、`memmap.h` 由来。ただし **CPL=3 のバイナリはこの番地を焼き込む**
  (`docs/KAPI_SPEC.md` §3-2 の「全体 clean rebuild」がこのため)。
- `include/memmap.h` が番地の正典 (CLAUDE.md の Architecture 表)。ここは移植時に
  丸ごと書き換える前提の場所なので「残っている」には数えない。

### 移植時にやること

1. `exec/exec.c:473` の VRAM 直値を `memmap.h` の定数にする (1 行 × 2 か所)。
2. 「固定番地の KAPI 関数表」は、位置独立にする (アプリ起動時にポインタを渡す)
   か、そのまま別の固定番地にするかの設計判断。N1 では触っていない。

---

## (g) タイマと割込みコントローラ (100Hz `link_tick` の前提)

### N1 で足した前提

- `kernel/isr_handlers.c:476-486` (`timer_handler`) — `snd_tick()` → `ne2k_timer_tick()`
  → **`link_tick()`** → `lgy98_tick()` の順。**リンクプロトコルはここでしか進まない。**
- 周期は `kernel/kernel.c:155` の `pit_init(PIT_HZ)`、分周比は
  `kernel/idt.c:212-216` が `PIT_CLOCK / hz` を 8254 に書く。
  `PIT_CLOCK` は `kernel/idt.h:97` = **1996800 (PC-98 固有のクロック)**。
- `tick_count` は `kernel/isr_stub.asm:454-455` の `inc dword [tick_count]`
  (宣言は `kernel/idt.h:116`)。**32bit で周回する**。
- リンク層のタイマ定数は全部 tick 単位で `net/link.h:81-86` に置いた
  (`LINK_RTO_TICKS` 20 = 200ms、`LINK_PROBE_TICKS` 100 = 1 秒、`LINK_TRIES` 5、
  `LINK_PROBE_MAX` 5)。**100Hz であることが定数の意味そのもの**。
- 期限の比較は `net/link.c:551` の `since(t) = tick_count - t` で、u32 の巻き戻し
  (wrap-around) を前提にした引き算。周回しても正しい。

### 残っている (場所と理由)

- 100Hz は `PIT_HZ` 1 か所で決まるが、`net/link.h` の 4 つの定数は「100 = 1 秒」を
  **数値として**持っている (コメントで明記)。周波数を変えると意味が変わる。
- `drivers/kbd.c` / `drivers/serial.c` の `hlt` 待ちも「タイマが 100Hz で起こしてくれる」
  前提 ((b) と同じ場所)。
- 割込みコントローラは 8259A (`kernel/idt.c` の `irq_enable` / `pic_eoi`)。
  ARM なら GIC、RISC-V なら PLIC で、EOI の作法も優先度の持ち方も違う。

### 移植時にやること

1. `net/link.h:81-86` を「ミリ秒 → tick」の式で書く (`LINK_RTO_MS * HZ / 1000`)。
   そうすれば周波数が変わっても意味が保たれる。**いまは 100Hz 直値** ― N1 の範囲を
   超えるので変えていない。
2. `tick_count` の増分は ISR (アセンブラ) にあるので、移植先のタイマ ISR で同じことをする。
   u32 の周回を前提にした `since()` の書き方は ISA に依らない (そのまま使える)。
3. `timer_handler` の呼び出し順 (`ne2k_timer_tick` → `link_tick`) は**契約**なので、
   移植先でも保つ。試験 `tools/tests/test_net_link.py` の `check_timer_calls_link_tick()`
   がソース本文でこれを見張る。

---

## (h) エンディアンと型幅

### N1 で直した

- ワイヤは全部 LE アクセサ経由 ((a) 参照) なので、**リンク層はホストのバイト順に
  依存しない**。EtherType だけ明示的に BE で組む (`net/link.c:227`)。
- `tools/tests/net_link_host.c:36-43` — ホスト試験は 64bit で走るので、
  **贋 `types.h` で `u32` を `unsigned int` に固定**した。カーネルの `include/types.h:20`
  は `typedef unsigned long u32;` で、これは **ILP32 でしか 32bit にならない**。

### 残っている (場所と理由)

- `include/types.h:18-23` — `u32 = unsigned long` / `i32 = signed long`。
  **LP64 (AArch64 Linux 風) では 64bit になって全部壊れる。** 現状は
  `-m32` 固定なので露見しない。`STATIC_ASSERT` による幅の表明は `types.h:36-38` に
  仕組みだけあり、`u32` の幅には**使われていない** (`grep -n 'STATIC_ASSERT' include/types.h`
  で定義 1 件、幅の表明は 0 件)。
- ext2 / kcg の LE 依存は (a) に挙げた場所がそのまま該当。
- `fs/hostdrvfs_proto.h` の packed 構造体は x86 Windows のプロトコル ((a) 参照)。
- ポインタを `u32` に落とす箇所: `kapi/kapi_host.c:32` (`u32 a = (u32)p;`)、
  `exec/exec.c` の `ring3_ptr_ok(u32 p)` / `ring3_user_range_ok(u32 p, u32 len)`。
  **32bit ポインタ前提**。ホスト試験ではここが効かないので、
  `tools/tests/net_link_host.c:235-262` が「登録した実ポインタ帯の下位 32bit 一致」で
  代用している (`tools/tests/kapi_db_v50_host.c` と同じ手)。

### 移植時にやること

1. `include/types.h` を `<stdint.h>` 相当 (`uint32_t` / `int32_t`) にする。C89 縛り
   ([C1]) があるので、幅は `STATIC_ASSERT(sizeof(u32) == 4, u32_width)` で表明する。
   これは**移植前に今すぐ足せる**安い保険 (本票では範囲外なので入れていない)。
2. ポインタを `u32` で受ける API (`ring3_ptr_ok` / `ring3_user_range_ok`) は
   `uintptr_t` 相当にする。`kapi/kapi_host.c` は `host_range_ok` の 1 行だけ直せばよい。
3. バイト順は (a) の置き換えが済めば残らない。

---

## まとめ — N1 が残した「移植しやすい形」

| 部品 | 移植時に触るか |
|---|---|
| `net/link.c` / `net/link.h` (プロトコル本体) | **触らない**。依存は `LINK_IRQ_SAVE/RESTORE/IDLE` の 3 マクロ (`net/link.c:29-45`) と、100Hz を前提にした 4 定数 (`net/link.h:81-86`) だけ |
| `kapi/kapi_host.c` (KAPI ラッパー) | ポインタを `u32` で扱う 1 行 (`:32`) だけ |
| `tools/host_agent.py` | 触らない (ホスト側の Python) |
| ディスパッチャの引数取り出し (`exec/exec.c:1020-1090`、`kernel/ring3_entry.asm`) | **書き直し** ((e)) |
| `drivers/ne2000*.c` (ポート I/O・PIO) | **書き直し** ((c)) |
| `fs/ext2_*.c` (LE + 非アライン) | **要置換**。iso9660 が手本 ((a)) |
| `include/types.h` の `u32 = unsigned long` | **要修正** ((h)) |

「リンク層より下だけを書き直せば上は動く」という切れ目は、N1 の実装で
`drivers/ne2000.h` の 6 本の API に閉じたことで実際に確かめられている ―
ホスト試験 `tools/tests/net_link_host.c` が、その 6 本を贋物に差し替えるだけで
`net/link.c` を **1 行も変えずに** x86-64 Linux 上で走らせている。

## (b) 追記 — 順序 2 で実施 (2026-09-15)

上記「移植時にやること」の 1 と 2 は移植準備の順序 2 として実施した。`hlt` 12 か所を `_halt()` /
`_stop()` へ、`net/link.c` の `sti;hlt` を **不可分の `_idle()`** へ寄せ、`include/io.h` に各原始命令の
契約を書いた。`exec/exec.c` の CPL=3 へ降りる asm ブロック内の `cli` は切り出せないので
`ARCH-ASM-OK` の印を付けて残した (順序 3 で `arch/x86/` へ丸ごと移す)。番人 `tools/check_arch_asm.py`
(`make check` の `check-arch-asm`) が対象ディレクトリの C ソースに直書きが無いことを検査する。

## (c) 追記 — 順序 3 で実施 (2026-09-15)

`include/io.h` を **契約** (宣言と註だけ) と **実装** に割り、実装を軸ごとの
ディレクトリへ移した。CPU に属するもの (割り込み制御・CPU 停止・`_lidt`) は
`arch/x86/arch_io.h`、機種に属するもの (ポート I/O・`io_wait`) は
`platform/pc98/platform_io.h`。契約側は末尾で固定名 `arch_io.h` /
`platform_io.h` を引き、どの実装が来るかは `build/config.mk` の
`ARCH ?= x86` / `PLATFORM ?= pc98` と `INC_COMMON` の 2 本の `-I` だけが決める。
足し方は [`arch/README.md`](../../../arch/README.md)。x86 の実装本体は 1 文字も
変えていない (`C_KERNEL` 92 本の `.o` が基点と一致)。

**上の「順序 3 で `arch/x86/` へ丸ごと移す」は、まだ行っていない。**順序 3 の
範囲は `io.h` の分割と受け皿の導入まで。`exec/exec.c` の `ARCH-ASM-OK` ブロック、
`kernel/gdt.c` / `kernel/tss.c` / `kernel/paging.c` の `cr0`/`cr3`、
`gfx/gfx_internal.h:52` の移設は、移設の単位 (関数かファイルか) と移設先の
判断が要るので次の票へ送った。ARM 計測で (a) に残っている 5 本がそれ
([`ARM_GAUGE.md`](ARM_GAUGE.md) §9)。
