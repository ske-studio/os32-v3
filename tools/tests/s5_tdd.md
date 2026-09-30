# S5-C — `cfg_bench` と `cfg status` の pool 表示のホスト TDD 記録

票: [`docs/archive/settings/TASK_S5.md`](../../docs/archive/settings/TASK_S5.md) 第 1 版 §0 の **S5-C**。
契約の正典は [`S0_FOUNDATION.md`](../../docs/archive/settings/S0_FOUNDATION.md) §2、
[`DESIGN.md`](../../docs/tasks/settings/DESIGN.md) §6 (実測項目)、
API は `userland/lib/cfg/libos32cfg.h`。RED→GREEN はこの記録が正典。

走らせ方:

```bash
python3 -B tools/tests/test_cfg.py                 # 45 本 + TSV parity 58 本
python3 -B tools/tests/test_cfg.py --target        # + i386-elf -Werror コンパイル
python3 -B tools/tests/test_cfg.py --sanitize s5_pure s5_bench s5_pool cmd
```

コーダーの範囲はホスト TDD までなので、**票 §2 の実測 (M1〜M6) と §3 の回帰 (R1〜R4) は
未実行** ([V4])。`make all` / `make check` / 配備 / エミュレータには触れていない。
走らせたのは上の 3 本と、対象を絞った `make build/out/lib/libos32cfg.a sdk/crt/*.o`
(リンク確認のため) だけ。

---

## 0. 組み方

| | |
|---|---|
| 対象 (1) | `userland/tests/cfg_bench.c` — CPL=3 の OS32X。`main` が最初の関数、`libos32cfg` を静的リンク |
| 対象 (2) | `userland/cmds/cfg.c` — `cfg status` の末尾に ` pool <db_mem_used> B` |
| ハーネス | 既存の `tools/tests/cfg_host.c` を流用。実 SQLite + 実 `os32_sqlite_vfs.c` + 実 `kapi_db.c` + 実 `libos32cfg` + 実 `cfg.c` の上に **実 `cfg_bench.c` をそのまま載せる** (`#define main bench_main`) |
| 贋物 | `host_api.db_mem_used = kapi_db_mem_used` — 実 `sqlite3_memory_used()` なので**実値**。`host_api.version = 50`。`host_gettick` に `host_tick_step` を足し、S5 の試験だけが tick を進める (既定 0 なので S2 までの見え方は不変) |
| 命名 | `cfg.c` と同じ翻訳単位に載るので、`cfg_bench.c` の静的関数は `bn_` / `bo_` 接頭辞で衝突を避けた |

新しい case は 3 本 (`test_cfg.py` の `CASES` に登録済み):

| case | 見るもの |
|---|---|
| `s5_pure` | `cfg_bench` の純関数 — 引数解釈、引く先の巡回、失敗の数え方、集計 (min/max/avg/peak/end)、整形 |
| `s5_bench` | `cfg_bench` の通し — 実 DB の上で読み / 書きを回し、**yield しないこと**・プールが戻ること・終了コードを見る |
| `s5_pool` | `cfg status` の ` pool <n> B` と符号なし整形 `fmt_u32` |

既存 case `cmd` の C2 の期待値も `"OK schema_version 1"` → `"OK schema_version 1 pool "` に
追従させた (追記が消えたら `cmd` も落ちる)。

---

## 1. 仕様として固定したこと

票の文面のうち、実装で判断が要った点:

