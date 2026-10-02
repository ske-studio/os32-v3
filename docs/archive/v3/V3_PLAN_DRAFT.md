# V3_PLAN_DRAFT — v3 本案の草案 (草案群のまとめ)

> 状態: **完了記録 (2026-09-30)** — **本案 [V3_PLAN.md](../../tasks/v3/V3_PLAN.md) に昇格した** (2026-09-30。U3 (OS64) と U4 (目的と範囲) を同日ユーザーが決定し、§7 の判断と突き合わせが揃ったため)。以後この文書は昇格時点の記録で、目的・範囲・目標の 2 段・柱と順序の正典は本案。論点の経緯 (§1 振り分け、§4 食い違い、§5 互換、§6 fork、§7 判断の往復) の置き場としてここに残す。
> それまでの状態: **草案 (2026-09-30)** — v3 に関わる草案・票・決定を 1 本にまとめたもの。**本案ではない**。本案への昇格はユーザー判断と Codex の突き合わせ (§7) の後。既存の草案の本文は書き換えていない (ここから参照するだけ)。**P1 メモリマップは 2026-09-30 に決定** (§3-1、正典は [TASK_MEMMAP_V3.md](../../tasks/v3/TASK_MEMMAP_V3.md))。目標の 2 段・SQLite/FEP の常時読み込み・P8/P9 の別柱化・KAPI/ABI の後方互換・高位カーネルの時期も同日に決定 (§2、§3、§7-1)。**§7-2 の論点 X1〜X8 は 2026-09-30 に Codex (gpt-6-astra) が回答し、ユーザーが推奨をすべて承認** — 結論と反映先は §7-2 の表、決定の改めは TASK_MEMMAP_V3 **D35** (D7 の改訂: fork 時の KAPI 整理でスロット順を変えてよい) と「IRQ の合成器は整数演算から」(U5) の 2 点、残りは決定を変えない補足 (TASK_MEMMAP_V3 §8-4)。
>
> 発行: コーダー `claude-fable-5-1` (worktree `wt/v3-draft`、基点 feat/gui `39a09b89`)、PM の指示とユーザー指示 (2026-09-29「v3 の草案をまとめる」) による。
> **メモリマップの柱 (§3-1) は決定済み** — ユーザー・Fable・Codex の 3 者討論 (2026-09-29〜30、Codex Approve) で決めた。§3-1 は論点の記録と決着先だけを置き、結論の本文は TASK_MEMMAP_V3 に書いた (二重に持たない)。

決定済みの前提 (正典は [ROADMAP.md §0-1](../../ROADMAP.md)、ここには写さない): 現行の開発は v3 / v2.x (os32 の `main`、タグ `v2.1`) は戻り先 / 本案確定後に別リポジトリ **os32-v3 (パブリック)** へ fork / 文書の正典は os32-v3 / CI は作り直し / `apps` `game` の submodule は引き継ぐが当面の保守は os32 側 / `tools/` と `~/np21w-src` は共有・動作保証は v3 側だけ。

読み方: §1 が材料の一覧、§2 が筋、§3 が柱と順序の案、§4 が食い違い、§5 が互換、§6 が fork、§7 が判断点。数字 (カーネル予算など) はすべて写しで、正典は [02_memory.md §2-1](../../02_memory.md) の生成ブロックと [KAPI_SPEC.md](../../KAPI_SPEC.md)。

---

## 1. 草案の一覧 (所在・状態・要旨・v3 で扱うかの提案)

「提案」列: **v3** = v3 本案の範囲 / **v3 後半** = v3 の入れ物ができてから / **v4** = v3 の後 (ゲーム基盤・多機種) / **並行** = v3 と独立に実機の前に居るときに閉じる (新機能ではない) / **記録** = 完了記録・現行仕様として参照するだけ / **捨てる** = 撤回を提案 (理由つき)。状態は各票の状態行の写し (2026-09-29 時点)。

### 1-1. v3 の計画と票 (`docs/tasks/v3/`)

| 文書 | 状態 | 要旨 | 提案 |
|---|---|---|---|
| [PLAN.md](PLAN.md) | 計画 (2026-09-17) | 機能を足す前に入れ物を作り直す。順序 C11 → 再配置 → 動的読み込み → PCI → 82557 (§1「動かさない」)、再配置の前に決めること (§2)、ドライバの動的読み込み (§3)、HAL の棚卸し (§3-1)、アプリへの払い出し (§4)、温めているアイデア 5-1〜5-7 (§5) | **v3 (本案へ書き直す元)**。§1 の順序は 09-22 の決裁で崩れている (§4 D1)。§5 は個別に振り分ける (§1-6) |
| [TASK_MEMMAP_V3.md](../../tasks/v3/TASK_MEMMAP_V3.md) | 設計中 (**方針確定 2026-09-30**、3 者討論で決定、Codex Approve) | v3 のメモリマップ。決定 D1〜D34 (D23〜D28 = 池の運用規則の残りと SQLite の予算、D29〜D34 = 保留 5 件 U6 の拾い方、2026-09-30)、帯の表 (恒等のシステム + 池 + アプリ帯 0x80000000 + lease 窓)、物理台帳と P2V/V2P、池の運用規則 R1〜R7、SQLite・FEP・モジュール・ブート順、Unicode 表・OpenType・BB、票 T0〜T7、受入、未確認の前提 U1〜U24、経緯 | **v3 — 決定** (§3-1)。P1 の正典。実装は T0〜T7 の順 |
| [TASK_HAL_WIRING.md](../../tasks/v3/TASK_HAL_WIRING.md) | 受入完了・実機確認待ち (残: W7) | 結線の土台: 割り込みの動的登録 / 8237 の共通部 / DMA プール (0x2E8000、**暫定承諾**) / PCI の結線表 / µs 時計。v2.1 に同梱。`struct pci_driver.size` は外部モジュールへの口だけ | **並行** (W7 実機) + **v3** (§3 の取り決めを外部モジュールへ開く、DMA プールの再配置は §3-1 の論点) |
| [TASK_PCM_CS4231.md](../../tasks/v3/TASK_PCM_CS4231.md) | 受入待ち (E0〜E3 合格、残 E4・E5 NP21/W、E6 実機) | CS4231 の PCM 再生 (§5-5 の P1)。KAPI v61。リングは DMA プール、ステージングは KHEAP | **並行** (E4〜E6) + **v3 後半** (P2〜P4 = ソフト OPNA 合成器、動的読み込みの顧客) |

### 1-2. 思想の草案 (`docs/` 直下)

| 文書 | 状態 | 要旨 | 提案 |
|---|---|---|---|
| [DESIGN_APP_FIRST.md](../../DESIGN_APP_FIRST.md) | 草案 (2026-09-26)。策定時の「設計思想の草案」は語彙外 → 2026-09-30 (手順 f) に揃えた (§4 D19) | 前景 1 アプリへ資源を集中する。マルチタスクを主目的にしない。評価軸は「1 本にどこまで渡せるか」。640×480×16bit を高機能グラフィックスの境界とし、Video HAL / VESA2 的互換層 / SDL 1.2 の受け皿。ZSNES を負荷試験台に。判断基準 7 項目 (§12) | **v3 (思想の芯)**。§12 の判断基準を本案の「変更の受け入れ基準」に採る。Video HAL・互換層・SDL は **v3 後半** (§3 P9) |
| [AUXILIARY_CORE_SERVICE.md](../../AUXILIARY_CORE_SERVICE.md) | 草案 (2026-09-28) | 余剰 CPU コアを固定機能アクセラレータ (Graphics / Audio worker) として使う。SMP スケジューラは持たない。x86 AP / ARM で同一モデル。縮退モデル。実装段階案 1〜7 | **v4** (対象機に複数コアの PC-98 は無い。Ra266 は 1 コア)。v3 では HAL のバックエンド表に「補助コア / ホスト / 主コア」の差し込み口を**塞がない**ことだけ守る |
| [LEGACY_LIVING_PRESERVATION.md](../../LEGACY_LIVING_PRESERVATION.md) | 草案 (2026-09-28) | 実機の動態保存。判断基準「実機で行う意味があるか」。時代依存の処理 (TLS / codec / AI) は Host Service / OS64 へ。レガシー側の契約は小さく安定させる。ACS と Host Service の役割分離 | **v3 (思想の芯)**。Host Services の延長 (§1-6 の 5-6 / 5-7) の根拠。**OS64** の語はここと ACS だけ (§4 D6、§7 U3) |
| [V4_GAME_PLATFORM_DRAFT.md](../v4/V4_GAME_PLATFORM_DRAFT.md) / [tasks/v4/README.md](../../tasks/v4/README.md) | 草案 (2026-09-07) | Portable API / Platform API の分離、`arch/` `platform/` の長期構造、Native / Hosted / Game Runtime、エンジン Core / Module、Rust は上位から、OS32 Fabric / OS32 Link、想定ターゲット表、Phase V4-0〜4 | **v4** (決定済み: v3 の後)。v3 で守るのは V4 §11 の 8 原則のうち 1・2・4・6 (PC-98 固有を portable 層へ流さない、HAL 境界、固定番地を契約にしない、platform-only は明示) |

### 1-3. 設定・メモリの設計提案 (`docs/tasks/settings/`、v1.3 の残り。v3 の入力)

| 文書 | 状態 | 要旨 | 提案 |
|---|---|---|---|
| [DESIGN.md](../../tasks/settings/DESIGN.md) | 受入完了 (2026-09-14) | 設定レジストリ (`system.cfg` + `settings.db`) の置き場の正典。§7 対象外 (アクセス制御・通知) | **記録** (現行仕様)。v3 で触るなら §7 の対象外だけ |
| [DEVICE_RESERVATION.md](../../tasks/settings/DEVICE_RESERVATION.md) | 計画 (2026-09-30、U6 決定を注記) | 任意デバイス窓の予約 broker: PFN 半開区間の一括 transaction、owner 台帳、GUI 境界 (master CR3・live AS=0・exec 不在) での初回予約、exec arena 全域を将来用途として禁止、mapping と公開順 T1〜T5 | **v3 P4 — 改訂して拾う (U6 決定 2026-09-30、TASK_MEMMAP_V3 D33)**: 核と owner 台帳は P1 T1 の台帳の MMIO 登録へ、時期は識別 + 予約 = 起動時 / probe + enable = GUI 境界、許可範囲を検証済みの実測 BAR へ (X4 を Codex へ)。§5 と exec arena の禁止は削除。改訂本文は P4 着手時 |
| [MEMORY_RAM_INTEGRATION.md](../settings/MEMORY_RAM_INTEGRATION.md) | **撤回 (2026-09-30)、archive へ** | RAM 統合 Phase 2: physmem → 起動時配置 → pgalloc → RAM mapping → sys/exec → activation を一緒に接続する | **撤回 (U6 決定 2026-09-30、TASK_MEMMAP_V3 D32)**: A / B の大半は K6 で着地済み、残り (A3 / C / D) は D3 で撤去と決めた機構の完成形。残る 2 点 (8MB もモデル経路 = legacy fallback 撤去、0594h の機種照合 U24) は P1 T1 の受入へ |
| [FEP_BOUNDARY.md](../../tasks/settings/FEP_BOUNDARY.md) | 設計中 (2026-09-30、U6 決定を注記) | FEP の KAPI 境界で未検証ポインタを SQLite に渡さない (bounded kernel copy、checked copyout)。上限表 §3 | **v3 P10 — 独立票にせず P1 の T2 / T4 / T5a の要件として (U6 決定 2026-09-30、TASK_MEMMAP_V3 D31)**: B1 → T2、B3 / B4 → T4、B2 → T5a。旧 `db_exec` / `db_prepare` の 1024B 超は失敗に |
| [F2_OWNERSHIP.md](../../tasks/settings/F2_OWNERSHIP.md) | 設計中 (2026-09-30、U6 決定を注記。F2b の内部基盤だけ着地、呼び出し側は未接続) | SQLite 接続単位の VFS FD 所有・失敗隔離・exec 終了統合。接続専用 VFS instance + FD lease | **v3 P10 — 残り全部 (F2b 呼び出し側・F2c・F2d 隔離・R0 / R1) を P1 の T4 + T5a の受入に畳む (U6 決定 2026-09-30、TASK_MEMMAP_V3 D30)**。RESIDENT group は 2 本 |

