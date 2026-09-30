# OS32 の開発の経緯と推移 — 2026-04-07 の初期コミットから os32-v3 への fork まで

> 状態: **完了記録 (2026-09-30)** — os32 (v2.x、タグ `v2.1`) の開発の経緯を、os32-v3 へ持っていくために要約したもの。
> 開発中の詳細 (票・引き継ぎ・教訓の本文) は書かず、**os32 リポジトリ (v2.1 時点の記録) への参照**で済ます。
> 発行: コーダー `claude-fable-5-1` (feat/gui `8e00edec`)、[FORK_PLAN](tasks/v3/FORK_PLAN.md) §1-4 / §3 b による。
> 数字は `git log` / `git ls-tree` の実測 (2026-09-30、HEAD `8e00edec`)。

読み方: §1 が節目の年表、§2 が版ごとの要約、§3 が体制の推移、§4 が主要なユーザー決定の年表、§5 が数字、§6 が os32 側の参照先。
os32-v3 の読者は §2 と §4 だけ読めば「なぜ今の形か」が分かる。教訓 (普遍的なもの) は [CASE_STUDIES.md](CASE_STUDIES.md)。

---

## 1. 節目の年表

| 日付 | 節目 | 印 |
|---|---|---|
| 2026-04-07 | 初期コミット (228 ファイル、51,348 行。git 以前の DOS 上の実験から 32 ビット OS へ) | `504e3b99` |
| 2026-04-14〜16 | シェルの PATH・環境変数・タブ補完、FD リダイレクトとパイプ (KAPI v25)、`exec_exit()` の資源自動回収、YM2203 (KAPI v26) | — |
| 2026-04-17 | **v1.0** — 最初の安定版・公開。同日に v1.x GUI シェルのロードマップ | タグ `v1.0.0` |
| 2026-04-22〜30 | HostDrvFS v2 と `hsync`、マウス、SQLite のカーネル統合 (KAPI v29)、FEP (SKK 辞書を SQLite に)、ゲーム基盤のライブラリ群、ブート改革 (INT 1Bh トランポリン + ext2/LZ4 ローダ)、Rust の no_std クレート | — |
| 2026-06-08 | ゲーム側の 4 コミットだけ (5〜7 月は本流に記録なし。VDM は別ブランチ `archive/vdm-wip-20260805`) | — |
| 2026-08-06〜07 | feat-vdm からの移植 (FDC 幾何、loop_dev、KCG フォント、libos32ui、FEP の候補・ユーザー辞書)、FatFs へ一本化 | タグ `v1.0.1` (08-07) |
| 2026-08-08 | NP21/W 内蔵デバッグ API の MCP 統合 (ai-debug フォーク) | タグ `v1.0.2` |
| 2026-08-08〜12 | **V86**: TSS と I/O 許可ビットマップ → 割り込み反射 → Ys のタイトル (08-09) → 曲が鳴る (08-10) → **MS-DOS 5.00A が OS32 の上で動く** (08-11) → NHD/HDI からのブート (08-12) | タグ `v1.0.3` (08-11) |
| 2026-08-25〜26 | MGX 画像形式、`gen_font16.py`、ローカル LLM の自動プレイ基盤、SDK の配布 tarball | — |
| 2026-09-01 | **v1.1** — ツリー再編 (`userland/` `sdk/` `apps/` `game/`)、`SYS_VERSION` に一本化 | タグ `v1.1.0` |
| 2026-09-03 | **v2.0 — リング 3 (CPL=3) ネイティブ** (M1 土台 → M2 KAPI トランポリン → M3 全外部プログラムを CPL=3、1 日で着地) | タグ `v2.0` |
| 2026-09-04 | apps / game を submodule に、GUI シェルの設計記録と凍結契約 (API_CONTRACTS)、KAPI v40 (GUI HAL 枠) | — |
| 2026-09-05〜06 | LGY-98 (NE2000) ドライバ M0〜M4 とリンク層 L0〜L3 (Host Services)。**GUI 1.1** (gshell WM、libos32gui.shlib、9801 / PEGC / Cirrus の 3 バックエンド、KAPI v42) を 2 日で並列 12 票 → main へ (09-06) | — |
| 2026-09-07 | **GUI 1.2** (デスクトップ環境) を main へ | — |
| 2026-09-07〜09 | hermes を PM に据えた多層構成を試し撤収 (RETROSPECTIVE) | — |
| 2026-09-09〜14 | **GUI 1.3** — 端末 (libos32term)、GUI アプリ 4 本の協調、物理 RAM 上限 16MB の撤廃 (K6)、入力統合 (K7)、全画面 GFX 復帰 (T8)、CPL=3 のシェル `sh` (T9)、設定レジストリ settings.db (S0〜S5) | main へ 09-10〜13 |
| 2026-09-14〜18 | **GUI 1.4** — Host Services N1〜N4 (wget / lpr / クリップボード / 時刻)、hsync H1〜H4、ext2 B8、エディタ GUI 版、移植準備 (arch/、kstring の C 版) | — |
| 2026-09-15〜16 | 文書整理 A〜F (状態行の統一、教訓の立項、版数表、索引の再編、生成される試験一覧、リンク・孤児の検査、票 44 本を archive へ) | — |
| 2026-09-17 | 実機 **PC-9821Ra266** を入手。v3 の計画 (入れ物を作り直す) と実機の計画を起票 | — |
| 2026-09-18 | シリアル 38400 が実機では出ていなかった (8253 の整数分周) — 実機の前の最初の「エミュレータが模擬しない量」 | — |
| 2026-09-22 | **実機初日** — FD 起動合格 (FDC の 3 原因はどれも NP21/W では出ない)、シリアル 115200、PCI 列挙 (`lspci`)、内蔵 82557 の計画 | HANDOVER_2026-09-22 |
| 2026-09-23 | PIT のクロック判定 (tick 8.125ms → 10ms)、結線の土台 TASK_HAL_WIRING (Codex 12 往復で Approve、同日着地、KAPI v59〜v60)、PCM CS4231 の設計 (9 往復)、CI (GitHub Actions) の本体ビルド | — |
| 2026-09-24 | KAPI のデータ欄の固定 (v63)、HDD インストーラ段 1・2 (KAPI v64)、パッケージ生成、VK32 CRC (v65)、**v3 分岐前の区切りをタグ v2.1 にする**と決定 | — |
| 2026-09-25 | **実機で CD から HDD インストール → HDD 起動に成功** (v65、`e146022`)。ERASE、SerialFS、MINIMAL をレスキュー兼インストーラに | HANDOVER_2026-09-22 (09-25 の節) |
| 2026-09-26 | キーボードだけの GUI 操作 (KBD_NAV)、カナ・CAPS のロックキー、`kbdstat -w`、検査の 3 段 (check-fast / check-changed / check)、変異試験を写しの木へ、`hsync --root` | CHECKLIST_2026-09-26 |
| 2026-09-29 | 実機 ROM の OUT 列 (`v86 -g`) に合わせた PEGC 640x480、SerialFS で HDD 起動のまま更新 (21 分)、v2.1 の棚卸し (票 54 本を archive へ、状態行の語彙、ROLES §0 を 1 表に)、**v2.1 確定** | タグ `v2.1` = `6ccc4049` |
| 2026-09-29〜30 | v3 の本案の草案 (V3_PLAN_DRAFT)、**メモリマップの 3 者討論** (ユーザー / Fable / Codex) で方針確定、Rust vs C11、T0 (C11 化)、**fork の段取り (FORK_PLAN) を承認**、公開前の監査 (手順 a)、この文書 (手順 b) | `d995e078` → `8e00edec` |

