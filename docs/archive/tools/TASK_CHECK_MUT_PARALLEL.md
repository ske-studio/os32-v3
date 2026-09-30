# TASK_CHECK_MUT_PARALLEL — `make check` の変異試験を並列にする

> 状態: **受入完了 (2026-09-26)** — 変異試験を全部写しの木へ移し、`check-mut` の段と `-j1` を廃止。(2026-09-29 の棚卸しで更新)
>
> それまでの状態: **実装済み**。check-par の重い 6 本 (案 4) は 2026-09-26 に実装した (コーダー
> Claude Code `claude-opus-5-5`、worktree `wt/mut-parallel`)。check-mut の 14 本 (§5) も同日に全部
> 写しの木へ移し、`check-mut` の段と `-j1` を消した (コーダー `claude-opus-5-5`、worktree `wt/check-narrow`、§5-1)。
> 発行: PM (Claude Code `claude-opus-5-5`)、2026-09-26。承認: ユーザー 2026-09-26 (案 4)。
> 関係: [`docs/POLICY_DEBUG.md`](../../POLICY_DEBUG.md) §4-40 (打ち切りで変異が残る)・§4-41 (2 段に割った経緯)、
> `build/sdk.mk` の `check:` / `check-par:` / `check-mut:`、共通部 `tools/tests/mutpar.py`。

## 0. 背景

16 コアの機械で `make check` の後半がほぼ 1 コアで回っている (ユーザーの指摘、2026-09-26)。
測ると、1 段目 `check-par` (497 秒) の wall は重い 6 本の試験で決まっていた。どの試験も変異を
**一時ディレクトリの写し**に当てているので並列にしてよいのに、変異を 1 本ずつ逐次に回し、変異ごとに
ハーネスを全部組み直し、落ちた後も残りのケースを全部流していた。

測定の生データ (PM、2026-09-26): セッションの scratchpad `m/results.tsv`・`m/logs/`。

| 目標 | 試験 | 変異の数 | 単独の時間 | 変異 1 本の作り直し |
|---|---|---|---|---|
| check-hdd-stage2-host | `test_hdd_stage2.py` | 128 + 対照 5 | 228 秒 | pure / cdi / ins / mini の 4 本 |
| check-cd-read-host | `test_cd_read.py` | 61 + 対照 1 | 172 秒 | ASan 付きで read / wide / pkg の 3 本 |
| check-serialfs-host | `test_serialfs.py` | 86 (C 80 + Python 6、対照を含む) | 108 秒 | 1 本 (ファイルで選ぶ) |
| check-vfs-fd-path-host | `test_vfs_fd_path.py` | 60 + errno 4 + IME 12 + SQLite 7 | 93 秒 | FatFs (ff.o) + ハーネス |
| check-hdd-stage1-host | `test_hdd_stage1.py` | 61 + 対照 9 | 60 秒 | pure / part の 2 本 |
| check-vk32-crc-host | `test_vk32_crc.py` | 38 + 対照 1 | 35 秒 | 2 幾何 × (C 2 + ASM 2 + ハーネス) |

## 1. 実装 (案 4)

### 1-1. 共通部 `tools/tests/mutpar.py`

- `jobs()` — 並列度。環境変数 **`OS32_MUT_JOBS`** (1 以上の整数、1 なら逐次)、既定は `os.cpu_count() // 2` (§3)。
- `run_ordered(fn, items, processes=False)` — 変異 1 本を `fn(item)` として並列に回し、結果を
  **変異の番号順に**返す (`Executor.map`)。出力は順に印字するので、行の順序と書式は逐次のときと同じ。
  既定はスレッド (中身は gcc と実行ファイルの subprocess)。変異 1 本の中で Python の計算が重いもの
  (`test_vk32_crc.py` は像を組んで 43 通りに壊す、`test_serialfs.py` の Python の変異はモジュールを
  読み込み直す) は `processes=True`。スレッドだと GIL で詰まり、vk32 は 8 並列でも 33 秒のままだった。