### 1-4. 実機 (`docs/tasks/realhw/`) と GUI・メモリの残件

| 文書 | 状態 | 要旨 | 提案 |
|---|---|---|---|
| [realhw/PLAN.md](../../tasks/realhw/PLAN.md) | 実装中 (2026-09-29) | Ra266 で動かす計画。到達点は RELEASE_v2.1 §1。残: 82557 L-B、PCM E6、Trident、PEGC 目視。§0「主戦場を移さない」、§5 LGY-98 は消さない、§6 PCI は土台、§9 USB/1394 はまず 82557 の後 | **v3** (§3 P5)。§0・§7・§9 の古い行 (「v1.x のあいだは着手しない」「Trident 保留」) は状態行と §7 の追記で上書き済み |
| [TASK_TRIDENT_DRIVER.md](../../tasks/realhw/TASK_TRIDENT_DRIVER.md) | 設計中 (設計票 v5、Codex 5 回目で Approve、実装未着手) | 内蔵 Trident 1023:9660 を GUI の画面に。段 0 (採取済み) → 資料 → 段 1 (許可リストの読み取り) → 段 2 / 2-X (探索試験、承認済み) → 段 3 (backend、8bpp) → 段 4 (解像度) → 段 5 (エンジン)。決裁 T1〜T10 (T8・T9・T10 済み)。帯 [0xFE400000, 0xFE800000) の枠、面公開 API の変種 (§4-2) | **v3** (§3 P5)。T7 (静的か動的か) は §3 P2 と連動。§4-2 の記述子・paging の変種は **P4 の要件** |
| [TASK_LAN_82557.md](../../tasks/realhw/TASK_LAN_82557.md) | 実装中 (L-A・L-D 着地、次は L-B) | 内蔵 82557 で Host Services。L-A PCI 列挙 (KAPI v58) 済み、R1〜R4 実機で取得 (irq 3、io 0x6000)。L-B (10〜15KB) は静的で入れる決裁 (b)。L-C NIC 境界、L-E 疎通 | **v3** (§3 P5 の最初)。§1 の予算行 (442.6KB/468KB) は古い (§4 D2) |
| [TASK_PEGC480_REALHW.md](../../tasks/realhw/TASK_PEGC480_REALHW.md) | 受入完了・実機確認待ち (GUI の目視) | Ra266 の PEGC 640x480。実機 ROM の OUT 列に合わせた。v2.1 に同梱 | **並行** (目視だけ) |
| [TASK_FD144.md](../../tasks/realhw/TASK_FD144.md) | 受入完了・実機確認待ち | 1.44MB FD 起動 (エミュレータ合格)。D88 の 1.44MB は未対応 | **並行** |
| [memory/APP_BAND_PDE.md](../../tasks/memory/APP_BAND_PDE.md) | 受入待ち (§5 のゲスト受入が未記録) | アプリ帯の可変 PDE 化 (1〜2 枚、0x400000〜0xBFFFFF、天井は PEGC 窓 0xF00000)。実装 `b8dab24` は v2.1 に同梱。4-B: 8MB 構成の EXEC_DYN_RESERVE の重なりは既存のまま | **並行** (§5 受入を先に閉じる — PLAN §4「受入していない土台の上に見直しを積まない」) + **討論の論点** (§3-1 M4) |
| [gui/TASK_KBD_NAV.md](../../tasks/gui/TASK_KBD_NAV.md) | 受入完了・実機確認待ち (K3、カナ) | キーボードだけで GUI を操作 (Win98 の割り当て)。`libos32ui` (microUI) にはマウスキーが届かない (RELEASE §6) | **並行** (K3) + **v3 後半** (microUI の扱い、§3 P8) |

### 1-5. 移植性・アーキテクチャ (v3 の前提を縛るもの)

| 文書 | 状態 | 要旨 | 提案 |
|---|---|---|---|
| [portability/ARM_GAUGE.md](../../tasks/portability/ARM_GAUGE.md) | 計測記録 (55/93、2026-09-15) | ARM コンパイル計測。**§10: 移植は 32 ビットのみ (ユーザー決定 2026-09-17)、64 ビット対応の抽象化は足さない**、`u32` = ポインタ幅 | **v3 の前提** (固定長維持、§3 P0 の「`u32` か `<stdint.h>` か」の根拠)。ARM 実装そのものは **v3 の後** (再配置の後、KSTACK §7-5 の 4) |
| [portability/SURVEY_N1.md](../portability/SURVEY_N1.md) | 調査 (2026-09-14) | N1 で触れた CPU 依存 (LE アクセサ、`cli`/`sti`/`hlt`、直列化)。新しい層ごとに同じ観点で追記する規則 | **記録** + 規則は v3 でも維持 (新しい層の票に移植性の節) |
| [arch_port/00_INDEX.md](../arch_port/00_INDEX.md) (+ M0 監査、Brain i.MX28) | 計画 (2026-09-08、**別リポジトリの快照**) | 他アーキ移植調査の索引。M0: 非整列 3 か所・キャッシュ前提 (ロードしたコードへ飛ぶ経路・ページ表操作)。§3 の 3「ISA 非依存化の切り分け」は未実施 | **v4** (快照は更新しない)。M0 の「ロードしたコードへ飛ぶ経路」は §3 P2 (動的読み込み) の設計に効く |
| [arch/README.md](../../../arch/README.md) | 現行 | `arch/<arch>/` (CPU) と `platform/<platform>/` (機種) の 2 軸。足す手順 | **記録** (現行仕様)。V4 §4 の長期構造と同じ形 |
| [ROADMAP.md §2](../../ROADMAP.md) | 計画 | 長期: 協調型 → **v3 でプリエンプティブ寄りを検討**、実機、v3 の全体計画、再配置と C11 の順序、着手の前に決めること 3 点、他アーキ、GUI アプリ群 (先送り)、16bit DOS 移植スキーム | **v3** (§3 P6・P0)。「プリエンプティブ寄り」は APP_FIRST と衝突 (§4 D3)。DOS 移植スキームは **v4** か捨てる (V86 が既にあり、INT 21h → KAPI 変換の需要が票に無い) |

### 1-6. PLAN §5 のアイデア (計画ではない) の振り分け

| 項 | 内容 | 提案 | 理由 |
|---|---|---|---|
| 5-1 | ネットワーク越しのホストを仮想メモリに | **捨てる (v3 では)** | 再入 (NIC ドライバのメモリが退避済み) が核心で未解決、先に LAN の往復 ms を測る必要。APP_FIRST §4 の「1 本に大きく渡す」は物理 RAM (Ra266 64MB) で足りる |
| 5-2 | V86 の装置要求をホストへ逃がす (86 ボードを演じる) | **捨てる** (PLAN 自身が 5-5 で置き換え) | ホスト無しで同じ目的を達する 5-5 がある。「実機に無いプリンタを DOS に見せる」だけ **v3 後半** の候補 |
| 5-3 | V86 の EMS をネットワークへ | **v4 か保留** | INT 67h 1 本で収まるが、需要 (EMS を要る DOS ソフト) が票に無い |
| 5-4 | 実機のレガシー VRAM をホストへ転送 | **v3** (P5 の実機検証の道具) | 実機の画面を取る手段が今も無い (`pegcchk`・写真頼み)。テキスト VRAM 数 KB を SerialFS / LAN で送るのは小さい |
| 5-5 | ソフトウェア 86 互換音源 (P1 PCM 済み、P2 Rust 合成器、P3 snd の口、P4 V86 へ) | **v3 後半** (P2〜P4) | 動的読み込み (P2) の顧客。Rust をカーネル文脈で呼ぶ点は決裁 (§4 D8) |
| 5-6 | ネットワーク越しの CD でインストール | **v3 後半** | 82557 L-B〜L-E の上。`cdinst` 無変更 |
| 5-7 | HostDrv を Host Services 経由でも | **v3 後半** | 5-6 と同じ土台。SerialFS が LAN 無し機種の同じ役 |

### 1-7. 現行仕様・完了記録で v3 が前提にするもの

| 文書 | 何を前提にするか |
|---|---|
| [02_memory.md §2-1](../../02_memory.md) (生成) / `include/memmap.h` | 現行の地図。**カーネル本体 583.0KB / 予算 596KB (残り 13.0KB)**。デバイス窓の帯 `[0xFE000000, 0xFF000000)` (`MEM_DEVICE_APERTURE_*`、`MEM_PHYS_RAM_CEILING` = 0xFE000000)、Cirrus は `[0xFE000000, 0xFE200000)` (NP21/W は 4MB)、PEGC は 0xF00000 の 15MB 穴、先頭 4MB の PT を静的に 1 枚。DMA プール 0x2E8000 (64KB)、KHEAP 192KB、静的ページ表 36KB は画像の中 |
| [archive/kernel_v21/TASK_KSTACK_USER.md §7-4 / §7-5](../kernel_v21/TASK_KSTACK_USER.md) | v3 に持ち越す懸念 5 点: 再配置と C11 を同時にしない / C11 を先に / 上限を先に決める / ARM との順序 / `u32` か `stdint` か |
| [KAPI_SPEC.md](../../KAPI_SPEC.md) | v68、関数表の容量 R = 300 (実装 240 = スロット 0〜239。v63 で予約した 230〜299 のうち 230〜239 は v64〜v68 で使用済み、残り 60)、データ欄 0x4B8 固定、OS32X ヘッダ v3。規則: **残りが 16 本を切ったら次の R を決める票** (ROADMAP §0) |
| [tasks/gui/DESIGN.md §6](../../tasks/gui/DESIGN.md) | HAL の下の 2 層 (チップドライバ + ボードグルー)。Trident はこの形に載る |
| [archive/agents/HANDOVER_v14.md §3](../agents/HANDOVER_v14.md) | 保留 5 件 (F3a〜c / F2c / FEP_BOUNDARY / MEMORY_RAM_INTEGRATION / DEVICE_RESERVATION) — 「v3 本案の段で拾うかを決める」(ROADMAP §1 v1.3)。**2026-09-30 に U6 で決定「一部」** ([U6_PENDING_REVIEW](U6_PENDING_REVIEW.md)、TASK_MEMMAP_V3 D29〜D34) |
| [tasks/network/PLAN.md](../../tasks/network/PLAN.md) / [LINK_PLAN.md](../../tasks/network/LINK_PLAN.md) / [HOST_SERVICES_PLAN.md](../../tasks/network/HOST_SERVICES_PLAN.md) | リンク層と Host Services は NIC の上に載る (LGY-98 は回帰用に残す)。N5 は 82557 L-B に吸収 |
| [tasks/hotdeploy/DESIGN.md](../../tasks/hotdeploy/DESIGN.md) / [boot_reform](../v21/boot_reform/00_OVERVIEW.md) / [v86v2](../v21/v86v2/README.md) | 完了済みの領域。hotdeploy 窓は 2026-09-09 に撤去 (文書は archive 候補、HANDOVER の「文書の整理の第 2 陣」) |

---

## 2. v3 の目的と範囲 (草案群から読み取れる 1 本の筋)

草案群には 4 つの動機が別々の文書に書かれている。並べると 1 本になる。

| 動機 | 出典 | 一言 |
|---|---|---|
| **入れ物を作り直す** | PLAN §0、KSTACK §7-4 | 1MB の帯に 1041KB を詰めた穴の根は帯の切り方。残り 13KB では PCM の P2 も 82557 の先も積めない。**帯の中で削るのは延命** |
| **アプリを優先する** | DESIGN_APP_FIRST | 前景 1 本に CPU・RAM・装置を集中する。評価軸は「何本同時に」ではなく「1 本にどこまで安全に渡せるか」。GUI の 4 本同時 (T2a) はこの軸を否定しないが主目的ではない |
| **実機で動かす** | realhw/PLAN、RELEASE_v2.1 | v2.1 で FD 起動・HDD 起動・シリアル・PCI 列挙まで届いた。残りは装置 (82557 / PCM / Trident) で、**エミュレータが再現しない**ものばかり。実機はエミュレータの嘘を暴く道具であって主戦場ではない |
| **レガシーを生きたまま保存する** | LEGACY_LIVING_PRESERVATION | 実機で行う意味のある仕事 (当時の CPU・表示・音・入力) は実機に残し、時代依存の処理 (TLS / codec / AI) は Host Service へ。レガシー側の契約は小さく安定させる |

