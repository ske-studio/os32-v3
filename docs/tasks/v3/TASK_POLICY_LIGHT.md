# TASK_POLICY_LIGHT — 開発方針を軽くする (決まりの棚卸し・試験の 4 段・1 検査 1 ファイルのランナー)

> 状態: **実装中 (2026-10-31)** — 素案 r2 をユーザーが承認、要判断 G-1〜G-8 は推奨どおり。**main**: P1b・P1a R0〜R3 (5ab2b134、ランナーへ切替・旧 3 か所を削除、R2 は 200 コミットで 3 集合が一致)、P3(a) (安定 11 本を tier 4)、P2(a)(b)(c) (規則の ID 19→15・CONSTRAINTS 217→105・CLAUDE 103→74・POLICY_DEV 429→106・POLICY_DEBUG 1,590→244・スキル 12→7、事故の知識は設計書 41 本の落とし穴へ)。**P1 の所要は未判定**: 直近 10 件の取り込みの差分で測ると段 2 が 1 件 171 s、段 3 が 5 件 805〜1,239 s・段 4 が 4 件 1,381〜1,691 s (全件 rc=0、`~/os32-tmp/run/p1-measure/summary.tsv`) だが、段 3 の 5 件はすべて削除済みの旧 check_map.d が unmatched になったもの — 切替の後の取り込みで測り直す。次は P3(b) (切り出し試験の安定部分の削除) と P4 (置き場の移動)。
> 上位: 開発規約 [POLICY_DEV](../../POLICY_DEV.md)・体制 [ROLES](../agents/ROLES.md) §0。

## ユーザーの指示と決定 (2026-10-30)

- 「あまりルールが多すぎると制限が多すぎて身動きが取れなくなっている」「v3 のアーキテクチャ上すでに安定したものに関してのテストなどは外していくことを望みます」「テストのレベルを 4 段階ほどに分割…ソースのネスト 3 + 1」「Fable の素案を astra にレビューさせてください。astra はナーバスなので…私がある程度判断します」。
- 「どんな検査を入れるかのソースと検査そのものを分離したい」「検査リストを 1 ファイル 1 検査の体制で実行する形」→ 素案 r2 の §C-2。
- astra のレビュー 8 件 (R-1〜R-8) は全部採る、素案 r2 の要判断 G-1〜G-8 は推奨どおり (「OK」)。
- 素案は Fable が起草 (`~/os32-tmp/run/policy-review/DRAFT.md` → r2)、レビューは astra (`REVIEW.md`)。
- 2026-10-31: P3(b) の振り分け (108 本: 消す 0・tier 4 2・直す 83・そのまま 23、`~/os32-tmp/run/policy-p3/P3b_SURVEY.md`) について「**触るときに直す**」— 手本 1 本 (test_exec_image.py) だけ今直し、残りはその試験を触る段で §D の形に。P4 (置き場の移動) は「**見送る**」— 新しい検査だけソースと同じ置き場に。

- 2026-10-31 (PM の見つけたこと、未着手): `tools/checks/run.py` の `compiler_headers` は `gcc -MM` の結果のうち `.h` だけを拾い、`.inc` を捨てる。加えて旧 yaml から写した covers が `.inc` を手で並べているので、`.inc` を 1 本切り出すたびに宣言を何十本も追従させることになる (E01b で 51 本)。案: `.inc` も `-MM` から拾い、covers に `.c` を持つ検査は手で並べた `.inc` を外す — 200 コミットの同値を当たりの増加側に限って確かめてから。 → **2026-10-31 済み** (fb32ce6c、ユーザー承認): `.inc` を -MM から拾い宣言 130 本から 933 項目を外した (200 コミットで当たり ⊇、増加 5 件)。同時に、ビルド定義のコメントだけの差で段 4 にしない (基点があるとき)・ユーザーランドの中間物を .SECONDARY に (2 回目の make all が 804→329 行) — sol・terra 実装、astra レビュー。

## 本文 (素案 r2 の写し)


> 状態: **草案 r2 (2026-10-30)** — r1 (DRAFT.md) に astra のレビュー 8 件 (REVIEW.md、ユーザー「OK」で全部採る) と、ユーザーの新しい提案「検査を入れるかのソースと検査そのものを分離、1 ファイル 1 検査で実行」を反映した版。決定ではない。
> 書き方は r1 と同じ: 減らす提案を先に、足す仕組みは「それで何が消えるか」を併記。数字は実測 (出所は §H)、推測は [推]。D4 に従い docs/archive/・docs/hw/ は読んでいない。

## r1 からの直し (レビュー 8 件、どこをどう直したか)