| # | 決めたこと | 理由 |
|---|---|---|
| A | int の get は `cfg_read_int` + 既定値の埋め戻しで**その場に `cfg_get_int` を展開**した | 票の失敗の数え方は「get の負 (NOTFOUND は数えない)」。`cfg_get_int` は値を返す口なので負の戻りが**エラーではなく値**になり、数えられない。`cfg_get_int` は `cfg_read_int` の 1 行のラッパ (`libos32cfg.c:612`) なので引く手間は同じ |
| B | 「無いキー」は `gshell nosuch/key`、4 件に 1 件 (`j % 4 == 3`)。残りは 3 キーの巡回 | m=20 で 無いキー 5 件 / 各キー 5 件 (`s5_pure` で固定) |
| C | `iter` 行は 10 回ごと **と最終回** | 票は「10 回ごと」だが、`cfg_bench 3 8` のような短い測り方で 1 行も出ないのは実測票として使えない |
| D | プールは close の**直前** (接続を抱えている間) と**直後**の 2 点を読み、ピークは両方の最大、end は最後の直後値 | close 後だけだとピークが取れない (RED R7)。start はループに入る前の 1 回 |
| E | avg は四捨五入。`total` と `n` も同じ行に出す | tick が粗いので、PM が生値から再計算できるようにする |
| F | `status` 行は数値と名前の両方 (`status 0 OK`) | 票は `<cfg_status>` (数値)。名前は人が読む用 |
| G | `cfg status` の pool は**状態に関わらず**末尾に付け、**接続を持っている間**の値を読む | `MISSING` でも FEP 辞書などの常駐分が見える。close 後だと素の値に戻って「この DB を開くのに何 B 要るか」が消える |
| H | 符号なし整形を `fmt_u32` / `out_unum` として新設 | `db_mem_used` は `u32`。既存の `fmt_int` に通すと 2GB 超が負に化ける (RED R9) |

---

## 2. 出力の書式 (固定)

```
cfg_bench read n 50 m 20
iter 10 ticks 3 pool 123456 B
...
ticks min 2 max 9 avg 6 total 11 n 2
pool start 4096 peak 200704 end 4096 B
status 0 OK
failures 0
```

書きは 1 行目が `cfg_bench write n 20`。`cfg status` は

```
OK schema_version 1 pool 12345 B
MISSING pool 0 B
CORRUPT sqlite=26 pool 0 B
```

失敗に数えるのは open の負 / get の負 (NOTFOUND を除く) / begin・set・commit・close の負 /
`cfg_status != OK` (1 回につき 1)。0 なら終了コード 0。

---

## 3. RED → GREEN

実装を 1 か所ずつ壊して、どの CHECK が落ちるかを実際に走らせて確かめた
(`/tmp` のスクリプトで自動化。壊す → 走らせる → 戻す)。

| # | 壊し方 | 落ちる CHECK |
|---|---|---|
| R1 | `bn_avg` を切り捨てに | `FAIL c_s5_pure: st.t_sum == 3 && bn_avg(&st) == 2` (1.5 → 2) |
| R2 | `bn_pick` が「無いキー」を混ぜない | `FAIL c_s5_pure: bn_pick(j) == want[j]` |
| R3 | `bo_u32` が溜め場を検査しない | `FAIL c_s5_pure: bo_end(&o) == -1` (数字だけが溢れる幅 cap=4 で越境。ASAN でも踏む) |
| R4 | `bn_should_log` が最終回を出さない | `FAIL c_s5_bench: cap_has("iter 3 ticks ")` |
| R5 | `bn_get_failed` が NOTFOUND を数える | `FAIL c_s5_bench: cap_u32_after("failures ") == 2` |
| R6 | open 〜 close の間に `sys_yield()` を 1 本入れる | `FAIL c_s5_bench: cap_yields == 0` — **票 §7 の直列化の契約**を守る番人 |
| R7 | プールを close の**後**だけ読む | `FAIL c_s5_bench: peak > start` |
| R8 | `cfg status` の pool を `CFG_OK` のときだけ出す | `FAIL c_s5_pool: cap_has("MISSING pool ")` |
| R9 | `fmt_u32` を `fmt_int(buf, cap, (int)v)` に | `FAIL c_s5_pool: fmt_u32(..., 4294967295u) == 10 && !strcmp(buf, "4294967295")` |

GREEN (壊す前 / 戻した後):

```
SUMMARY 45/45 PASS
TSV PARITY 58/58 PASS (cfg init == mk_settings_db.py)
HOST GNU89 -Werror compile PASS
TARGET i386-elf GNU89 -Werror compile PASS
```

ASAN (`--sanitize s5_pure s5_bench s5_pool cmd`) も 4/4 PASS、検出なし。

### 途中で直したこと

- `s5_bench` の書きの節で `cap_u32_after("pool end ")` と書いてしまい、実際の行は
  `pool start <a> peak <b> end <c> B` なので探す語が無く -1 が返って落ちた。
  読みの節と同じ `" end "` に直した (試験側の取りこぼし。実装は正しかった)。