- `gcc_deps(cmd, root)` / `check_rebuild_table(table, deps, files)` — §1-2 の表を `gcc -MM` の依存と
  突き合わせる。表が依存より**狭い** (変異を当てたファイルに依存するハーネスを組み直さない) と
  `REBUILD TABLE STALE: …` を出して試験を落とす。広いのは害が無いので通す。
- 変異を回し始める前に、プロセスの優先度を 1 回だけ下げる (**`OS32_MUT_NICE`**、既定 +10、0 で下げない)。
  スレッドも子プロセス (gcc・ハーネス) も受け継ぐ。変異は後回しにしてよいバッチの仕事 (§4 の lan-bridge)。

各変異は自分専用の `tempfile.TemporaryDirectory` (または `tmp/mut<i>`) に写して組む。写し同士・実物の
ソースは共有して書かない (実物は読むだけ)。共有するのは、実物で 1 回組んだ実行ファイル・`ff.o`・
`sqlite.o` と、読むだけの入力 (像・PKG) だけ。

### 1-2. 作り直しを絞る表 (各スクリプトの `REBUILD`)

変異を当てたファイル → 組み直して回すハーネス。**ほかのハーネスは実物で組んだもの (main の 1 回目で
全ケースが通ったもの) をそのまま使い、そのケースも回さない** (同じ実行ファイルに同じ入力なので結果も同じ)。
**表に無いファイル (ヘッダなど) はハーネスを全部組む** (安全側)。表は変異の前に `gcc -MM` と突き合わせる。

| 試験 | 表 |
|---|---|
| hdd-stage2 | `inst_disk.c` → pure・cdi・ins / `inst_hdd.c` → cdi・ins / `cdinst.c` → cdi / `install.c` → ins / `boot/ext2_mini.c` → mini |
| cd-read | `drivers/atapi.c`・`fs/iso9660.c` → read・wide (+ replay) / `userland/lib/rt/pkg.c` → pkg (+ replay)。replay は pkg の記録を read で再生するので、どちらかを組み直したら回す |
| hdd-stage1 | `pc98pt.c`・`ext2_layout.c` → pure・part / `ide_addr.c`・`hdprep_plan.c` → pure / `ext2_super.c`・`ext2_fmt.c` → part |
| vfs-fd-path | 変異を当てる 8 ファイル (fs/vfs.c ほか) → ハーネスだけ。FatFs (`fs/fatfs/ff.c`) は実物で 1 回組んだ `ff.o` を使う (変異は FatFs に当たらない) |
| serialfs | 従来どおりファイルで 1 本を選ぶ (`drivers/serial.c` → gate、`fs/serialfs_session.c` → session、ほか → serialfs_host)。表の形にはしていない |
| vk32-crc | 絞っていない。ハーネスは幾何ごとに 1 本で、組み立ての大半は小さい (並列とプロセス化で足りた) |

組み直すハーネスが複数あるときは **1 本ずつ組んでは回し、落ちたら後のハーネスは組まない**
(cd-read は read → wide → pkg → replay、hdd-stage2 は pure → cdi → ins → mini、hdd-stage1 は pure → part)。

### 1-3. 最初に RED になったケースで打ち切る

変異は RED かどうかだけ分かればよいので、最初に落ちたケースで残りを流さない。

- hdd-stage2 / cd-read / hdd-stage1 / serialfs (C・Python) — ケースの列の途中で抜ける (`first_fail`)。
- vfs-fd-path — ハーネスは 1 プロセスで全段を回すので、選択子 **`+first-fail`** を足した
  (`tools/tests/vfs_fd_path_host.c` の `run()`: どこかの段で落ちたら段を打ち切る)。e2fsck も最初に汚れた像で止める。
  IME / SQLite の変異は 1 本の実行ファイルのまま (従来どおり)。
- vk32-crc — もともと最初の `Fail` で止まる。
- hdd-stage1 の Python の変異 (`--py` の子プロセス) は中身が 1 本の検査なので打ち切りは入れていない。

### 1-4. serialfs の変異のケースの時間上限