**v3 の目的 (提案の一文 → U4 で承認 2026-09-30、正典は本案 [V3_PLAN.md](../../tasks/v3/V3_PLAN.md) §1)**: *PC-9821 の実機で、1 本のアプリケーションに機械の資源を渡し切れる入れ物を作り直す。入れ物とは、カーネル帯の切り方 (再配置)、ドライバの置き場 (動的読み込み)、装置窓の資源割当、実機の装置ドライバ、そしてその上の HAL の口である。時代依存の仕事は Host Service に任せ、OS32 側の契約は小さく保つ。*

**v3 の範囲に入れないもの (提案 → U4 で承認 2026-09-30。ただし GUI アプリ群は範囲外から外して v3 後半へ。正典は本案 §2-1)**: 多機種・多アーキテクチャ (V4)、OS32 Fabric / Link (V4)、補助コア ACS (V4)、SMP、GUI アプリ群の拡充 (ROADMAP §2「GUI アプリケーション群」は v3 後半の末尾か v4)、16bit DOS 移植スキーム、USB / IEEE 1394 (realhw PLAN §9)、スワップ (APP_BAND_PDE §6)、**カーネルの仮想アドレス化 (高位カーネル) — v4 以降の候補** (ユーザー決定 2026-09-30、TASK_MEMMAP_V3 D20。v3 は恒等写像のまま P2V/V2P の集約だけ)。

### 2-1. 目標の 2 段 (ユーザー決定 2026-09-30。正典は本案 [V3_PLAN.md](../../tasks/v3/V3_PLAN.md) §2-2 へ移した — 下は昇格時点の記録)

| 段 | 内容 | 測る項目 |
|---|---|---|
| **最低条件** | **8MB 機で GUI + 私有メモリ総量 2MB のアプリ** が動く (9801 planar と PEGC の両方。「2MB」はコード・BSS・ヒープ・スタック・shlib data 複製を含む私有総量、内訳はアプリの任意) | 起動 kselftest 0 fail、地図検査 0 件、2MB の試験アプリ 3 種の起動・描画・終了 (TASK_MEMMAP_V3 §7) |
| **快適さの判定** | **前景アプリ 1 本**で、P100/32MB の Windows 95 の同種アプリより快適 (同時使用はしない — プリエンプティブではないので、判定に複数アプリの同時実行は含めない) | Word/Excel 級の起動 2 秒以内、入力→描画 100ms 以内、マルチメディア 1 本が途切れない |
| **目標** | **32〜64MB 機で、前景アプリ 1 本について Windows 2000 当時のアプリの使用感** | 画面の解像度と色数、大きな文書を開く時間、再計算、入力→描画 |

同日の決定で v3 の柱に効くもの (正典は各票、ここは一覧):

- **SQLite と FEP は常に読み込む。例外は FD 起動の回復用構成 (MINIMAL) だけ** (`db_*` は「機能なし」の決まった誤りを返す)。FEP の任意省略は決めない。理由: SQLite は KAPI 経由でユーザーランドに開放された機能 (`settings.db`、libos32cfg、libos32db、install)。8MB の勘定は読み込んだ構成でも余る (決定時 +1.57〜1.74MB、**MEMSYS5 512KB (D27) で +1.44〜1.62MB**、TASK_MEMMAP_V3 D21・D27・§4-3)。
- **P8 (音・入力の層) と P9 (Video HAL / VESA2 的 / SDL) は P1 から切り出した別の柱** (TASK_MEMMAP_V3 D19)。BB は v3 の最初は gshell 所有 + 全画面 lease、PEGC 直描きは全画面 lease に限る、アプリ私有サーフェス + WM 合成はサーフェス層の票で。
- **KAPI / ABI の後方互換は基本考えない** (P7、§7-1 U8)。
- **P6 の意図** (下の表) と **P5 のディスクの速さ** (同) は 2026-09-30 の同じ回で決めた。

**v3 の変更の受け入れ基準 (提案)**: DESIGN_APP_FIRST §12 の 7 項目 (前景アプリの CPU 時間・メモリ・コピー・ハード本来の能力・再利用・古い機との互換が新しい機を縛らない・汎用 OS らしさのための複雑性) を、票の「しないこと」の判定に使う。迷ったら「この変更で 1 本のアプリが PC-98 をより有効に使えるか」。

---

## 3. v3 の柱 — 出典・依存関係・順序の案

| 柱 | 内容 | 出典 | 依存 | 段 (案) |
|---|---|---|---|---|
| **P0 規約と道具** | C11 への移行 ([C1] の改訂、`_Static_assert`)、`u32` か `<stdint.h>` か (混在が最悪)、fork 先での `make check` / CI / 文書検査の作り直し、文書の正典の移転。**先にする根拠の書き直し (Codex X1、2026-09-30、承認)**: 「C11 の `_Static_assert` が無いと再配置を検査なしで行う」**は成立しない** — 現行の `STATIC_ASSERT` (負の配列長、`include/types.h:32`・`kernel/paging.c:108`)、`build/os32.ld` の ASSERT (`:102,115`)、`gen_memmap.py --check`、起動時の PDE 0 照合 (`kselftest.c:543`) が既に配置の矛盾を拒否する (例: SHM の定数不一致は `shm.c:23` で止まる)。C11 を先にする理由は **コンパイラの規格変更と配置の変更を別々に受け入れ、障害の原因を分けるため**。既存の検査は残し、T1 / T2 / T3 ごとに検査対象を広げる (実行時 BAR・owner・AS ごとの lease・動的 PT の失敗回収は静的表明では見えない — 言語規格と独立の作業)。`_Static_assert` への置換だけを再配置の受入条件にしない。型体系の全面改名や無関係な整形を P0 に集めない。**票は [TASK_C11_MIGRATION](TASK_C11_MIGRATION.md) (T0、計画 2026-09-30)** — 範囲は「言語モードと検査の移行」に限定 (全面書き換えなし)。**訂正 (同票 §2)**: 「旗 5 行 + マクロ 1 行」では不足 — `CFLAGS_SQLITE` は `CFLAGS_COMMON` を継承するので言語指定の分離が要る、`kernel.mk:175` は SQLite 本体でなく `os32_sqlite_test.c`、`shm.c` の 2 本は 2026-09-17 に修正済み、`check_constraints.py` は ID の整合だけで [C1] の言語検査はしていない (T0 で `check-c-dialect` を新設)。ユーザー判断 3 点 (同票 §8) と [C1] の文面 (§9) は未決 | PLAN §1 の 1、ROADMAP §2「着手の前に決めること」、KSTACK §7-5、ROADMAP §0-1、TASK_MEMMAP_V3 §8-4 X1、TASK_C11_MIGRATION | 無し (最初) | **1** |
| **P1 メモリマップの再構築** | **決定 (2026-09-30) — [TASK_MEMMAP_V3](../../tasks/v3/TASK_MEMMAP_V3.md) 参照**。要点: カーネル・シェル・通常 RAM は恒等写像のまま、アプリだけ 0x80000000〜 の私有写像 (lease 窓 0xF0000000〜)。物理地図と所有権台帳を先に作り、固定帯はカーネル 3MB + シェル 1MB だけ、残りは owner 付きの池。SQLite は同一リンクをやめてモジュール、FEP・音源・LAN・NP21/W 専用もモジュール (FDC はコア)。低位 640KB は V86 へ、Unicode 表はカーネル `.rodata` 28KB、`.kcgfont` 廃止 → OpenType (KCG ROM は予備)。票は T0〜T7 (T1 の設計票は [TASK_T1_LEDGER](TASK_T1_LEDGER.md)) | TASK_MEMMAP_V3 §0 (D1〜D28)、§2、§3-5、§6 | P0 (C11 = T0 が先) | **2** (T1 台帳 → T2 アプリ帯 → T3 カーネル帯 → T4 SQLite → T5 モジュール → T6b ローダ → T7 低位・OpenType) |
| **P2 ドライバの置き場 (動的読み込み)** | カーネル権限のモジュールを起動時に一覧から順に載せる。内側向けの取り決め (ドライバが実装する口 / カーネルが提供する口)、失敗時の扱い、どれを外に出すか (ブートに要るものは静的)。**形 (Codex X2、2026-09-30、承認)**: shlib ローダの流用ではなく、**形式検査・表生成の共通部を使った専用のモジュールローダ** — ロード時の検証 (生成時の合格と分ける: 範囲・重複再配置・世代)、信頼する配布物 (起動一覧と同梱域) だけを読む (チェックサムは実行権限の証明ではない)、**IRQ 登録と初期化状態の結び付け** (STARTING → RUNNING、export の公開は init 成功後)、**停止を証明できない失敗は隔離保持か起動停止** (owner 回収へ進めるのは IRQ・tick・callback・DMA の参照が切れたときだけ)、実行可能として公開する境界 (M0 §6)。正典は TASK_MEMMAP_V3 §4-7 | PLAN §3、HAL_WIRING 1-4 (`struct pci_driver.size`)、shlib.c の再配置・ジャンプ表、M0 §6 (ロードしたコードへ飛ぶ経路)、TASK_MEMMAP_V3 §4-7 | P1 (置き場は帯と同時に決める) | 2 |
| **P3 HAL の結線** | HAL_WIRING の残 (W7)、NIC 境界 `net/nic.h` (L-C)、音源バックエンド (P3)、1kHz tick、ISA 非依存化の仕分け | HAL_WIRING §2・§4・§6、PLAN §3-1、§5-5 | P2 (取り決めの形を共有) | 2〜3 |
| **P4 デバイス窓の帯の資源割当** | **U6 決定 (2026-09-30、TASK_MEMMAP_V3 D33) で改訂**: 予約 broker の核 (一括 transaction、owner 台帳) は **P1 T1 の台帳の MMIO 登録**へ; P4 に残るのは順序契約 (副作用のない識別 → 予約 → 写像 → probe / enable → 面公開、失敗時の永久保持、「予約拒否」と「probe 失敗」の区別)、gfx バックエンド 2 本 + glue の識別 / probe 分離、**Trident の実測 BAR の口** (許可範囲を定数だけ → 検証済みの実測範囲へ、X4 を Codex へ)、単独 CPL3 アプリの初回 `gfx_init` で backend を activation しない規則。**時期は識別 + 予約 = 起動時 (TASK_MEMMAP_V3 §4-5)、probe + enable = GUI 境界** (TRIDENT T8 (i))。>16MiB の検出源は T1 の受入 (D32)、面公開 API の記述子化は §2-2 lease が上位互換、APP_BAND §5 の受入 | DEVICE_RESERVATION (改訂待ち)、TRIDENT §4-2、02_memory「デバイス窓」、APP_BAND_PDE、U6_PENDING_REVIEW §1-5 (MEMORY_RAM_INTEGRATION は撤回 → archive) | P1 の T1 (台帳) の後、T2 の後・Trident 段 3 (P5) の前 | 2〜3 |
| **P5 実機ドライバ** | 82557 L-B〜L-E (最初)、PCM E4〜E6 → P2〜P4 (合成器)、Trident 資料 → 段 1 → 2-X → 3〜5、PEGC 目視、FD144、KBD_NAV K3、実機の画面取得 (5-4) | TASK_LAN_82557、TASK_PCM_CS4231、TASK_TRIDENT_DRIVER、realhw/PLAN | L-B は P3 の上 (済み)、静的で入る (決裁 (b))。Trident 段 3 は P4 と T7。合成器は P2 | 82557 は **1〜2 (P1 を待たない)**、他は 3〜4 |
|  **P5 (追加、ユーザー決定 2026-09-30)** | **ディスクの速さ: IDE の DMA 転送と ext2 の性能**。今は IDE を PIO で読み書きしており、大きな文書・表計算のファイルを開く速さに直接効く。Ra266 の PCI 0:12.0 `1103:0004` (Storage、HighPoint 系の Ultra DMA IDE と推測 — 資料で未確認) のバスマスタ DMA、内蔵 IDE の DMA、ext2 の読み書きの経路 (ブロックキャッシュ・先読み) を対象に。目標「32〜64MB 機で Windows 2000 当時のアプリの使用感」の要素 | ユーザー決定 (2026-09-30) | — | 実測 (Ra266 のファイル読み書き速度) から始める |
| **P6 実行モデル** | 協調 4 本 (T2a、現行) を保つか、タイマ割り込みのプリエンプティブ寄りへ寄せるか、前景集中 (APP_FIRST) との折り合い。1kHz tick、`sys_time_us` は済み | ROADMAP §2、DESIGN_APP_FIRST §3、PLAN §5-5 (1kHz) | **決裁が先** (§7 U2)。実装は P1 の後 | 決裁 1、実装 3〜4 |
|  **P6 (ユーザーの意図の明確化、2026-09-30)** | **複数のアプリは同時に起動して見える (窓が並ぶ) だけで、同時に実行する必要はない**。ただし**前面のアプリが固まったときには、カーネルに処理が戻らなければならない** — 協調型を基本に、固まったアプリからカーネルが制御を取り戻す仕組み (タイマ割り込みによる見張り・CTRL+STOP 等で前面アプリを止めて WM/シェルへ戻す) を必須とする。スケジューリングとしてのプリエンプションは求めない。**P6 の票に載せる要件 (Codex X5、2026-09-30、承認)**: 「固まった」の自動判定とユーザーの強制停止を別に定義 (今の暴走判定 `appslot.c:669` は軽い KAPI の無限連打を見ない、KAPI 内の永久待ちは R1 の移譲条件に入らない)、長い KAPI の期限・取消・巻き戻し可能点、**KAPI の中で AS を切り替えない**、trim (D24) は要求を記録して安全点で配送 (`ring3_wm_depth` を再入防止の代用にしない)、**x87 の状態をアプリごとに保存・復元** (`AppSlot` に保存欄が無い)、受入 (CPL=3 無限ループ / KAPI 連打 / 期限待ち / PCM 再生中 / trim 配送 / FP 設定の異なる 2 アプリ)。本文は TASK_MEMMAP_V3 §3-5-3 | ユーザー決定 (2026-09-30)、TASK_MEMMAP_V3 §3-5-3 | — | U2 の選択肢はこの意図で読み直す。実装方式の票は上の要件を含める |
| **P7 KAPI / ABI** | 追記のみ (v69〜) を続けるか fork で 1 回整理するか、R = 300 の残り 60 本、OS32X ヘッダ、64 ビットを返せない (出力 2 本)、SDK ヘッダの C11 化と apps/game の C89。**決定 (ユーザー 2026-09-30): 後方互換は基本考えない** — 全再ビルド・旧形式は拒否・互換層なし (TASK_MEMMAP_V3 D7)。~~KAPI のスロット順は維持する ([ABI2])~~ → **D35 (2026-09-30、Codex X6 を承認): fork 時の整理で**スロットの順を変えてよい** ([ABI2] の追記のみは fork 後に再開)。条件 = **世代の識別 (OS32X ヘッダの形式版・KAPI ABI 世代・メモリ配置世代・shlib プロトコル) と旧新混在の試験を P7 の必須に** — 拒否契約 (入口の前で常駐シェルも検査、未知の形式版を拒否、コンパイル単位の ABI 識別、成果物を一組で固定、v2 / v3 の配備先を分ける、Rust 生成器の未知型は生成失敗) の本文は TASK_MEMMAP_V3 D35。v2.1 のバイナリを v3 で動かすことは目的にしない。追記のみか 1 回整理か → **1 回整理 (§5 C1 (b)、§7-1 U8)** | KAPI_SPEC §3-2・§4-0、ROADMAP §0、TASK_KAPI_DATA_FIELDS、ARM_GAUGE §10 | P0 (C11)、§5 の決裁 | 決裁 1、実施は P1 と同時 |
| **P8 GUI 層** | 1.4 で閉じた → v3 の線。8bpp のまま (Trident T6)、640×480×16bit の境界の扱い (§4 D5)、microUI へのマウスキー、F3a〜c 等の保留、GUI アプリ群 (先送り)。**追加 (ユーザー指示 2026-09-30): コントロールパネル (設定を GUI で変えるアプリ) の設計を v3 後半に — 計画票 [TASK_CONTROL_PANEL](../../tasks/gui/TASK_CONTROL_PANEL.md)** (T4・T5a と P8 のウィジェットの後)。**追加 (ユーザー発案 2026-09-30): UI メッセージの多言語対応 (SQLite のメッセージ表、既定は英語、区分 + 短い名前をビルドで番号に畳む) — 計画票 [TASK_I18N](../../tasks/gui/TASK_I18N.md)** (T4 とコントロールパネルの後、他の文字体系は T7b の後)。**決定 (2026-09-30): 音 (PCM リング) と入力の層は P1 ではなくここ (別の柱)** (TASK_MEMMAP_V3 D19) | ROADMAP §0・§2、TRIDENT §4-3、KBD_NAV、HANDOVER_v14 §3 | P5 (Trident 段 3) | 4 |
| **P9 アプリ層・互換層** | Video HAL の共通化 (framebuffer / pitch / VSync / flip / optional BitBLT)、VESA2 的互換層、SDL 1.2 の受け皿、ZSNES 試験台、Host Services の延長 (ネット CD 5-6、HostDrv 5-7)。**決定 (2026-09-30): P1 から切り出した別の柱** (TASK_MEMMAP_V3 D19)。全画面アプリの表示面 / フリップ面は lease で受け取る (同 §2-2)、アプリ私有サーフェス + WM 合成はサーフェス層の票で。**移植候補の一覧と挑戦順 (ZSNES は移植する、難度の低いものから) は [PORT_CANDIDATES.md](../../tasks/v3/PORT_CANDIDATES.md)** (ユーザー決定 2026-09-30: GUI アプリ群と P9 は v3 後半、基盤が整い次第) | DESIGN_APP_FIRST §7〜§10、PLAN §5-6・5-7、TASK_MEMMAP_V3 §5-7、PORT_CANDIDATES | P5 (Trident か Cirrus の 16bit 面)、P4 (面公開)、P1 (lease 契約) | **v3 後半** (5) |
| **P10 データ・設定層** | F2 (SQLite 接続単位の FD 所有)、FEP_BOUNDARY、F3a〜c (SQLite VFS の正直化)、settings §7 の対象外。**追加 (ユーザー決定 2026-09-30): [TASK_DICT_META](../../tasks/fep/TASK_DICT_META.md) — FEP 辞書のメタ情報 (形式の版・dict_id・license・`mem_reserve_kb` 等) と学習データの別ファイル化 (`/etc/fep_user.db`)。着手は v3 後半** (TASK_MEMMAP_V3 D27・D28、§4-6)。**U6 決定 (2026-09-30、TASK_MEMMAP_V3 D29〜D31): F2 の残りと FEP_BOUNDARY は独立票にせず P1 の T2 / T4 / T5a の要件、F3a + F3c の VFS 側は T4、F3b は TASK_DICT_META の後に「同一 DB の排他 open」(lock 表は作らない)。P10 に独立票として残るのは TASK_DICT_META と F3b だけ** | F2_OWNERSHIP、FEP_BOUNDARY、settings/DESIGN、HANDOVER_v14 §3、TASK_DICT_META、U6_PENDING_REVIEW | F2 / FEP_BOUNDARY / F3a・F3c は P1 の T2〜T5a に畳む。TASK_DICT_META は T4・T5a の後、F3b はその後 | **決裁済み (U6 = 一部、2026-09-30)**。実施は T2〜T5a の中、TASK_DICT_META と F3b は v3 後半 |