1. **R-1 共有試験の段 4 送り** → §B-2 を試験名でなく**ファイル単位**にし、covers に活発な領域 (mm・isofs) を含む cd-read・dma-pool は段 4 の候補から外れる (基準の作りで自動に外れる)。
2. **R-2 allocator-bench** → 「性能計測」の削除候補から外す (`build/checks.d/check-allocator-bench-host.mk:2` が合否を持つ)。性能だけの検査の扱いは §G-3 に残す。
3. **R-3 領域ごとの安定扱い** → §B-1 の 2 をファイル単位 (covers の各ファイルの振る舞いの修正の数) にし、kbd.c (5)・serial.c (4)・fdc.c・pcm_cs4231.c・fm.c (各 3) は残る。候補を実測し直すと **11 検査・約 7 秒** (§B-2)。
4. **R-4 変更箇所だけのゲスト受入** → ROLES §4 (2) (`ROLES.md:86`) を §C の段 3 に 1 行で畳み、段 2 の票に host で見られない受入項目があれば段 3 へ上げる。毎回の一式は回さない。
5. **R-5 触った像を組む** → §C の段 1 の列に「`make kernel` + 触った像」(ROLES §0 :17 の文) を戻し、段 2〜3 は増分の `make all`。
6. **R-6 文書の適用の順** → §F で P2 (文書) の依存を P1 (道具) の後にし、新しい操作 (`--files`・`CC_TIER`) を書く文書は道具が入ってから。
7. **R-7 実機の調べの版** → §C の段 4 の「実機配備の前」を**通常版の配備**に限り、調べの版の短い流れ (ROLES §0 :17、2026-10-28) を段の表の註に残す。
8. **R-8 削除済み kstr-bench・本数** → §0 の数を直す: 検査は今 **237 本** (`check_lists`)、所要の記録は 2 つ (10-04 頃の 226 本 3,195 s は 38 本が消え 49 本が未計測、10-25 の 198 本 1,311 s)、kstr-bench は効果から外す。

## 0. 何が起きているか (実測、r2 で直した所だけ太字)

| 対象 | 数 | 読み方 |
|---|---|---|
| 規則の文書 | POLICY_DEV 429 行、POLICY_DEBUG 1,590、CONSTRAINTS 217、ROLES 126、SRC_MOVE 63、CLAUDE.md 103、AGENTS.md 48、skills 12 本 1,442 — 計 4,018 行 | ROLES §0 の「検査」の行 2,614 字に 3 つの流れ (3 段・Tx・実機の版) と承認の日付 6 件が混ざる |
| 検査の 3 か所 | **`make` の列 237 本** (`check_lists`)、build/checks.d **244** .mk、tools/check_map.d **241** .yaml **8,699 行** (平均 36 入力、82 本が `**` を含む)、tools/tests/test_*.py **313** 本 (mk から呼ばれるのは 181 本、残りは `host32_check` 経由や他の道具 63 本) | 1 検査が 3 ファイルに分かれ、`check_select.py` (1,565 行) が読み合わせる。mk の 43 本は recipe が 2 行以上、4 本は前提の目標を持つ (`check-ver-info-host`、`sdk.mk:174`) |
| 3 か所の追従 | 10-16 以降の非 merge コミットで check_map.d を触ったもの **253**、checks.d 131、tools/tests 491。yaml だけのコミットは **3** | 対応表は「試験を直すたびに一緒に直す」税。独立に価値を足した回数 (3) に対して 250 回の追従 |
| 所要 | 10-25 の記録 (`n8/check_times.tsv`、198 本、変異なし) 直列 **1,311 s**、181 本は 10 s 未満、静的 3 本 (c-dialect 154・p2v 108・arch-asm 69) で 331 s (25%)。古い記録 (226 本) は 3,195 s だが -j の重なりの下の値で、**38 本が今は無い (kstr-bench を含む)、49 本が未計測** | 今の 237 本の直列の所要は未計測 (記録は 198 本まで)。静的 3 本が 1/4〜1/3 は 2 記録で一致 |
| 選び方 | `plan()` の mode `sel` は `st = list(par)` = **全部を回し**、変異だけ当たりに絞る (`check_select.py:1402`)。**直近 60 コミット**: 文書だけ 18、`sel` 30 (当たり **中央値 16 本**、最小 2、最大 46)、`full` (安全側で全部) 4、ヘッダを含む 8 は未計測 | 当たりだけ回せば 237 → 16 本 (中央値)。待ちの主因はここ |
| 試験の形 | ソースを `read_text()` で読む 224 本、実カーネルの .c を組む 144 本、文字位置で切り出す 36 本、hash・行数の固定値 64 本。共有部品: host32.py (154 本が使う)・kernel_includes.py (144)・mutpar.py (80)・kernel_layout.py (19)・kernel_boot_source.py (14) | 313 本のうち `main` を持つ 305、`--mutate` を受ける 240、`MUTATIONS` 149、unittest/pytest 59。宣言 + `run()` への機械変換が利く形 |
| 振る舞いの修正 | kernel 層 297 ファイルのうち振る舞いの修正 0 が 165、2 件以下が 240 (beh.tsv)。**2 件以下かつ活発な領域 (exec・mm・init・kapi・v86・gfx・ime・video・module・serialfs・e82557・hwdev・isofs・hostdrv・ext2・fat・kdebug) の外は 112 本** | 安定はファイル単位で取れる。ただし検査の単位では covers に活発な 1 本が混ざるものが大半 (§B-2) |