---

## 2. 版の推移

版数の対応の正典は os32 の `docs/ROADMAP.md` §0。ここは各版 5〜10 行。

### v1.0 (2026-04-17) — 最初の安定版

i386 プロテクトモード + ページング + ガードページ、IDT/PIC、kmalloc、KernelAPI v26 (関数 123 本)、VFS (ext2 R/W、FAT12、ISO 9660)、
パイプとリダイレクト、外部プログラム方式のシェル (補完・履歴・環境変数・スクリプト)、KBD / IDE / FDC / Serial / FM / RTC / KCG のドライバ、
640x400 16 色の CPU 描画 (`libos32gfx`)。カーネル本体 73KB、全体約 47,000 行。到達までのロードマップは `docs/archive/ROADMAP_v1.0.md`。

### v1.0.1〜v1.0.3 (2026-08) — VDM と道具

feat-vdm ブランチで作っていたもの (FDC の幾何抽象化、loop_dev、KCG フォント、libos32ui、FEP の候補・ユーザー辞書) を本流へ移植し、
NP21/W の ai-debug フォークにデバッグ HTTP サーバと MCP (21 ツール) を組み込んだ。V86 (VDM) は 5 日で TSS → 割り込み反射 → BIOS HLE → IPL ブート →
Ys I が本編まで → **MS-DOS 5.00A が起動** (08-11) → NHD/HDI からのブート (08-12) まで進んだ。