**順序の案 (PLAN §1 の引き直し)**:

1. **P0** (C11・型・道具・文書) — 最初に単独で。**コンパイラの変更と配置の変更を別々に受け入れ、障害の原因を分けるため** (Codex X1、2026-09-30。「検査なしになる」は根拠にしない — 既存の STATIC_ASSERT・リンカ ASSERT・gen_memmap・起動時検査で足りる。KSTACK §7-5 の 2 の言い換え)。
2. **P1 の討論** (3 者、**2026-09-30 に決着**) と **P6 / P7 の決裁** (P6 の意図と P7 の後方互換は同日に決定、実装方式の決裁は残る) — 帯を切る前に「上限・置き場・払い出し・実行モデル・ABI」を決める (PLAN §2「§3 と §4 を決めてから帯を切る」)。並行して **82557 L-B** (静的、決裁 (b)) と **並行の実機確認** (W7 / E6 / PEGC 目視 / FD144 / K3) を実機の回で進める — これらは P1 を待たない。
3. **P1 実装 → P2 → P4** (帯・置き場・資源割当は一組)。票と順序は TASK_MEMMAP_V3 §6 (T0〜T7: T1 台帳が P4 の土台、T4〜T5c が P2)、受入は同 §7。
4. **P3 → P5 の残り** (PCM の合成器、Trident 段 3〜5) → **P8**。
5. **P9** (互換層) と PLAN §5 の残り (5-6 / 5-7 / 5-4)。

PLAN §1 の「1 と 2 を同時に動かさない」「§5 を §1 に割り込ませない」は維持する。**PCI と 82557 が静的に先行した事実** (v2.1) を本案の順序に反映する (§4 D1)。

### 3-1. P1 メモリマップの再構築 — **決定 (2026-09-30) — [TASK_MEMMAP_V3](../../tasks/v3/TASK_MEMMAP_V3.md) 参照**

ユーザー・Fable 5.1・Codex の 3 者討論 (2026-09-29〜30、Fable 最終案 v3 に Codex が Approve) で決めた。**結論の本文は TASK_MEMMAP_V3 だけに書く** (決定一覧 §0、帯の表 §2-1、票 §6、受入 §7、経緯 §11)。決定の要点:

- **カーネル・シェル・通常 RAM は恒等写像 (supervisor) のまま、アプリだけ 0x80000000〜 の私有写像**。lease 窓 0xF0000000〜 (AS ごとの私有 PT) でサーフェス (BB・VRAM) を貸す。高位カーネルは v4 以降 (P2V/V2P の集約だけ v3 で)。
- **物理地図と所有権台帳を先に** (PFN 半開区間、owner 付き)。`exec_child_claim` / `sys_reserve_top` / `EXEC_DYN_RESERVE` / `--cpl0` は撤去。DMA は `dma_alloc(size, align, limit)`、15〜16MB は既定で予約。
- **固定帯はカーネル 3MB (0x100000〜、本体 → KHEAP → KAPI → SHM の鎖、余りは KERNEL_SLACK として池へ、末尾に DMA プール・固定 PT・kstack) + シェル 1MB (0x400000〜、1 箱)** だけ。0x500000〜 と 16MB 以上は池。
- **SQLite は同一リンクをやめてモジュール** (ベース 0 リンク + `--emit-relocs`、インポートスタブ、外部データ禁止)。FEP・V86・音源・LAN・FAT/iso9660・NP21/W 専用もモジュール、**FDC はコア**。SQLite と FEP は常に読み込む (例外 MINIMAL)。
- **低位 640KB は V86 へ**: Unicode 表はカーネル `.rodata` 28KB、フォントは `.kcgfont` 廃止 → OpenType (私有キャッシュ)、KCG ROM は予備、BB は池 (gshell 所有)。スプラッシュはユーザーランドの最初のアプリ。
- **8MB 機 (9801 / PEGC) で GUI + 私有 2MB のアプリ** が成立する勘定 (+1.57〜1.74MB の余り)。受入は NP21/W 8MB / 17MB、実機 64MB、FD 起動。
- 票は **T0 C11 → T1 台帳 → T2 アプリ帯 + lease → T3 カーネル帯 → T4 SQLite → T5a FEP → T5b ブート FS 同梱 → T5c 他モジュール → T6b ローダ → T7a 低位解放 → T7b OpenType**。

以下の論点表 M1〜M15 と食い違い MD1〜MD9 は**討論に渡した記録**として残す (本文は 09-29 のまま)。決着先: M1〜M3・M5〜M7・M11 → TASK_MEMMAP_V3 §2-1 (T3)、M4・M8・M9・M13 → 同 §2-3・§6 (T2)、M10 → 同 §4-5 (T5b + T6b)、M12 → 同 §2-1・§3-1 (台帳の MMIO 登録、T1)、M14 → C11 が先 (T0)、M15 → v3 の後 (§1-5)。MD1〜MD9 は「先に事実を揃える項目」として討論の材料に使い、TASK_MEMMAP_V3 は正典の数字 (02_memory §2-1) で書いた。