- 最初の `s5_pure` は `bn_iter_line(small, 8, ...)` で溢れを見ていたが、これは `bo_str` の
  検査に先に当たるので `bo_u32` を壊しても GREEN のままだった (R3 が素通り)。
  `bo_init(&o, small, 4); bo_u32(&o, 12345)` という**数字だけが溢れる**幅を足して固定した。

---

## 4. 実 DB の上で観測できたこと (ホスト、参考値)

`s5_bench` は実 SQLite + 実 `kapi_db.c` を踏むので、`sqlite3_memory_used()` は実値。

- 読み (`cfg_bench 3 8`) / 書き (`cfg_bench -w 2`) のどちらも **`pool end == pool start`**
  (= 接続を閉じればプールは開始値に戻る)。票 M1 の「差 0」はホスト上では成立している。
- 書き 1 回のピークは約 **28KB** (`pool start 0 peak 28288 end 0 B`)。
- `sys_yield` は 1 度も呼ばれない (`cap_yields == 0`)。

ゲスト (NP21/W、FEP 常駐あり) の値は違うので、票 §2 の M1 / M4 / M6 は**実測が要る** ([V4])。

---

## 5. リンク確認 (PM への申し送り)

`build/programs.mk` にはまだ規則が無いので、手で同じ組み方を再現して未解決シンボルが
無いことだけ確かめた:

```bash
make build/out/lib/libos32cfg.a sdk/crt/crt0.o sdk/crt/crt0_c.o sdk/crt/syscalls.o sdk/crt/help.o
i386-elf-gcc $(PROGRAM_FLAGS) -c userland/tests/cfg_bench.c -o /tmp/cfg_bench.o
i386-elf-ld $(PROGRAM_LDFLAGS) -o /tmp/cfg_bench.elf $(CRT0_OBJ) /tmp/cfg_bench.o \
      --start-group build/out/lib/libos32cfg.a --end-group -lc -lgcc
# => text 14009  data 1648  bss 736
```

`userland/tests/%.elf` の既定パターンはライブラリを引けないので、`cfg.elf` と同じ形の
**明示規則**が要る (PM の範囲、票 §0)。

---

## 6. レビュー往復 1 の blocker 4 件 (2026-09-13)

Codex の所見 (blocker 4 / non-blocker 2)。**反例をホストで踏んでから**直した。
走らせ方は §0 と同じ。`--sanitize` に **符号付き桁あふれの検査**を足した:

```python
san = ["-fsanitize=address,signed-integer-overflow",
       "-fno-sanitize-recover=signed-integer-overflow", "-fno-omit-frame-pointer"]
```

`undefined` を丸ごと付けると `kapi/kapi_db.c` の SHM レイアウト (ゲストでは詰めた
バイト列) が alignment 検査に引っかかるので、この 1 種だけを有効にした。

### 直した内容

| # | 所見 | 直し方 |
|---|---|---|
| B1 | `bn_sink += iv` が `INT_MAX + INT_MAX` で signed overflow (`cfg set gshell desktop/color int 2147483647` 後の `cfg_bench`) | 捨て場を `static volatile u32 bn_sink;` にし、`bn_sink ^= (u32)iv;` の**符号なし XOR** で畳む |
| B2 | 状態を open 直後に採るので get 中の `CFG_ERROR` 遷移を見落とし、最終行が `status 0 OK` | 読み / 書きとも **操作を終えて close する直前**に `cfg_status` を採り、状態による失敗加算もその値で行う |
| B3 | `n ≤ 1,000,000 × m ≤ 10,000` を受けるのに `int failures` / `u32 total` が溢れる | 上限を**集計の幅から**決め直し (`BN_MAX_N 10000` / `BN_MAX_M 1000`、総失敗の上限 `n*(m+6) ≈ 1.0e7`)、集計を `u32` + **飽和加算** (`bn_add_sat`) に。飽和したら `saturated 1` を 1 行足して**失敗扱い** (`bn_bad`)。戻り値は `int` 0/1 なので数を縮めない。平均も `(t_sum + n/2)/n` をやめ、商と余りで丸める |
| B4 | GUI 端末 (con_sink 8KB、満杯で古い行を捨てる) で iter 行が読み出される前に消える | `bn_emit` を 1KB ごとに刻み、その境で `sys_yield` (cfg の `out_flush_console` と同じ)。端数は `bn_drain` が最後に吐き切る。**呼ぶのは必ず close の後** |
| nb1 | `pool peak` は 2 点観測の最大 | `bn_sample` のコメントに「prepare / step / commit 途中の一時確保を含む真の最大ではない」と明記 |
| nb2 | `cfg status` の pool は FEP 等を含む全体値 | `do_status` のコメントを「プール全体。設定 DB 単体の取り分ではない」に直した |