### v1.1 (2026-09-01) — ツリー再編、V86 で MS-DOS、ホストからの直接配置

`programs/` を `userland/` と `game/` に、SDK を `sdk/` に切り出し、標準アプリを `apps/` (SDK だけでビルド) に分離。KernelAPI v26 → v39 (関数 123 → 168 本)。
稼働中のゲストへホストから直接ファイルを置く経路 (`/api/deploy` + `kernel/hotdeploy.c`)、カーネル内 selftest、`irq_save`/`irq_restore`、
`STATIC_ASSERT`。要約は `CHANGELOG.md` の v1.1。

### v2.0 (2026-09-03) — リング 3 ネイティブ

外部プログラムは CPL=3 で自分のページディレクトリの上で走り、不正なポインタはアプリだけを殺す (`fault_kill_count`)。KAPI は `int 0x80` の
トランポリン経由で、**既存アプリはソース無変更**で CPL=3 に移った (M2)。判断の根拠 (リング 3 か Rust か → 保護はリング 3、Rust は新規ユーザーランドだけ) と
M1〜M3 の完了記録は `docs/archive/kernel_v2/PLAN.md`。

### GUI シェル 1.1〜1.4 (2026-09-04〜29、カーネル 2.0 → 2.1 の上)

Win3.1 / 早期 Win95 の見た目、協調型シングルタスク、WM = gshell (シェル帯に常駐)、`libos32gui` (Rust no_std、共有ライブラリ帯 0x400000〜)、
`gui_call` 1 本 + GUI SHM の wire protocol、InvalidateRect 方式 + XOR 枠 + 全画面 1 枚のバックバッファ、HAL のバックエンド表 (9801 planar / PEGC 256 色 / Cirrus GD54xx)。

| 版 | 期間 | 中身 |
|---|---|---|
| 1.1 GUI 基盤 | 09-04〜06 | 設計記録と凍結契約 → レーン H/K/W/C の 12 票を並列 → 2 日で main へ (KAPI v42)。ゲート G1 描ける → G5 9821 |
| 1.2 デスクトップ環境 | 09-06〜07 | セッション、ダイアログ、ファイルマネージャ、CUI ⇄ GUI の往復、Shut Down |
| 1.3 端末と CUI 抽象化 | 09-09〜14 | libos32term、GUI アプリ 4 本の協調 (K5)、RAM 上限 16MB 撤廃 (K6)、入力統合 (K7)、全画面 GFX 復帰 (T8)、CPL=3 のシェル `sh` (T9)、settings.db (S0〜S5)。hermes が独断で着手した T5b/T6a を監査して撤去 |
| 1.4 ホストサービスと最小のアプリ | 09-14〜29 | Host Services N1〜N4、hsync H1〜H4、エディタ GUI 版、About、R2 計測、キーボードだけの GUI 操作 (KBD_NAV)。**GUI の版はここで閉じ、以後は v3 の線** (ユーザー決定 09-29) |

設計記録は `docs/tasks/gui/DESIGN.md`、契約は `docs/tasks/gui/API_CONTRACTS.md`、票は `docs/archive/gui_v1{1,2,3,4}/`。

### v2.1 (2026-09-29) — 実機 PC-9821Ra266 で動く区切りの版