**論点 (M1〜M15)** — TASK_MEMMAP_V3 (討論前の v2) の B1〜B9 を含む:

| # | 論点 | 出典 | いまの事実 (写し) |
|---|---|---|---|
| M1 | **カーネル本体の上限**をどこに置くか (決めないと削ってもまた同じ場所に戻る) | PLAN §2、ROADMAP §2、KSTACK §7-5 の 3 | 583.0KB / 596KB、残り 13.0KB (02_memory §2-1)。ドライバ群 ≈ 89KB、USB は単独 100KB 超 (PLAN §3) |
| M2 | **帯の切り方**: カーネル帯 2MB 案 (0x100000〜0x2FFFFF、SQLite 0x300000〜、シェル 0x400000〜、shlib 0x500000〜、プログラム 0x600000〜) か、別の切り方か | MEMMAP_V3 2-2 | 現行: カーネル 1MB / SQLite 1MB / シェル 1MB / shlib 1MB / アプリ 0x500000〜 |
| M3 | **ページ表を画像の外へ** (master PD 1 + ブート PT 8 = 36KB)。置き場 (帯の末尾か、SQLite 帯の予約か、pgalloc か) | MEMMAP_V3 2-1、TRIDENT §4-4 (追加 PT を予算に数える) | 画像の中 (`paging.o`)。効果 +44KiB (整列の捨て 8KB は 2-3 で回収済み) |
| M4 | **アプリ帯の開始・既定上端・最大枚数・物理配布域**を一組で決める (B1 / B2 / B7)。シェルの写像 (PDE 1 に置くと CPL=3 起動で消える)、pgalloc の配布域とシェルの分離、9MB / 16MB での起動可能サイズと NOMEM の仕様、V86 の 636KiB 連続物理 | MEMMAP_V3 B1・B2・B7、APP_BAND_PDE §4-A・4-B、02_memory「開発方針」 | 現行: PDE 1〜2 (0x400000〜0xBFFFFF)、天井 = PEGC 窓 0xF00000、`heap_size` 明示時だけ 2 枚。8MB 構成では EXEC_DYN_RESERVE の穴とアプリ帯が重なる (既存)。§5 のゲスト受入は未記録 |
| M5 | **シェルの 2 ヒープ** (newlib sbrk + exec_heap 0x380000) を持ち続けるか | PLAN §4、02_memory §2-1 (2026-09-03 の分離の経緯) | 0x300000 帯に .text + sbrk (468KB 上限) / スタック 40KB / exec_heap 512KB |
| M6 | **固定 PD/PT の初期化順序** (PG=0 で作る → NP 化の後に張り直す → CR3 → PG) (B3) | MEMMAP_V3 B3 | `paging_reclaim_conventional` が #PF する反例 (往復 1) |
| M7 | **予算式** (浮動部分の上限を最初の固定領域の手前に置く)。地図生成器の kstack の終端の構造 (B4) | MEMMAP_V3 B4、gen_memmap.py | 現行の `MEM_KERNEL_IMAGE_MAX` = 帯 − KHEAP − KAPI − ガード − SHM |
| M8 | **番地を焼く経路**の全列挙と照合 (app_sys.ld / shlib.ld / mkshlib / stub.rs の 0x400000 / `--sqlite-addr`) (B5) | MEMMAP_V3 B5 | 5 経路 (crt0 は直書き無し) |
| M9 | **NHD の移行単位**: カーネル・常駐シェル / gshell・shlib・in-tree・apps/game を一組として配備、旧シェル・番地不明バイナリの拒否方針 (B6) | MEMMAP_V3 B6、08_build §8-4 (v63 の移行手順) | `load_addr = 0x500000` の旧バイナリは拒否、`is_shell` は番地検査を通らない |
| M10 | **ローダの上限** (HDD ローダは `vmkernel.lz4` を 508KiB まで) と FD の低位ステージング。常駐予算と圧縮配布サイズを別の制約に (B8) | MEMMAP_V3 B8、`boot/boot_defs.h` | 現行のイメージは超えていない (値は生成物で見る) |
| M11 | **DMA プールの置き場** (SQLite 帯の予約域 0x2E8000 固定は暫定承諾、v3 で再配置し得る)。PCM リング 16KB + 82557 CB/RFD 16KB + 将来 | HAL_WIRING §5 決裁 (1)、1-3 | 64KB、代償: SQLite の成長余地 120KB → 44KB |
| M12 | **デバイス窓の帯**との関係: `[0xFE000000, 0xFF000000)` は実装済み (帯の先頭 4MB の PT を静的に 1 枚)。Trident の枠 [0xFE400000, 0xFE800000) の PT の確保時期 (T8 の境界)、高位 RAM の登録経路 (legacy 経路では帯が UNKNOWN のまま)、`MEM_PHYS_RAM_CEILING` = 0xFE000000 | 02_memory「ページング」、memmap.h、TRIDENT §4-2、DEVICE_RESERVATION §3 | 明示の MMIO 登録は v3 の資源割当で引き継ぐ (02_memory の注記) |
| M13 | **受入** (B9): 新シェルからの CPL=3 起動・終了・fault 復帰・GUI の複数 AS 切替、全 AS での写像と supervisor、9MB / 16MB の pgalloc と V86、旧バイナリ混在時の拒否、固定境界を越える変異をリンクと地図検査が止める | MEMMAP_V3 B9 | 起動 + 現行 MM 検査 (PDE 0 だけ) では B1/B2 を取り逃す |
| M14 | **順序**: C11 の前か後か (MEMMAP_V3 v2 の PM 判断は「2-2 は v3 §1 (C11) の前の独立した段階」、PLAN §1 と KSTACK §7-5 は「C11 が先」) | MEMMAP_V3 冒頭、PLAN §1、KSTACK §7-5 の 1・2 | **食い違い** (下) |
| M15 | **ARM 実装との順序** (再配置してから ARM に手を付けると配置の前提が 2 回動く) | KSTACK §7-5 の 4、ROADMAP §2 | KAPI 生成器の arch 対応は v3 まで保留 |

**草案どうしの食い違い (メモリマップに関するもの)** — 事実の列挙のみ:

| # | 食い違い | どこ |
|---|---|---|
| MD1 | 予算の数字: 「余り 134.2KiB (本体の伸び代)」(MEMMAP_V3 §0、09-23) / 「残り 21KB」(PLAN §5-5 P2、TRIDENT §4-4) / 「残り 25.4KB」(LAN_82557 §1) / 「予算が 6KB しか無い」(PCM 2-1) / **「残り 13.0KB」(02_memory §2-1、正典)** | 日付の違う写し。**済 (2026-09-30、手順 f)**: LAN_82557 §1・§3、TRIDENT §4-4、PCM §2-1 (本文と末尾の「票からの逸脱」)、MEMMAP_V3 §0・§11-1 を正典を指す形に (数字を残した箇所は日付つき)。PLAN §0・§1 は既にその形 |
| MD2 | C11 との順序: 「C11 が先」(PLAN §1、KSTACK §7-5) / 「2-2 は C11 の前の独立した段階」(MEMMAP_V3 冒頭の PM 判断) | PLAN vs MEMMAP_V3 |
| MD3 | 着手の条件: 「134KB の余裕で PCM と 82557 は入るので 2-2 は急がない」(MEMMAP_V3) / 「13KB では静的に積めない」(PLAN §3、ROADMAP §2) | 数字が動いた後に前提が変わった |
| MD4 | デバイス窓の番地: 「Cirrus リニア窓 0x1000000 (PDE 4)」(APP_BAND_PDE §4) / 「16MiB からの全 2MiB linear aperture」(MEMORY_RAM_INTEGRATION §7) / **`[0xFE000000, 0xFE200000)`** (memmap.h、DEVICE_RESERVATION §3、02_memory) | 09-29 の移設が未反映の票。**済 (2026-09-30、手順 f)**: APP_BAND_PDE §4 を移設後の番地に。MEMORY_RAM_INTEGRATION は `docs/archive/settings/` (完了記録) なので本文は触らない |
| MD5 | hotdeploy 窓: MEMORY_RAM_INTEGRATION §2・§4・§6 は hotdeploy descriptor と窓を前提に書く / 窓は 2026-09-09 に撤去 (02_memory、`kernel/hotdeploy.c` は無い) | 撤去前の票。**済 (2026-09-30、手順 f)**: 票は `docs/archive/settings/` (完了記録) なので本文は触らず、本行の注記で足りる |
| MD6 | PCM のステージングの置き場: 「プールを暫定で使う (カーネル予算が 6KB)」(PCM 2-1) / 「KHEAP へ (暫定を解く)」(MEMMAP_V3 2-3、KAPI_SPEC v61) | PCM 票の本文が古い。**済 (2026-09-30、手順 f)**: PCM §2-1「メモリ」をステージング = `kmalloc` に (実装 `drivers/pcm_cs4231.c` と一致)、§2-1 末尾「票からの逸脱」3 の -Os 行に「596KB への合流で外した」を追記 |
| MD7 | アプリ帯の天井の根拠: 「PDE 3 に PEGC の窓」(APP_BAND_PDE、02_memory) — 帯 2MB 案でアプリが 0x600000 から始まると PDE 1 開始のままでは仮想帯が 1MB 減る (MEMMAP_V3 B7) | 2-2 と APP_BAND の前提の衝突 (論点 M4) |
| MD8 | 最低 RAM: 「設計上の下限は 9.6MB」「PEGC の GUI は 9MB」「8MB では EXEC_DYN_RESERVE がアプリ帯と重なる (既存)」(02_memory、APP_BAND 4-B) / MEMORY_RAM_INTEGRATION は「8MiB PEGC の動作維持を本票だけで達成したとしない」 | 下限の数字が 3 か所 |
| MD9 | physmem の接続: 「`physmem.c` はまだ C_KERNEL にない」(MEMORY_RAM_INTEGRATION §2) / 現行 `build/kernel.mk` はリンクしている (K6-RAM、`memory_boot_add_high`) | 09-13 の票の根拠行。**済 (2026-09-30、手順 f)**: 票は `docs/archive/settings/` (完了記録) なので本文は触らず、本行の注記で足りる |

討論に渡した形: M1〜M15 を議題、MD1〜MD9 を「先に事実を揃える項目」とした (討論は済み、上の決着先を見る)。MD1・MD4・MD5・MD6・MD9 の古い写しは §7 U9 のとおり本文で直した (2026-09-30、手順 f。archive/ の票は注記だけ)。

---

## 4. 草案どうしの食い違い・重複 (メモリマップ以外) と解消案