### 試験側の変更

- `cap_yields == 0` → **`cap_yields_open == 0`**。`host_yield` が `g_db.in_use`
  (libos32cfg が接続を掴んでいる印) を見て「**接続保持中の** yield」を別に数える。
  出力は close の後なのでそこでの yield は契約に触れない。
- 1KB の刻みそのものも固定した: 小さな走り (`cfg_bench 3 8`、出力 < 1KB) は
  `cap_yields == 1` (最後の吐き切りだけ)、`cfg_bench 400 1` (出力 > 1KB) は
  `cap_yields >= 2`。

### RED → GREEN (往復 1)

| # | 壊し方 | 落ちる CHECK |
|---|---|---|
| B1 | `static int bn_sink;` + `bn_sink += iv;` に戻す | `--sanitize` で `cfg_bench.c:515: runtime error: signed integer overflow: 2147483647 + 2147483647 cannot be represented in type 'int'` — レビューの反例そのもの (試験が DB に `2147483647` を入れてから走らせる) |
| B2 | 状態を open 直後に採る | `FAIL c_s5_bench: cap_has("status 4 ERROR\n")` (`inj_prep_fail = "ival"` で get の prepare だけ落とす) |
| B3a | 上限を `1000000 / 10000` に戻す | `FAIL c_s5_pure: bn_parse_u32("10001", 1, BN_MAX_N, &v) == -1` |
| B3b | `t_sum += ticks` に戻す | `FAIL c_s5_pure: st.saturated && st.t_sum == 4294967295u` |
| B4 | 1KB ごとの yield を止める | `FAIL c_s5_bench: cap_yields >= 2` |
| B4b | 最後の吐き切りを止める | `FAIL c_s5_bench: cap_yields == 1` |

**ホストで踏めなかった 1 件** ([V4]): B3 の「平均の丸めの桁あふれ」。`include/types.h`
の `u32` は `unsigned long` で、**ホスト (LP64) では 64bit** になるため
`(t_sum + n/2)` が巻き戻らない。i386 の 32bit でしか起きない。ホストでは
「`t_sum = 4294967295, n = 2` → `avg 2147483648`」という**値**だけを固定し
(新しい形は両方の幅で通る)、桁あふれを踏まないことはコード (商と余りで丸める)
で保証した。`bn_add_sat` の飽和は値で書いてあるので幅に依存しない。

### GREEN (往復 1 の後)

```
SUMMARY 45/45 PASS   (plain / --sanitize とも)
TSV PARITY 58/58 PASS
HOST GNU89 -Werror compile PASS
TARGET i386-elf GNU89 -Werror compile PASS   (-Wall -Wextra -Werror 単体も PASS)
```

出力の書式で変わったのは 2 点だけ (§2 の他は不変):

- `status` 行は**操作後**の状態を出す (ERROR 遷移が見える)。
- 集計が飽和したときだけ `saturated 1` の 1 行が増え、終了コードが 1 になる。

### 未実行 ([V4])

票 §2 の実測 (M1 / M4 / M6 は PM が `5ccf6b8` の版で取得済み。**この往復の版では
取り直しが要る** — 出力に `saturated` 行が増える可能性と、1KB ごとの `sys_yield`
が**窓の外**とはいえ全体の所要時間に乗るため)、§3 の回帰 R1〜R4、`make all` /
`make check` / 配備 / エミュレータ。上限を `n ≤ 10000` に下げたので、
`cfg_bench 50 20` / `cfg_bench -w 20` という実測の指定はそのまま通る。