結論の形は r1 と同じ: 規則は半分以下に (§A)、検査は「全部を毎回」から「宣言の段で選ぶ 4 段」へ (§C)、**選ぶ元を 1 検査 1 ファイルの宣言に 1 本化** (§C-2、新)、安定した部分は段 4 だけ (§B)、文字切り出しを書かない (§D)、増やさない決まり (§E)。

## A. 決まりの棚卸し

r1 §A-1〜A-7 のまま (CONSTRAINTS 19 → 14 ID、POLICY_DEV 34 → 15 節、POLICY_DEBUG 1,590 → 約 300 行、ROLES §0 約 60 → 約 25 文、skills 12 → 8 本、総行数 4,018 → 約 1,600 [推])。r2 で変わる点だけ:

- **ROLES §0「検査」の行** は §C の表 + §C-2 の 1 段落で置き換える。**実機の試験・調べの版の 1 文** (`ROLES.md:17`「ビルドが通ればそのまま実機に入れる … 既定の成果物が基点と同じこと・直接の試験の MUTATE=0」) は消さず、§C の表の註に移す (R-7)。
- **ROLES §4-1 の「対応表に `Makefile`・`build/*.mk` を直接書かない」** と `--lint` の説明は、対応表が無くなるので消える (§C-2)。
- **08_build §8-4 の検査の節・`sdk.mk:105-113` の註釈** は「3 か所 + check_select」の説明から「宣言 + ランナー」の説明に書き換える (P2 で、P1 の後 — R-6)。
- 文書の畳みで新しい操作 (`--files`、`CC_TIER`、`run.py`) に触れる箇所は、**その操作が main に入ってから**書く (R-6)。先に入れてよいのは経緯・日付の削除と写しの除去だけ。

## B. 安定した部分の試験を外す

### B-1 「安定」の基準 (ファイル単位、3 つ全部)

1. **票が受入完了で、実機 Ra266 で動いている版に入っている** (HANDOVER 2026-10-30: main 0366fd9c、selftest 1807/0)。
2. **そのファイルの v3 の「振る舞いを変えた」コミットが直近 1 か月で 2 件以下** (`beh.tsv`、件名から整理・移動・定数・文書・試験を除いた数)。
3. **外からの境目が凍結** — KAPI の版・部品の契約 (`*_MODULE_ABI_GEN`)・配置の世代に次の票で触れない (ROADMAP・HANDOVER §2)。

**検査を段 4 (tier 4) にできるのは、その検査の covers (§C-2) の .c/.asm/.inc の全部が 1〜3 を満たすとき**。1 本でも満たさないファイルを組む検査は段 4 にしない — これで cd-read (isofs/iso9660.c は 4 件)・dma-pool (mm/dma_pool_math.c は 2 件だが init/kernel.c・fdc.c を組む) は候補から外れる (R-1)。kbd.c 5・serial.c 4・fdc.c 3・pcm_cs4231.c 3・fm.c 3 を組む検査も外れる (R-3)。

### B-2 候補の実測 (今の yaml を covers の代わりに使った近似)