KernelAPI v39 → v68 (関数表の容量 300、実装 240、データ欄は v63 で固定)。実機で **FD 起動 (2HD / 1.44MB) → CD からの HDD インストール (`cdinst`、ERASE) → HDD 起動**、
シリアル 115200 と SerialFS (HDD 起動のまま更新)、PCI 列挙、PIT のクロック判定、キーボード 8251 のコマンド語、Ra266 の PEGC 640x480 を実機 ROM の OUT 列に合わせた
(画面を見ない条件で受け入れ、目視は残件)。カーネル層の修正: WM の文脈の KAPI 出力ポインタ検査、VFS の FD の失効、SHM 帯とカーネルスタックの重なり、
Cirrus のリニア窓をデバイス窓の帯 (0xFE000000〜) へ。道具: 検査の 3 段、変異試験の写しの木、CI の本体ビルド、`np21w_ctl.py`。
v3 に先行して着地したもの: 結線の土台 (HAL_WIRING)、PCM CS4231、KHEAP の切り直し、デバイス窓の帯。
リリースノートは `docs/RELEASE_v2.1.md`、実機の判定は `docs/archive/realhw_v21/CHECKLIST_2026-09-26.md`。

### v3 (2026-09-17 計画 → 09-30 方針確定 → os32-v3 へ fork)

「機能を足す前に入れ物を作り直す」版。2026-09-17 にカーネル帯 1MB の帯域超過 (必要量 1041KB) が判明し、再配置で凌いだ (修正後の余裕は 34KB、`docs/archive/kernel_v21/TASK_KSTACK_USER.md` §7) が、v2.1 の時点では本体の予算の残りが約 13KB — 穴の根は帯の切り方で、帯の中で削るのは延命、というのが出発点。
目的の一文と目標の 2 段 (8MB 機で GUI + 私有 2MB のアプリ / 前景 1 本で P100/32MB の Win95 より快適、32〜64MB 機で Win2000 当時の使用感) は `docs/tasks/v3/V3_PLAN_DRAFT.md` §2。
メモリマップ (システムは恒等のまま、アプリだけ 0x80000000〜、物理台帳、SQLite のモジュール化、低位 640KB を V86 へ、OpenType) は 3 者討論で決めた (`docs/tasks/v3/TASK_MEMMAP_V3.md`、D1〜D36)。
最初の票は T0 (C89 → C11、`docs/tasks/v3/TASK_C11_MIGRATION.md`)。os32-v3 は新しい履歴 (初期コミット) で始め、経緯はこの文書で持つ (FORK_PLAN)。

---

## 3. 体制の推移