| # | 食い違い / 重複 | どこ | 解消案 |
|---|---|---|---|
| D1 | **順序**: PLAN §1「C11 → 再配置 → 動的読み込み → PCI → 82557、動かさない」 vs 2026-09-22 の決裁 (b) で PCI (L-A) と HAL の結線・PCM が静的に先行し v2.1 に入った (ROADMAP §0・§2、LAN_82557 §3) | PLAN vs 決裁 | 本案で PLAN §1 を §3 の順序に書き直す。82557 L-B も静的で入れる決裁のまま (P1 を待たない) |
| D2 | **カーネル予算の写し**が 5 通り (§3-1 MD1) | PLAN / MEMMAP_V3 / LAN_82557 / TRIDENT / PCM | 数字は 02_memory §2-1 だけを正典とし、票の数字には日付を付ける (PLAN §0 は既にそうしている) |
| D3 | **マルチタスク**: ROADMAP §2「v3 でタイマ割り込みのプリエンプティブ寄りを検討」 vs DESIGN_APP_FIRST §3「プリエンプティブを最終目標としない」「前景 1 本に集中」 vs ACS「主実行系は 1 コア、SMP スケジューラ無し」 vs 現行 T2a (協調 4 本、`OP_WAIT` だけで PD 切替) | ROADMAP vs APP_FIRST | **決裁 (§7 U2)**。折り合いの候補 (優劣は付けない): (a) 協調のまま + 前景以外は `OP_WAIT` で止まる、(b) タイマで背景の補助プログラムだけ切る (重量級は前景 1 本)、(c) プリエンプティブ寄り。ROADMAP §2 の「window / process の独立実行」「desktop が独立して応答」は (b)(c) を含意する |
| D4 | **Cirrus の位置づけ**: DESIGN_APP_FIRST §6「Cirrus 等で同等の表示能力を提供できる機種」を Video HAL の backend に数える vs ユーザー決定 2026-09-29「Cirrus はエミュレータ用、実機は Trident」(RELEASE §3、02_memory、realhw PLAN §7) | APP_FIRST vs 決定 | APP_FIRST §6 は「能力で境界を定める」例示として読み、実機の機種表は realhw が正典。Cirrus は NP21/W での回帰経路 (LGY-98 と同じ役) |
| D5 | **16bpp**: APP_FIRST §5「640×480×16bit をネイティブに提供できること」を高機能アプリの境界に vs TRIDENT T6「16bpp はしない (推奨、Codex 同意)」、GUI 1.x は PACKED8 | APP_FIRST vs TRIDENT | 「高機能アプリ向け Video HAL (P9) は 16bit 面を持てる」と「GUI シェル (gshell / libos32gui) は 8bpp のまま」を分けて書く。KAPI の `GFX_FMT_*` 追記は P9 の票で ([ABI2]) |
| D6 | **OS64**: LEGACY / ACS に「Modern host / OS64」「将来の OS64 ホスト環境」として登場。ARM_GAUGE §10「64 ビットは範囲外 (64 ビット機に載せたい場合は利用者が 64 ビットのプログラムを書く)」。版数表には載せない (決定済み) | LEGACY・ACS vs ARM_GAUGE | **決裁 (§7 U3)**: OS64 が (i) ホスト側 (現代機) で動く Host Service の実装の名前か、(ii) OS32 とは別の 64 ビット OS の構想か、(iii) 語を使わないか。(ii) なら ARM_GAUGE §10 と両立するのは「別物」としてだけ。いずれでも語の定義を 1 か所 (LEGACY の冒頭) に置く。**決定 (2026-09-30、U3): (ii)** — 定義は [LEGACY_LIVING_PRESERVATION.md](../../LEGACY_LIVING_PRESERVATION.md) の冒頭 |
| D7 | **「v3」の多義**: カーネル版 v3 / OS32X ヘッダ v3 (08_build、KAPI_SPEC §4-0) / 設計票の改訂 v3 (HAL_WIRING「v3 で変えたこと (Codex 往復 2)」、TRIDENT「v3 の要点」、SERIAL_HOSTFS §1-v3、HDD_INSTALL §1-v3) / ローダ v3 (S3I2) | 全体 | os32-v3 では文書の改訂を「改訂 N」「往復 N」と書き、`vN` はカーネル版と ABI (ヘッダ・ローダ・ワイヤ) に限る規約を POLICY_DEV に足す |
| D8 | **Rust の適用範囲**: PLAN §5-5 P2「合成器の核は Rust (no_std、C ABI)、描画は PCM の DMA 割り込み (カーネル文脈) から呼ぶ」 vs V4 §7「KAPI / arch / drivers は当面 C / asm、Rust は上位から」 | PLAN vs V4 | **決裁 (§7 U5)**: v3 でカーネル文脈 (IRQ) から呼ぶ Rust モジュールを認めるか。認めるなら V4 §7 を「純粋計算の staticlib は例外」と改訂。**決裁済み (2026-09-30): 認める。ただし IRQ 文脈の合成器はまず整数演算に限り、生成コードに x87 / SSE / MMX が無いことを検査する** (Codex X5、TASK_MEMMAP_V3 §3-5-3) |
| D9 | **C11 と [C1]**: PLAN §1・ROADMAP §2「規約を C11 へ」 vs CONSTRAINTS [C1]「C89 (GNU89) 厳守」(`make check` が照合)。SDK ヘッダ (`sdk/include`) と apps/game (C89 のまま?) への波及が未記述 | PLAN vs CONSTRAINTS | P0 で [C1] を改訂 (カーネル・in-tree は C11、SDK ヘッダは C89 互換を保つか決める §5)。`tools/check_constraints.py` も同時 |
| D10 | **デバイス窓の番地・hotdeploy** (§3-1 MD4・MD5) | MEMORY_RAM_INTEGRATION、APP_BAND_PDE | 再棚卸し (P4 の最初) |
| D11 | **physmem の接続** (§3-1 MD9) | MEMORY_RAM_INTEGRATION §2 | 同上 |
| D12 | **動的 vs 静的**: PLAN §3「動的読み込み」/ LAN 決裁 (b)「静的で入れて後で外に出す」/ TRIDENT T7「段 3 着手時に決める」/ PLAN §5-5 P2「動的の最初の顧客 (82557 の次)」 | 4 票 | P2 で「外に出す順」を 1 表に: 合成器 (最初から外)、Trident (T7)、82557 (静的で入れてから外へ)、USB (外)。ブートに要るもの (IDE / コンソール / FDC) は静的 |
| D13 | **実機の位置づけの古い行**: realhw PLAN §0「v1.x のあいだは着手しない」、§9「1.44MB の対応 (しない)」「Trident (しない)」 — どちらも票内で上書き済み | realhw PLAN | 本文は残してよい (状態行と追記が正)。fork 時に「策定時の記述」と注記済み。**済 (2026-09-30、手順 f)**: §0 冒頭・§9 を現状 (v2.1 で HDD 起動済み、1.44MB・Trident は着手) に書き直し、v3/PLAN §0・§6 の同じ行も |
| D14 | **ACS / Host Service / Fabric の 3 語**: LEGACY は ACS (同一機の余剰コア) と Host Service (現代ホスト) の 2 分、V4 は Fabric / OS32 Link (OS32 ノード同士、remote device)。同じ「外の計算資源」を 3 語で呼ぶ | LEGACY・ACS・V4 | v4 側で用語表を 1 つ (ACS = 同一機、Host Service = 現代ホスト、Fabric = OS32 ノード)。v3 では Host Service だけを使う |
| D15 | **移植の範囲**: ARM_GAUGE §10「32 ビットのみ、16/64 は範囲外」(決定 09-17) vs V4 §10 の候補表に riscv64 / Z80・R800 (MSX2 系「Compact / Runtime」) / MIPS (Hosted) | ARM_GAUGE vs V4 (09-07、決定より前) | v4 草案の候補表に「32 ビット限定の決定 (09-17) より前の表」と注記。v3 は 32 ビット限定を維持 |
| D16 | **1kHz tick**: PLAN §5-5「IRQ0 を 100Hz → 1kHz」、HAL_WIRING §4「1kHz と汎用 hz は別票」 — 票が無い | PLAN vs HAL_WIRING | P3 に票を起こす (実機と NP21/W で割り込み負荷を実測してから既定に、PLAN §5-5 の順序 (2)) |
| D17 | **DEVICE_RESERVATION の対象**: 「初回は PEGC と Xe10 のみ、許可は既知候補の定数だけ」 vs TRIDENT §4-2「実測 BAR の検証済み範囲を渡す口を同票へ追加」 | DEVICE_RESERVATION vs TRIDENT | DEVICE_RESERVATION を改訂 (Trident の要求 1〜3 を取り込む)。**X4 の答え (2026-09-30、承認)**: 生の `{base, size}` ではなく**検証済み資源レコード** (BDF・ID・BAR 番号・確定 decode 幅と根拠・世代) を台帳に、予約 / 写像 / 面の範囲を分け、GUI 境界で装置の同一性を再確認、永久予約とモジュール回収を分離。Trident の今の採取値は未検証 (TASK_MEMMAP_V3 §8-4・T1) |
| D18 | **面公開 API**: 現行 `gfx_bb_phys_range()` は仮想 = 物理を前提 (恒等)。TRIDENT §4-2 は高位仮想窓で壊れると指摘し、記述子 (`virt`/`phys`) と `paging_addrspace_map_user_range_phys_keep` を要件に | TRIDENT vs 02_memory「デバイス窓の貸し出し規則」 | P4 の要件として本案に載せる (Trident 専用ではなくカーネル共通)。**X3 の答え (2026-09-30、承認)**: `_phys_keep` (共有 PT の USER 昇格) は v3 では採らない — 面は AS ごとの私有 lease PT に張り、記述子は SURFACE 台帳への参照、lease の仮想番地は別に返す、キャッシュ属性は台帳から (TASK_MEMMAP_V3 §2-2、T2) |
| D19 | **状態行の語彙**: DESIGN_APP_FIRST「設計思想の草案」(語彙外だが `docs/` 直下は検査対象外)、V4_GAME_PLATFORM_DRAFT に状態行が無い (tasks/v4/README にはある) | POLICY_DEV §8 | fork 時に「草案 (日付)」へ揃える (本文は変えない、状態行だけ)。**済 (2026-09-30、手順 f)**: DESIGN_APP_FIRST「草案 (2026-09-26)」、V4_GAME_PLATFORM_DRAFT に「草案 (2026-09-07)」の状態行を追加 |
| D20 | **GUI アプリ群の位置**: ROADMAP §1 v1.4「先送り (v3 以降)」、§2「協調型マルチタスクの拡張の後」 — v3 の柱には無い | ROADMAP | v3 後半の末尾か v4 (§2 の範囲外に置いた。決裁 §7 U7)。**決定 (2026-09-30、U7・U4): v3 後半** — 範囲外の一覧から外した |
| D21 | **重複**: PLAN §3-1 (HAL の棚卸し) と HAL_WIRING §0 (なぜ要るか)、PLAN §5-5 (合成器の設計前提) と PCM 票 §0、realhw PLAN §5・§6 と LAN_82557 §1 — 同じ事実が 2 か所 | 4 組 | 本案では票側を正典にし、PLAN は 1 行 + リンクへ縮める |

---

## 5. v2.x との互換 — 論点

前提: 「既存アプリは全部自作 — 再ビルド方向」(ユーザー、TASK_KAPI_DATA_FIELDS ラリー 3)。互換の目的は「v2.1 のバイナリを v3 で動かす」ことではなく、**(a) 戻り先 (os32 v2.1) を壊さない**、**(b) apps/game の submodule を両方から使える**、**(c) 移行の手順が 1 回で済む**こと。

