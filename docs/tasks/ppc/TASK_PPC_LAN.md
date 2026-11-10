# TASK_PPC_LAN — PowerPC 移植: 内蔵 Ethernet (GMAC) で起動ログをホストへ、固定の診断コマンド

> 上位: [TASK_PPC_KBOOT](TASK_PPC_KBOOT.md)

> 状態: **設計中 (2026-11-10)** — 設計は承認されて実装を待つ。確定設計は [PPC_LAN_DESIGN](../../decisions/ppc/PPC_LAN_DESIGN.md)。手本は BSD (ユーザー決定)。メーカー資料は未入手で、実機に載せる条件にはしない。実機で初めて確かめる物 (clock・PHY・DMA・cache) は設計の §8 の順で観測する。QEMU で確かめられる段から始める。

## 0. ユーザー決定

- 2026-11-09 決定 (c) (票 TASK_PPC_SEP): 到達点 ② の直後は、内蔵 Ethernet (GMAC) と Host Services の LAN 経由のログ・遠隔操作を最優先にする (実機にはシリアルを簡単に取り出す経路が無い)。
- 2026-11-10 (PM の 3 問への回答):
  1. 道は「GMAC を最初から」— QEMU の sungem と実機 QuickSilver で同じ driver を使う (既存の 82557 を先に動かす案・GMAC を実機だけにする案は採らない)。
  2. ログの保証は「届く分だけ + 欠落数」— 固定長の待ち行列にためて polling で送り、あふれた分は捨て、起動の識別・連番・欠落数をホストに見せる。再送しない。割り込みは使わない。
  3. 遠隔操作は「固定の診断コマンドから」— 決まった要求に答える小さな monitor を先に作る。任意の shell とファイル同期 (hsync) は、ユーザープログラムを動かす段 (③) の後に分ける。

- 2026-11-10 (PM の問い「メーカー資料が入手できない場合に BSD の実装だけを根拠に実機へ載せてよいか」への回答): **「BSD を手本に」** — GMAC・UniNorth・PHY は BSD (OpenBSD・NetBSD・FreeBSD) の実装を手本にして進める。メーカー資料は、手に入れば照合に使うが、実機に載せる条件にはしない。

PM 決定 (2026-11-10、設計の往復の中で。ユーザーに報告済み):

- 起動の識別 (boot_id) はゲストが作る。相手 (peer) の既定は broadcast で、OF の bootargs は任意の上書き。実機で起動ごとの手入力を要求しない。
- 診断コマンドは 5 個。5 番目は範囲つきの読み出し (PEEK): managed RAM とカーネルの像だけ、1 回 256 バイトまで、範囲外・MMIO・NIC が持っている受信 buffer は拒否。書込みと任意番地は入れない。
- ホストの道具 (tools/ppc_lan_host.py) の運び方は 2 つ: QEMU と試験は UDP (QEMU の socket netdev は Ethernet の frame を 1 datagram に 1 枚で運ぶ。root も TAP も要らない)、実機は AF_PACKET。codec は共通。
- PLN1 (線の上の形式) に認証は無い。既定では起動ログを broadcast で流し、同じ L2 から PEEK でカーネルの RAM を読める。開発用の LAN での診断が前提で、共用の LAN につなぐときは peer を指定する。
- ② の境界 (mount・module・exec・USER なし) は広げない。NIC の失敗で OS を止めない (止めるのは「bus master の解除を書いたのに読み戻しが 1」のときだけ)。

## 1. 詳細設計 (確定) の要約

全文は [PPC_LAN_DESIGN](../../decisions/ppc/PPC_LAN_DESIGN.md)。

- 層: OF の tree の採取 (takeover の前) → UniNorth の PCI の設定空間 (OF が示す 1 つの BDF だけ) → MMIO の alias (BAR 2MiB のうち 64KiB を I/G で) → DMA の pool (像の予約の中、descriptor は I/G、buffer は cache の同期) → GEM の core (reset・MIF/PHY・ring・MAC・tx・rx、レジスタも descriptor も LE) → ログの sink (byte 詰めの待ち行列) と線の上の形式 → 診断 monitor → ホストの道具。
- 置き場: drivers/net/gem/ (core)、platform/powermac/ (tree・PCI・DMA・clock・ログ・線・monitor・service)、arch/ppc/ (alias・cache・probe)、tools/ (ホストの道具と meter)。x86 の生成物は変えない (全段 I、build_compare で確かめる)。
- 段: 製品の段 P0〜P33 (1 ファイル = 1 段) と、各段の直後の host 試験の段 H*。並列に出せる組と前提は設計の §6。最初の段 P0 は QEMU での採取とホストの UDP 経路の確認、資料の取り寄せ。
- 受入: A0〜A12 (設計の §7)。QEMU の meter (tools/ppc/kboot_run.py) に LAN の印を足し、既存の KBOOT の 9 行は変えない。7450 と 7455 で、保存したログと欠落数・診断の応答を照合する。
- 実機で初めて確かめる物: clock gate、PHY の固有の処置、DMA の到達性と cache、PCI の domain の差。QEMU の合格から外挿しない。

## 2. 段の状態

| 段 | 状態 |
|---|---|
| 調査 (x86 の LAN の層、GMAC の手順、PPC の PCI、案の比較) | 済 (2026-11-10、~/os32-tmp/run/ppc-lan-survey/SURVEY.md) |
| 設計の往復 (起草 r1 → レビュー → r2 → レビュー → 確定) | 済 (2026-11-10) |
| P0 (QEMU での採取、ホストの UDP 経路、資料の取り寄せ) | 一部 (QEMU 8.2.2 のモニタでの観測だけ: 既定の NIC は sungem 106b:0021、BAR0 は 2MiB、UDP の socket netdev の指定が通る。ゲストを走らせた採取と資料は未) |
| P1〜P33・H* | 未着手 |