| 期間 | 体制 | 出典 (os32) |
|---|---|---|
| 2026-04〜08 | ユーザー + AI コーディングアシスタント 1 セッション。設計・実装・検証を同じセッションで。`CLAUDE.md` は 558 行 (`21cd48e9`、09-05) まで膨らんだ | README「制作経緯」、`docs/POLICY_DEV.md` §1 |
| 2026-09-01 | 3 層 (設計 = Fable 5 / コーディング = 自宅サーバーのローカル LLM / ビルド検証 = NPU のローカル LLM) を組む | `docs/archive/agents/RETROSPECTIVE_2026-09-09.md` |
| 2026-09-04〜06 | GUI 1.1 で**レーン別の並列コーダー** (worktree 隔離のサブエージェント 12 票) と、外部の設計レビュー (契約の改訂 3 回) を初めて使う | `docs/tasks/gui/API_CONTRACTS.md`「改訂の記録」 |
| 2026-09-05 | 文書運用の決定: 情報単位ごとに正典 1 か所、CLAUDE.md は入口だけ、手順はスキルへ | `docs/INDEX.md` 冒頭の正典表 |
| 2026-09-07〜09 | hermes (Agent CLI) を PM に据え Claude / Codex / ローカル LLM をワーカーに。過剰なバグ懸念と 1 ステップごとの中断、設定の消失、Codex のクォータ枯渇で**撤収** | RETROSPECTIVE_2026-09-09 |
| 2026-09-09 | **4 役**に固定: PM = Claude Code (Fable 5.1) / コーダー = Opus サブエージェント (worktree) / レビュアー / テスター = ローカル AI (`tools/emu_agent/`)。規約は 3 行に固定 (インシデントごとに 1 行足さない) | `docs/tasks/agents/ROLES.md` 旧 §1 |
| 2026-09-12〜13 | **レビューは Codex** (`codex exec -s read-only`) へ PM が自動依頼し往復する。往復は 3 回まで、決着しなければユーザーへ。依頼文に網羅性 (到達可能な欠陥を一度に全部、経路ごとの見た / 見ていない) を要求 | ROLES §5 |
| 2026-09-13〜16 | レビュアーの枯渇と代替: Codex → Fable サブエージェント (09-13) → Codex に戻す (09-14) → Antigravity CLI (09-16) → ローカルモデルの補助レビュー (`tools/review_local.py`、09-16)。「挙げなかったことは無いの証拠にならない」 | ROLES 旧 §5 の在庫表 |
| 2026-09-14 | 「アプリ層だけ別エージェント (Claude Code は設計 + レビュー)」を発行し**同日に撤回** | `docs/archive/agents/HANDOVER_v14.md` |
| 2026-09-15 | コーダーは基点の SHA を確かめて報告する (worktree が古い基点から始まるのを 2 度踏んだ)。カーネル層の不具合は新機能より優先 | ROLES §2、`docs/POLICY_DEV.md` §1 |
| 2026-09-23 | PM = Opus 5.5 (Fable の枯渇)、レビュアー = Fable + Codex の 2 者で突き合わせ → 同日 Fable が月間上限で Opus サブエージェントに。コーダーは Opus 5.5 (モデル ID を報告させて確認) | ROLES 旧 §0 |
| 2026-09-25 | **レビュアーは Codex だけ**。コーダーは Codex の重大 (P1) 指摘を含む修正なら Fable 5.1、他は Opus 5.5 | ROLES §0 |
| 2026-09-26 | Codex にモデル名を名乗らせ、astra でなければ休ませて Fable が代行。検査の 3 段とレビューの重さ (P3 だけの直しは PM が読んで着地) | ROLES §0 |
| 2026-09-29 | PM = Sonnet 5.5 (取り次ぎのみ) → 同日夕に Opus 5.5 へ戻す。ドライバ設計のレビューは必ず Codex と突き合わせる。推奨は基本的に承認、[D2] と実機の物理操作は個別承認 | ROLES §0 |
| 2026-09-29〜30 | **3 者討論** (ユーザー = 決定者、Fable 5.1、Codex gpt-6-astra、PM は進行) — 意見書 → 反論と合成 → 最終案 → Codex 往復 → ユーザー決裁の形式で v3 のメモリマップを決めた | `docs/tasks/v3/TASK_MEMMAP_V3.md` §11 |

現行の体制 (fork 時点) は ROLES §0 の 1 表。os32-v3 でも同じ体制で始める (FORK_PLAN §3 g)。

---

## 4. 決裁の年表 (主要なユーザー決定)

出典は各行の票。決定の本文はそちらにあり、ここでは 1 行で写す。