| # | 論点 | 選択肢 (優劣は付けない) | 関係する決定・事実 |
|---|---|---|---|
| C1 | **KAPI の版**: v69 以降も追記のみで続けるか、fork で 1 回整理するか | (a) 追記のみ (v2.1 のバイナリは R = 300・データ欄 0x4B8 が同じなら v3 カーネルでも起動できる) / (b) fork で 1 回だけ整理 (欠番 v43、R の見直し、OS32X ヘッダ v4) → 全バイナリの作り直し (v63 と同じ移行) / (c) 追記のみで進め、残り 16 本 (ROADMAP §0 の規則) を切ったときに (b) | R = 300、実装 240、残り 60。トランポリン 1 ページの上限 R ≤ 318。[ABI2] 追記のみ。**→ (b) に決定 (2026-09-30、TASK_MEMMAP_V3 D35): 世代の識別と旧新混在試験が条件** |
| C2 | **メモリマップの再配置は ABI か**: shlib の帯・ジャンプ表 (`stub.rs` の 0x400000)、`load_addr = 0x500000` の焼き込み、シェルの 0x300000 が動くなら、KAPI が同じでも旧バイナリは起動しない (MEMMAP_V3 B5・B6) | 再配置 = ABI の境界と扱い、C1 の (b) と同時にするか、別々に 2 回の全再ビルドを許すか | 02_memory「現開発段階では ABI 変更を許容し、クリーン再ビルドと配備整合性で揃える」 |
| C3 | **SDK ヘッダと C11**: カーネルを C11 にしたとき `sdk/include/os32/*.h` (apps/game が読む) を C89 互換に保つか | (a) SDK は C89 互換 (両方から使える) / (b) SDK も C11 (apps/game を C11 に上げる = os32 側の保守と衝突) | apps/game の保守は当面 os32 側 (決定済み)。Rust 側は `kapi.json` から生成 |
| C4 | **submodule の同じコミット**を os32 と os32-v3 が指す期間、apps/game のビルドは v2.1 の SDK と v3 の SDK の**両方で通る**必要がある | (a) v3 の SDK が v2.1 の SDK の上位互換であるあいだだけ同じコミット / (b) fork の時点で apps/game にブランチを切る (`v3`) | `make external` は SDK ライブラリの変更で再ビルド (08_build §8-4) |
| C5 | **v2.x へのバックポート**: 「致命的な不具合のときだけ」— 何が致命的か (起動不能・データ破壊・実機で戻れない) | 基準を ROADMAP §0-1 に 1 行足す | 決定済みの範囲を明文化するだけ |
| C6 | **道具の互換**: `tools/` と `~/np21w-src` は共有・動作保証は v3 側だけ。v2.x に戻る必要が生じたとき、v3 向けに変わった `tools/` (配備・検査・NHD) が v2.1 の木で動かない可能性 | (a) 戻るときは os32 のタグ `v2.1` の worktree にある `tools/` を使う (手順を HANDOVER に 1 行) / (b) `tools/` の互換を保つ (動作保証しないと決めたので (b) は無い) | 決定「動作保証は v3 側だけ」 |
| C7 | **NHD / 媒体の移行**: 実機 Ra266 の HDD (v2.1) を v3 に上げる手順は一組 (カーネル・ローダ・シェル・shlib・in-tree・apps/game)。SerialFS `hsync --root` (21 分) か CD | MEMMAP_V3 B6、SERIAL_HOSTFS | v63 の移行と同じ型 (カーネルを先、ユーザーランドを後)。**X8 (2026-09-30、承認): 二段移行** — (1) 既存データ (文書・`settings.db`・`fep.db` の学習) を保持した媒体で v3 のシステム一式を停止中に更新 (新規 NHD に一式を配備しただけでは旧 NHD の文書・設定・学習が残される)、(2) 後半に TASK_DICT_META で学習を別ファイルへ移す。**v2.x の戻り先は旧バイナリに加えて旧データの写しも保持**。HDD の再区画・ext2 の再フォーマットは v3 の再配置では不要。**X6**: 「新 kernel を先に起動して旧 shell から更新」は前提にせず、v2 / v3 の配備先を分けて停止中に一組を更新する (D35) |
| C8 | **KAPI の 64 ビット戻り値**: `sys_time_now` は出力 2 本 (v59)。v3 で ABI を整理するなら「64 ビットを返す口」を決めるか | (a) 出力 2 本の規則を続ける / (b) 構造体を返す口を足す | ARM_GAUGE §10 (`int` は 32 ビット)。C1 と同時。**X6**: 型や構造体返却を変えるなら Rust 生成器 (`kapi_rust_gen.py:45` の未知型 → `u32` は生成失敗に)・引数幅・呼出規約 (戻りは EAX) を同時に検証 |

---

## 6. fork の段取り (os32-v3 に何を持っていくか、履歴の扱い)

決定済み (ROADMAP §0-1) の上に、**まだ決まっていない**段取りを並べる。

### 6-1. 持っていくもの・持っていかないもの

> **段取りの票は [FORK_PLAN.md](../../tasks/v3/FORK_PLAN.md) (2026-09-30、計画)** — ディレクトリ単位の一覧 (§1)、公開前の監査の項目と実行結果 (§2)、手順 a〜g (§3)、判断点 J1〜J8 (§4)。U9 (古い数字は fork 後に本文で直す)・U10 (submodule はそのまま)・U11 (新しい履歴で始め、経緯は HISTORY / CASE_STUDIES に起こす) はユーザー決定済み (2026-09-30)。下の表は起票時の案で、確定は FORK_PLAN 側。

| 対象 | 扱い (案) | 備考 |
|---|---|---|
| ソース全体 (`kernel/ fs/ exec/ kapi/ gfx/ drivers/ net/ boot/ lib/ arch/ platform/ userland/ sdk/ tools/ build/ include/`) | 持っていく | v3 はこの上に積む |
| `docs/` | 持っていく。**fork 直後に v2.1 時点の票を `docs/archive/v21/` へ**落とし (`tools/move_docs.py`)、INDEX の正典表を os32-v3 のものに書き換える | os32 側の INDEX は「正典は os32-v3」の注記 (既に冒頭に有る) を残す |
| `docs/hw/` | **持っていかない** (gitignore、著作権物、`tools/sync_hwdocs.sh` で各自ミラー) | 履歴に入っていないことの確認は §6-3 |
| `.env` / `SOUL.md` | 持っていかない (gitignore) | [D3] |
| `apps/` `game/` (submodule) | 同じ URL・同じコミットを参照 (決定済み) | **private repo** (08_build §8-6) — パブリックの os32-v3 から private submodule を指す形になる (§7 U10) |
| `.github/workflows/build.yml` `check.yml` | **持っていかない** (作り直す、決定済み)。`tools/ci/build_cross.sh` は共有の道具として持っていく | SUBMODULE_TOKEN の扱いは U10 |
| `images/` `build/nhd/` (生成物) | 持っていかない (gitignore のはず。確認) | — |
| タグ `v2.0` `v2.1` | 履歴を持っていくなら付いてくる (§6-2) | — |
| `.claude/` (skills、settings.json の ask/deny) | 持っていく (体制は同じ)。ROLES.md の「開発の場所」を os32-v3 に書き換える | — |
| ライセンス表記 | MIT (LICENSE)。vendor した第三者コード (SQLite public domain、lz4、zlib、microtar MIT、FatFs、newlib は toolchain 側) の一覧を README か `docs/` に 1 表 | パブリック化の前に揃える |

### 6-2. 履歴の扱い — 選択肢 (優劣は付けない)

| 案 | やり方 | 得るもの | 失うもの / 条件 |
|---|---|---|---|
| A | **全履歴を持つ** (`git clone` → 新 remote、`main` を feat/gui の先頭か v2.1 に置く) | 文書が引く SHA (`b8dab24`、`aa536e9`、`44bd0fe1`…) がそのまま辿れる。`git blame`。タグ | 履歴の全量が公開される → §6-3 の監査が全履歴に要る。リポジトリの大きさ |
| B | **v2.1 の木を初期コミットに** (履歴なし、「os32 `6ccc4049` (v2.1) から fork」と初期コミットに記す) | 監査が 1 スナップショットで済む。小さい | 文書中の SHA 参照が os32 側でしか辿れない (archive の票に多数)。blame が消える |
| C | **全履歴 + 文書の SHA 参照を「os32@SHA」の形に書き換え** | A と同じ + fork 後にどちらの履歴の SHA かが分かる | 書き換えの作業 (`tools/move_docs.py --rewrite-only` の延長) |
| D | A か C で持ち、**feat/gui を os32 の main に先に取り込む** (v2.1 のタグの後の文書整理 8 コミットを os32 側にも残す) | os32 の main = 戻り先が文書整理を含む | 「新機能は入れない」の範囲内 (文書だけ) か確認 |

### 6-3. パブリック化の前に要る監査 (どの案でも)

- **秘密**: 履歴全体に `.env`・API キー・パスワード・ホスト名/IP (ノート・実機のシリアル設定) が入っていないか (`git log --all -p | grep` 相当、または secret scanner)。`git log --all -- .env docs/hw` は 0 件 (2026-09-29 に確認) だが、本文への貼り付けは別に見る。
- **著作権物**: `docs/hw/` (Bible / UNDOCUMENTED / Crystal / Intel の PDF) が履歴のどこにも無いこと。`lcd_osd_cui_2026-09-29.jpg` のような写真は問題ない。
- **第三者コード**: `lib/` の vendor 一覧とライセンスの表記。NP21/W のフォーク (`~/np21w-src`) は別リポジトリなので対象外。
- **個人情報**: 実機の写真・ログに写り込んだもの (無いと思うが目で見る)。

### 6-4. fork の手順 (案、決裁後)

1. os32 側: feat/gui の文書整理を main に取り込むか決める (D)。HANDOVER に「戻るときの手順」(§5 C6 の (a)) を 1 行。
2. 監査 (§6-3) → 履歴の案 (A〜D) を決める。
3. `os32-v3` を作る (パブリック)。`main` = v3 の線。ブランチ規約 (worktree の基点) を ROLES に。
4. os32-v3 側: INDEX の正典表を書き換え、v2.1 の票を `docs/archive/v21/` へ、`CLAUDE.md` の体制欄を更新、[C1] を P0 で改訂するまでは C89 のまま (T0 = [TASK_C11_MIGRATION](TASK_C11_MIGRATION.md)、[C1] の改訂文面は同票 §9、決定後に CONSTRAINTS を直す)。
5. CI を作り直す (build.yml を写さず、`tools/ci/build_cross.sh` を使って最小から)。
6. os32 側: INDEX 冒頭の注記に os32-v3 の URL を足す。以後 os32 は「戻り先」。

---

## 7. ユーザーの判断が要る点と、Codex に突き合わせるべき論点

### 7-1. ユーザーの判断 (U1〜U11)