C16 「静まるのを待つ口に上限が無い」は止まらない変異で、ケースの時間上限 (60 秒) でしか RED にならない。
並列にしても serialfs の wall がこの 1 本の 60 秒で決まるので、**変異のケースだけ上限を 20 秒**にした
(`MUT_CASE_TIMEOUT`)。実物のケースは長いもので 0.44 秒 (`rshell_watchdog_junk`) — 20 秒は 45 倍。
実物のケース (変異でない) の上限は 60 秒のまま。

## 2. 結果 (2026-09-26、16 論理 CPU の WSL2)

測定の間、別の worktree で `make check` が走っていた時間帯がある (load average 4〜25)。数字は ±30% ほど揺れる。

| 試験 | 前 (単独) | 後 (単独、既定 8 並列) | 後 (単独、4 並列) |
|---|---|---|---|
| hdd-stage2 | 228.3 秒 | 20.7 秒 | 27.2 秒 |
| cd-read | 172.0 秒 | 19.5 秒 | 26.0 秒 |
| serialfs | 108.0 秒 | 32.3 秒 | 33.1 秒 |
| vfs-fd-path | 93.0 秒 | 26.2 秒 | 31.7 秒 |
| hdd-stage1 | 59.6 秒 | 11.6 秒 | 30.6 秒 (揺れ) |
| vk32-crc | 34.7 秒 | 9.1 秒 | 34.0 秒 (揺れ) |

「前 (単独)」は PM の `results.tsv`。「後」は `--target --mutate` (vk32 は `--real` も) の recipe そのまま。

**6 本を同時に走らせた wall** (`make -j` の下に近い形):

| | wall | いちばん遅い 1 本 |
|---|---|---|
| 前 | 405.5 秒 | hdd-stage2 405.5 秒 (cd-read 320、vfs 192、serialfs 140、hdd-stage1 126、vk32 77) |
| 後、`OS32_MUT_JOBS=4` | 64.4 秒 | vfs-fd-path 64.3 秒 |
| 後、既定 (8) | 62.4 秒 | vfs-fd-path 62.4 秒 |
| 後、`OS32_MUT_JOBS=16` | 60.2 秒 | vfs-fd-path 60.2 秒 |

6 本同時では CPU が埋まり (6 本の CPU 時間の合計 ≈ 16 CPU × 60 秒)、並列度 4〜16 で差がほとんど無い。
単独では 4 並列が 8 並列より 3〜6 秒遅い。

`make check` 全体 (この worktree、`build/sdk.mk` は変えていない): **350 秒、rc=0** (12:04〜12:10、開始時の
load average 24 — 別の worktree の作業と重なった)。前は check-par 497 秒 + check-mut 103 秒 = 600 秒 (PM の測定)。
check-par の wall はいま **check-pcm-cs4231-host (単独 248 秒、本票の対象外)** で決まる — 重い 6 本は 6 本同時でも
60 秒台で終わる。

## 3. 並列度の既定値

`os.cpu_count() // 2` (この機械で 8)。理由: 6 本が同時に走ると 6 × 8 = 48 スレッドで 16 CPU を
3 倍に過剰に割るが、実測の wall は 4 並列 (24 スレッド) と変わらず (62.4 / 64.4 秒)、単独で走るとき
(`make check-hdd-stage2-host` だけ回す、または `check-par` の終わりに 1 本だけ残る) は 4 並列より速い。
16 並列は 6 本同時で 2 秒速いだけで、単独の試験が CPU を全部取る。機械に合わせて `OS32_MUT_JOBS` で変える。

## 4. 判定と出力が今までと同じであること

- 6 本とも、変異の行 (`MUTATION …` / `MUT nn …` / `MUTANT n …` / `IME MUTANT` / `SQLITE MUTANT` /
  `ERRNO MUTANT` / `CONTROL …`) と集計の行を前後で突き合わせ、**同じ順・同じ判定**だった
  (RED: hdd-stage2 128、cd-read 61、serialfs 83 (+ 対照 3)、vfs-fd-path 60 + errno 4 + IME 12 + SQLite 7、
  hdd-stage1 61、vk32-crc 38。SURVIVED 0、ERROR 0、対照すべて期待どおり)。
