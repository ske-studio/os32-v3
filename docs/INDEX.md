# OS32 ドキュメント索引

PC-9801シリーズ向け 32ビット ベアメタルOS

---

## 情報単位ごとの正典 (更新先は 1 つ)

同じ事実を 2 か所で独立に更新する構造は必ず食い違う (2026-09-05 の診断で 6 件)。
**変わりやすい数値・手順・進捗は下表の正典だけを更新し、他の文書は要約と参照に留める。**

> **文書の正典はここ (このリポジトリ os32-v3、現行の開発 = v3)。** [os32](https://github.com/ske-studio/os32) (v2.x、タグ `v2.1`、`main`) は **v2.1 時点の記録・戻り先で、更新しない** — 新機能は入れず、手を入れるのは戻る必要が生じたときと致命的な不具合のときだけ (ユーザー決定 2026-09-29、正典は [ROADMAP.md §0-1](ROADMAP.md))。
> fork は 2026-09-30 (os32 v2.1 `6ccc4049` + feat/gui `dfa97f57` から、新しい履歴で。段取りと持ってこなかったものは [tasks/v3/FORK_PLAN.md](tasks/v3/FORK_PLAN.md) §1・§3)。経緯の要約は [HISTORY.md](HISTORY.md)、普遍的な教訓は [CASE_STUDIES.md](CASE_STUDIES.md)。

| 情報単位 | 正典 (ここだけ更新) | 参照側 (要約 + リンクのみ) |
|---|---|---|
| 制約規則 [C/HW/ABI/V/D] | [CONSTRAINTS.md](CONSTRAINTS.md) | CLAUDE.md (ID 参照、`make check` が照合)。Hermes 用の SOUL.md は 2026-09-29 に `SOUL.md` (非公開・リポジトリ外、`.gitignore`) へ (効力なし) |
| 引き継ぎ (次の PM への申し送り)・現在地と残件 | [tasks/agents/HANDOVER_2026-10-06.md](tasks/agents/HANDOVER_2026-10-06.md) (入口の 1 枚、50 行以内・上書き。進捗の本文は持たない) | 前回は [archive/agents/HANDOVER_2026-09-30.md](archive/agents/HANDOVER_2026-09-30.md) (fork 初日) |
| エージェント運用体制 (役割・起動・規約) | [tasks/agents/ROLES.md](tasks/agents/ROLES.md) (§0 が現行の 1 表。経緯は持たない) | CLAUDE.md (体制 1 段落 + リンク)。経緯は [archive/agents/ROLES_HISTORY_2026-10-06.md](archive/agents/ROLES_HISTORY_2026-10-06.md)、見直しの根拠は [tasks/agents/REVIEW_2026-10-06.md](tasks/agents/REVIEW_2026-10-06.md) |
| 延ばした試験・SKIP・ホストだけの合格・未結線 (持越し) | [tasks/DEFERRED_TESTS.md](tasks/DEFERRED_TESTS.md) (未完了だけを持つ。終わった行は消す) | 各票・引き継ぎは参照のみ。同じ延期を票や memory に写さない |
| 番地・帯域 | `include/memmap.h` (定義)、[02_memory.md §2-1](02_memory.md) (説明)、`build/out/MEMMAP.md` (実ビルドの生成地図、`make docs-gen`) | CLAUDE.md は入口のみ |
| KAPI の一覧・オフセット・版 | `sdk/kapi.json` → [KAPI_SPEC.md §4](KAPI_SPEC.md) | README.md / このファイル / KAPI_SPEC.md の版番号 (`tools/check_kapi_version.py` が照合。CLAUDE.md は版数を持たない) |
| KAPI 追加手順 | [KAPI_SPEC.md §3-1](KAPI_SPEC.md) | スキル `.claude/skills/os32-kapi-add` と CLAUDE.md (どちらもポインタのみ) |
| KAPI 版番号・エラー番号の予約 (未実装の先取り調停) | [KAPI_SPEC.md §3-2](KAPI_SPEC.md) | 各計画 (GUI TASK_K1、network LINK_PLAN) は参照 |
| エラーコード | `os32_kapi_shared.h` の `OS32_ERR_*` | 各 FS は境界で翻訳 |
| GUI 共有プロトコル (op / イベント / 構造体 / SHM 配置) | `sdk/include/os32/os32_gui_shared.h` (C が正典) → [tasks/gui/API_CONTRACTS.md](tasks/gui/API_CONTRACTS.md) (契約) | `sdk/rust/os32api/src/gui/proto.rs` は写し (`tools/check_gui_proto.py` = `make check-gui-proto` が照合)、[tasks/gui/PROTO_LAYOUT.md](tasks/gui/PROTO_LAYOUT.md) |
| ビルドターゲット・ツール・コンパイラフラグ | [08_build.md](08_build.md) (フラグの実体は `build/config.mk`) | CLAUDE.md「Build Commands」(日常分のみ) |
| 配備 3 経路の使い分け | [08_build.md §8-4](08_build.md#配備3経路) | [POLICY_DEV.md §4](POLICY_DEV.md) (表のみ)、CLAUDE.md (1 行)、スキル `os32-build-verify` |
| ディレクトリ木 | [08_build.md §8-3](08_build.md) | CLAUDE.md / INDEX は参照のみ |
| CI の本体ビルド (GitHub Actions の成果物・キャッシュ・取り方) | [08_build.md §8-6](08_build.md) (`.github/workflows/build.yml`、`tools/ci/build_cross.sh`、`tools/ci_fetch.sh`) | `check.yml` の冒頭コメント |
| ファイル → 役割 → 仕様の対応 | [DEVELOPMENT.md §2](DEVELOPMENT.md) | — |
| 第三者の部品とライセンス (版・改変の有無・原文の所在) | [../THIRD_PARTY.md](../THIRD_PARTY.md) (原文は各 vendor ディレクトリと `assets/*/README.OS32`) | README.md「ライセンス」(リンクのみ)、[../requirements.txt](../requirements.txt) (ホストの Python 依存) |
| 作業別の参照先 | [DEVELOPMENT.md §1](DEVELOPMENT.md) | — |
| 実行モデル (ローダ、ネスト、リング3、資源回収、exec_run の分割壁) | [09_exec.md](09_exec.md) | [10 §10-9](10_notes.md)、`archive/kernel_v2/` (設計経緯) |
| 描画方式 (ページフリップ、200 ライン) | [05_drivers.md §5-5](05_drivers.md) | CLAUDE.md は参照のみ |
| 落とし穴の経緯・検証記録 | [POLICY_DEBUG.md §4](POLICY_DEBUG.md) | 1 行の注意は同じ §4 の冒頭「領域別の早見」(2026-10-06 に CLAUDE.md から移した)。CLAUDE.md は入口の 1 行だけ |
| コーディング規約 (C11 (gnu11)、kstring、三層定数、asm) | [POLICY_DEV.md §2](POLICY_DEV.md) | CONSTRAINTS [C1]〜[C4] (規則行) |
| 進捗 | 各票の冒頭の状態行 (語彙は [POLICY_DEV.md §8](POLICY_DEV.md)、`make check-docs-status`) と、最新の引き継ぎの残件表 (上の「引き継ぎ」の行)。領域の中の進捗は領域別索引 ([tasks/fep/00_INDEX.md](tasks/fep/00_INDEX.md)、[archive/v21/v86v2/04](archive/v21/v86v2/04_implementation_status.md)) | [ROADMAP.md](ROADMAP.md) (計画)、[CHANGELOG.md](../CHANGELOG.md) (履歴)。[tasks/gui/TASKS.md](tasks/gui/TASKS.md) のゲートは v1.1 の記録 |
| プログラムの一覧 | 各層の `deploy.yaml` (機械可読の正典)、コマンドは [07_shell.md §7-1](07_shell.md) | 09_exec / INDEX に表を持たない |
| LAN の設計・進捗 | ドライバ = [tasks/network/PLAN.md](tasks/network/PLAN.md)、リンク層と Host Services = [tasks/network/LINK_PLAN.md](tasks/network/LINK_PLAN.md) | 05_drivers / DEVELOPMENT は要約 + リンク |
| 設定の置き場 (system.cfg の残すキー、settings.db のスキーマ / API / リカバリ) | [tasks/settings/DESIGN.md](tasks/settings/DESIGN.md) (計画、v1.3) | ROADMAP は 1 行 |
| アプリ帯の広さ・私有量 (v3) | [TASK_MEMMAP_V3](tasks/v3/TASK_MEMMAP_V3.md) D9・D11・§3-5 (決定)、[TASK_T2_APPBAND](tasks/v3/TASK_T2_APPBAND.md) と [T2d〜h](tasks/v3/TASK_T2D_T2H.md) (実装・受入) | 02_memory.md は地図の説明。旧 v2.1 の帯と未記録ゲスト受入は [APP_BAND_PDE](tasks/memory/APP_BAND_PDE.md) の履歴として区別 |
| 試験の一覧 (`make check` のターゲット、`_tdd.md` と票の対応) | `build/out/TESTS.md` / `build/out/HOST32.md` (生成表、`make docs-gen`)、[TESTS.md](TESTS.md) (手書きの読み方・判断) | [08_build.md](08_build.md) は手順と参照のみ |
| 移植性 (CPU / 機種の 2 軸、ARM 計測、順序 1〜4 の経過) | [tasks/portability/ARM_GAUGE.md](tasks/portability/ARM_GAUGE.md) (計測と経過)、[../arch/README.md](../arch/README.md) (足し方) | [archive/portability/SURVEY_N1.md](archive/portability/SURVEY_N1.md) (調査)、`archive/arch_port/` は**別リポジトリ `pw-sh4-research` の調査の快照** (正典はそちら。本リポジトリでは更新しない)、[archive/portability/TASK_KSTRING_BENCH.md](archive/portability/TASK_KSTRING_BENCH.md) (kstring の速度実測 **完了 2026-09-17** — x86 は asm 維持、C 版は他 32 ビットアーキ向け。数字は ARM_GAUGE §9、語長の前提は §10) |
| 版数 (カーネル 2.1 / GUI 1.4 で閉じた / 現行の開発 v3 / v4 草案) と v3 の fork の段取り | [ROADMAP.md §0](ROADMAP.md) | 各版の要約は [CHANGELOG.md](../CHANGELOG.md) (3〜5 行 + リリースノートへのリンク)、詳細は `RELEASE_vX.md` ([RELEASE_v2.1.md](RELEASE_v2.1.md))。`ver` の文字列、タグ |
| v3 の目的・範囲・目標の 2 段・柱 (P0〜P10) と順序・v3 後半の票 | [tasks/v3/V3_PLAN.md](tasks/v3/V3_PLAN.md) (本案、2026-09-30 昇格) | ROADMAP.md の v3 の行、README.md (目的の一文)。メモリマップの決定の本文は TASK_MEMMAP_V3 (D 番号) |
| v3 T2d〜T2h の詳細な実装契約・分割・受入 | [T2d〜T2h詳細](tasks/v3/TASK_T2D_T2H.md) (契約・分割・受入条件・未実施の手順だけ。実行記録は [archive/v3/TASK_T2D_T2H_RECORDS.md](archive/v3/TASK_T2D_T2H_RECORDS.md)、変異対応・分類別集計は [T2E_MUTANTS](tasks/v3/T2E_MUTANTS.md)、延期は [DEFERRED_TESTS](tasks/DEFERRED_TESTS.md)。決定はTASK_MEMMAP_V3とTASK_T2_APPBAND、a〜cの実績は後者§5-1) | TASK_T2_APPBAND §5-1は段の要約とリンク |
| NHD ext2のerrors印の原因調査 | [ext2調査票](archive/v3/TASK_EXT2_ERRORS_INVESTIGATION.md) (受入完了 2026-10-01、再インストールで印を解消・原因の発生時点は未確定。T2h受入前ゲートの照合先) | T2d〜h詳細 §0・§5・§8は順序とリンク |
| v3 T3以降の実装契約・分割・受入と後半接続 | [T3配置](tasks/v3/TASK_T3_LAYOUT.md)、[T4〜T6bモジュール/起動](tasks/v3/TASK_T4_T6_MODULES.md)、[T7/後半接続](tasks/v3/TASK_T7_AND_FOLLOWUPS.md) (設計中。D決定の本文はTASK_MEMMAP_V3) | V3_PLAN / TASK_MEMMAP_V3 はリンクのみ |
| 実機 Ra266 の画面ドライバ (内蔵 Trident 1023:9660) の設計・資料・段取り | [tasks/realhw/TASK_TRIDENT_DRIVER.md](tasks/realhw/TASK_TRIDENT_DRIVER.md) (設計中 — 設計票 v5 が Codex 5 回目で Approve、実装は未着手) | [tasks/realhw/PLAN.md](tasks/realhw/PLAN.md) §7、[ROADMAP.md](ROADMAP.md) (1 行) |
| 実機 Ra266 の PEGC 640x480 (実機 ROM の OUT 列に合わせたモード設定と受け入れ) | [tasks/realhw/TASK_PEGC480_REALHW.md](tasks/realhw/TASK_PEGC480_REALHW.md) | [RELEASE_v2.1.md](RELEASE_v2.1.md) §2-1、[POLICY_DEBUG.md §4-62](POLICY_DEBUG.md)、`archive/realhw_v21/TASK_PEGC_RA266_TIMING.md` (起票の完了記録) |
| 現行 / 未実装 / 過去 の区別 | 各文書の冒頭に「現行仕様」「計画」「YYYY-MM-DD 時点のスナップショット」を明記 | — |
| 票の状態行の語彙 | [POLICY_DEV.md §8](POLICY_DEV.md) の表 (`tools/check_docs_status.py` がそこから読む) | 下の「タスク」節の冒頭 (語の列挙のみ) |

T3 前の文書再整備の候補・未解決点は [DOCS_REORG_T3](tasks/v3/DOCS_REORG_T3.md) (草案)。正典の差替えやアーカイブ移動の承認を意味しない。

## 生成文書の見方

リポジトリのルートで `make all` → `make docs-gen` を実行する。
現在のビルド成果物を読み、次の 3 つを `build/out/` に書き出す。
`docs-gen` 自体はビルドしない。地図の入力が無い・古い場合は再ビルドを求めて停止する。
ツールチェーンの準備は [INSTALL.md](../INSTALL.md)。
試験表だけなら `make tests-inventory` (カーネルのビルド不要)。

| 出力 | 内容・説明の正典 |
|---|---|
| `build/out/TESTS.md` | Make の検査ターゲット・コマンド・対象ソース・記録・票・CI。読み方と判断は [TESTS.md](TESTS.md) |
| `build/out/HOST32.md` | runner ごとに実行する試験の一覧。運用は [08_build.md §8-4](08_build.md) |
| `build/out/MEMMAP.md` | 現在の `kernel.map` と `memmap.h` から求めた絶対番地・予算・余裕。説明は [02_memory.md §2-1](02_memory.md) |

生成物は既存の gitignore 対象ディレクトリに置き、コミットしない。
ブランチ間の競合と生成本文の鮮度による取り込み失敗を避け、必要な時点で再生成して読む。
生成前にも索引を読めるよう、出力パスはコード表記とする。
`check-tests-inventory` は登録・入力対応、`check-memmap` は配置・予算・写し・定数を検査する。
生成文書との一致はゲートに含めない。`kernel.map` 自体のビルド鮮度の確認は残す。
`make docs-win` は生成後、文書と 3 つの生成物を同じ相対配置で Windows 側へ写す。

## カーネル技術仕様書 (§1-§10)

| ファイル | 内容 |
|---------|------|
| [01_system.md](01_system.md) | **§1** システム概要 — アーキテクチャ、ブートシーケンス、レイヤー構造 |
| [02_memory.md](02_memory.md) | **§2** メモリマップ — 物理メモリ配置、DMA制約、ガードページ |
| [03_disk.md](03_disk.md) | **§3** ディスクレイアウト — FDD/HDD仕様、セクタ配置、INT 1Bh |
| [04_interrupts.md](04_interrupts.md) | **§4** 割り込みシステム — IDT/PIC/PIT |
| [05_drivers.md](05_drivers.md) | **§5** デバイスドライバ — KBD/Serial/FM/FDD/GFX/RTC/KCG/NP2SysP/libos32gfx |
| [06_filesystem.md](06_filesystem.md) | **§6** ファイルシステム — VFS/ext2/IDE/FDリダイレクト/パイプ |
| [07_shell.md](07_shell.md) | **§7** シェル — コマンド一覧、入力機能、スクリプトエンジン |
| [08_build.md](08_build.md) | **§8** ビルドシステム — パイプライン、ディレクトリ構造、デプロイツール、GitHub Actions |
| [09_exec.md](09_exec.md) | **§9** 外部プログラム実行 — OS32X/exec、ネスト実行、ステータスコード |
| [10_notes.md](10_notes.md) | **§10** 既知の制約と注意事項 |

## API・ガイド・ポリシー

| ファイル | 内容 |
|---------|------|
| [CONSTRAINTS.md](CONSTRAINTS.md) | **プロジェクト制約の正典** — C/ABI・ハードウェア・KernelAPI・検証・破壊的操作。CLAUDE.md は ここの規則行を ID で参照する (`make check` が照合) |
| [POLICY_DEV.md](POLICY_DEV.md) | **開発ポリシー** — コーディング規約、ビルド/デプロイ、Gitコミット、テスト、リリース |
| [POLICY_DEBUG.md](POLICY_DEBUG.md) | **デバッグポリシー** — 仮説駆動デバッグ、バイナリ反映確認、教訓集、AI協調ルール |
| [KAPI_SPEC.md](KAPI_SPEC.md) | KernelAPI v71 仕様書 — 304 エントリの表 (ヘッダ 2 + 関数表の容量 300 (実装 248) + データフィールド 2、データ欄は 0x4B8 に固定) + API追加手順 |
| [DEVELOPMENT.md](DEVELOPMENT.md) | **開発案内** — 作業別の参照先 (読む / 触る / 検証) と、ファイル → 役割 → 仕様のファイル地図。仕様本文は持たない |
| [ROADMAP.md](ROADMAP.md) | リリースロードマップ — **§0 版数の対応表と v3 への fork の段取り** (正典)、v1.x GUI の記録、v3 以降の長期項目 |
| [RELEASE_v2.1.md](RELEASE_v2.1.md) | **v2.1 のリリースノート** (2026-09-29、タグ `v2.1`、KAPI v68) — 実機 Ra266 で動くようになったもの、PEGC 640x480 の受け入れ条件、分かっている制限。版の対応は [ROADMAP.md §0](ROADMAP.md)、各版の要約は [CHANGELOG.md](../CHANGELOG.md) |
| [HISTORY.md](HISTORY.md) | **開発の経緯と推移 (完了記録、2026-09-30)** — 初期コミット (2026-04-07) から v1.0 / v1.1 / v2.0 / GUI 1.1〜1.4 / v2.1 / v3 の fork までの年表、版ごとの要約、体制の推移、主要なユーザー決定の年表、数字。os32-v3 へ持っていく写しの原本 |
| [CASE_STUDIES.md](CASE_STUDIES.md) | **ケーススタディ (現行、2026-09-30 の快照)** — 普遍的な教訓 10 件 (資料の食い違い、エミュレータと実機の差、PEGC 480、3 者討論、設計レビューの型、文書運用、変異試験、所有者タグ、配備の反映確認、ドラッグ枠の残像)。os32-v3 側で足していく |
| [archive/README.md](archive/README.md) | **アーカイブの運用** — 受入完了した票をどこへどう移すか (`tools/move_docs.py`)、移したあとも守ること。籠の一覧は「ログ」節 |
| [NHD_FORMAT.md](NHD_FORMAT.md) | NHD r0形式ファイル構造仕様 |
| [MGX_FORMAT.md](MGX_FORMAT.md) | MGX 漫画専用グレースケール画像形式 仕様 (48Bヘッダ + パレット表 + deflate、4bpp 16階調、ホスト側エンコード専用) |
| [BENCHMARK.md](BENCHMARK.md) | ベンチマークプログラム(bench.bin) の仕様とテスト内容 |

## プロジェクトルート

| ファイル | 内容 |
|---------|------|
| [LICENSE](../LICENSE) | MIT License (著作者: すけさん) |
| [README.md](../README.md) | プロジェクト概要・機能一覧・クイックスタート |
| [INSTALL.md](../INSTALL.md) | インストール・ビルド手順 |
| [CHANGELOG.md](../CHANGELOG.md) | リリース変更履歴 |

## ハードウェア技術資料 (外部リファレンス)

ハードウェアリファレンスはリポジトリ外の `C:\WATCOM\docs` (WSL: `/mnt/c/WATCOM/docs`) に配置されている:

| ドキュメント | 内容 |
|-------------|------|
| `C:\WATCOM\docs\undocumented\` | **非公開メモリ・I/Oポート資料集 (独自調査基盤、より正確)** |
| `C:\WATCOM\docs\PC9800Bible\` | PC-9800シリーズ テクニカルデータブック (公式資料ベース) |
| `docs/hw/` (git 管理外) | 上記と UNDOCUMENTED の Markdown をローカルにミラーしたもの。`tools/sync_hwdocs.sh` で更新。著作権物なので git に入れない |
| `C:\WATCOM\docs\D88_FORMAT_SPEC.md` | D88 フロッピーイメージ形式仕様 |
| `C:\WATCOM\docs\NP21W_DEBUG_PORT.md` | NP21/W デバッグポート資料 |

> **注意:** PC9800Bible と UNDOCUMENTED の記述が矛盾する場合は、UNDOCUMENTED の方を優先してください。

## ログ (歴史的記録)

| ドキュメント | 内容 |
|-------------|------|
| `docs/logs/PM_PIO_TEST.md` / `docs/logs/HDD_BIOS_DEBUG.md` (os32 リポジトリのみ) | 2026-04 の実証実験・デバッグ記録 (プロテクトモード IDE PIO、HDD ブートの INT 1Bh)。os32-v3 には持ってこなかった ([HISTORY.md](HISTORY.md) §1) |
| [archive/REFACTORING_PLAN.md](archive/REFACTORING_PLAN.md) / [archive/ROADMAP_v1.0.md](archive/ROADMAP_v1.0.md) | 初期のリファクタ計画 / v1.0 到達までのロードマップ |

### archive/ — 受入完了した票 (運用は [archive/README.md](archive/README.md))

領域ごとの籠。中身は下の「タスク」の各小節からも同じ票を指している (**索引からは消さない**)。

| 籠 | 何の票か | 完了 |
|---|---|---|
| [archive/gui_v11/](archive/gui_v11/TASK_H1_hal_backend.md) | GUI シェル v1.1 の票 12 本 (H1〜H3 / K1〜K4 / W1 W2 / C1〜C3)。索引は [tasks/gui/TASKS.md](tasks/gui/TASKS.md) | 2026-09-06 |
| [archive/gui_v12/](archive/gui_v12/TASK_K5_v12_proto.md) | GUI シェル v1.2 の票 5 本 (K5 / W3 W4 / C4 C5)。索引は [archive/gui_v12/TASKS.md](archive/gui_v12/TASKS.md) | 2026-09-07 (`d739494`) |
| [archive/gui_v13_reviews/](archive/gui_v13_reviews/REVIEW_T0_T1.md) | GUI シェル v1.3 のレビュー記録 5 本 + [限定クロスリンク](archive/gui_v13_reviews/CROSS_LINK_T4.md) + [途中保存](archive/gui_v13_reviews/VERIFICATION_PROGRESS.md)。票そのものは `tasks/gui/v13/` に残る | 2026-09-14 |
| [archive/settings/](archive/settings/TASK_S2.md) | 設定レジストリの票 5 本 (S2 / S3 / S3I2 / S4 / S5)。S0 / S6 / S6P と設計文書は `tasks/settings/` に残る。撤回した MEMORY_RAM_INTEGRATION (2026-09-30) もここ | 2026-09-13〜14 |
| [archive/network/](archive/network/TASK_N0.md) | Host Services の票 5 本 (N0〜N4)。計画 3 本は `tasks/network/` に残る | 2026-09-14〜15 |
| [archive/kernel_v2/](archive/kernel_v2/PLAN.md) | カーネル 2.0 の完了記録 8 本 (計画・M1〜M3・凍結契約・コーダー票 3 本)。タグ `v2.0` | 2026-09-03 |
| [archive/TEST_INVENTORY_2026-09-14.md](archive/TEST_INVENTORY_2026-09-14.md) | 試験の棚卸しの快照。正典は [TESTS.md](TESTS.md) へ移行 | 2026-09-14 |
| [archive/shell/](archive/shell/TASK_H1.md) | hsync H1〜H4・B8 (FS_TYPE)・シェルの切り詰め・終了コードの票 7 本 | 2026-09-15〜16 |
| [archive/test/](archive/test/TASK_TEST_RUNNER.md) | ゲスト試験ランナー 3 段の票 (FSTAT_REDIR / TEST_RESULT / TEST_RUNNER) | 2026-09-17 |
| [archive/tools/](archive/tools/TASK_KEY_INJECT.md) | キー注入・変異試験の並列化の票 | 2026-09-18〜26 |
| [archive/kernel_v21/](archive/kernel_v21/TASK_VFS_FD_PATH.md) | v2.1 までに直したカーネル層の不具合の票 6 本 | 2026-09-17〜24 |
| [archive/gui_v13/](archive/gui_v13/TASK_K5_multiapp.md) | GUI シェル v1.3 の票 15 本 + 監査。計画は [tasks/gui/v13/PLAN.md](tasks/gui/v13/PLAN.md) に残る | 2026-09-09〜14 |
| [archive/gui_v14/](archive/gui_v14/TASK_EDIT_GUI.md) | GUI 1.4 のエディタ GUI 版 | 2026-09-18 |
| [archive/realhw_v21/](archive/realhw_v21/TASK_FDC_REALHW.md) | v2.1 で実機 Ra266 に届いた票 5 本、PEGC_RA266_TIMING (完了記録)。実機の回の手順 3 本 (CHECKLIST_2026-09-24〜26、09-26 が v2.1 の判定) は os32 リポジトリの `docs/archive/realhw_v21/` のみ | 2026-09-22〜29 |
| [archive/portability/](archive/portability/TASK_KSTRING_BENCH.md) | kstring の速度実測 | 2026-09-17 |
| [archive/agents/](archive/agents/RETROSPECTIVE_2026-09-09.md) | 過去の引き継ぎ・体制の快照・`SOUL.md` (非公開・リポジトリ外) (Hermes の最上位プロンプト、撤収済み・効力なし) | 〜2026-09-29 |
| [archive/debug_kcg_load_font.md](archive/debug_kcg_load_font.md) | `kcg_load_font` クラッシュの仮説計画 (単発の障害記録) | — |

## タスク

領域ごとに、**計画 → 票 → 記録**の順。票の冒頭の `状態:` 行が正典 (語彙: 計画 / 設計中 / 実装中 / 受入待ち / 受入完了・実機確認待ち / 受入完了 / 撤回 / 完了記録 / 草案 / 現行。**語彙の正典と意味は [POLICY_DEV.md §8](POLICY_DEV.md)**、`make check-docs-status` が機械で見る)。
受入完了して参照頻度が下がった票は `docs/archive/<領域>/` へ落とすが、**索引の行は残す** (リンク先が archive になるだけ)。
移し方と運用は [archive/README.md](archive/README.md)。**v2.1 の時点の現在地と残件の一覧は最新の引き継ぎ** (正典表の「引き継ぎ」) にある。

### エージェント運用・引き継ぎ

| ドキュメント | 内容 |
|-------------|------|
| [tasks/agents/HANDOVER_2026-10-06.md](tasks/agents/HANDOVER_2026-10-06.md) | **引き継ぎ 2026-10-06 (現行)** — T2e の途中の現在地、次の一手、段をまたぐ道具と罠。入口だけ (50 行以内) |
| [tasks/agents/REVIEW_2026-10-06.md](tasks/agents/REVIEW_2026-10-06.md) | **開発体制の見直し 2026-10-06 — 完了記録** — 遅さの原因 (根拠つき)、決めたこと、残りの作業 a〜e |
| [tasks/DEFERRED_TESTS.md](tasks/DEFERRED_TESTS.md) | **持越し台帳** — 延ばした試験・SKIP・ホストだけの合格・未結線を 1 か所に |
| [archive/agents/HANDOVER_2026-09-30.md](archive/agents/HANDOVER_2026-09-30.md) | 引き継ぎ 2026-09-30 (os32-v3 の初日、履歴) |
| [archive/agents/ROLES_HISTORY_2026-10-06.md](archive/agents/ROLES_HISTORY_2026-10-06.md) | 体制の経緯 (2026-09-09〜10-06 の日付つき指示と、3 役へ畳む前の ROLES 本文) |
| [archive/agents/HANDOVER_2026-09-29.md](archive/agents/HANDOVER_2026-09-29.md) | **fork 時点の申し送り (2026-09-29、完了記録)** — v2.1 のタグの時点の現在地、v3 の fork の段取り (決定済み、2026-09-30 に実施)、残件表、道具、罠。表だけ。次の引き継ぎは os32-v3 で新しく起こす |
| [tasks/agents/ROLES.md](tasks/agents/ROLES.md) | **体制の正典** (現行) — §0 の 1 表が現行の体制 (3 役)、§1〜§5 が細則・起動・規約・合否・レビュー依頼 |
| [archive/agents/HANDOVER_2026-09-22.md](archive/agents/HANDOVER_2026-09-22.md) | 引き継ぎ 2026-09-22〜25 (完了記録) — 実機初日 (FD 起動・シリアル 115200・PCI 列挙・LAN の橋) から HDD 起動まで、日ごとの追記 |
| [archive/agents/HANDOVER_2026-09-18.md](archive/agents/HANDOVER_2026-09-18.md) / [HANDOVER_2026-09-16.md](archive/agents/HANDOVER_2026-09-16.md) | それ以前の引き継ぎ (完了記録) — 09-16 は残件 (H2 / H4 / arch 移設 / kstring 判断 / ゲスト試験ランナー / ARM / LAN 実機 / 小物) の推奨順・決裁点 |
| [archive/agents/HANDOVER_v14.md](archive/agents/HANDOVER_v14.md) | v1.4 の引き継ぎ — **撤回 (2026-09-14)**。アプリ層を別エージェントへ渡す案は取りやめ |
| [archive/agents/RETROSPECTIVE_2026-09-09.md](archive/agents/RETROSPECTIVE_2026-09-09.md) / `SOUL.md` (非公開・リポジトリ外) | 体制の快照 (2026-09-09) / Hermes の最上位プロンプト (撤収済み・効力なし) |

### 実機 PC-9821Ra266 (v2.1 で FD 起動・HDD インストール・HDD 起動まで到達)

| ドキュメント | 内容 |
|-------------|------|
| [tasks/realhw/PLAN.md](tasks/realhw/PLAN.md) | 実機で動かす計画 **実装中** — 到達点は [RELEASE_v2.1.md](RELEASE_v2.1.md) §1。残: 82557 の L-B、PCM の E6、Trident、PEGC の目視 |
| [tasks/realhw/TASK_PEGC480_REALHW.md](tasks/realhw/TASK_PEGC480_REALHW.md) | **Ra266 の PEGC 640x480 の正典** — **受入完了・実機確認待ち (2026-09-29)**。実機 ROM (INT 18h AH=30h) の OUT 列 (`v86 -g`) に値と順序を合わせ、画面を見ない条件で受け入れ。残るのは GUI の目視だけ |
| [tasks/realhw/TASK_TRIDENT_DRIVER.md](tasks/realhw/TASK_TRIDENT_DRIVER.md) | 内蔵 Trident (1023:9660) ドライバ **設計中** — 設計票 v5 が Codex 5 回目で Approve (2026-09-29)、実装は未着手。ユーザー決定「Cirrus はエミュレータ用、実機は Trident」 |
| [tasks/realhw/TASK_LAN_82557.md](tasks/realhw/TASK_LAN_82557.md) | 内蔵 LAN (Intel 82557) で Host Services — **実装中** (L-A PCI 列挙・L-D Linux の橋は着地、次は L-B) |
| [tasks/realhw/TASK_FD144.md](tasks/realhw/TASK_FD144.md) | 1.44MB FD からの起動 — **受入完了・実機確認待ち** (エミュレータで F1〜F13 合格) |
| [archive/realhw_v21/TASK_FDC_REALHW.md](archive/realhw_v21/TASK_FDC_REALHW.md) / [TASK_SERIAL_VFAST.md](archive/realhw_v21/TASK_SERIAL_VFAST.md) | 実機の FD 起動 (2026-09-22) / シリアル 115200 (2026-09-22〜23) — 受入完了 |
| [archive/realhw_v21/TASK_HDD_INSTALL.md](archive/realhw_v21/TASK_HDD_INSTALL.md) / [TASK_SERIAL_HOSTFS.md](archive/realhw_v21/TASK_SERIAL_HOSTFS.md) / [TASK_ATAPI_TIMEOUT.md](archive/realhw_v21/TASK_ATAPI_TIMEOUT.md) | CD から HDD へのインストールと HDD 起動 (2026-09-25) / SerialFS と `hsync --root` による実機の更新 (2026-09-29) / ATAPI の待ち上限を秒単位に (2026-09-26) — 受入完了 |
| `docs/archive/realhw_v21/CHECKLIST_2026-09-2{4,5,6}.md` (os32 リポジトリのみ) | 実機の回の手順と結果 (完了記録、日次の記録なので os32-v3 には持ってこなかった)。**09-26 の末尾が v2.1 の判定の記録** |
| [archive/realhw_v21/TASK_PEGC_RA266_TIMING.md](archive/realhw_v21/TASK_PEGC_RA266_TIMING.md) | v2.1 の PEGC 実機課題の起票 (完了記録)。正典は上の TASK_PEGC480_REALHW |

### カーネル層 (memory・VFS・KAPI・SQLite)

カーネル層に分かっている不具合があるあいだは新機能より先 ([POLICY_DEV.md §1](POLICY_DEV.md))。v2.1 までに直したものは [archive/kernel_v21/](archive/kernel_v21/TASK_VFS_FD_PATH.md) へ。

| ドキュメント | 内容 |
|-------------|------|
| [tasks/memory/APP_BAND_PDE.md](tasks/memory/APP_BAND_PDE.md) | アプリ帯の可変 PDE 化 **受入待ち** — 実装 `b8dab24` は v2.1 に入り kselftest で毎起動検証、§5 のゲスト受入 (8MB / 15MB、`heap_test`、`ring3_guard` 否定試験) は未記録 |
| [archive/kernel_v21/TASK_KSTACK_USER.md](archive/kernel_v21/TASK_KSTACK_USER.md) | SHM 帯がカーネルスタックに食い込んでいた (スタックは設計の 16KB ではなく 4KB だった) — 受入完了 (2026-09-17) |
| [archive/kernel_v21/TASK_DB_ERRSTR.md](archive/kernel_v21/TASK_DB_ERRSTR.md) | `db_last_error()` がカーネル番地を返していた (CPL=3 のアプリが #PF) — 受入完了 (2026-09-17) |
| [archive/kernel_v21/TASK_EXT2_EMPTY_NAME.md](archive/kernel_v21/TASK_EXT2_EMPTY_NAME.md) / [TASK_VFS_FD_PATH.md](archive/kernel_v21/TASK_VFS_FD_PATH.md) | NHD のルートの名前の無いディレクトリ項目 / FD のパスの引き直しと長いパスの切り詰め (VFS の既存欠陥) — 受入完了 (2026-09-24) |
| [archive/kernel_v21/TASK_KAPI_DATA_FIELDS.md](archive/kernel_v21/TASK_KAPI_DATA_FIELDS.md) / [TASK_KAPI_OUTPUT_GUARD.md](archive/kernel_v21/TASK_KAPI_OUTPUT_GUARD.md) | KAPI のデータ欄の固定 (v63) / 出力ポインタを受ける KAPI 43 本が RO ページに書けた件 — 受入完了 (2026-09-23〜24) |

### v3 (現行の開発 — 本案は [tasks/v3/V3_PLAN.md](tasks/v3/V3_PLAN.md))

版の線と fork の段取りは [ROADMAP.md §0](ROADMAP.md)。下の票のうち HAL_WIRING・PCM・KHEAP の切り直し・デバイス窓の帯は v2.1 に先行して着地した (残件があるので v3/ に残す)。

| ドキュメント | 内容 |
|-------------|------|
| [archive/v3/V3_PLAN_DRAFT.md](archive/v3/V3_PLAN_DRAFT.md) | **v3 本案の草案 — 草案群のまとめ (2026-09-30、完了記録: 本案 V3_PLAN.md に昇格)** — 草案の一覧と振り分け (§1)、目的と範囲・**目標の 2 段** (§2、§2-1)、柱と順序の案 (§3)、**メモリマップの柱は決定済み** (§3-1 は要点と決着先、正典は TASK_MEMMAP_V3)、食い違いの一覧 (§4)、v2.x との互換 (§5)、fork の段取り (§6)、ユーザー判断と Codex の論点 (§7)。**§7-2 X1〜X8 は 2026-09-30 に Codex が回答しユーザーが承認 (結論と反映先は表)**。本案への昇格はユーザー判断と Codex の突き合わせの後 |
| [tasks/v3/FORK_PLAN.md](tasks/v3/FORK_PLAN.md) | **os32-v3 への fork の段取り — 計画 (2026-09-30、ユーザー承認「準備を承認」)** — 持っていくものの一覧 (`git ls-files` 1,551 ファイルをディレクトリ単位で 持つ / 持たない / 要判断、§1)、経緯の要約とケーススタディの候補 (HISTORY / CASE_STUDIES / THIRD_PARTY、§1-4)、**公開前の監査 (秘密・著作権物・第三者ライセンス・実パス・ホスト名、コマンドと合格条件、2026-09-30 の結果、§2)**、手順 a〜g と [D2] の地点 (§3)、判断が要る点 J1〜J8 (§4)。リポジトリの作成・push は未実施 |
| [archive/v3/PLAN.md](archive/v3/PLAN.md) | **v3 の計画 (2026-09-17)** — 機能を足す前に入れ物を作り直す (C11 → メモリマップ再配置 → ドライバの動的読み込み → PCI → 82557)。カーネル本体の大きさと残りは `build/out/MEMMAP.md` ([生成手順](#生成文書の見方))。アプリへの払い出し §4、アイデア §5。**完了記録 (2026-09-30): 策定時の計画。本案は V3_PLAN.md** |
| [tasks/v3/TASK_MEMMAP_V3.md](tasks/v3/TASK_MEMMAP_V3.md) | **v3 のメモリマップ — 設計中 (方針確定 2026-09-30、3 者討論で決定、Codex Approve)**。決定 D1〜D35 (D29〜D34 = 保留 5 件 U6 の拾い方、**D35 = D7 の改訂: fork 時の KAPI 整理でスロット順を変えてよい、世代の識別と旧新混在試験が条件**、2026-09-30; システムは恒等のまま、アプリだけ 0x80000000〜、物理台帳、SQLite のモジュール化、低位 640KB を V86 へ、OpenType)、帯の表、票 T0〜T7、受入条件、**Codex X1〜X8 の補足の対応表 (§8-4)**、経緯。実装は未着手 |
| [archive/v3/U6_PENDING_REVIEW.md](archive/v3/U6_PENDING_REVIEW.md) | **U6 の仕分け表 — 決裁済み (2026-09-30、ユーザーが 6 点すべて推奨どおりに決定 → TASK_MEMMAP_V3 D29〜D34)**: 保留 5 件 (F3a〜c / F2c / FEP_BOUNDARY / MEMORY_RAM_INTEGRATION / DEVICE_RESERVATION) の現状 (コードで確認)、TASK_MEMMAP_V3 の決定で置き換わった部分と残る部分、拾う先 (T2 / T4 / T5a の要件、P4、撤回)、判断点 6 つとその決定 |
| [archive/v3/TASK_T1_LEDGER.md](archive/v3/TASK_T1_LEDGER.md) | **T1: 物理地図と所有権台帳 — 受入完了 (2026-10-01、T1a〜T1f 着地、残件は状態行)** — TASK_MEMMAP_V3 §6 T1 の範囲と T2 以降との境界 (§1)、物理メモリの確保・予約・写像の現状一覧と P2V / V2P の監査見積り 161 件 (§2)、台帳の 4 層 (地図・割当可否・owner・区間の表)、owner の種別 (AS / 永続 / モジュール / device)、検証済み資源レコード (X4、span ごとの資源と `res_mask`)、SURFACE の型 (Cirrus の CLIENT / DISPLAY を含む)、API、起動順 (8MB 型の backing と exec 上端、PCI 採取値の取り込み)、P2V / V2P / `P2V_CONST` と `check_p2v.py`、R1 の計数 (観測用の深さと broker の判定を分ける、jmpbuf の 3 保存先)、`dma_alloc`、gfx の識別 → 予約 → 写像 (候補群 1 owner) (§3)、段 T1a (モデル経路) 〜 T1f と受入・8MB / 17MB / 64MB の確かめ方・段の境目の owner 表 (§4)、リスクと未確認 (§5)、Codex の論点 X9〜X16 と 1 回目 B1〜B7 / 2 回目 B8〜B11 の対応表 (§6) |
| [archive/v3/TASK_T2D_RESULTS.md](archive/v3/TASK_T2D_RESULTS.md) | **T2d (B1 checked copy) d0a〜d6 の実装結果と受入 — 完了記録 (2026-10-02)** — [TASK_T2D_T2H](tasks/v3/TASK_T2D_T2H.md) の §10-2〜§10-16 をそのまま移したもの (節番号は元のまま)。各段のコーダーの実装結果・レビュー対応・PM のゲスト受入、d6 の変異の仕分けと size 確定 |
| [archive/v3/TASK_T2D_T2H_RECORDS.md](archive/v3/TASK_T2D_T2H_RECORDS.md) | **T2e〜T2h の実行記録 — 完了記録 (2026-10-06)** — [TASK_T2D_T2H](tasks/v3/TASK_T2D_T2H.md) から切り離した e1〜e10a・kapinull・f1a〜f4・h2・h3・STOP 修正・検査の整理 (ci-stab / ci-select) の実装結果と受入。見出しは元の行番号。未実施は [DEFERRED_TESTS](tasks/DEFERRED_TESTS.md) が正 |
| [archive/v3/TASK_T2ABC_RESULTS.md](archive/v3/TASK_T2ABC_RESULTS.md) | 完了記録 — T2 親票 §5-1 の T2a〜T2c 実装・受入結果 (未実施・残件を含む)。設計・受入条件は TASK_T2_APPBAND に残す |
| [tasks/v3/TASK_T2_APPBAND.md](tasks/v3/TASK_T2_APPBAND.md) | **T2: アプリ帯 + lease 窓 — 設計中 (2026-10-01)**。SURFACE lease 契約、現状調査、高位 AS・可変スタック・R1/R2/R3-e・B1・D35、T2a〜T2h の受入、サイズ予算と独立レビュー論点 |
| [archive/v3/TASK_CLANG_CHECKS.md](archive/v3/TASK_CLANG_CHECKS.md) | C ソースの静的検査 5 本 (check-p2v・check-c-dialect・check-le-access・check-arch-asm・audit_cast_align) を clang の構文木で作り直した。旧版 (`tools/legacy_checks/`) は撤去 (ユーザー決定 2026-10-01、リポジトリ外にバックアップ) **着地 (2026-10-01、`8612b06`)** |
| [tasks/v3/PORT_CANDIDATES.md](tasks/v3/PORT_CANDIDATES.md) | **既存ソフトウェアの移植候補 — 計画 (2026-09-30、v3 後半・P9)** — 移植の前提となる基盤 (libc の穴・C++ 無し・x87・Video HAL・PCM・協調型・ライセンスの置き場)、候補 23 本の表 (ライブラリ / テキスト系 / 8bpp ゲーム / メディア / ワープロ・表計算)、**ZSNES の難度と障害**、推奨の挑戦順 (zlib → Lua → … → doomgeneric → Wolf4SDL → ZSNES)、未確認事項 |
| [tasks/v3/RUST_VS_C11.md](tasks/v3/RUST_VS_C11.md) | **Rust にする部品と C11 に揃える部品 — 設計中・方針確定 (2026-09-30、U5 の調査票、決定は TASK_MEMMAP_V3 D36)** — C11 化 (T0) の範囲とコスト (小)、Rust の今の使われ方と道具 (nightly + build-std、`i686-os32-none.json` の既定 CPU は `generic`、os32api、`os32_lz4` のカーネルリンク実績、ビルド時間)、IRQ 文脈の制約、候補 24 件の比較表 (新規 / 既存 / 既に Rust)、推奨 (合成器と OpenType は Rust、カーネル核・FS・FEP・SDK・デコーダは C11)、混在の約束 (FFI・所有権・panic・x87・target JSON)、相対コスト、ユーザー決定 7 点 + (a)、**386 下限の守り方 (§7: `cpu=i386` でも LLVM は `bswap` / `cmpxchg` / `xadd` を出す — 回避実装 + 最終成果物の検査を必須に)**、Codex 事実確認の対応表 (§8) |
| [archive/v3/TASK_C11_MIGRATION.md](archive/v3/TASK_C11_MIGRATION.md) | **T0: C89 (gnu89) → C11 (gnu11) への移行 — 受入完了 (2026-09-30、main `f5bcb35`)** — 範囲は「言語モードと検査の移行」に限定 (本体・ブート・userland・SDK 実装は gnu11、公開 SDK ヘッダは C89 互換、SQLite 系は gnu89 の専用規則、全面書き換えなし)、前提文書の訂正 (§2: `kernel.mk:175` は SQLite 本体でない、`CFLAGS_SQLITE` の継承、`shm.c` は修正済み、`check_constraints.py` は言語検査をしていない)、作業の分類表 (§3)、C11 機能の採用範囲 (§4)、gnu89 → gnu11 の意味の差 (§5)、実施順序 1〜7 と検査 (§6)、受入 (§7)、ユーザー判断 3 点 (§8) は推奨どおり承認・[C1] は §9 の文面で改訂済み、`make check-c-dialect` を新設、A8 は Codex Approve・A9 は NP21/W で合格 |
| [tasks/v3/TASK_HAL_WIRING.md](tasks/v3/TASK_HAL_WIRING.md) | 結線の土台 (割り込みの動的登録 / 8237 / DMA プール / PCI の結線表 / µs 時計) **受入完了・実機確認待ち** (残: W7) |
| [tasks/v3/TASK_PCM_CS4231.md](tasks/v3/TASK_PCM_CS4231.md) | CS4231 (MATE-X PCM) の PCM 再生ドライバ **受入待ち** (E0〜E3 合格、残: E4・E5 は NP21/W、E6 は実機)。ini の SNDboard は [D2] |
| [tasks/settings/DEVICE_RESERVATION.md](tasks/settings/DEVICE_RESERVATION.md) / [FEP_BOUNDARY.md](tasks/settings/FEP_BOUNDARY.md) / [F2_OWNERSHIP.md](tasks/settings/F2_OWNERSHIP.md) | v3 の入力になる設計提案 (計画 / 設計中 / 設計中)。**U6 決定 (2026-09-30) を状態行に注記**: DEVICE_RESERVATION は改訂して P4 (核は T1 の台帳)、FEP_BOUNDARY と F2 の残りは P1 の T2 / T4 / T5a の要件。MEMORY_RAM_INTEGRATION は撤回して [archive/settings/](archive/settings/MEMORY_RAM_INTEGRATION.md) へ |
| [DESIGN_APP_FIRST.md](DESIGN_APP_FIRST.md) | **アプリケーション優先設計の草案** — 前景 1 アプリへ資源を集中する設計思想。640×480×16bit を高機能グラフィックスの境界とし、Video HAL / VESA2 的互換層 / SDL 等の判断基準を整理。ロードマップではない |
| [AUXILIARY_CORE_SERVICE.md](AUXILIARY_CORE_SERVICE.md) / [LEGACY_LIVING_PRESERVATION.md](LEGACY_LIVING_PRESERVATION.md) | 草案 (2026-09-28) — 余剰コアを固定機能アクセラレータに / レガシー実機の動態保存と OS32・OS64・Host Service の役割分離。ACS は v3 の範囲外。**OS64 は OS32 とは別の 64 ビット OS の構想** (v3 / v4 の範囲外、版数表に載せない。定義は LEGACY の冒頭、2026-09-30) |
| [tasks/v3/V3_PLAN.md](tasks/v3/V3_PLAN.md) | **v3 本案 — 実装中 (2026-09-30)** — 目的の一文 (§1)、範囲と v3 / v4 の外 (§2-1)、**目標の 2 段の正典** (§2-2)、柱 P0〜P10 と正典 (§3)、順序 (§4)、v3 後半の票 (§5)、持ち越した宿題 (§6)、決定と経緯の在りか (§7)。T0 受入完了、次は T1 |

### シェル・配備 (hsync)

| ドキュメント | 内容 |
|-------------|------|
| [tasks/shell/HSYNC_IMPROVEMENT_PLAN.md](tasks/shell/HSYNC_IMPROVEMENT_PLAN.md) | hsync 改善案 (ユーザー起草、2026-09-14) — **H1〜H4 すべて受入完了 (2026-09-16)** |
| [archive/shell/TASK_H1.md](archive/shell/TASK_H1.md) | H1 **受入完了 (2026-09-15)** — 同サイズ内容比較、ストリーム CRC + 読戻し検証、dry-run、理由表示、HostDrv stat の是正。Codex 往復 5 の記録 |
| [archive/shell/TASK_H3.md](archive/shell/TASK_H3.md) | H3 **受入完了 (2026-09-15)** — HostDrv の FILETIME→mtime、`sys_set_mtime` (KAPI v52)、日時を前置フィルタに (決裁)。`hsync sys` 25.8 s → 0.26 s |
| [archive/shell/TASK_H2.md](archive/shell/TASK_H2.md) | H2 **受入完了 (2026-09-16)** — hsync の置換安全化。`O_EXCL` (KAPI v53)、ext2 のファイル置き換えを宛先エントリの inode 書き換えに、一時ファイル `.hs~` → 検証 → rename。決裁 D1〜D3 |
| [archive/shell/TASK_H4.md](archive/shell/TASK_H4.md) | H4 **受入完了 (2026-09-16)** — 配備マニフェストと世代の確認。古い配備元で新しい成果物を上書きする事故を検出する。`--expect-build` と行指向の名札 |
| [archive/shell/TASK_FS_TYPE.md](archive/shell/TASK_FS_TYPE.md) | B8 **受入完了 (2026-09-15、6 往復)** — 読み取り失敗を不存在・未割当・別の型と読み替えていた ext2/VFS/HostDrv の経路。remount-ro 相当、e2fsck を正解に。残る制限は §2-6 / §2-7 |
| [archive/shell/TASK_SH_TRUNCATION.md](archive/shell/TASK_SH_TRUNCATION.md) | シェルの入力切り詰め **受入完了 (2026-09-16)** — 切り詰めたまま実行を続ける 26 経路。`if` の比較が 255 文字で切れて条件が逆転し破壊的なコマンドが走る欠陥を含む |
| [archive/shell/TASK_EXIT_STATUS.md](archive/shell/TASK_EXIT_STATUS.md) | 終了コードの配線と `$?` **受入完了 (2026-09-16)** — ゲスト試験ランナーの 1 段目 (KAPI v55、`exec_last_result`)。決裁 E1 / E2 |
| [tasks/shell/INHERITED_BUGS.md](tasks/shell/INHERITED_BUGS.md) | 継承バグ台帳 (T9 で起こした、常駐シェルと sh.bin の共通) |
| [tasks/hotdeploy/DESIGN.md](tasks/hotdeploy/DESIGN.md) | ホットデプロイ (再起動なしの配備) の設計 |

### 試験と道具

| ドキュメント | 内容 |
|-------------|------|
| [archive/test/TASK_FSTAT_REDIR.md](archive/test/TASK_FSTAT_REDIR.md) | `fstat` がリダイレクトを見ない **受入完了 (2026-09-17)** — fd 0/1/2 を無条件でキャラクタデバイスと答えるので `isatty` と食い違う |
| [archive/test/TASK_TEST_RESULT.md](archive/test/TASK_TEST_RESULT.md) | 合否を機械が読める形にする **受入完了 (2026-09-17)** — ランナー 2 段目。終了コード 0/1/2 と集計行 `<名前>: PASS n/m`、結果チャネルは HostDrv (決裁 R1)。追補 §11 |
| [archive/test/TASK_TEST_RUNNER.md](archive/test/TASK_TEST_RUNNER.md) | ゲストで一括実行してホストで集計する **受入完了 (2026-09-17)** — ランナー 3 段目。`make check-guest` として `make check` とは別 (R2〜R4 未実施は本文に記載) |
| [archive/tools/TASK_KEY_INJECT.md](archive/tools/TASK_KEY_INJECT.md) | キー注入で任意のバイトを送る **受入完了 (2026-09-18)** — `/api/key` の `text=ABC` が `abc` になっていた (検証の側の穴) |
| [archive/tools/TASK_CHECK_MUT_PARALLEL.md](archive/tools/TASK_CHECK_MUT_PARALLEL.md) | `make check` の変異試験を写しの木で並列に **受入完了 (2026-09-26)** — check-mut の段と `-j1` を廃止。検査の 3 段は [08_build.md §8-4](08_build.md) |

### 設定レジストリ (settings、v1.3 で完了)

完了した票 (S0 / S2 / S3 / S3I2 / S4 / S5 / S6 / S6P) と S0 の計画・基盤設計は [archive/settings/](archive/settings/TASK_S2.md) へ。v3 の入力になる設計提案 3 本は上の「v3」節 (MEMORY_RAM_INTEGRATION は 2026-09-30 に撤回して下の表へ)。

| ドキュメント | 内容 |
|-------------|------|
| [tasks/settings/DESIGN.md](tasks/settings/DESIGN.md) | **設定レジストリ**の設計 — `system.cfg` (起動キー) + `/etc/settings.db` (SQLite)。置き場の正典 |
| [archive/settings/S0_PLAN_2026-09-13.md](archive/settings/S0_PLAN_2026-09-13.md) | 着手計画 (PM 縮約案、2026-09-13 決裁) |
| [archive/settings/S0_FOUNDATION.md](archive/settings/S0_FOUNDATION.md) | S0 の基盤設計 (配備の保護、所有権) |
| [archive/settings/MEMORY_RAM_INTEGRATION.md](archive/settings/MEMORY_RAM_INTEGRATION.md) | RAM 統合 Phase 2 — **撤回 (2026-09-30、U6 決定)**。A / B は K6 で着地済み、残りは TASK_MEMMAP_V3 D3 で撤去側。残る 2 点は T1 の受入へ (D32) |
| [archive/settings/TASK_S0.md](archive/settings/TASK_S0.md) / [TASK_S2.md](archive/settings/TASK_S2.md) / [TASK_S3.md](archive/settings/TASK_S3.md) / [TASK_S3I2.md](archive/settings/TASK_S3I2.md) / [TASK_S4.md](archive/settings/TASK_S4.md) / [TASK_S5.md](archive/settings/TASK_S5.md) / [TASK_S6.md](archive/settings/TASK_S6.md) / [TASK_S6P.md](archive/settings/TASK_S6P.md) | 票 S0〜S6P (いずれも受入完了、2026-09-13〜14) — KAPI v50 db_*、libos32cfg と `cfg`、install の回復、gshell の消費者、実測、tar |

### ネットワーク・Host Services (v1.4)

完了した票 (N0〜N4) は [archive/network/](archive/network/TASK_N0.md) へ。計画 3 本はここに残る。

| ドキュメント | 内容 |
|-------------|------|
| [tasks/network/PLAN.md](tasks/network/PLAN.md) | LGY-98 / NE2000 **ドライバ**計画 — M1〜M3 エミュレータ合格、既定で有効 (2026-09-14 決裁) |
| [tasks/network/LINK_PLAN.md](tasks/network/LINK_PLAN.md) | リンクプロトコル (ワイヤ v2) / Host Services 計画 |
| [tasks/network/HOST_SERVICES_PLAN.md](tasks/network/HOST_SERVICES_PLAN.md) | Host Services 詳細計画 — N1〜N4 受入完了。N5 (実機 LAN) は実機の内蔵 82557 の票 ([TASK_LAN_82557](tasks/realhw/TASK_LAN_82557.md) の L-B) に吸収。LGY-98 はエミュレータで回帰を取る経路として残る |
| [archive/network/TASK_N0.md](archive/network/TASK_N0.md) / [TASK_N1.md](archive/network/TASK_N1.md) / [TASK_N2.md](archive/network/TASK_N2.md) / [TASK_N3.md](archive/network/TASK_N3.md) / [TASK_N4.md](archive/network/TASK_N4.md) | 票 N0〜N4 (受入完了、2026-09-14〜15) — 設計 v5、KAPI v51 + libos32host、PRINT/CLIP、wget/lpr、ファイラ印刷と端末の貼り付け |

### 移植性

| ドキュメント | 内容 |
|-------------|------|
| [tasks/portability/ARM_GAUGE.md](tasks/portability/ARM_GAUGE.md) | **ARM コンパイル計測の基準値と経過** — `make check-arm-compile` (計測、合否ではない)。順序 1〜4 の前後表 (§9)。2026-09-15 時点 55/93 |
| [archive/portability/SURVEY_N1.md](archive/portability/SURVEY_N1.md) | 移植性調査 (N1 起点) — 直列化とアライメント、`cli`/`sti`/`hlt` の一覧、順序 2 / 4-a の実施記録 |
| [archive/portability/TASK_KSTRING_BENCH.md](archive/portability/TASK_KSTRING_BENCH.md) | kstring の速度実測 **受入完了 (2026-09-17)** — x86 は asm 維持、C 版は他 32 ビットアーキ向け |
| [../arch/README.md](../arch/README.md) | **`arch/` と `platform/` の正典** — 移植の 2 軸 (CPU / 機種)、`ARCH` `PLATFORM` の選び方、新アーキテクチャの足し方 |
| [archive/arch_port/00_INDEX.md](archive/arch_port/00_INDEX.md) | 他アーキテクチャ移植調査の索引 — **別リポジトリ `pw-sh4-research` で進む調査の快照 (2026-09-08〜09、本リポジトリでは更新しない)**。M0 監査 (`tools/audit_cast_align.sh`)、SHARP Brain (i.MX28) のハード調査 |

### GUI シェル (v1.1〜v1.4、GUI の版は 1.4 で閉じた)

完了した票は [archive/gui_v11/](archive/gui_v11/TASK_H1_hal_backend.md) (v1.1 の 12 本) /
[archive/gui_v12/](archive/gui_v12/TASK_K5_v12_proto.md) (v1.2 の 5 本) /
[archive/gui_v13/](archive/gui_v13/TASK_K5_multiapp.md) (v1.3 の 15 本と監査) /
[archive/gui_v13_reviews/](archive/gui_v13_reviews/REVIEW_T0_T1.md) (v1.3 のレビュー記録) /
[archive/gui_v14/](archive/gui_v14/TASK_EDIT_GUI.md) (v1.4 のエディタ) へ。
設計・契約・索引 (`DESIGN.md` `API_CONTRACTS.md` `TASKS.md` `PROTO_LAYOUT.md` と v1.3 の `PLAN.md`) はここに残る。

| ドキュメント | 内容 |
|-------------|------|
| [tasks/gui/DESIGN.md](tasks/gui/DESIGN.md) | **GUI シェル v1.x 設計記録** (2026-09-04) — 再描画モデル、HAL/バックエンド表 |
| [tasks/gui/API_CONTRACTS.md](tasks/gui/API_CONTRACTS.md) | libos32gui 凍結インターフェース契約 (2026-09-04 凍結) |
| [tasks/gui/TASKS.md](tasks/gui/TASKS.md) | v1.1 作業分担票とゲート表 — v1.1 の票 (`archive/gui_v11/TASK_*.md` 12 本) の索引 |
| `archive/gui_v12/` | v1.2 の票 5 本 (受入完了 2026-09-07 `d739494`)。索引は [archive/gui_v12/TASKS.md](archive/gui_v12/TASKS.md) |
| [tasks/gui/v13/PLAN.md](tasks/gui/v13/PLAN.md) | **v1.3 計画と票の索引** (受入完了 2026-09-14) — K5b / K6 / K7 / T7〜T9、監査 (`AUDIT_2026-09-10.md`)、レビュー記録 (`archive/gui_v13_reviews/REVIEW_*.md`、完了記録) |
| [archive/gui_v13/TASK_K6C_A_terminal.md](archive/gui_v13/TASK_K6C_A_terminal.md) / [TASK_T7_terminal_cmd.md](archive/gui_v13/TASK_T7_terminal_cmd.md) / [REVIEW_T5A_APP.md](archive/gui_v13_reviews/REVIEW_T5A_APP.md) | v1.3 の票のうち `PLAN.md` から直接辿れない 3 本 (端末アプリ、端末からの CUI 起動、T5a アプリのレビュー記録) |
| [tasks/gui/TASK_KBD_NAV.md](tasks/gui/TASK_KBD_NAV.md) | マウスなしで GUI を操作する (WM のショートカット + マウスキー、Windows 98 の割り当て) **受入完了・実機確認待ち** (K1・K2 合格、残: K3 実機) |
| [tasks/gui/TASK_CONTROL_PANEL.md](tasks/gui/TASK_CONTROL_PANEL.md) | コントロールパネル (設定を GUI で変えるアプリ、基準は Windows 98) **計画 (2026-09-30)** — 範囲・候補項目・設計で決めること (Q1〜Q8) だけ。設計票の作成は v3 後半 (T4・T5a と P8 の後) |
| [tasks/gui/TASK_I18N.md](tasks/gui/TASK_I18N.md) | UI メッセージの多言語対応 (SQLite のメッセージ表、既定は英語、区分 + 短い名前をビルドで番号に畳む、PO 形式と POSIX `%n$` に乗る) **計画 (2026-09-30)** — 設計票は v3 後半 (T4・コントロールパネルの後)、他の文字体系は T7b の後 |
| [archive/gui_v14/TASK_EDIT_GUI.md](archive/gui_v14/TASK_EDIT_GUI.md) | テキストエディタの GUI 版 **受入完了 (2026-09-18)** — v1.4 の最後の受入試験。API の退行検出を兼ねる (複数行の編集部品) |
| [../tools/tests/gui_review_20260910_tdd.md](../tools/tests/gui_review_20260910_tdd.md) / [gui_review3_20260910_tdd.md](../tools/tests/gui_review3_20260910_tdd.md) | v1.3 レビュー往復 (2026-09-10) の試験記録 (完了記録) |

### カーネル 2.0 (完了記録)・ブート刷新・v4 草案

2.0 の 8 本はすべて [archive/kernel_v2/](archive/kernel_v2/PLAN.md) へ (完了記録)。

| ドキュメント | 内容 |
|-------------|------|
| [archive/kernel_v2/PLAN.md](archive/kernel_v2/PLAN.md) | **カーネル 2.0 の計画 (完了記録)** — リング 3 / Rust の適用範囲 / KAPI 呼び出し実測。M1〜M3 は 2026-09-03 完了、タグ `v2.0`。版数の対応は [ROADMAP.md §0](ROADMAP.md) |
| [archive/kernel_v2/M1_RING3.md](archive/kernel_v2/M1_RING3.md) / [M2_KAPI_TRAMPOLINE.md](archive/kernel_v2/M2_KAPI_TRAMPOLINE.md) / [M3_VERIFY.md](archive/kernel_v2/M3_VERIFY.md) / [CONTRACTS.md](archive/kernel_v2/CONTRACTS.md) | 2.0 の設計 (リング 3 土台、KAPI トランポリン、検証、凍結契約)。完了記録 |
| [archive/kernel_v2/TASK_coder1_M0b_privileged.md](archive/kernel_v2/TASK_coder1_M0b_privileged.md) / [TASK_coder1_M1_ring3.md](archive/kernel_v2/TASK_coder1_M1_ring3.md) / [TASK_coder2_libos32gui.md](archive/kernel_v2/TASK_coder2_libos32gui.md) | 2.0 のコーダー票。完了記録 |
| [V4_GAME_PLATFORM_DRAFT.md](archive/v4/V4_GAME_PLATFORM_DRAFT.md) / [tasks/v4/README.md](tasks/v4/README.md) | ゲーム基盤 v4 の草案 (2026-09-07)。v3 の後 |
| [archive/v21/boot_reform/00_OVERVIEW.md](archive/v21/boot_reform/00_OVERVIEW.md) | ブート刷新 (vmkernel.lz4 / ext2 ローダー) — 設計 (全 8 部) |

### FEP・V86・SQLite・ライブラリ

V86・SQLite・タイルマップ・ライブラリ設計書 (と上の boot_reform、下の単発 2 本) は fork 時 (2026-09-30) に完了記録として
[archive/v21/](archive/README.md) へ落とした (FORK_PLAN §4 J4)。

| ドキュメント | 内容 |
|-------------|------|
| [tasks/fep/00_INDEX.md](tasks/fep/00_INDEX.md) | FEP (日本語入力) 拡張 — 詳細設計 P1〜P7 の索引 (実装状況付き) |
| [tasks/fep/FEP_STATUS.md](tasks/fep/FEP_STATUS.md) / [FEP_FUTURE.md](tasks/fep/FEP_FUTURE.md) | FEP のアーキテクチャ説明 (2026-04-27 の快照) / 今後の拡張 |
| [tasks/fep/TASK_DICT_META.md](tasks/fep/TASK_DICT_META.md) | FEP 辞書のメタ情報 (形式の版・dict_id・license/attribution・`mem_reserve_kb` 等) と学習データの別ファイル化 — **計画** (ユーザー決定 2026-09-30、着手は **v3 の後の方**。v3 P10)。学習データの移行は二段 (M6、Codex X8) |
| [archive/v21/v86v2/README.md](archive/v21/v86v2/README.md) | **V86 サブシステム (再挑戦)** — 16bit ゲスト実行 (完了記録)。到達点は `04_implementation_status.md` |
| [archive/v21/wintree_port/PORT_PLAN.md](archive/v21/wintree_port/PORT_PLAN.md) | feat/vdm 系作業ツリーの移植計画と実施結果 |
| [archive/v21/sqlite/00_INDEX.md](archive/v21/sqlite/00_INDEX.md) | SQLite カーネル統合 — 設計・実装 (全 7 部) |
| [archive/v21/tilemap/00_INDEX.md](archive/v21/tilemap/00_INDEX.md) | タイルマップ / ブリット最適化 — 設計・最適化・TODO の索引 (全 8 部) |
| [archive/v21/libmath/LIBMATH_DESIGN.md](archive/v21/libmath/LIBMATH_DESIGN.md) / [archive/v21/libinput/LIBINPUT_DESIGN.md](archive/v21/libinput/LIBINPUT_DESIGN.md) / [archive/v21/libasset/LIBASSET_DESIGN.md](archive/v21/libasset/LIBASSET_DESIGN.md) / [archive/v21/libecs/LIBECS_DESIGN.md](archive/v21/libecs/LIBECS_DESIGN.md) / [archive/v21/libtext/LIBTEXT_DESIGN.md](archive/v21/libtext/LIBTEXT_DESIGN.md) | ライブラリ設計書 (math / input / asset / ecs / text) |
| `tasks/libai/` `libbattle/` `libboard/` `libecon/` `libevent/` `libinv/` `tilemap/` | 各ゲームライブラリの設計書群 (別リポジトリ `os32-game` に移った分は `os32-game:docs/...`) |
| `os32-game:docs/game/GAME_PORT_PLAN.md` / `os32-game:docs/game/ENGINE_EXTENSION_PLAN.md` / `os32-game:docs/libchem/LIBCHEM_DESIGN.md` | 対戦スゴロク RPG の移植・エンジン拡張・化学エンジン (別リポジトリ ske-studio/os32-game) |

### 単発の記録

| ドキュメント | 内容 |
|-------------|------|
| [TESTS.md](TESTS.md) | **試験の一覧の入口** (読み方・判断の正典) — `make check` の全ターゲット、`_tdd.md` と票の対応、改善提言 |
| [archive/TEST_INVENTORY_2026-09-14.md](archive/TEST_INVENTORY_2026-09-14.md) | 試験の棚卸し (2026-09-14 の快照)。正典は `TESTS.md` へ移行 |
| [archive/debug_kcg_load_font.md](archive/debug_kcg_load_font.md) | `kcg_load_font` クラッシュの仮説計画 (単発の障害記録) |
| [archive/v21/cross_compiler_rebuild.md](archive/v21/cross_compiler_rebuild.md) / [archive/v21/ext2_dind_debug.md](archive/v21/ext2_dind_debug.md) | クロスコンパイラ再構築 / ext2 二重間接の障害記録 |

## man ページ

`docs/manpages/*.1` — ゲスト内 `man` コマンド用マニュアル (約60ページ)。
`userland/deploy.yaml` の登録 (タグ `docs`) で `/usr/man/` に配備され、CD では `NORMAL.PKG` に入る ([08_build.md](08_build.md) の `tools/mkpkg.py` の節)。

## ソースツリー概要

[08_build.md §8-3](08_build.md) を参照 (複製しない)。

- [TASK_MEMMAP_V3_RESULTS](archive/v3/TASK_MEMMAP_V3_RESULTS.md) — TASK_MEMMAP_V3 から切り出した完了段の記録 (未確認事項は元票に保持)。

- [TASK_HAL_WIRING_RESULTS](archive/v3/TASK_HAL_WIRING_RESULTS.md) — TASK_HAL_WIRING から切り出した完了段の記録 (未確認事項は元票に保持)。

- [TASK_PCM_CS4231_RESULTS](archive/v3/TASK_PCM_CS4231_RESULTS.md) — TASK_PCM_CS4231 から切り出した完了段の記録 (未確認事項は元票に保持)。

- [DEFERRED_CLOSED](archive/v3/DEFERRED_CLOSED.md) — e11 統合受入で閉じた持越し台帳の行と証拠。