| # | 判断 | 選択肢 | 出典 |
|---|---|---|---|
| U1 | **メモリマップ (P1) の討論の進め方** — **決定 (2026-09-30)**: 意見書 → 反論と合成 → Fable 最終案 → Codex 突き合わせ (3 往復) → Approve、ユーザーが決裁点に答える形で進めた。記録は TASK_MEMMAP_V3 に書き戻した (新票は起こさない)。**討論も済み** | — | TASK_MEMMAP_V3 §11 |
| U2 | **実行モデル (P6)**: 協調のまま / 背景だけタイマで切る / プリエンプティブ寄り。APP_FIRST「前景 1 本」と ROADMAP §2 の折り合い。**意図は決定 (2026-09-30、§3 P6 の行)**: 同時実行は求めない、前景が固まったらカーネルが取り戻す (番犬・CTRL+STOP)。同時使用の快適さは判定に含めない (§2-1)。**実装方式 (番犬の形) は P6 の票で** | (a) + 番犬 | §4 D3、§3 P6 |
| U3 | **OS64 の位置**: ホスト側 Host Service の名前 / 別の 64 ビット OS の構想 / 語を使わない。**決定 (2026-09-30): (ii) OS32 とは別の 64 ビット OS の構想**。v3 / v4 の範囲に入れない。ARM_GAUGE §10 の「64 ビットは範囲外」とは「別物」として両立させる。語の定義は [LEGACY_LIVING_PRESERVATION.md](../../LEGACY_LIVING_PRESERVATION.md) の冒頭 1 か所 (AUXILIARY_CORE_SERVICE・ROADMAP はそこを指す) | (i)(ii)(iii) → (ii) | §4 D6 |
| U4 | **v3 の目的の一文** (§2) と **範囲に入れないもの** (V4 / ACS / Fabric / GUI アプリ群 / DOS 移植スキーム / USB / スワップ) の承認。**目標の 2 段 (§2-1) と「高位カーネルは v4 以降」は決定 (2026-09-30)**。~~一文と範囲外の一覧そのものは未承認~~ → **決定 (2026-09-30): 一文と範囲外の一覧を承認。ただし GUI アプリ群は範囲外の一覧から外し v3 後半に入れる** (U7 の決定、[gui/TASK_CONTROL_PANEL.md](../../tasks/gui/TASK_CONTROL_PANEL.md)・[gui/TASK_I18N.md](../../tasks/gui/TASK_I18N.md) と合わせる)。正典は本案 [V3_PLAN.md](../../tasks/v3/V3_PLAN.md) §1・§2 | 承認 / 修正 → 修正つきで承認 | §2、§2-1 |
| U5 | **Rust をカーネル文脈 (IRQ) から呼ぶ**モジュール (合成器 P2) を v3 で認めるか。**決定 (2026-09-30): 認める** (IRQ 文脈で Rust を呼ぶ合成器 (音源) は認める)。同日のユーザー指示「Rust 化するほうが有利なものを再度確認する (C11 化との実装コスト比較で)」の調査票は [RUST_VS_C11.md](../../tasks/v3/RUST_VS_C11.md) (合成器と OpenType は Rust、カーネル核・FS・FEP・SDK・デコーダは C11 に揃える推奨。判断 7 点は同日ユーザーが決定: 1〜5 推奨どおり、6 nightly は固定済み、7 TCP/IP は足さない)。**386 下限は Rust でも守る (ユーザー決定 (a) 2026-09-30、RUST_VS_C11 §7: `cpu=i386` でも LLVM は `bswap` / `cmpxchg` / `xadd` を出し (#58470、バージョンアップでは直らない)、最終成果物 (LTO・最終リンク後、compiler_builtins・依存・asm 込み) に 386 に無い命令が無いことを必須の工程に。出れば Rust で作らないかコードで避ける。TASK_MEMMAP_V3 D36)** | 認める / 認めない (合成器はユーザランド常駐にして V86 中は鳴らさない) | §4 D8。**IRQ の合成器は整数演算から** (ユーザー決定 2026-09-30、Codex X5: x87 の状態をアプリごとに保存・復元する契約が P6 で入るまで IRQ で FP を使わず、生成コードに x87 / SSE / MMX が無いことを検査。Rust 容認は維持。TASK_MEMMAP_V3 §3-5-3) |
| U6 | **保留 5 件** (F3a〜c / F2c / FEP_BOUNDARY / MEMORY_RAM_INTEGRATION / DEVICE_RESERVATION) を v3 で拾うか。**決定 (2026-09-30): 「一部」** — F3a / F3c → T4、F3b → TASK_DICT_META の後に「同一 DB の排他 open」、F2 の残り → T4 + T5a、FEP_BOUNDARY → T2 / T4 / T5a の要件 (旧 `db_exec` / `db_prepare` の 1024B 超は失敗に)、MEMORY_RAM_INTEGRATION → 撤回 (archive、残る 2 点は T1)、DEVICE_RESERVATION → 改訂して P4 (核は T1、識別 + 予約は起動時・probe + enable は GUI 境界、実測 BAR へ広げる → X4 を Codex へ)。仕分け表 [U6_PENDING_REVIEW.md](U6_PENDING_REVIEW.md) の推奨どおり (TASK_MEMMAP_V3 D29〜D34) | 拾う (P4・P10) / 一部 / 拾わない | HANDOVER_v14 §3、§1-3、U6_PENDING_REVIEW |
| U7 | **GUI アプリ群と Video HAL / 互換層 (P9)** を v3 後半に置くか v4 に送るか。**決定 (2026-09-30): P8 / P9 は P1 から切り出した別の柱** (TASK_MEMMAP_V3 D19)。**時期も決定 (2026-09-30): v3 後半、基盤が整い次第**。ZSNES は移植する、難度の低いものから — 候補と挑戦順は [PORT_CANDIDATES.md](../../tasks/v3/PORT_CANDIDATES.md) | v3 後半 / v4 | §4 D20、§3 P9 |
| U8 | **KAPI / ABI の整理** (§5 C1〜C3): 追記のみ / fork で 1 回整理 / 16 本を切ったとき。SDK ヘッダの C89 互換。**決定 (2026-09-30): 後方互換は基本考えない** (TASK_MEMMAP_V3 D7 — 全再ビルド・旧形式拒否・互換層なし)。~~追記のみか 1 回整理するかの選択は残る~~ → **決定 (2026-09-30、TASK_MEMMAP_V3 D35): (b) fork で 1 回整理。スロットの順を変えてよいが、世代の識別 (形式版・KAPI ABI 世代・メモリ配置世代・shlib プロトコル) と旧新混在の試験を P7 の必須にする** (Codex X6) | (a)(b)(c) → (b) | §5、§3 P7 |
| U9 | **古い写しの扱い**: MD1・MD4・MD5・MD6・MD9 の古い数字・番地を票の本文で直すか、注記だけ足すか (「草案の本文は書き換えない」の範囲)。**済 (2026-09-30、手順 f): 直した** — 数字は書き直さず 02_memory §2-1 の生成ブロックを指す形に、残した数字には日付。archive/ の票は §3-1 の注記だけ | 直す / 注記 | §3-1、FORK_PLAN §3 f |
| U10 | **決定 (2026-09-30): そのまま** (apps/game は個人で作ったアプリなので private の submodule をトークンが要る状態で参照する。NP21/W のフォークは submodule ではなく、ライセンスの判断で private のまま)。**submodule の公開範囲**: パブリックの os32-v3 が private の os32-apps / os32-game を指す形でよいか (clone で落ちる、CI は SUBMODULE_TOKEN) | そのまま / apps・game も公開 / os32-v3 では参照を外す | §6-1 |
| U11 | **履歴の案** (A〜D) と **監査の実施者・方法** (§6-3) | A / B / C / D | §6-2 |

### 7-2. Codex に突き合わせる論点 (X1〜X8) — 設計の分岐・前提の妥当性

**回答済み (2026-09-30、Codex gpt-6-astra、基点 d0284b81、読み取りのみ)** — ユーザーは推奨をすべて承認 (同日)。結論と反映先を 4 列目に足した。反映の本文と Codex の根拠 (`file:line`) は TASK_MEMMAP_V3 §8-4。

| # | 論点 | 何を確かめてもらうか | 結論と反映先 (2026-09-30) |
|---|---|---|---|
| X1 | **P0 → P1 の順序の前提**: 「C11 の `_Static_assert` が無いと再配置を検査なしでやることになる」(KSTACK §7-5) は、現行の `STATIC_ASSERT` (負の配列長) と `build/os32.ld` の `ASSERT`・`gen_memmap.py --check` で既に代替できていないか。C11 を先にする根拠が今も成立するか | 再配置の検査の網羅 (リンク時 / 生成時 / 起動時 kselftest) を経路ごとに | **成立しない** (既存の STATIC_ASSERT・リンカ ASSERT・gen_memmap・起動時検査で代替できている)。C11 単独先行の順序は維持し、根拠を「コンパイラの変更と配置の変更を別々に受け入れ原因を分ける」に書き直した → §3 P0、順序の案 1 |
| X2 | **動的読み込み (P2) の取り決めの形**: 共有ライブラリ (`shlib.c`) の再配置・ジャンプ表を流用してカーネル権限のモジュールにする案の穴 — CPL=0 で載せるコードの検証 (署名なし)、割り込みハンドラの登録 (HAL_WIRING 1-1) との結合、失敗時の巻き戻し、M0 §6 のキャッシュ (ロードしたコードへ飛ぶ経路) | 反例の到達可能性 (ブート中に載せる順と依存) | **条件付き** (流用では検証・公開・失敗回収が不足)。専用のモジュールローダ: ロード時検証、信頼する配布物だけ、IRQ 登録と初期化状態 (STARTING → RUNNING)、停止を証明できない失敗は隔離、公開の境界 → §3 P2、TASK_MEMMAP_V3 §4-7・T4 / T5a / T5c |
| X3 | **面公開 API の記述子化** (TRIDENT §4-2、D18): 恒等を前提にした `gfx_bb_phys_range()` → `{virt, phys, size}` と `map_user_range_phys_keep` の変種で、既存 3 バックエンド (回帰) と高位窓の両方が正しく USER 昇格するか。共有 PT を壊す経路が残らないか | ホスト試験の (a)〜(e) (TRIDENT §4-2) の十分性 | **条件付き** (私有 lease PT の契約は成立、`_phys_keep` の共有 PT 昇格だけでは不成立)。記述子は SURFACE 台帳への参照、lease の仮想番地は別に返す、キャッシュ属性は台帳から、共有 PT 書込の拒否、ページ端の占有と解除順、試験 (a)〜(e) の読み替え → TASK_MEMMAP_V3 §2-2・§2-3 ⑥ (d)・T2、TRIDENT §4-2 の注記、§4 D18 |
| X4 | **デバイス予約 broker** (DEVICE_RESERVATION §4) を Trident の実測 BAR にも開く設計 (D17): 「定数だけ許可」から「検証済み実測範囲」へ広げたとき、所有権・decode 幅・寿命の検証が緩まないか。**広げること自体は U6 で決定 (2026-09-30、TASK_MEMMAP_V3 D33) — Codex に問うのは緩まない設計の形** | 予約の transaction の不変条件 | **条件付き** (装置の同一性・完全な decode 範囲・永久所有権に結び付ける)。検証済み資源レコード、予約 / 写像 / 面の範囲の分離、全 span の一括 commit、GUI 境界での同一性の再確認、永久予約とモジュール回収の分離。Trident の今の採取値は未検証 → TASK_MEMMAP_V3 D33・T1・§4-5、DEVICE_RESERVATION 状態行、TRIDENT §4-2 の注記、§4 D17 |
| X5 | **実行モデル (U2) の各案の代償**: 協調のままで APP_FIRST の「前景に集中」を満たすには何が足りないか (背景の WM がアプリの syscall の中で走る現行 T8 の制約、`ring3_wm_depth`)、タイマで切るなら資源回収 (owner 単位) と KAPI の再入がどこで壊れるか | 反例 (再入・回収) | **条件付き** (協調 + 強制終了で成立、停止可能点・KAPI 再入・x87 の契約が要る)。P6 の票の要件 (自動判定と強制停止の分離、長い KAPI の期限・取消、KAPI 中に AS を切り替えない、trim は記録して安全点で配送、x87 をアプリごとに保存・復元、受入 6 項目)。**IRQ の合成器はまず整数演算** (U5) → §3 P6、TASK_MEMMAP_V3 §3-5-3 |
| X6 | **KAPI の整理 (U8)** を fork で 1 回やる場合の移行の穴: v63 の移行 (ヘッダ v3・`os32_kapi_v63`) と同じ型で足りるか、shlib の `shlib_init` の版検査、常駐シェルの拒否、Rust の生成器 | 旧新混在の全経路 | **条件付き** (v63 型の検査では不足: 容量を保ったスロット整理を識別できない、常駐シェルの番地照合が `!is_shell` の内側、shlib は min KAPI だけ、`clean` に `clean-external` が無い、`hsync` は `/sys` を除外)。**D35**: スロット順を変えてよい、世代の識別 4 つ、拒否契約、旧新混在試験を P7 の必須に → §3 P7、§5 C1・C8、§7-1 U8、TASK_MEMMAP_V3 D35・§7 |
| X7 | **メモリマップの討論の議題 (M1〜M15) の網羅性**: TASK_MEMMAP_V3 の B1〜B9 (往復 1) に、その後に足された事実 (デバイス窓の帯、DMA プール暫定、APP_BAND の 4-B、MEMORY_RAM_INTEGRATION §8 の >16MiB 検出源) で論点が増えていないか。**案の優劣ではなく論点の抜け**だけを問う | 抜けの指摘 | **「抜けなし」は成立しない** (元の 4 項目は D32 / D33 / U24 で取り込み済み。補う境界条件 4 件): BB 確保の時点を D33 に統一、gfx の識別部はコアに残すか早期ロード、P2V / V2P は恒等 supervisor 領域だけ (lease / AS は台帳から、DMA は物理記述子)、排他 open までの同一 DB の重複接続 (parked アプリ同士) → TASK_MEMMAP_V3 §2-2・R4・§4-5・§3-4・§4-6・T4 |
| X8 | **v2.x 互換 (§5) の前提**: 「全部自作なので再ビルド方向」で落とせないもの (NHD 上のデータ・settings.db・ユーザー辞書・実機 HDD の区画) が再配置で影響を受ける経路 | データの互換 (バイナリ以外) | **全再ビルドだけでは成立しない** (再配置はデータ形式を変えないが、配備の上書きと辞書の変更に明示の保存・移行が要る)。二段移行: 既存データを保持した媒体で v3 一式に更新 → TASK_DICT_META で学習の移行 (件数・内容・再実行・途中失敗の復旧、S/M/L の `dict_id` 対応)、`fep_user.db` を保護名に、v2.x の戻り先は旧データの写しも保持。HDD の再区画・ext2 の再フォーマットは不要 → §5 C7、TASK_DICT_META §3・§5・§6 |

---

## 8. この文書でしないこと

- 草案の本文の書き換え (古い数字・番地は §3-1 MD と §4 で指すだけ)。
- メモリマップの結論の本文 (正典は TASK_MEMMAP_V3。ここは §3-1 の要点と決着先だけ)。
- 本案への昇格 (ユーザー判断と Codex の突き合わせの後) → **済 (2026-09-30、[V3_PLAN.md](../../tasks/v3/V3_PLAN.md))**。
- コードの変更、fork の実行。
