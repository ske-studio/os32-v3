# FORK_PLAN — os32-v3 への fork の段取り (持っていくもの・公開前の監査・手順)

> 状態: **実装中 (2026-09-30)** — **手順 c 完了 (2026-09-30): [os32-v3](https://github.com/ske-studio/os32-v3) (パブリック) を初期コミット `a238ab1` (os32 `6ccc4049` + feat/gui `dfa97f57` の写し、1,540 ファイル) で作成し push。§1-2 と違い `docs/archive/` は CHECKLIST 3 本を除く 100 本を持った (持つ文書から archive へのリンクが 414 か所あり、INDEX が索引しているため — J4 の archive/v21 への整理は手順 e で)。除いたのは旧 CI の yml 2・docs/logs 5・CHECKLIST 3・opencode.json。以後の手順 d〜g は os32-v3 側で進め、この票の更新も os32-v3 側の写しで行う。** それまでの経過: ユーザー承認「準備を承認」(2026-09-30) を受けて票と一覧を作成。**手順 a (監査で見つかったものの修正) はブランチ `wt/fork-prep` で実施済み** (J1〜J8 は推奨どおり、J6 の manga は未回答のため触らず)。**手順 b (経緯の要約とケーススタディ) は 2026-09-30 に feat/gui で実施済み** (§1-4 の HISTORY / CASE_STUDIES を作成、INDEX と README に凍結の注記。`main` への取り込みは PM の着地)。§2 の「結果」列の *(a)* が修正後の値。リポジトリの作成・push・GitHub 操作は行っていない (§3 c の [D2])。監査の grep は基点 `d995e078` の作業ツリーで**実行済み** (§2 の「結果」列)。判断が要る点は §4。

決定済みの前提 (正典は [ROADMAP.md §0-1](../../ROADMAP.md)、[HANDOVER_2026-09-29 §2](../agents/HANDOVER_2026-09-29.md)、[V3_PLAN_DRAFT §6・§7-1 U9〜U11](V3_PLAN_DRAFT.md)。ここには写さず要点だけ):

| 項目 | 決定 |
|---|---|
| 現行の開発 | **v3**。os32 (v2.x、タグ `v2.1`、`main`) は戻り先 — 新機能は入れない |
| リポジトリ | **`os32-v3`、パブリック** |
| 文書の正典 | **os32-v3 へ**。os32 の docs は「v2.1 時点の記録」(INDEX 冒頭の注記は既に有る) |
| CI | **新環境で作り直す** (`build.yml` / `check.yml` を写さない) |
| submodule `apps/` `game/` | **private のまま、トークンで参照** (U10)。当面の保守は os32 側 |
| `tools/`・`~/np21w-src` | 共有。**動作保証は v3 側だけ** |
| **履歴 (U11)** | **os32-v3 は新しい履歴 (初期コミット) で始める**。開発の経緯と推移は文書に起こす (§1-4 の HISTORY / CASE_STUDIES)。開発中の詳細は os32 に閉じ込める。ケーススタディのまとめは持ってよい |
| **古い数字・番地 (U9)** | 本文での修正は **fork 後** (§3 f) |

読み方: §1 が持っていくものの一覧 (`git ls-files` が根拠)、§2 が公開前の監査 (実行済み)、§3 が手順、§4 が判断が要る点、§5 がしないこと。

---

## 1. 持っていくものの一覧

根拠: `git ls-files` (基点 `d995e078`、追跡 **1,551 ファイル**)。単位はディレクトリ。**「持つ」は作業ツリーの写しをそのまま初期コミットに入れる**、「持たない」は写しから除く、「要判断」は §4 で決める。
生成物・gitignore のもの (`build/out/` `build/nhd/` `images/` `*.img` `*.iso` `docs/hw/` `.env` `SOUL.md` `docs/DESIGN_PHILOSOPHY.md` `.claude/settings.local.json`) は追跡されていないので、写しに入らない (§2 で追跡 0 件を確認)。

### 1-1. コード・ビルド (持つ)

| ディレクトリ | 追跡数 | 扱い | 備考 |
|---|---|---|---|
| `kernel/` `exec/` `drivers/` `fs/` `gfx/` `kapi/` `net/` | 91 / 12 / 69 / 41 / 10 / 7 / 2 | **持つ** | v3 はこの上に積む (T0 の C11 化から) |
| `lib/` | 48 | **持つ** | vendor: `lib/sqlite3/` (public domain)、`lib/zlib/` (zlib、無改変、`README.OS32`)、`lib/microtar/` (MIT、`LICENSE` 同梱)、`lib/os32_lz4/` (自作 Rust)。`lib/lz4.c` は自作 (LZ4 ブロック仕様に準拠、出所表示なし)。**`lib/puff.c` は既に無い** (zlib inflate に乗り換え済み — [MGX_FORMAT.md](../../MGX_FORMAT.md)「puff.c から乗り換えた経緯」。CLAUDE.md の記憶行は古い) |
| `include/` `arch/` `platform/` | 24 / 4 / 2 | **持つ** | `include/pc98.h` `pegc.h` などは資料の §番号を出典として書いている (§2-2) |
| `boot/` `build/` `Makefile` `rust-toolchain.toml` | 14 / 13 / 1 / 1 | **持つ** | `build/os32.ld` の ASSERT・`gen_memmap.py --check` は T1 の検査の土台 (V3_PLAN_DRAFT §3 P0) |
| `sdk/` | 31 | **持つ** | `sdk/kapi.json` が [ABI1]。fork 後の P7 (KAPI の 1 回整理、D35) の対象 |
| `userland/` | 396 | **持つ** | `userland/lib/ui/microui.c` は rxi の MIT (§1-4 の THIRD_PARTY へ)。Rust の外部クレートは `ttf-parser` `ab_glyph_rasterizer` `libm` (font_test のみ、`Cargo.lock` 同梱) |
| `assets/` | 27 → 17 | **持つ**、ただし **`assets/ipadic/` (13 CSV) は配布条件の原文を同梱してから**。**`assets/fonts/`: TTF (`ipaex{g,m}.ttf`) は持たない — リポジトリに含めず、ビルド時に `tools/fetch_fonts.py` が IPA の公式配布から同意つきで取得する。ライセンス文・`README.OS32`・fetch は持つ** (ユーザー決定 2026-09-30: 日本語 OpenType はリポジトリに含めずビルド時にダウンロード、v3 の OpenType 層はそれを配布物に入れる (同意した人がビルドした物)、CUI は KCG ROM のみ、移植先の GUI 基本フォントは寛容ライセンスの欧文 OpenType — 候補 Go fonts (3-clause BSD) / DejaVu (Bitstream Vera License)、選定は T7b) | IPADIC の原文は本リポジトリに無い ([TASK_DICT_META §1](../fep/TASK_DICT_META.md))。`assets/manga/*.MGX` (7 本) は **2026-09-30 に削除**、生成も配備もしない (§4 J6) |
| `tools/` | 413 | **持つ** (`tools/tests/` 332 を含む) | 共有の道具 (決定済み)。`tools/ci/build_cross.sh` `tools/ci_fetch.sh` は CI の作り直し (§3 d) で使う。`tools/tests/*_tdd.md` (97 本) は §4 J3 |
| `.gitmodules` `apps` `game` | 3 | **持つ** (同じ URL・同じコミット) | 再登録の手順は §3 c。private なので clone は落ちる (README に書く) |
| `.gitignore` `.env.sample` `.mcp.json` | 3 | **持つ** | `.mcp.json` の `autoplay` は `../os32-game/...` を指す (相対、そのままでよい)。`opencode.json` は §4 J7 |
| `README.md` `LICENSE` `INSTALL.md` `AGENTS.md` `CLAUDE.md` `CHANGELOG.md` | 6 | **持つが v3 用に書き直す** | README は「os32 v2.1 からの fork」「submodule は private」「v3 の目的」を冒頭に。CHANGELOG は **v3 で新規** (v2.x の履歴は HISTORY.md へ要約、詳細は os32 の CHANGELOG を指す)。LICENSE は MIT のまま (著作権表示の年と名義は §4 J1) |

### 1-2. `docs/` (持つ・持たない・要判断)

| 対象 | 追跡数 | 扱い | 備考 |
|---|---|---|---|
| `docs/INDEX.md` (正典表)、`01`〜`10_*.md`、`CONSTRAINTS.md`、`POLICY_DEV.md`、`POLICY_DEBUG.md`、`ROADMAP.md`、`DEVELOPMENT.md`、`KAPI_SPEC.md`、`TESTS.md` (生成)、`MGX_FORMAT.md`、`NHD_FORMAT.md`、`BENCHMARK.md`、`DESIGN_APP_FIRST.md`、`.orphans-allow` | 25 | **持つ** | INDEX 冒頭の注記を「正典はここ (os32-v3)。os32 は v2.1 時点の記録」に書き換える (§3 e)。ROADMAP §0 の版数表は v3 の行を「現行」に。POLICY_DEBUG §4 は全部持つ (v3 でも踏む罠。普遍的なものは CASE_STUDIES に要約) |
| `docs/RELEASE_v2.1.md` | 1 | **持つ** (経緯の要約として) | fork の起点の記録。HISTORY.md から指す |
| `docs/AUXILIARY_CORE_SERVICE.md` `LEGACY_LIVING_PRESERVATION.md` `V4_GAME_PLATFORM_DRAFT.md` | 3 | **持つ** (草案) | ROADMAP §0 の版数表に載せない扱いは変えない |
| `docs/manpages/` | 67 | **持つ** | コマンドの正典。`sfs.1` の `/dev/ttyUSB0` は一般名で問題なし |
| `docs/images/` (2 png) | 2 | **持つ** | README が使う |
| `docs/tasks/v3/` (8 本 + 本票) | 9 | **持つ** | 正典の移転先。V3_PLAN_DRAFT は本案に昇格するまで草案のまま |
| `docs/tasks/agents/ROLES.md` `HANDOVER_2026-09-29.md` | 2 | **持つ** | ROLES §0「開発の場所」を os32-v3 に。HANDOVER は「fork 時点の申し送り」として残し、次の HANDOVER は os32-v3 で起こす |
| `docs/tasks/realhw/` (PLAN、TASK_TRIDENT_DRIVER、TASK_PEGC480_REALHW、TASK_FD144、TASK_LAN_82557) | 5 | **持つ** (生きている票) | すべて 実装中 / 設計中 / 実機確認待ち。§0 の「v1.x のあいだは着手しない」など策定時の行は U9 (§3 f) |
| `docs/tasks/settings/` (DESIGN、DEVICE_RESERVATION、F2_OWNERSHIP、FEP_BOUNDARY) | 4 | **持つ** | v3 の入力 (U6 決定)。DESIGN は受入完了だが settings.db の正典 (INDEX の正典表) |
| `docs/tasks/fep/` (00_INDEX、TASK_DICT_META、FEP_STATUS、FEP_FUTURE、01〜07) | 11 | **持つ** | TASK_DICT_META は v3 P10。01〜07 は領域別索引が指す設計記録 |
| `docs/tasks/memory/APP_BAND_PDE.md`、`docs/tasks/gui/` (9)、`docs/tasks/network/` (3)、`docs/tasks/portability/` (5)、`docs/tasks/shell/` (2)、`docs/tasks/hotdeploy/`、`docs/tasks/v4/` | 22 | **持つ** | INDEX の正典表が指すもの (GUI プロトコル、LAN 設計、ARM_GAUGE、v4 草案)。`tasks/arch_port/` (3) は別リポジトリの快照 — 持つが更新しない (INDEX の注記どおり) |
| `docs/tasks/sqlite/` (9)、`tilemap/` (8)、`v86v2/` (15)、`boot_reform/` (8)、`wintree_port/`、`lib*/` (5)、`cross_compiler_rebuild.md`、`ext2_dind_debug.md` | 48 | **要判断** (§4 J4) | 受入完了 (2026-04〜08) の設計記録で、状態行の無いものが多い。案: **持つが fork 直後に `docs/archive/v21/` へ落とす** (`tools/move_docs.py`、V3_PLAN_DRAFT §6-1 の案)。理由: SQLite / V86 / tilemap の「なぜその実装か」は v3 でも問われる |
| `docs/archive/` | 103 | **大半は持たない** (完了記録)。持つのは §1-4 の CASE_STUDIES が引く数本だけ (要約して引き、原本は os32 を指す) | `archive/README.md`・`ROADMAP_v1.0.md`・`kernel_v2/PLAN.md`・`kernel_v21/`・`realhw_v21/` (PEGC・FDC・LAN の完了票) が HISTORY / CASE_STUDIES の材料。**`archive/realhw_v21/CHECKLIST_2026-09-2{4,5,6}.md` (3 本) は持たない** (実機作業の日次記録 = 開発中の詳細) |
| `docs/logs/` (HDD_BIOS_DEBUG、PM_PIO_TEST、png ×3) | 5 | **持たない** | 2026-04 のデバッグ記録。HISTORY に 1 行 |
| `docs/hw/` | 0 (gitignore) | **持たない** (そのまま) | `tools/sync_hwdocs.sh` で各自ミラー。§2-2 |

### 1-3. その他

| 対象 | 追跡数 | 扱い | 備考 |
|---|---|---|---|
| `.claude/skills/` (7 スキル、8 ファイル) | 8 | **持つ** (§4 J5 で確定) | 体制は同じ。`run-os32/driver.py:44` の `/mnt/c/os32` はユーザー名を含まない |
| `.claude/settings.json` | 1 | **持つ** | [D1]〜[D3] の ask / deny。`settings.local.json` は gitignore (持たない) |
| `.github/workflows/build.yml` `check.yml` | 2 | **持たない** (決定済み、§3 d で作り直す) | `check.yml` の冒頭コメントにある検査一覧は新 CI の仕様の材料 |
| `sample/` (Gemini 生成 jpg ×3、`.vbz` ×7) | 10 | **要判断** (§4 J6) | VBZ の試験データ (2026-04-13)。`.gitignore` に `/sample/` があるが追跡されている (ignore より前に add)。jpg は Gemini 生成 (著作権は問題ないが、出所を書く) |
| `tests/gfx_demo.c` `gfx_test_lib.c` | 2 | **要判断** (§4 J6) | 初期コミットの残り。`.gitignore` の `/tests/` に反して追跡。`userland/tests/` と重複していないか確認して、不要なら持たない |
| `tasks/vzeditor_status.md` | 1 | **持たない** | VZ Editor 移植の残課題 (2026-04)。`.gitignore` の `/tasks/` に反して追跡。HISTORY に 1 行 |
| `serial` (空ファイル) | 1 | **持たない** | 2026-09-22 のコミット `e5c4a479` で誤って追加された 0 バイトのファイル |
| `opencode.json` | 1 | **要判断** (§4 J7) | 私設の Ollama ホスト名 `google-colab-qwen38` を含む (§2-5) |
| タグ `v1.0.x` `v1.1.0` `v2.0` `v2.1` `archive/*` `baseline-before-split` | — | **持たない** (新しい履歴) | 初期コミットの本文に「os32 `dbf4f84b` (タグ `v2.1`) + feat/gui `d995e078` (文書整理) から fork」と SHA を書く |

### 1-4. 新しく書く文書 (経緯の要約 + ケーススタディ) — 候補一覧

U11「開発の経緯と推移は文書に起こす。ケーススタディのまとめは持ってよい」の実体。**os32 側で書いて `main` に入れ (§3 b)、写しを os32-v3 の初期コミットに含める**。原本の SHA・票は os32 側を指す (os32-v3 の履歴には無い)。

| 文書 | 内容 (候補) | 材料 (os32 側) |
|---|---|---|
| **`docs/HISTORY.md`** — **作成済み (2026-09-30、手順 b、[HISTORY.md](../../HISTORY.md))** | 決裁の年表と版の推移。**2026-04-07 初期コミット → v1.0 (最初の公開) → v1.1 (ツリー再編、V86 で MS-DOS、HostDrv) → v1.x GUI シェル 1.1〜1.4 (Win3.1 風、libos32gui、shlib 帯) → v2.0 リング 3 ネイティブ (2026-09-03) → v2.1 実機 Ra266 (FD 起動・CD インストール・HDD 起動・PEGC 480、2026-09-29) → v3 へ fork**。1,772 コミットの要約を版ごとに 5〜10 行。体制の推移 (単独 → 4 役: PM / コーダー worktree / Codex レビュー / ローカル AI テスター、hermes の撤収) も 1 節 | `CHANGELOG.md`、`docs/RELEASE_v2.1.md`、`docs/archive/ROADMAP_v1.0.md`、`docs/archive/kernel_v2/PLAN.md`、`docs/tasks/agents/ROLES.md` (経緯の節)、`docs/archive/agents/RETROSPECTIVE_2026-09-09.md`、`git log --format='%ad %s' --date=short` |
| **`docs/CASE_STUDIES.md`** — **作成済み (2026-09-30、手順 b、[CASE_STUDIES.md](../../CASE_STUDIES.md)、10 候補すべて採用)** | 普遍的な教訓の集 (os32 固有の番地・手順は書かない)。候補: (1) **資料どうしが食い違ったときの決着** (POLICY_DEBUG §4-50・§4-59: Bible と UNDOCUMENTED で極性が逆、信号レベルの記述を採る、NP21/W の実装で裏を取る); (2) **エミュレータで確認済みが実機で通らない** (§4-47〜§4-57: FDC の時間上限、8253 の整数分周、`io_wait` 連打、`volatile`); (3) **PEGC 640x480 — 実機 ROM の OUT 列に合わせる** ([TASK_PEGC480_REALHW](../realhw/TASK_PEGC480_REALHW.md) §3、§4-62: 画面を見ない受入条件の作り方); (4) **メモリマップの 3 者討論** ([TASK_MEMMAP_V3 §11](TASK_MEMMAP_V3.md): 意見書 → 反論と合成 → 最終案 → Codex 3 往復 → ユーザー決裁の形式、B1〜B9 の「先に事実を揃える」); (5) **Trident 設計レビュー 5 回** ([TASK_TRIDENT_DRIVER §8](../realhw/TASK_TRIDENT_DRIVER.md): 汎用機構を足すと指摘が集中する、資料に「あること」と「無いこと」を分ける); (6) **Codex / Fable / PM の 3 者討論の形式と規約** (ROLES §5 の依頼書式、`codex exec -s read-only`、astra でなければ休ませる、3 ラリーで決着しない争点はユーザーへ); (7) **文書運用** (情報単位ごとの正典 1 か所、孤児検査、状態行の語彙、`move_docs.py` — 2026-09-15 の棚卸しで何が起きたか); (8) **変異試験と検査の 3 段** (§4-40・§4-41、`check-changed`); (9) **資源回収を所有者タグで** (§4-15・§4-16、`exec_exit`); (10) **配備の成否を文言で判断しない** (§2、§4-8・§4-17・§4-29・§4-33) | `docs/POLICY_DEBUG.md` §4 (62 項)、`TASK_MEMMAP_V3.md` §11、`TASK_TRIDENT_DRIVER.md` §8、`TASK_PEGC480_REALHW.md`、`ROLES.md`、`docs/archive/realhw_v21/` |
| **`THIRD_PARTY.md`** (置き場は §4 J2) — **作成済み (2026-09-30、手順 a、ルート [../../../THIRD_PARTY.md](../../../THIRD_PARTY.md))** | 第三者コードと資料の一覧と配布条件: §2-3 の表をそのまま | `lib/*/README.OS32`、`LICENSE` 同梱ファイル |
| `README.md` (書き直し) | v3 の目的 (V3_PLAN_DRAFT §2 の一文、U4 の承認後)、fork 元、submodule が private であること、ビルド (INSTALL) と CI、ライセンス | 現 README、INSTALL.md |
| `CHANGELOG.md` (新規) | v3 の版だけ。冒頭に「v2.x 以前は HISTORY.md と os32 の CHANGELOG」 | — |

---

## 2. 公開前の監査 — 項目・調べるコマンド・合格条件・結果

**新しい履歴で始めるので、見るのは作業ツリーだけ** (os32 の履歴は公開しない)。すべて `git ls-files` の追跡ファイルに限る (`git grep` は追跡ファイルだけを見る)。結果の列は 2026-09-30 に基点 `d995e078` で実行したもの。**秘密が見つかっても値は書かない** (ファイル名と行番号だけ)。

### 2-1. 秘密 (.env・トークン・パスワード) — [D3]

| # | 調べるコマンド | 合格条件 | 結果 (2026-09-30) |
|---|---|---|---|
| S1 | `git ls-files \| grep -E '^\.env$\|SOUL\.md\|DESIGN_PHILOSOPHY\|settings\.local'` | 0 件 | **0 件** (合格)。`.env.sample` は雛形 (値は空) |
| S2 | `git log --all --oneline -- .env SOUL.md docs/DESIGN_PHILOSOPHY.md docs/hw \| wc -l` | 0 (新履歴では不要だが、os32 側の確認として) | **0** (合格) |
| S3 | `git grep -n -I -iE "(api[_-]?key\|secret\|passw(or)?d\|token)\s*[:=]\s*['\"]?[A-Za-z0-9_\-]{8,}\|sk-[A-Za-z0-9]{20,}\|ghp_[A-Za-z0-9]{20,}\|github_pat_[A-Za-z0-9_]{20,}\|AKIA[0-9A-Z]{16}\|-----BEGIN (RSA \|OPENSSH \|EC )?PRIVATE KEY\|xox[baprs]-[A-Za-z0-9-]{10,}\|AIza[0-9A-Za-z_\-]{30,}"` | 実物 0 件 (当たりは目視で偽陽性と確認) | **34 件、実物 0** — `exec/launch.c` (6: 起動要求の番号 `token`)、`tools/tests/launch_host.c` (8: 同)、`tools/tests/test_emu_playbook.py:701` (試験用の偽値)、`lib/sqlite3/sqlite3.c` (18: SQLite 本文の語) |
| S4 | `git grep -n SUBMODULE_TOKEN` | 名前だけ (値なし) | **9 件、すべて名前** (`build.yml`、`08_build.md:771`、V3_PLAN_DRAFT、archive の HANDOVER) |
| S5 | 公開の直前に secret scanner を 1 回 (`gitleaks detect --no-git -s <写しの木>` または GitHub の secret scanning を有効に) | 0 件 | **未実施** — *(a)* 手元に gitleaks が無い。S3 の grep は修正後も同じ 33 件 (実物 0)。写しを作る c で gitleaks か GitHub の secret scanning を |

### 2-2. 著作権物 (PC-98 資料のミラーと引用)

| # | 調べるコマンド | 合格条件 | 結果 (2026-09-30) |
|---|---|---|---|
| C1 | `git ls-files docs/hw \| wc -l`; `git log --all --oneline -- docs/hw \| wc -l` | 0 / 0 | **0 / 0** (合格)。`.gitignore` の `/docs/hw` と `tools/sync_hwdocs.sh` の注意書きはそのまま持つ |
| C2 | `git grep -n -I -iE "Bible\|UNDOCUMENTED\|io_[a-z0-9_]+\.md" -- docs ':!docs/archive'` | 出典表示 (§番号・ファイル名) と数行の要旨だけ。**表・図・段落の丸写しが無い** | **25 ファイル、様式は出典表示** — 多いのは `TASK_TRIDENT_DRIVER.md` (42)、`POLICY_DEBUG.md` (15)、`TASK_PEGC480_REALHW.md` (9)、`gui/DESIGN.md` (7)。**目視の要判断**: POLICY_DEBUG §4-47・§4-50・§4-53・§4-57・§4-59 (資料の文を「」で引く箇所) と TASK_TRIDENT §3 (資料の節)。短い引用は出典付きで残し、長い写しは要旨に書き換える (§3 a)。*(a)* 目視の結果: §4-53 (io_fdd.md の 1 文)・§4-57 (io_kb.md の 2 文)・TRIDENT §2-2 (io_wab.md 0FACh の 2 文) を要旨に書き換え。§4-47 (引用なし)・§4-50 (数値のみ)・§4-59 (ビット名のみ)・TRIDENT §3-1 (資料名の列挙) は変更なし |
| C3 | 同じ grep を `-- ':!docs' ':!lib/sqlite3'` で (コード側) | 同上 | **`include/pc98.h` (31)、`include/pegc.h` (19)、`drivers/serial.c` (9)、`drivers/pci_decode.h` (8)、`drivers/dma8237.h` (8) …** — すべて `/* 出典: PC9800Bible §1-3 */` の形の §番号参照 (合格) |
| C4 | `git ls-files \| grep -iE '\.(pdf\|jpg\|jpeg\|png)$'` を目視 | 資料のスキャン・写真の写り込みが無い | **8 件**: `docs/images/` (2、スプラッシュとデモ)、`docs/logs/*.png` (3、2026-04 の画面写し。§1-2 で持たない)、`sample/Gemini_Generated_Image_*.jpg` (3、生成画像) — 資料のスキャンは無い (合格) |

### 2-3. 第三者ライセンス (THIRD_PARTY.md の材料)

| 部品 | 場所 | ライセンス | 原文の有無 (2026-09-30) | 合格条件 |
|---|---|---|---|---|
| SQLite 3.53.0 | `lib/sqlite3/sqlite3.{c,h}` | public domain (blessing) | 本文冒頭に有る | 表に載せる |
| zlib (inflate のみ、無改変) | `lib/zlib/` | zlib | `zlib.h` 冒頭 + `README.OS32` | 表に載せる。条項 2 (改変版の明示) は無改変なので不要 |
| microtar (rxi) | `lib/microtar/` | MIT | `LICENSE` 同梱 | 表に載せる |
| microui (rxi) | `userland/lib/ui/microui.{c,h}` | MIT | ファイル冒頭 | 表に載せる |
| FatFs (ChaN) R0.15 系 | `fs/fatfs/` | FatFs license (BSD 風) | `ff.c` 冒頭 | 表に載せる |
| LZ4 ブロック展開 (自作) | `lib/lz4.c`、`lib/os32_lz4/`、`boot/lz4_mini.c` | MIT (本リポジトリ) | — | 「仕様準拠の自作、公式コード不使用」と 1 行 |
| newlib 4.4.0 nano | ツールチェーン側 (`tools/ci/build_cross.sh`)。成果物にリンク | 各ファイルの BSD 系 | リポジトリに無い (toolchain) | 表に「リンクされる」と載せ、`COPYING.NEWLIB` の入手先を書く |
| Rust クレート `ttf-parser` 0.21.1、`ab_glyph_rasterizer` 0.1.10、`libm` 0.2.16 | `userland/rust/font_test/` (`userland/rust/Cargo.lock`) | ttf-parser・libm = MIT OR Apache-2.0、ab_glyph_rasterizer = Apache-2.0 (a で crates.io の表記を確認) | vendor していない (ビルド時に取得) | 表に載せる。成果物 (font_test) に含まれるので配布物には表記が要る |
| **IPADIC** (MeCab 版 CSV 13 本) | `assets/ipadic/` → `assets/fep.db` (生成、配布物) | IPADIC の配布条件 (ICOT 由来の独自条件) | ~~原文が無い~~ → *(a)* **`assets/ipadic/COPYING` を同梱** (mecab-ipadic 2.7.0-20070801 の原文、無改変。CSV 13 本は上流と sha256 一致。出所は `README.OS32`) | **原文を取り寄せて `assets/ipadic/COPYING` に置き、`fep.db` の `meta` に `license` / `attribution` を運ばせる** (TASK_DICT_META M5) — **公開の前提条件** |
| **IPAex フォント** (`ipaexg.ttf` `ipaexm.ttf` と派生の `*.kcgfont`) | `assets/fonts/` | IPA Font License Agreement v1.0 | ~~原文が無い~~ → *(a)* **`assets/fonts/IPA_Font_License_Agreement_v1.0.txt` を同梱** (IPAexfont00401.zip の原文、無改変、`.gitattributes -text` で改行も保つ。TTF は zip と sha256 一致)。**サブセット TTF (`ipaexg_subset.ttf`) は 2026-09-30 に廃止して解消、原本の TTF はリポジトリに含めない** (同日、追跡をやめてビルド時取得に。ユーザー決定: 日本語 OpenType はリポジトリに含めずビルド時にダウンロード。§1-1)。`.kcgfont` は D14 で廃止予定 (os32 側では名前の問題が残る、`README.OS32`) | **v3 では TTF を持たず、ライセンス文・`README.OS32`・`tools/fetch_fonts.py` を持つ** — v3 の OpenType 層はビルド時に取得した ttf を配布物に入れる (同意した人がビルドした物なので、配布物にはライセンス文を添える。§4 J2 の IPAex の項はそのまま) |
| `assets/joyo_kanji.txt` | 常用漢字 2,136 字 | 文化庁の告示 (事実の一覧) | — | 出所を 1 行 |
| Python (ホストの道具) | `tools/` | Pillow、PyYAML、lz4、numpy、fontTools、zopfli、pyserial、pywin32、pyautogui、pandas | ~~`requirements*.txt` が無い~~ → *(a)* ルートの `requirements.txt` (import を git grep で確認、Windows 専用 2 つは環境マーカー) | 一覧は *(a)* ルートの `THIRD_PARTY.md` (J2 (a))。libm 0.2.16 は crates.io では MIT 単独 |

### 2-4. ユーザー名を含む実パス

| # | 調べるコマンド | 合格条件 | 結果 (2026-09-30) |
|---|---|---|---|
| P1 | `git grep -n -I -E "/home/hight\|/mnt/c/Users\|C:\\\\Users\|C:/Users"` | 0 件 (プレースホルダ `<user>` と試験の架空値は可) | **7 ファイル 8 件** — 合格: `docs/08_build.md:745` (`<user>`)、`tools/tests/test_np21w_ctl.py:1432` (架空値)。**直す (6)**: `drivers/wab_cirrus.c:16`、`include/pegc.h:265`、`include/wab_xe10.h:14`、`tools/os32_server.py:9`、`tools/tests/test_key_inject.py:23` (いずれも `/home/hight/np21w-src` → `~/np21w-src` か `$NP21W_SRC`)。*(a)* **修正済み**: コメント 4 か所は `~/np21w-src`、`test_key_inject.py` は環境変数 `NP21W_SRC_DIR` (既定 `~/np21w-src`)。残りは `<user>`・架空値・本票の grep 文字列だけ |
| P2 | 同じ grep を `-- .claude` で | 0 件 | **0 件** (合格。`C:\os32` `/mnt/c/os32` はユーザー名を含まない) |
| P3 | `git grep -n -I "np21w-src"` | `~/np21w-src` の形 (別リポジトリの名前として可) | 上記 5 件以外は `~/np21w-src` (合格) |

### 2-5. 実機のホスト名・IP・メール・私設のサービス名

| # | 調べるコマンド | 合格条件 | 結果 (2026-09-30) |
|---|---|---|---|
| H1 | `git grep -n -I -iE "hight-PC\|VJ24"` | 0 件 | **0 件** (合格。実機ホストの名前は memory 側だけ) |
| H2 | `git grep -h -o -I -E "\b[0-9]{1,3}(\.[0-9]{1,3}){3}\b" -- . ':!lib/sqlite3' \| sort \| uniq -c` | 127.0.0.1、10.0.2.2 (NP21/W の NAT) 以外が無い | **合格** (127.0.0.1 ×71、10.0.2.2 ×1、残りは版番号) |
| H3 | `git grep -n -I -oE "[A-Za-z0-9._%+-]+@[A-Za-z0-9.-]+\.[a-z]{2,}" -- . ':!lib/sqlite3' ':!lib/zlib' ':!fs/fatfs'` | 実在のアドレスが無い | **合格** (`t@example.invalid` のみ) |
| H4 | `git grep -n -I -E "google-colab-[a-z0-9]+"` | 0 件 (私設の Ollama ホスト名) | **3 ファイル** (`opencode.json` (baseURL)、`tools/review_local.py` (×2)、`.claude/skills/os32-local-review/SKILL.md` — `os32-local-ai/SKILL.md` には無かった)。*(a)* **修正済み**: 環境変数 `OS32_REVIEW_LLM_URL` (無ければ `.env` の同名の行、既定 127.0.0.1)。`.env.sample` に行を追加。残るのは本票の記述だけ |
| H5 | `git grep -n -I "/dev/ttyUSB"` | 一般名だけ | **合格** (`tools/rshell_serial.py`、`TASK_LAN_82557.md`、`manpages/sfs.1` — いずれも既定値の例) |

### 2-6. NP21/W の ini・NHD・ディスクイメージ・写真

| # | 調べるコマンド | 合格条件 | 結果 (2026-09-30) |
|---|---|---|---|
| M1 | `git ls-files \| grep -iE '\.(ini\|nhd\|hdi\|img\|d88\|iso\|fdi)$'` | 0 件 | **0 件** (合格。`*.img` `*.d88` `*.iso` `/build/nhd/` は gitignore。`.ini` は gitignore に無いが追跡も無い — *(a)* **`.gitignore` に `*.ini` を足した**) |
| M2 | `git ls-files \| grep -iE '\.(jpg\|jpeg\|png)$'` を目視 | 個人情報の写り込みが無い | **合格** (§2-2 C4 の 8 件。実機の写真 `lcd_osd_cui_2026-09-29.jpg` は追跡されていない) |
| M3 | `git ls-files \| xargs -d '\n' ls -l \| sort -k5 -n \| tail` (大きいファイル) | 100KB 超は理由が言える | *(a)* **20 件、すべて理由が言える**: `ipadic/*.csv` (4)、~~`ipaex*.ttf` (2)~~ (2026-09-30 に追跡をやめた — ビルド時取得)、`sqlite3.{c,h}`、`sample/*.jpg` (3、Gemini 生成) と `sample/*.vbz` (5)、`fs/fatfs/ff.c`、`tools/tests/b8_open_host.c` (223KB の試験)、`Noun.adjv.csv`。`assets/manga/*.MGX` は 100KB 未満 (2026-09-30 に削除、`ipaex*.ttf` は追跡しない) |

### 2-7. 追跡と `.gitignore` の食い違い (写しの前に片付ける)

`.gitignore` に `/sample/` `/tasks/` `/tests/` があるのに `sample/` (10)、`tasks/vzeditor_status.md`、`tests/` (2) が追跡されている (ignore より前に add)。空ファイル `serial` も同じ。§1-3 のとおり `serial` と `tasks/` は持たず、`sample/` `tests/` は §4 J6。*(a)* **片付け済み**: `.gitignore` の 3 行を外し、`serial`・`tasks/vzeditor_status.md`・`tests/` (2 本、どのビルド規則も参照せず `userland/tests/gfx_demo200.c` が後継) を削除、`sample/README.OS32` に出所を記載。

---

## 3. 手順 (順に)

「誰が」: **PM** = Claude Code (Opus 5.5)、**Fable** = コーダー (Fable 5.1、worktree)、**ユーザー**。**[D2]** = 不可逆・公開に当たる操作で個別承認が要るもの。PM の推奨は基本的に承認 (CLAUDE.md) だが、**リポジトリの作成・公開・push は [D2]**。

| 手順 | 内容 | 誰が | [D2] |
|---|---|---|---|
| **a. 監査** | §2 の全項目を os32 の作業ツリーで実施し、結果を本票の「結果」列に更新。**直すもの**: 実パス 5 か所 (§2-4 P1)、`google-colab-*` の逃がし先 (§2-5 H4、J7)、`.gitignore` に `*.ini`、IPADIC / IPAex の原文の同梱 (§2-3)、`tools/requirements.txt`、Bible / UNDOCUMENTED の長い引用の書き換え (§2-2 C2 の目視 5 か所)、`serial` と `tasks/vzeditor_status.md` の削除、`sample/` `tests/` の判断 (J6)。**secret scanner を 1 回** (S5)。os32 側に 1 コミット (「fork 前の監査」) | Fable (grep と修正)、PM (合否) | 無し (削除は追跡ファイル 3 本、`rm -rf` ではない) |
| **b. 経緯の要約とケーススタディ** | §1-4 の `HISTORY.md` `CASE_STUDIES.md` `THIRD_PARTY.md` を os32 側で書き、**os32 の `main` に入れる** (feat/gui → main の取り込み、V3_PLAN_DRAFT §6-2 D の「文書だけ」の範囲)。同時に os32 の `docs/INDEX.md` 冒頭の注記を「**v2.1 時点の記録**。正典は os32-v3 (URL は c の後に足す)」へ、`README.md` 冒頭にも 1 段落。**結果 (2026-09-30)**: `docs/HISTORY.md` (年表・版ごとの要約・体制の推移・決裁の年表・数字・os32 側の参照先、`tasks/vzeditor_status.md` の 1 行を含む) と `docs/CASE_STUDIES.md` (10 候補すべて、各 1〜2 段落 + 出典) を feat/gui に作成、INDEX 冒頭の注記を「v2.1 時点の記録 (戻り先)・fork は予定」に、README 冒頭に 3 行、INDEX に 2 行。`make check-docs-links check-docs-orphans check-docs-status` rc=0。**Codex (gpt-6-astra) の事実確認 1 往復 (2026-09-30、修正要 12 件 — KAPI の関数数、行数の集計条件、時点の混同、出典より強い断定、固有の定数の再掲) は同日に修正済み**。**残り**: `main` への取り込み (PM) | Fable (執筆)、Codex (CASE_STUDIES の事実確認、read-only)、PM (着地) | 無し |
| **c. os32-v3 を新しい履歴で作る** | (1) `git archive HEAD \| tar -x -C <新しい木>` で**追跡ファイルだけ**の写しを作る (作業ツリーの未追跡物・`docs/hw`・`.env` が混ざらない)。§1 の「持たない」を写しから除く。(2) `git init` → `LICENSE` (MIT、J1) → `README.md` を v3 用に → 初期コミット (本文に「os32 `dbf4f84b` (v2.1) と feat/gui `d995e078` から fork。履歴は os32 に」)。(3) `git submodule add https://github.com/ske-studio/os32-apps.git apps` / `os32-game.git game` を**同じコミット**で再登録 (`.gitmodules` は写しに有るので `git submodule update --init` でもよい。private なので clone 時にトークンが要る旨を README に)。(4) `make all` と `make check-fast` が新しい木で rc=0 (ツールチェーンは同じ)。(5) **GitHub に `os32-v3` を作り (パブリック)、push** | Fable ((1)〜(4))、**ユーザー ((5) の作成と公開、または PM が `gh repo create --public` を承認のうえ実行)** | **(5) は [D2]** (公開は不可逆) |
| **d. CI を新環境で** | `build.yml` を写さず、`tools/ci/build_cross.sh` を使って最小から: ① `check.yml` 相当 (ツールチェーン無しの静的ゲート: KAPI 版番号、生成物一致、制約、shlib 番号表、GUI プロトコル、**lychee の文書リンク・孤児・状態行・試験一覧**、ne2000 ホスト試験) → ② 本体ビルド (toolchain のキャッシュ、`make all external`、成果物) → ③ apps/game は `SUBMODULE_TOKEN` (fine-grained PAT、2 リポジトリの Contents: read) が有るときだけ、無ければ core だけ (os32 と同じ方針)。最初の範囲は J8 | Fable (YAML と `tools/ci/`)、**ユーザー (`SUBMODULE_TOKEN` の secret 登録)** | secret の登録はユーザー操作 ([D3]: 値を出力に出さない) |
| **e. 文書の正典の移行** | os32-v3 側: `docs/INDEX.md` 冒頭の注記を「正典はここ」に、`ROADMAP.md §0` の版数表で v3 を「現行」に、`ROLES.md §0` の「開発の場所」を os32-v3 に、`CLAUDE.md` の体制欄を更新、`HANDOVER_2026-09-29.md` を「fork 時点の申し送り」に (次の HANDOVER は os32-v3 で)。J4 で決めた完了票を `docs/archive/v21/` へ (`tools/move_docs.py`)。os32 側: INDEX の注記に os32-v3 の URL を足し、以後 os32 は「戻り先」(**凍結**: 手を入れるのは戻る必要が生じたときと致命的な不具合のときだけ) | Fable、PM (着地: `make check-docs-links check-docs-orphans check-docs-status` rc=0) | 無し |
| **f. U9 — 古い数字・番地の本文修正** | os32-v3 の**最初の作業**。V3_PLAN_DRAFT §3-1 の MD1・MD4・MD5・MD6・MD9 (KHEAP の余り、SQLite 帯の伸び代、カーネル本体の残りなど) と §4 D13・D19 (realhw PLAN の策定時の行、状態行の語彙) を本文で直す。正典は `02_memory.md §2-1` の生成ブロックと `KAPI_SPEC.md` なので、写しはそこを指す形に | Fable、Codex (数字の照合) | 無し |
| **g. v3 の最初の票 T0 (C11)** | [TASK_C11_MIGRATION](TASK_C11_MIGRATION.md) (計画 2026-09-30) = [TASK_MEMMAP_V3](TASK_MEMMAP_V3.md) の T0 = [C1] の改訂 (C89 → C11、`_Static_assert` は既存の検査を置き換えない — V3_PLAN_DRAFT §3 P0)。ここから os32-v3 の通常の体制 (ROLES §0) | PM (起票)、Fable / Opus (実装)、Codex (レビュー) | 無し |

順序の理由: a → b は「公開しても困らないものにしてから、経緯を書く」。b を os32 の `main` に入れるのは、os32 が戻り先として経緯を持つため (V3_PLAN_DRAFT §6-2 D)。c の写しは `git archive` で取るので a の結果だけが載る。d を e より先にするのは、e の文書検査 (lychee) を新 CI で回してから正典を宣言するため。

---

## 4. 判断が要る点 (J1〜J8)

| # | 判断 | 選択肢と推奨 | 決める人 |
|---|---|---|---|
| J1 | **`LICENSE` と `README` の文面** | LICENSE: MIT のまま、著作権表示は現 LICENSE の「Copyright (c) 2025-2026 すけさん」を踏襲 (名義を英字にするかはユーザー)。README: 冒頭 3 段落 = v3 の目的 (U4 の一文の承認後)・fork 元 (os32 v2.1、履歴は os32)・submodule が private (clone は `--recurse-submodules` 無しで、apps/game 無しでも core は組める)。**推奨: 名義は現 LICENSE のまま、README は c の前に PM が案を出してユーザー承認** | ユーザー |
| J2 | **ライセンス表記の置き場** | (a) ルートに `THIRD_PARTY.md` 1 本 (§2-3 の表) + 各 vendor ディレクトリの原文はそのまま / (b) `docs/THIRD_PARTY.md` / (c) README の 1 節。**推奨: (a)** — 配布物 (`os32-sdk`、ディスクイメージ) にも同じファイルを同梱できる。IPAex の派生フォントは第 3 条の条件 (名前に IPA を含めない・ライセンス同梱) を `assets/fonts/` の `README.OS32` に書く | ユーザー (置き場)、PM (本文) |
| J3 | **TDD 記録 `tools/tests/*_tdd.md` (97 本) を持つか** | (a) 全部持つ (試験の根拠。`TESTS.md` が生成で指し、孤児検査の対象) / (b) 持たない (試験コードだけ持ち、`TESTS.md` の対応列を空に) / (c) 生きている票 (§1-2 で持つ票) が指すものだけ。**推奨: (a)** — 検査器 (`check_docs_orphans.py` の (2)、`gen_tests_inventory.py`) が `_tdd.md` を前提にしており、(b)(c) は道具の改修が要る。「開発中の詳細」だが、試験の意図の記録であって日次記録ではない | ユーザー |
| J4 | **CHECKLIST と受入完了の設計記録を持つか** | CHECKLIST (`archive/realhw_v21/CHECKLIST_2026-09-2{4,5,6}.md`): **持たない** (実機作業の日次記録 = 開発中の詳細、HISTORY に要約)。`docs/tasks/sqlite/ tilemap/ v86v2/ boot_reform/ wintree_port/ lib*/` (48 本): (a) 持って `docs/archive/v21/` へ / (b) 持たない (os32 を指す) / (c) INDEX の正典表が指すものだけ。**推奨: (a)** — 「なぜその実装か」は v3 でも問われ、SQLite の MEMSYS5 や V86 の設計は v3 P4 / P6 の入力。移すときは状態行を「完了記録 (YYYY-MM-DD)」に揃える (D19) | ユーザー |
| J5 | **`.claude/` を持つか** | skills (7 本): **持つ** (体制は同じ、パスにユーザー名なし)。`settings.json` (ask / deny): **持つ**。`settings.local.json`: 持たない (gitignore)。`os32-local-review` / `os32-local-ai` の Colab ホスト名は J7。**推奨: skills + settings.json を持つ** | PM (推奨どおりなら承認不要) |
| J6 | **`sample/` `tests/` `assets/manga/` の扱い** | `sample/` (VBZ 試験データ 7 + Gemini jpg 3): 使う試験 (`tools/tests/test_check_select.py`、`test_mk_settings_db.py`、`tools/img2vbz.py`) があるので **持つ**が、`.gitignore` の `/sample/` を消して出所 (`sample/README.OS32`: jpg は Gemini 生成、vbz はそこから変換) を書く。`tests/` (2): `userland/tests/` と重複するなら持たない (確認は a)。`assets/manga/*.MGX` (7): **元画像の出所が文書に無い** — 自作か生成画像なら出所を 1 行 (`assets/manga/README.OS32`)、第三者の画像なら差し替え。**推奨: sample は持つ、tests は a で確認、manga は出所をユーザーに確認** → **決着 (2026-09-30)**: ユーザーが「AI 生成と思われ、破棄してよい」と判断し削除。実際は `sample/*.jpg` 3 枚から `tools/img2mgx.py` で作ったものと同一 (7 本ともバイト列が一致) な。いったんビルド時に `build/out/manga/` へ作り直す形にしたが、同日にユーザーが「manga の実データは必要ない」と決め、生成も配備もやめた (`tools/img2mgx.py` だけ残す) | ユーザー (manga の出所) |
| J7 | **私設のサービス名 (`google-colab-qwen38` / `-a100`)** | `opencode.json` (baseURL)、`tools/review_local.py`、2 スキル。(a) そのまま (LAN 内の名前、実害なし) / (b) `.env` の `OLLAMA_HOST` に逃がし、`opencode.json` は持たない (opencode は使わない方針 — ROLES) / (c) `opencode.json` だけ持たない。**推奨: (b)** | PM |
| J8 | **CI の最初の範囲** | (a) 静的ゲート (check.yml 相当) だけで公開、本体ビルドは後 / (b) 静的ゲート + 本体ビルド (toolchain キャッシュ、core だけ) / (c) (b) + `SUBMODULE_TOKEN` で apps/game まで。**推奨: (b) を最初に、(c) はユーザーが secret を登録した時点で** — 本体ビルドが無いと「動く写しか」を CI が言えない。lychee の文書検査は (a) に含める | ユーザー ((c) の secret)、PM (範囲) |

---

## 5. この票でしないこと

- リポジトリの作成・push・GitHub の設定変更 (§3 c (5) と d の secret は [D2] / ユーザー操作)。
- コードの変更 (§2-4 P1 の 5 か所の修正も §3 a で行う。本票は一覧だけ)。
- 古い数字・番地の本文修正 (U9 — §3 f、fork 後)。
- HISTORY / CASE_STUDIES / THIRD_PARTY の本文 (§3 b で書く。ここは候補一覧)。
- os32 の履歴の書き換え (履歴は公開しないので不要)。