| 日付 | 決定 | 出典 (os32) |
|---|---|---|
| 2026-09-01 | v2 は保護機構の導入 (リング 3)。Rust は新規ユーザーランド層だけ | `docs/archive/kernel_v2/PLAN.md` §1 |
| 2026-09-04 | GUI の設計前提: **486 機能ティアは見送り** (可否は CPU 世代ではなくダーティ面積で決まる)、InvalidateRect / XOR 枠 / 単一バックバッファ、9821 は PEGC → Cirrus の順、共有ライブラリ帯は案 A (0x400000〜)、API 契約の凍結 (以後の変更はユーザー承認) | `docs/tasks/gui/DESIGN.md`、`API_CONTRACTS.md` |
| 2026-09-05 | 文書運用: 情報単位ごとの正典 1 か所 (外部の冗長性診断 6 件を受けて) | `docs/INDEX.md` 冒頭 |
| 2026-09-09 | hermes の撤収、4 役の体制 | RETROSPECTIVE_2026-09-09 |
| 2026-09-11 | 物理 RAM 上限 16MB → 32MB 以上 (目標 128MB、K6-RAM)。CPL=3 アプリ生存中は `--cpl0` の子を拒否 (A1)。8MB は 1 本立てば要件 | `docs/archive/gui_v13/` |
| 2026-09-12 | レビューは Codex へ PM が自動依頼、往復 3 回まで。T8: cpl0 は GUI から起動禁止、`OS32X_FLAG_GFX` を全画面の宣言に | ROLES §5、archive/gui_v13 |
| 2026-09-13 | 設定変更は OS 経由だけ (アプリは読みだけ → 書きも OS 経由の wrapper)。配備ツリー内の symlink は配備全体を拒否 | `docs/archive/settings/TASK_S2.md`、`TASK_S0.md` |
| 2026-09-14 | v1.4 を「ホストサービスと最小のアプリ」に縮める。LGY-98 を既定で有効 (Host Services を製品機能に)。アプリ層の別エージェント案を撤回 | `docs/ROADMAP.md` §1 |
| 2026-09-15 | **カーネル層に分かっている不具合があるあいだは新機能より先に直す**。次期カーネルを「v2」ではなく **v3** と呼ぶ (出荷済み 2.0 と衝突) | `docs/POLICY_DEV.md` §1、ROADMAP §0 |
| 2026-09-17 | **移植は 32 ビット限定** (16 / 64 ビットは範囲外、ポインタ幅の抽象化を足さない)。**x86 は kstring の asm を維持** (実測で C 版の遅さは `rep` の関数だけ)。実機 LAN の狙いは LGY-98 から内蔵 82557 へ | `docs/tasks/portability/ARM_GAUGE.md` §9・§10 |
| 2026-09-22 | 実機 LAN のホストは Ubuntu ノート (AF_PACKET の橋)、実機とはスイッチ直結 | `docs/tasks/realhw/TASK_LAN_82557.md` |
| 2026-09-23 | カーネル予算は「シュリンクではなく考え直す」。結線の土台の設計を先に起こす。DMA プール 0x2E8000 は暫定。往復の追加許可 (「あと 3」「解決まで」) | `docs/tasks/v3/TASK_HAL_WIRING.md` |
| 2026-09-24 | **v3 分岐前の区切りをタグ v2.1 にする**。**既存アプリは全部自作なので旧バイナリ互換より全再ビルド + 安い検出** (互換層は作らない) | ROADMAP §0、`docs/archive/kernel_v21/TASK_KAPI_DATA_FIELDS.md` |
| 2026-09-25 | MINIMAL を「起動・HDD への導入・回復」に絞る (フォントと一般コマンドは NORMAL へ)。インストール後の実機 HDD の e2fsck は行わない。レビュアーは Codex だけ | RELEASE_v2.1 §4、ROLES §0 |
| 2026-09-26 | 検査の 3 段 (コーダーの完了条件は `check-changed` rc=0、PM は着地で `check` を 1 回)。P3 だけの直しにレビューを回さない | ROLES §0 |
| 2026-09-29 | **v2.1 確定** — Ra266 の PEGC は画面を見ない条件で受け入れ (v3 を遅らせない)。**GUI の版は 1.4 で閉じる**。OS64 は版数表に載せない。資料不足は FreeBSD を参照。**fork の決定群**: 現行の開発は v3 / 別リポジトリ **os32-v3** (ブランチではない) / **パブリック** / os32 の v2.x は戻り先 (新機能は入れない) / submodule は引き継ぐが当面の保守は os32 側 / CI は作り直す / 文書の正典は os32-v3 / `tools/` と NP21/W フォークは共有だが動作保証は v3 だけ | ROADMAP §0-1、`docs/tasks/agents/HANDOVER_2026-09-29.md` §2 |
| 2026-09-30 | **メモリマップの方針** (3 者討論): 8MB 機は GUI + 私有 2MB のアプリ、バイナリ互換は捨てる (ソース互換は極力)、アプリ帯 0x80000000、`--cpl0` 廃止、SQLite はカーネルの機能だが同一リンクにしない、低位 640KB は V86 へ、OpenType / KCG 廃止の検討、同時使用はしない (快適の判定は前景 1 本)、池の運用規則 (口は `mem_map` / `mem_unmap` の 2 つ、閾値 64KB)、MEMSYS5 512KB、SQLite と FEP は常に読み込む (例外は MINIMAL)。D35: fork 時の KAPI 整理でスロット順を変えてよい。D36: Rust で作る部品も 386 下限を守る (最終成果物の検査を必須の工程に)。保留 5 件 (U6) の仕分け、Rust vs C11 (U5、IRQ 文脈の合成器は Rust 可)、T0 = C11 化。**fork の準備を承認** (J1〜J8 は推奨どおり: LICENSE は MIT のまま、THIRD_PARTY.md はルート、TDD 記録と設計記録は持つ、CI は静的ゲート + 本体ビルドから) | `docs/tasks/v3/TASK_MEMMAP_V3.md` §0・§11、`RUST_VS_C11.md`、`U6_PENDING_REVIEW.md`、`FORK_PLAN.md` |