| 当たる検査 (11 本) | covers (kernel 層) | 所要 (10-25、変異なし) |
|---|---|---|
| pit-clock-host (7 本)・time-math-host (6)・cpu-calibrate-host (2) | kernel/time/* (全部 0〜1 件) | 各 0.6〜0.7 s |
| pci-decode-host・pci-bind-host | drivers/pci/* (0〜1 件) | 1.3 / 0.8 s |
| dma8237-host | drivers/dma/* (0〜1 件) | 0.8 s |
| ne2000-ring・ne2000-dma-host | drivers/net/ne2000/ne2000_ring.c・ne2000.c (0〜1 件) | 0.6 / 未計測 |
| kstring-c-host・kprintf-attr-host | lib/kstring_c.c・kprintf_attr.c (0) | 1.1 / 0.7 s |
| ver-collect-host | platform/pc98/* (0) | 未計測 |

合計 **約 7 秒**。r1 の「45 本・505 s」は (a) 領域ごとの扱い (R-3)、(b) 消えた kstr-bench 238 s (R-8)、(c) 共有試験 (R-1) の 3 つを含んでいた。**段 4 送りは待ちを減らす策ではなく、追従を減らす策**と位置づけ直す: 当たった検査 11 本は横断の整理 (DEP_DIR・SRC_LAYOUT の形) で毎回落ちて直される側で、段 4 なら節目にまとめて追従すればよい。
今の yaml は include の連鎖 (init/kernel.c・kselftest/** など) を手で並べているので真の covers より広く、§C-2 で covers が「実際に組む .c」に狭まれば候補は増える [推、R2 の変換の後に数え直す]。候補の一覧は PM が R2 で書き、月 1 の棚卸し (§E-4) で見直す。

### B-3 外し方の 3 択 (変更なし、当て先だけ直す)

| 扱い | 意味 | 当てる所 |
|---|---|---|
| 消す | 検査ファイル 1 本を削除 (3 か所でなく 1 か所) | fat 内部の切り出し試験、hash・行数の固定値だけの試験 (**allocator-bench は含めない** — R-2) |
| tier 4 | 宣言の `tier=4`。段 1〜3 では当たっても回さず `make check` (節目) で回る | B-2 の 11 本 + R2 で covers が狭まった後の追加 |
| 変異だけ外す | `MUTATIONS` を段 4 だけ (宣言の `mutate_tier=4`) | 分岐の少ない表の試験 (pci・irq/time)。本数は R2 で確定 |

守る最低限: ビルドと静的の差分検査は全段、ゲストの最小回帰は段 3、退行は段 4 と実機の回で拾う (r1 と同じ)。

## C. 試験の 4 段 (段 = 宣言の tier)

段は **変更が届く木の高さ** (変更ファイルの共通の親の深さ) で道具が出し、各検査は宣言の `tier` (その検査が回り始める最も浅い段) で選ばれる。`CC_TIER` をランナーが表示する。

| 段 | 何が変わったか | 組む | 回す検査 | 変異 | ゲスト・実機 | 誰・いつ・目安 |
|---|---|---|---|---|---|---|
| **1 ファイル** | 深さ 3 の葉 1 つ (drivers/video/geforce/*.c、kernel/ime/*.c) | `make kernel` + **触った像** (userland/… を触ったらその像、R-5) | **covers に当たる tier 1 の検査** + 静的 3 本を**変わった翻訳単位だけ** (`--files`) + `--lint` | なし | なし | 実装者、段ごと、3〜5 分 |
| **2 機能** | 1 つの機能ディレクトリに閉じる (ヘッダ含む) | 増分 `make all` | 段 1 の集合 + 当たる tier ≤ 2 + 静的 5 本 (差分) | 当たりに変異 | 票に **host で見られない受入項目があれば段 3 へ上げる** (R-4) | PM の取り込み (レビュー後)、5〜10 分 |
| **3 層** | 複数の機能・共有ヘッダ・層をまたぐ (include/・kapi/・mm/・exec/・lib/・platform/)、または当たりが無い変更 (unmatched) | 増分 `make all` | 段 2 の集合 + **tier 3 (共有: tick・kstring・memmap など) は当たらなくても回す** | 当たりに変異 | **最小回帰** (配備の照合・起動・kselftest・db_test、ROLES §4 (1)) + **変更箇所だけのゲスト受入** (票の host で見られない項目だけ、ROLES §4 (2) — R-4) | PM の取り込み (カーネル層の段)、15〜25 分 |
| **4 全体** | build/・Makefile・sdk/kapi.json・配置の世代・ツールチェーン・ランナー自体、**または節目** (票の終わり・**通常版の実機配備の前**・リリース) | `make all` (KAPI・世代なら `make clean` 先) | 全部 (tier 4 を含む) = `make -k check` | 全部 | ゲストの約束を一通り (ROLES §4 (3)) + 実機 | PM、節目に 1 回、専用の作業木 (check_wt.sh)、60〜90 分 |

- **段 0 (文書だけ)**: tier 0 の検査 (docs-links・orphans・status・tree・constraints) だけ。変えない。
- **実機の試験・調べの版は段 1 相当で実機へ** (ROLES §0 :17、ユーザー決定 2026-10-28、R-7): ビルド (既定と診断版) rc=0・既定の成果物が基点と同じ・直接の試験 MUTATE=0。段 4 は**通常版**の配備の前だけ。取り込みの検査は取り込みのときに段 2〜4 で。
- **Tx の流れ** (段ごと `make kernel` + 触った像、最後に候補の全部): 段ごとは段 1、最後の段の後に段 2 (当たりの全部、MUTATE=0) を実装者が 1 回。変えない。
- **今の 3 段との対応**: 文書だけ → 段 0、小さい取り込み → 段 1〜2、check-changed → 段 2〜3、節目の全体 → 段 4。ROLES §0「検査」の 1 セルと §4 のゲスト受入 3 段をこの表で置き換える。
- **unmatched の扱い**: 今は「対応表に無い → 全部を変異込み」(`check_select.py:1388`、直近 60 コミットの 4 件)。段 3 に落とし (tier ≤ 3 + 共有)、漏れは段 4 で拾う。
- **段 4 の頻度**: 票の終わりか週 1 の早い方 (G-2)。CI は check-fast のまま。

## C-2. 1 検査 1 ファイルとランナー (新、ユーザー提案 2026-10-30)

**何が問題か**: 1 検査が mk (回し方)・yaml (選ぶ元)・test_*.py (中身) の 3 か所に分かれ、選ぶ元が手書き 8,699 行。試験を直すたびに yaml が追従し (10-16 以降 253 コミット)、`check_select.py` 1,565 行が 3 か所を読み合わせて辻褄を検査する (`--lint`、`check-map` で 101 s)。

### C-2-1 宣言の書き方

検査ファイルは**自分が何を見て、いつ回り、何が先に要るか**を先頭で宣言し、`run()` を持つ。ランナーは宣言を `ast` で読む (import しない — 読むだけなら 313 本で数秒 [推])。

```python
### tools/checks/mm/dma_pool.py   (移す前は tools/tests/test_dma_pool.py の先頭に足す)
CHECK = dict(
    name="dma-pool-host",            # make check-dma-pool-host の名 (CI・スキルが呼ぶ)
    tier=1,                          # 1 直接 / 3 共有 (段 3 以上で当たらなくても回す) / 4 節目だけ / 0 文書
    covers=["mm/dma_pool_math.c", "mm/dma_pool.c", "lib/dma_contract.c",
            "drivers/dma/dma8237_math.c"],   # 組む・読むソース。ヘッダは gcc -MM で導く (書かない)
    needs=[],                        # 先に要る make の目標: "kernel" / "all" / "sqlite-module-proto"
    budget=15,                       # 秒 (変異なし)。超えたら段 4 の報告に載る (rc は変えない)
    runners=["native", "qemu"],      # host32 を使う検査だけ (HOST32_RUNNERS の既定、sdk.mk:150)
    mutate_tier=2,                   # 変異を回す最も浅い段 (省略時 2、安定部分は 4)
)

def run(ctx) -> int:
    """ctx.mutate (bool) / ctx.runner / ctx.files (静的検査が受ける変わった翻訳単位) / ctx.tmp"""
    ...
```

- 静的検査 (p2v・c-dialect・arch-asm・privileged・kprintf-window) は `covers=["**/*.c", "**/*.asm"]`・`scope="files"` で、`ctx.files` に**変わった翻訳単位** (変えた .h を include する .c は gcc -MM の逆引き) を受けて差分だけ見る。今は `--root` しか受けない (`check_p2v.py:247`、`check_arch_asm.py:56`)。段 4 では全部。
- 他の道具を呼ぶだけの検査 (`check-memmap` → `gen_memmap.py --check`、`check-shlib` → `mkshlib.py --check`、63 本) は、`run()` が subprocess で呼ぶ 10 行の検査ファイルにする。道具の本体は動かさない。
- 生成物を読む検査 (今の `artifact_readers:` 10 本) は `needs=["all"]` + covers に `build/out/**` の読む物。`--lint` の「読み手の登録漏れ」(`check_select.py:1150-1207`) は「covers に無い build/out を読んだ」の 1 検査に縮む。
- 1 検査に試験スクリプトが 2 本以上 (mk の 43 本、`check-ver-info-host` は 11 本の前提 + 2 本) は、`run()` が順に呼ぶか、`needs` に他の検査の名を書く (前提は 4 本だけ)。

### C-2-2 ランナーの選び方 (規則はこの 1 か所、`tools/checks/run.py`)

1. **列**: `tools/checks/**/*.py` (移行中は `tools/tests/test_*.py` も) の CHECK を読み、`--list` で make の列を出す。並び順は `budget` の大きい順 (今の `CHECK_PAR_ORDER` の手書きの番号 244 個が消える)。
2. **変更**: `git diff <基点>` か `--files`。`ignore`・`docs_only`・`notest`・`full` の 5 種の glob (今の `check_map.yaml` の全体方針 115 行 → ランナーの定数 約 40 行)。
3. **当たり**: 変更 ∩ (covers ∪ covers の gcc -MM ヘッダ)。ヘッダは今の `compiler_headers` (`check_select.py:963`) をそのまま持ち込み、結果を `build/out/` にキャッシュ。
4. **段**: 変更の共通の親の深さ → 1〜3、`full` の glob に当たれば 4。当たりが無ければ 3。
5. **回す**: `{c | c が当たり ∧ c.tier ≤ 段} ∪ {c | c.tier ≤ 段 ∧ c が「共有」}` (tier 3 と 0 は共有)。変異は `段 ≥ c.mutate_tier` の当たりだけ。
6. **先に組む**: 回す検査の `needs` の和集合を 1 回 `make`。
7. **出力**: 今の `check_changed.sh` と同じ `CC_STAGE1`・`CC_MUT1` + `CC_TIER`・`CC_NEEDS`。`make check-changed` (`sdk.mk:134-144`) の形は変えない。
8. **`--lint`**: 宣言のあるファイルだけが列に載る・covers のパスが実在する・`name` が重複しない・build/out を読むなら needs にある。数秒。

### C-2-3 今の 3 か所からの移し替え (機械変換 + 同値の確認)

- **変換の道具** (一時、`tools/checks/convert.py`、終わったら消す): mk の recipe → `run()` (`python3 -B tools/tests/X.py $(MUT)` → `main` の呼び出し、`host32_check` → `runners`、gcc の行 → subprocess)、mk の前提 → `needs`、yaml → `covers` (**.c/.asm/.inc/.py/データだけ残し、ヘッダ・自分の mk/yaml・`**` を落とす**)、`tier` は PM が渡す一覧 (既定 1、`docs_always` → 0、共有 → 3、§B-2 → 4、`broad` の静的 → `scope="files"`)、`budget` は 10-25 の記録 × 2 (未計測は 60)。
- **同値の確認** (PM、取り込みの前): main の直近 200 非 merge コミットについて `check_select.py --select --files <差分>` の `CC_MUT1` (= 今の当たり) と `run.py --select --files <差分>` の当たりを比べ、**全件で同じ**ことを見る。違いが出たら理由を 1 行ずつ (ヘッダを落とした・自己参照・`**` の過大) 読み、過小 (新で選ばれない) は covers に戻す。直近 60 件で 3 分だったので 200 件は 10 分 [推]。
- **置き場の移動は分ける**: 宣言とランナーが入り同値が確かめられた後に、`tools/tests/test_x.py` → `tools/checks/<ソースと同じパス>/x.py` (ハーネス .c と `_tdd.md` も一緒) を `move_files.py` で機能ディレクトリごとに (G-7)。224 本が `read_text()` のパスを持つので、参照の書き換えは道具で。

### C-2-4 make・CI・スキルとの関係

- `make check-<名>` は残る: `sdk.mk` の `include build/checks.d/*.mk` (`:160`) を **1 つのパターン規則** `check-%: ; python3 tools/checks/run.py --run $*` と `CHECK_PAR_TARGETS := $(shell run.py --list)` に置き換える。`check` / `check-fast` / `check-changed` / `check-par` の名と `MUTATE=` の意味 (`sdk.mk:114-118`) は変えない。
- CI: `check.yml` の `make check-*` 28 行と `build.yml` の `make check-fast` は**そのまま**。`gen_tests_inventory.py` (mk と check.yml を読む) は `run.py --list --json` を読む形に。
- スキル: `os32-delegate` :30・:34、`os32-land` :35 の `check_select.py` → `run.py` (5 か所)。`--pack` (依頼文の参照) は使っていれば `run.py --pack` に移す。`test_changes.py:11` の `TEST_PREFIXES` から check_map.d・checks.d を外す。

### C-2-5 消えるもの・残るもの

| 今 | 後 |
|---|---|
| `tools/check_map.d/` 241 本 8,699 行 | **消える** (covers が検査ファイルの中) |
| `build/checks.d/` 244 本 (+ `CHECK_PAR_ORDER` の番号) | **消える** (パターン規則 1 つ) |
| `tools/check_map.yaml` 115 行 | ランナーの定数 約 40 行 (5 種の glob と基準枝) |
| `tools/check_select.py` 1,565 行 | `tools/checks/run.py` 約 400 行 [推] (持ち込むのは `compiler_headers`・`make_narrow`・変更の列挙) |
| `test_check_select.py` 1,222 + `test_checkinfra.py` 526 行、`check-check-select-host` 73 s + `check-map` 101 s | ランナーの試験 1 本 約 300 行 [推]、10 s 以内 |
| 共有部品 `tools/tests/{host32,kernel_includes,mutpar,kernel_layout,kernel_boot_source}.py` | **残る** (`tools/checks/lib/` へ移すのは置き場の移動のとき) |
| ROLES §4-1「対応表に Makefile を書かない」、08_build の「対応表」の説明 | 消える |

### C-2-6 段の分け方と実装者

| 段 | 中身 | 担当 | 見積り |
|---|---|---|---|
| R0 | 設計票 (宣言の形・選び方の規則・同値の確かめ方・CC_* の互換)、design-rally 1 往復 | astra 起草 → Opus レビュー | 1 日 |
| R1 | `run.py` (--list/--select/--run/--lint) + `convert.py` + ランナーの試験。`check_select.py` は 1,565 行なので terra に出さない | sol | 2 日 |
| R2 | 変換の実行、tier の一覧 (PM)、200 コミットの同値、違いの読み | PM (道具を回す) | 1 日 |
| R3 | 切替: `sdk.mk` のパターン規則、CI の inventory、スキル 5 か所、`test_changes.py`、旧 3 か所の削除、08_build §8-4・ROLES §0 の書き換え | terra (機械的) | 1 日 |
| R4 (任意、後) | 置き場の移動 `tools/checks/<path>/` を機能ごとに `move_files.py` で | terra 並列 (SRC_MOVE L の型) | 2 日 |

R1 と S (静的 3 本の `--files`、§F) は触るファイルが衝突しないので並列。

## D. 試験の書き方

r1 §D のまま、3 を差し替え: **3. 手書きの対応表を書かない → 宣言の `covers` に「実際に組む・読むファイル」だけを書く** (ヘッダ・include の連鎖を並べない、ランナーが導く)。残り (切り出し 36 本を書かない・固定値を生成物から読む・変異は分岐ごと 10 本以内・乱数と時刻を使わない) は同じ。
新しく書く試験の約束 5 行の 2 行目を「**1 試験 = 1 ファイル = 1 宣言**、置き場はソースと同じパス (設計書と同じ)」に。段 1 の「設計書の試験節が列挙する直接の試験」(r1 §C) は **covers の当たりで置き換える** — 設計書に試験の一覧を手で持たない (docs/design に「試験」節は今 0 本、足さない)。
既存の扱い・製品内の試験の口 (撤去候補なし) は r1 と同じ。

## E. 決まりを増やさない仕組み

r1 §E の 6 項に 1 つ差し替え: **6. 試験を足す = 検査ファイル 1 本を足す** (宣言を持つ、§D の約束を満たす、`budget` を書く)。段 2 の所要が 10 分を超えたら `tier=4` へ送る候補を同時に挙げる。1〜5 (2 回目の事故で規則・ネットゼロ・経緯を書かない・月 1 棚卸し・HANDOVER の 3 数字) は同じ。3 数字は「規則文の数・検査ファイルの本数 (= `run.py --list | wc -l`)・段 3 の所要 (分)」。
これで消えるもの: POLICY_DEBUG §4 への追記、ROLES §0 の日付つき追記、**check_map.d の手書きと追従コミット (2 週で 253)**、`CHECK_PAR_ORDER` の番号。

## F. 進め方 (順序を R-6・R-8 で直す、§C-2 を足す)

| 段 | 中身 | 担当 | 見積り | 依存 |
|---|---|---|---|---|
| P0 | この r2 → ユーザー判断 (§G) → 票 TASK_POLICY_REVIEW | PM | 半日 | — |
| **P1a ランナー** | §C-2-6 の R0〜R3 (R4 は後) | astra → sol → PM → terra | 5 日 | P0 |
| **P1b 静的の差分** (S) | p2v・c-dialect・arch-asm に `--files` (変わった翻訳単位)。完了条件: 1 ファイルの差分で 3 本合計 30 s 以内 [推]、`--files` なしは今と同じ結果 | sol | 1 日、P1a と並列 | P0 |
| P1 の完了条件 | 直近 10 件の取り込みの差分で段 2/3 の所要 10 分以内、段 4 で回る本数が今の 237 と同じ (消した・足したを除く)、200 コミットの同値 | PM | — | — |
| P2 文書 (a)(b)(c) | r1 と同じ 3 本 (CONSTRAINTS・CLAUDE・AGENTS・SRC_MOVE / POLICY_DEV・ROLES・skills / POLICY_DEBUG の振り分け)。**P1 の後** (R-6)。経緯・日付・写しの削除だけは P1 と並列で先に出してよい | terra / PM + terra / sol 調べ → PM 計画 → terra | 半日 / 1 日 / 2 日 | **P1a** |
| P3 試験の置き換え | (a) §B-2 の tier 4 (宣言の 1 語) と削除 (b) 切り出し 36 本のうち安定部分を削除、残りを §D の形 — 手本 1 本 sol、同じ形を terra (c) V1・D1 の道具化 | (a) PM (b) sol → terra (c) sol | (a) 1 時間 (b) 3 日 (c) 1 日 | P1a |
| P4 置き場の移動 | §C-2-6 の R4 | terra 並列 | 2 日 | P3、G-7 |
| P5 | 月 1 棚卸しの 1 回目、3 数字 | PM | 1 時間 | P2 |

合計: P0〜P3 で約 13 営業日、並列で約 9 日 [推]。r1 の P4「対応表の自動化」は P1a に吸収 (G-3 の判断は不要に)。**待ちの削減は P1a の R3 (当たりだけ) と P1b で出る**: 237 本 → 中央値 16 本 + 静的 3 本の差分。段 4 送り (P3a) の時間の効果は約 7 s で、追従の削減が目的 (R-8)。

## G. 要判断 (8 件以内、推奨付き)

| # | 争点 | 選択肢 | 推奨 |
|---|---|---|---|
| G-1 | 安定の基準 (§B-1、ファイル単位・3 条件・covers の全部) | (a) 3 条件すべて / (b) 受入完了 + 実機だけ / (c) 人が一覧を決める | **(a)**。(c) は増やさない仕組みと相性が悪い。候補は R2 の後に数え直す |
| G-2 | 段 4 の頻度 | (a) 節目だけ / (b) 週 1 / (c) 票の終わりか週 1 の早い方 | **(c)**。段 1〜3 で回さない tier 4 が週に 1 度は回る保証 |
| G-3 | 合否を持たない性能計測 (allocator-bench は**含めない**、R-2) | (a) 削除 / (b) tier 4 / (c) 残す | **(b)**。該当は R2 の変換時に PM が名を挙げる (今の候補は font-* の計測のみ [推]、kstr-bench は削除済み) |
| G-4 | 未使用のローカル AI スキル 2 本 (247 行) | (a) 消す / (b) 残す / (c) 1 本に | **(a)**。直近 1 か月の使用の記録が無ければ。git log から戻せる |
| G-5 | POLICY_DEBUG §4 の事故の本文の行き先 | (a) 設計書の「落とし穴」節 / (b) アーカイブ / (c) そのまま | **(a)**。D4 で archive は読まれないので (b) は消すのと同じ |
| G-6 (新) | 宣言の置き場 | (a) 検査ファイルの先頭 `CHECK = dict(...)` / (b) 隣の .yaml / (c) ランナーの 1 表 | **(a)**。ユーザー提案どおり 1 検査 1 ファイル。(b)(c) は「選ぶ元と検査が別ファイル」のまま |
| G-7 (新) | 置き場の移動 (R4) の時期 | (a) R3 と同時 / (b) 同値の確認の後に機能ごと / (c) しない | **(b)**。同値を先に確かめてから動かす。224 本のパス参照は `move_files.py` で |
| G-8 (新) | 旧 3 か所 (check_map.d・checks.d・check_select) の削除 | (a) R3 で即削除 / (b) 1〜2 週間併存 | **(a)**。併存は二重保守 (yaml は試験のコミットの半分に乗る)。戻すなら `git revert` 1 つ |

## H. 実測の出所 (r2 で足した分)

- 本数: `python3 -c "import check_select as c; ..."` で `check_lists` = 237、`load_map()["checks"]` = 241・入力 8,699・平均 36.1・中央値 25・`**` 82。`ls build/checks.d | wc -l` 244。mk の分類: `grep -l "tools/tests/test_" build/checks.d/*.mk` 181、recipe 2 行以上 43、前提あり 4 (`sdk.mk:174`、`check-ver-info-host.mk`)。
- 所要: `~/os32-tmp/run/n8/check_times.tsv` (2026-10-25、198 本、合計 1,310.95 s、10 s 未満 181)。古い `barlen-impl/final-times.tsv` (226 本、3,194.93 s) は今の列と突き合わせて 38 本が無い (kstr-bench は `3937eca9` 2026-10-24 で削除)、49 本が未計測。
- 選び方: `check_select.py:1402` (`st = list(par)`)、`:1388` (unmatched → 全部)、`:963` (gcc -MM)、`:975-989` (`extract`・`effective_checks`)、`:1150-1207` (lint)、`:1534` (main)。直近 60 コミットの `plan()` の実行: 文書だけ 18・`sel` 30 (当たりの中央値 16)・`full` 4・ヘッダ含み 8 (未計測)。
- 追従: `git log --since=2026-10-16 --no-merges -- tools/check_map.d` 253、`build/checks.d` 131、`tools/tests` 491、yaml だけのコミット 3。
- 試験の形: `main` 305、`--mutate` 240、`MUTATIONS` 149、unittest/pytest 59、共有部品の使用数は `grep -l "import host32|from host32"` など (154/144/80/19/14)。
- 安定の候補: `check_map.d/*.yaml` の kernel 層の .c/.asm/.inc を全部 `beh.tsv` ≤ 2 かつ活発な領域の外で絞る → 11 検査 (B-2 の表)、所要は n8 の値。ファイル単位では 112/297。
- 規則の行: `ROLES.md:17` (検査の行、実機の調べの版の文)、`:86` (ゲスト受入 3 段の (2))、`:108` (T4 で踏んだもの)。`sdk.mk:105-118` (MUTATE)、`:134-144` (check-changed)、`:150` (HOST32_RUNNERS)、`:156` (host32_check)、`:160` (include checks.d)。`check_p2v.py:247`・`check_arch_asm.py:56` (`--root` だけ)、`check_c_dialect.py:526` (main)。`.github/workflows/check.yml` の `make check-*` 28 行。スキルの参照: `os32-delegate/SKILL.md:30,34`、`os32-land/SKILL.md:35`。`tools/test_changes.py:11`、`tools/gen_tests_inventory.py` 冒頭 (mk と check.yml を読む)。
- r1 の出所 (§H) はそのまま有効。