- 違いは serialfs の `MUTATION C12 RED (1 件)` の「件」だけ — 打ち切るので落ちたケースの数は常に 1
  (前は 16 行で 2〜14 件)。書式は同じ。
- 1 本ずつ組んでは回す (§1-2) ので、前のハーネスで RED になった変異は後のハーネスを組まない。
  後のハーネスでだけ組めない変異があれば、前は ERROR、今は RED になる。いまの変異にそういうものは無い
  (ERROR 0 件)。
- **test_lan_bridge.py の変異 5 (SIGUSR1 のハンドラから直に print) が見逃しになった** — 再入の競合は確率で
  踏むもので、静かな機械では 12 回中 12 回踏むが、CPU が埋まると 12 回中 3〜5 回に落ち、`make check` では
  2 回続けて 1 回も踏まなかった (変異を並列にして check-par の最初の 1 分に CPU が埋まるようになったため)。
  変異のときだけ、どのケースも落ちなければ再入のケース 1 本を最大 30 回回し直す (1 回 0.2〜0.8 秒、1 回でも落ちれば
  RED、`RACE_RETRIES`)。実物の側は従来どおり 1 回で通ること。あわせて変異の優先度を下げた (§1-1)。
  直した後の `make check` で RED (1 件)。
- `REBUILD TABLE STALE` が出たら表を直す (依存が増えた: 例えばハーネスが新しい実物を `#include` した)。
  表を 1 行狭めて STALE で落ちることを確かめてある。
- 作業ツリー: 各試験の前後で `git status` に残留なし。

## 5. 残件: check-mut の 14 本

2 段目 `check-mut` は `-j1` で、14 本は**実物のソースに変異を当てて戻す**作りなので同じ作業ツリーで
並べると互いの変異がぶつかる。すでに多くの試験は一時ディレクトリの写しに変異を当てる作りへ移してあり
(`build/sdk.mk` の「写しの上で変異させるので並列 (check-par) で回せる」の注記、例: `test_hsync_h1.py`)、
残りの 14 本を同じ作りに変えれば `check-mut` の段そのものが要らなくなる。

対象 (2026-09-26 の `check-mut`):
`check-kapi-layout-host` `check-edit-doc-host` `check-fstat-redir-host` `check-kstring-c-host`
`check-kstr-bench-host` `check-sh-status-host` `check-hsync-h3-host` `check-hsync-h2-host`
`check-h4-manifest-host` `check-vfs-excl-host` `check-fs-kind-callers-host` `check-cat-linenum-host`
`check-result-conv-host` `check-guest-host`

やること:

1. **まず測る**: 14 本それぞれの所要時間を測り、長い順に並べる。効果の大きいものから移す。
2. 各試験を「写しに変異を当てる」作りへ: 変異が触るファイルと、ビルドに要る最小の木を
   `tempfile.TemporaryDirectory` に写し、その中で変異・ビルド・実行する。**実物のソースは読むだけ**。
   変異を並べるのは `tools/tests/mutpar.py` (§1-1) を使う。
3. 移したものは `check-mut` から `check-par` へ。全部移ったら `check-mut` の段と `-j1` を消す
   (`check_tree_unchanged.py` の番人は残す — 写しの作りが崩れて実物を書いたら 1 段目で捕まる)。
4. `check-par` 自身の並列度が効いているか (`make -j` が下位の make に渡っているか) も確かめる。

受け入れ:

- C1: `make check` の所要時間を移行前後で測って票に残す (同じ機械、同じ木)。
- C2: 変異の結果 (RED の本数、SURVIVED 0) が移行前と同じ。
- C3: `make check` を 2 つの worktree で同時に流しても、どちらも通り、どちらの木にも変異が残らない。
- C4: 途中で打ち切っても実物のソースに変異が残らない (§4-40 の罠が構造上なくなる)。

ほかの残り:

- serialfs (単独 32 秒) の残りの大半は実物の結合試験 (6 秒) と C16 の 20 秒の上限。
- vfs-fd-path の変異 1 本の大半は写しの `fs/` 全体の複写とハーネスのコンパイル。6 本同時の wall はいまこれで決まる。
- check-par の wall を決めているのは check-pcm-cs4231-host (単独 248 秒)。本票では触っていない。