---

## 5. 数字

git で数えられる範囲 (2026-09-30)。**集計の条件**: `git ls-tree -r` の追跡ファイルのうち、拡張子が C / ヘッダ / asm (`.c` `.h` `.asm` `.S`)、Rust (`.rs`)、Python (`.py`)、Markdown (`.md`) のものを、
Git blob の改行数 (`wc -l` と同じ) で数える。**除外は `THIRD_PARTY.md` の一覧が範囲**: SQLite (`lib/sqlite3/`)、zlib (`lib/zlib/`)、microtar (`lib/microtar/`)、microui (`userland/lib/ui/microui.{c,h}`)、
FatFs (`fs/fatfs/`)、`assets/` (IPADIC・IPAex・常用漢字表) と、gitignore のミラー `docs/hw/`。自作の LZ4 は含む。「全追跡数」は除外前の全ファイル、「集計対象」は上の拡張子で除外後のファイル数。
**ゲームと標準アプリのソースは v1.1.0 まで本流にあり、2026-09-02 `4740bea3` で本流から削除、2026-09-04 `2fa59440` で submodule (`apps/` `game/`) として再登録した**。v2.0 (09-03) は削除の後なので、それ以後の C の行数は本流だけの値。

| 時点 | コミット数 (累計) | 全追跡数 | 集計対象 | C / ヘッダ / asm | Rust | Python (道具・試験) | Markdown |
|---|---|---|---|---|---|---|---|
| 初期コミット `504e3b99` (04-07) | 1 | 228 | 208 | 26,286 | 0 | 4,568 | 2,247 |
| v1.0.0 (04-17) | 158 | 399 | 289 | 47,350 | 0 | 7,048 | 3,648 |
| v1.1.0 (09-01) | 501 | 838 | 619 | 103,459 (ゲーム・アプリを含む) | 2,679 | 16,335 | 23,584 |
| v2.0 (09-03) | 539 | 661 | 476 | 70,311 | 4,581 | 9,530 | 21,029 |
| v2.1 (09-29) | 1,740 | 1,541 | 1,287 | 204,439 | 53,397 | 66,638 | 77,262 |
| fork の準備の基点 `d995e078` (09-30) | **1,772** | **1,551** | 1,297 | 204,439 | 53,397 | 67,090 | 79,374 |
| 準備の着地 `8e00edec` (09-30) | 1,782 | 1,557 | 1,297 | 203,816 | 53,397 | 67,110 | 79,832 |

(Codex の事実確認 2026-09-30 の再計算 — microui と microtar を除かない条件 — は C 等が 104,981 / 71,833 / 206,489 / 205,866 で、差の 1,522 / 1,522 / 2,050 / 2,050 はその 2 部品の行数と一致する。)

- **月別のコミット**: 2026-04 = 373、06 = 4、08 = 119、09 = 1,286 (5 月と 7 月は本流に記録なし)。活動日は 60 日。
  多い日: 09-13 (161、GUI 1.3 の T9・S0〜S3)、09-06 (154、GUI 1.1 の並列 12 票)、09-23 (121、実機 2 日目・HAL_WIRING・PCM)。
