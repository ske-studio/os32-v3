# TASK_DEP_DIR — カーネルからドライバへの依存を、登録の口に寄せて外す

> 上位: [ROADMAP](../../ROADMAP.md)・開発規約 [POLICY_DEV](../../POLICY_DEV.md) §1 (カーネル層の不具合を先に)

> 状態: **実装中 (2026-10-27)** — D1〜D3 (調べの優先 1〜3) は main、受入は NP21/W で合格。kernel.c が直接読むドライバのヘッダは dev.h (抽象の口) だけ。KAPI の番号・ABI は不変。残りは §3 の「後」の行 (優先 4〜6、大)、粒度を決めてユーザーに出す。実機の確認は DEFERRED_TESTS の DEPDIR-D1-FD。

## 0. ユーザー指示 (2026-10-26)

「sol にドライバとカーネルの依存方向の状態を調査させてください。…カーネルがドライバ側への依存があると移植性が下がると思うので。ドライバがカーネルに依存するのは普通と思います。」→ 調べの後「1. から順に」(直す)。

## 1. 調べの結果

調べは sol (読み取りだけ、基点 d0f5b0e8)、表は `~/os32-tmp/run/depdir/SURVEY.md`。VFS・ブロックデバイス表・描画 HAL・部品の関数表は口を経ていて良い。残る逆向きは IRQ・タイマ・標準入力・時計・起動の配線・画面の復旧に装置の名前と知識が入っているもの。`.c` を `.mod` に移すだけでは PCI ID・IRQ 番号・型・起動の方針が常駐側に残り、向きは変わらない。

## 2. 方針 (PM 決定)

- 直すのは「カーネル → 具体ドライバ」の向きだけ。ドライバ → カーネルは普通の依存として残す (ユーザー指示)。
- 口は今ある形に寄せる (irq_register、VFS の登録表、部品の関数表)。新しい仕組みは最小にする。KAPI の番号と ABI は変えない (内側だけ差し替える)。
- 機械語は変わる。振る舞いは変えない — 各段のゲスト (FD 起動・HDD 起動・LAN・シリアル・入力) で前と同じことを確かめる。

## 3. 段

| 段 | 群 (SURVEY の #) | 中身 | 大きさ | 状態 |
|---|---|---|---|---|
| D1 | 16・17 | FDC の IRQ を FDC が irq_register で持つ、NIC・通信の周期処理を登録表に。設計 [DEP_DIR_D1_DESIGN](../../decisions/DEP_DIR_D1_DESIGN.md)、段は D1a (FDC/IRQ11) → D1b (tick の表) を sol、D1c (ゲスト・変異) は PM | 小〜中 | **main** (00d2a2ba、astra 取り込み可、PM のゲスト合格 `~/os32-tmp/evidence/2026-10-26/depdir-d1/RESULT.md`、Ra266 の FD 起動は DEPDIR-D1-FD) |
| D2 | 18〜25・33〜35・38・45 (ポート側) | 入力・コンソール出力・時計・シリアル診断を口の後ろへ (VFS の stdin・exec・GUI・KAPI の内側、FatFs の時刻も)。D1 の取り込みの後の main を基点に直列 | 中 (A1・A2・B・C の 4 段)。設計 [DEP_DIR_D2_DESIGN](../../decisions/DEP_DIR_D2_DESIGN.md) | **main** (959da3ea・3622900d、astra 取り込み可、P3 2 件は D3a へ、PM のゲスト合格 `~/os32-tmp/evidence/2026-10-26/depdir-d2/RESULT.md`) |
| D3 | 1〜14・31・32 | 起動の配線を platform の登録一覧に、root の選び方と装置名の解決をレジストリに。設計 [DEP_DIR_D3_DESIGN](../../decisions/DEP_DIR_D3_DESIGN.md)、段は D3a→b→c1→c2→d (codex)、D3e (PM) | 中 | **main** (97ffad3f・76f85591、astra 取り込み可 (P2 の disk.h を PM が直す)、PM のゲスト合格 `~/os32-tmp/evidence/2026-10-27/depdir-d3/RESULT.md`、Ra266 は DEPDIR-D1-FD) |
| 後 | 15・26〜30・36〜44・45 の PCI 側・46〜54 | 部品の match/IRQ 記述・専用 KAPI、PIC/PIT・メモリ地図・reset・clock、表示・障害出力・復旧・splash | 大 | D3 の後に粒度を決めてユーザーに出す |