### 5-1. 実施 (2026-09-26)

**共通部** (`tools/tests/mutpar.py` に追加):

- `overlay(root, dst, real)` — 写しの木。`real` (ファイルかディレクトリ) とその祖先のディレクトリだけ実体で、
  ほかの項目は実物への symlink。symlink のディレクトリから `..` で辿る `#include` は実物へ出るので、
  C の試験は **gcc -MM の依存を全部実体にする** (`mutant_tree(..., gcc_cmds=)`)。
  **写しに `.git` は置かない** (2026-09-26、代行レビュー P3): どの階層の `.git` も symlink にも複写にもせず、サブモジュールは
  実体のディレクトリにして中身を symlink にする — 写しの中で走る git の書き操作が実物の repo に届かない。`real` のディレクトリは
  git の追跡ファイル + 追跡外 (gitignore を除く) だけを写す (生成物を毎回複写しない。名指しのファイルは生成物でも写す)。
- `build_in_tree` (C: 写しの中で gcc を流す)、`run_script_in_tree` (Python: 試験スクリプト自身も実体で写し、
  `ROOT = Path(__file__).resolve().parents[2]` が写しを指すようにして流し直す)。
- `run_with_control` — **変異なしの写し**を変異と同じ関数で回し、GREEN であることを確かめる
  (`CONTROL 変異なしの写しの木 GREEN`)。写しの作りが壊れて全変異が別の理由で RED になるのを見逃さない。

**1 本ずつの時間** (移す前、`make check-<名前>-host MUTATE=1` を逐次、長い順):

| 試験 | 前 (秒) | 移し方 |
|---|---|---|
| sh-status | 23.7 | C。2 通り (常駐 / sh.bin) を写しで組む |
| kstr-bench | 20.7 | Python 流し直し (`--mut-scenario <台本>`、写しの中で指名した台本だけ) |
| h4-manifest | 9.8 | C (h4_manifest_host) + Python 流し直し (test_hostdrv_manifest) |
| hsync-h2 | 9.4 | C |
| kapi-layout | 9.0 | Python 流し直し。exec/ sdk/ include/ kapi/ を実体に (gen_kapi.py が cwd 相対で書く) |
| edit-doc | 8.4 | Rust。`#[path]` の 4 本を実体にして写しの木から組む |
| result-conv | 5.1 | Python 流し直し (`--mut-once`)。run_*.c の gcc -MM の依存を実体に |
| hsync-h3 | 3.0 | C |
| kstring-c | 2.1 | C (写しの lib/kstring_c.c を組む) |
| vfs-excl / fstat-redir / cat-linenum / fs-kind-callers | 1.6 / 1.4 / 1.4 / 1.2 | C |
| guest | 0.7 | Python 流し直し (`--self`) |

逐次の段の合計は約 98 秒。どれも移した後は `check-par` の中で並列に走り、段の wall を決めない。

**結果** (同じ機械・同じ木、16 論理 CPU の WSL2):

- C1: `make check` **238.5 秒 → 128.6 秒** (rc=0)。`make check-fast` 32.6 秒。
- C2: 14 本 (+ test_hostdrv_manifest) の `MUTATE …` 行を前後で突き合わせ、**同じ行・同じ判定** (RED 173 行の
  集合が一致、見逃し 0、SKIP 0)。違いは各試験に `CONTROL …` の 1 行が増えたことだけ。
- C4: どの試験も実物のソースを書かないので、打ち切っても変異は残らない。番人 (`check_tree_unchanged.py`) は
  `check:` の段の前後に残した。前後の `git status` に変異の残留なし。
- C3 (2 つの worktree で同時に `make check`) は測っていない。

## 6. しないこと

試験の中身 (何を確かめるか) は変えない。変えたのは変異を回す順・場所・打ち切りと、serialfs の変異のケースの時間上限 (§1-4)、
lan-bridge の変異の回し直し (§4) だけ。