- **タグ**: `v1.0.0` (04-17) → `v1.0.1` (08-07) → `v1.0.2` (08-08) → `v1.0.3` (08-11) → `v1.1.0` (09-01) → `v2.0` (09-03) → `v2.1` (09-29)。
- **KernelAPI**: v26 / 関数 123 本 (v1.0) → v39 / 168 本 (v1.1・v2.0) → v68 / 240 本、関数表の容量 300 (v2.1)。数は各タグの `kapi.json` の `api` 配列の実数 (データ欄は含まない。`CHANGELOG.md` の 118 / 172 は実体と合わない古い値)。
- **カーネル本体**: v1.0 の記録は 73KB (`docs/archive/ROADMAP_v1.0.md` の**バイナリ寸法**、旧記録からの引用値)。v2.1 は 583.0KB / 予算 596KB、残り 13.0KB (`docs/02_memory.md` §2-1 の **`.text` + `.data` + `.bss` のメモリ占有量**)。指標が違うので 2 つの数は直接比べられない。この残りが v3 の出発点 (§2)。
- **kselftest**: 実機 Ra266 で 225 / 225、NP21/W で 242 (機種で走らない項目がある)。
- **`make check`**: 94 本超のホスト試験 + 変異試験。`check-fast` 約 33〜40 秒、`check` 約 2〜3 分 (2026-09-17 の 15 分以上から)。
- **文書**: `docs/POLICY_DEBUG.md` §4 の教訓 62 項、archive へ移した票は 09-16 に 44 本 + 09-29 に 54 本。

---

## 6. os32 側の参照先 (v2.1 時点の記録)

この文書が要約したものの本文。os32-v3 には無いもの (archive の票、日ごとの引き継ぎ) がある。

| 情報 | os32 の場所 |
|---|---|
| 版数の対応と fork の段取り | `docs/ROADMAP.md` §0 |
| 各版の要約 / v2.1 のリリースノート | `CHANGELOG.md`、`docs/RELEASE_v2.1.md` |
| v1.0 到達までの経緯 | `docs/archive/ROADMAP_v1.0.md`、`docs/archive/REFACTORING_PLAN.md` |
| v2.0 (リング 3) の判断と完了記録 | `docs/archive/kernel_v2/PLAN.md` |
| GUI 1.1〜1.4 の設計・契約・票 | `docs/tasks/gui/DESIGN.md`、`API_CONTRACTS.md`、`docs/archive/gui_v11/`〜`gui_v14/` |
| 実機 Ra266 の票と判定 | `docs/archive/realhw_v21/` (CHECKLIST_2026-09-24/25/26、TASK_FDC_REALHW、TASK_SERIAL_VFAST、TASK_SERIAL_HOSTFS、TASK_HDD_INSTALL ほか)、`docs/tasks/realhw/` |
| 引き継ぎ (日ごとの記録) | `docs/tasks/agents/HANDOVER_2026-09-29.md`、`docs/archive/agents/HANDOVER_2026-09-{16,18,22}.md` |
| 体制の推移 | `docs/tasks/agents/ROLES.md` (§0 現行、末尾に経緯)、`docs/archive/agents/RETROSPECTIVE_2026-09-09.md` |
| 教訓 (障害の経緯と検証) | `docs/POLICY_DEBUG.md` §4 (§4-1〜§4-62)。普遍的なものの抜粋は [CASE_STUDIES.md](CASE_STUDIES.md) |
| v3 の草案・決定 | `docs/tasks/v3/` (V3_PLAN_DRAFT、TASK_MEMMAP_V3、RUST_VS_C11、U6_PENDING_REVIEW、TASK_C11_MIGRATION、FORK_PLAN) |
| VZ Editor 移植の残課題メモ | `tasks/vzeditor_status.md` (2026-04、マクロエンジンと大容量ファイルが未完のまま) は fork の準備 (FORK_PLAN §1-3) で削除した。エディタ本体は v1.0 から `edit` として同梱 |
