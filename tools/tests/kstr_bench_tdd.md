# kstr_bench — 計測の枠組みの RED→GREEN の記録

票: [`docs/archive/portability/TASK_KSTRING_BENCH.md`](../../docs/archive/portability/TASK_KSTRING_BENCH.md)
対象: `userland/tests/kstr_bench.c` / ハーネス `tools/tests/kstr_bench_host.c` /
実行 `tools/tests/test_kstr_bench.py` (`make check-kstr-bench-host`)

## 0. 何を試験するか

**数字そのものではなく、数字の作り方**を試験する。速さの値はホストでは意味を
持たない (ホストの `get_tick` は贋物) ので、見るのは次の 6 つだけ。

| ID | 見るもの | 落ちたら困る理由 |
|---|---|---|
| B1 | 出力が `KSTR <関数> <asm\|c> <長さ> <ずれ> <ティック> <回数>` の固定書式で、最後が `KSTR DONE` | 集計 (`tools/kstr_bench_report.py`) が黙って別の欄を読む |
| B2 | 1 ケース 1MB 以上から始め、30 ティック未満なら回数を倍にする | 100Hz タイマの分解能を割った値で比を作ると表が嘘になる |
| B3 | 倍にするのは 5 回まで | 時計が止まっていると終わらない (ゲストで固まる) |
| B4 | 食い違いを注入すると `KSTR MISMATCH` が出て**その関数の計測が飛ぶ** | 違うものを比べた数字が表に載る (票 K1) |
| B5 | 13 本すべてが表にある — `.asm` の `global` / `kb_add` の表 / `programs.mk` の改名表 / 贋物の名前表の 4 者一致 | 1 本落ちても出力は正常に見えるので気付けない |
| B6 | 13 本それぞれに**宣言どおりの長さが実際に渡った** (贋物側の目撃記録) | 入力の組み損ないは両版が同じものを見るので MISMATCH にならず、表だけが静かに嘘になる |

加えて `[V2]` の登録 (`build/app.conf` / `userland/deploy.yaml`) と、
**実物の `lib/kstring_asm.asm` と `lib/kstring_c.c` を同居させた版**の通し
(受入 K1 のホスト側) を見る。

## 1. 同居のしかた (これ自体が試験対象)

`lib/kstring_asm.asm` と `lib/kstring_c.c` は 13 本すべてが同名なので、
素では 1 つの実行ファイルに入らない。`tools/tests/test_kstring_c.py` と同じ手で
`objcopy --redefine-syms` に接頭辞を付けさせ、アセンブリ版を `a_*`、C 版を `c_*`
にする。**写しは作らない** — ゲストでもホストでも出荷するソースそのものを測る。

ゲスト側の改名は `build/programs.mk` の `KSTR_BENCH_FUNCS` が管理元。

## 2. RED→GREEN

`--mutate` は実物の `userland/tests/kstr_bench.c` をわざと壊し、**指名した台本が
落ちる**ことを見る (台本を指名するのは、毎回 6 台本を回すと時間が伸びるから)。

| 変異 | 壊し方 | 落ちる台本 | 結果 |
|---|---|---|---|
| `no_double` | `KB_MAX_DOUBLE` を 0 に (倍にしない) | `cap` | RED |
| `small_target` | `KB_TARGET_BYTES` を 16KB に | `double_once` | RED |
| `no_skip` | 食い違った関数も測る | `mismatch` | RED |
| `table_too_small` | `KB_FN_MAX` を 12 に (13 本目が黙って落ちる) | `format` | RED |
| `swap_ticks_reps` | ティックと回数の欄を入れ替える | `double_once` | RED |
| `short_write_lost` | short write を「全部書けた」ことにする | `short_write` | RED |
| `ret_only` | 宛先バッファの突き合わせを 0 バイトに | `mismatch` | RED |
| `stale_nul` | 前に植えた NUL を戻さない | `format` (目撃記録) | RED |

8 本とも**実行時に**落ちる (コンパイルが通らないだけの RED は残していない —
それでは試験の目が試験されないため)。

`stale_nul` は**実際に踏んだ欠陥**を票にしたもの。最初の版は `kb_src` に
ケースごとの終端 `0` を植えっぱなしにしていた。短いケースの `0` が残るので、
256KB の文字列を測っているつもりで実際には 4 バイトの文字列を測る。
両版とも同じ壊れた入力を見るので MISMATCH にはならず、`REAL` 台本も緑のままだった。
受け手として贋物側に「渡された長さ」の目撃記録 (B6) を足し、それで RED にしてから
`kb_plant_nul` で直した。

変異なしの通し: 静的 3 件 + 台本 6 件がすべて GREEN (`failures=0`)。

## 3. 台本

| 名前 | 贋の時計 | 見るもの |
|---|---|---|
| `format` | 1 呼び出し = 1 ティック | B1 / B5 / 長い長さで 3 回倍になる |
| `double_once` | 1 呼び出し = 4 ティック | B2 (256K だけ 1 回倍、短い長さは倍にならない) |
| `cap` | 止まったまま (0) | B3 (回数が 32 倍で頭打ち) |
| `mismatch` | 1 呼び出し = 1 ティック | B4。戻り値だけ違う版 (`kstrlen`) と、戻り値は同じでバッファが違う版 (`kmemcpy`) の 2 通り |
| `short_write` | 1 呼び出し = 1 ティック | `sys_write` が 1 バイトずつしか受けなくても 1 バイトも落とさない |
| `real` | 1 回 40 ティック固定 (倍化しない) | 実物の 2 版が 13 本 × 7 長 × 2 ずれ で一致 (受入 K1 のホスト側) |

`format` だけは stderr の目撃記録 (`WITNESS <名前> <7 通りのビット> <表に無い長さの回数>`)
も見る (B6)。

## 4. 試験していないこと ([V4])

- **実機 (NP21/W) では 1 度も走らせていない。** 速さの数字はまだ 1 つも無い。
  票 §2 の「2 条件 (既定 / 高速)」も、受入 K2 (3 回測ってばらつき 10% 以内) も、
  K3 (表が埋まる) も未着手。
- ホストの `real` 台本の**時間の数字には意味が無い** (`get_tick` は固定歩幅の
  贋物)。ここで確かめたのは一致と書式だけ。
- `make check` / `make all` / `make external` はこの作業では回していない
  (`make programs` の単体ターゲット `make kstr_bench` だけ)。
- ゲストでの所要時間は**見積もりしかない**。1 ケースが最低 30 ティック (0.3 秒)、
  計測は 364 ケースなので下限 110 秒、倍化と照合を入れて数分〜十数分を見込む。
  実行の待ち時間は短く切らない ([V3] は 60 秒以上)。
