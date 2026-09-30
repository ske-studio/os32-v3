# kapi_out — RED → GREEN の記録

票: [`docs/archive/kernel_v21/TASK_KAPI_OUTPUT_GUARD.md`](../../docs/archive/kernel_v21/TASK_KAPI_OUTPUT_GUARD.md) 受入 G1
対象: `sdk/gen_kapi.py` (`out` の解釈・検査・生成) / `sdk/kapi.json` (申告)
試験: `tools/tests/test_kapi_out.py`
実行: `make check-kapi-out` (`check-par` の列)。否定側は `--mutate` (手で回す)

## なぜホストで固定するのか

穴の本体はエミュレータでは**見えない**。OS32 は `CR0.WP = 0` で走るので、CPL=3 が
出力引数に共有ライブラリの `.text` を渡しても #PF は起きず、アプリは死なず、
**共有コードが静かに壊れる**だけ。壊れたことは何時間か後に別の症状で出る。

そのうえ本当に危ないのは「いま 45 本に検査を入れたこと」ではなく、**46 本目を足す人が
`out` を書き忘れること**。だから試験の中心は生成コードの中身ではなく、
**書き忘れで生成が落ちること**に置く。

## RED → GREEN

| # | 見るもの | RED (入れる前) | GREEN (入れた後) |
|---|---|---|---|
| 1 | 非 const ポインタがあるのに `out` が無い | 生成が**成功**し、検査の無い wrapper が出る | `sdk/gen_kapi.py` が exit 1、`t_len_u: 非 const のポインタ引数 buf があるのに "out" が無い` |
| 2 | `out` の `arg` / `len` が引数に無い | 生成物に `KAPI_OUT_LEN(nope, …)` が出てカーネルのビルドで初めて落ちる | 生成の時点で exit 1 (名前を挙げる) |
| 3 | 出力が 3 本あるのに `out` に 2 本しか書かない | 生成が成功し、3 本目だけ無検査 | exit 1 (`"b"` が `"out"` に無い) |
| 4 | `len` と `size` の同時指定 / `size` が 0 | どちらの意味にも取れるまま生成 | exit 1 |
| 5 | `u32` の長さ / `int` の長さ | — | `KAPI_OUT_LEN(buf, size)` / `KAPI_OUT_LEN_S(buf, size)` (負は 0 に丸めて既存の扱いを変えない) |
| 6 | 個数 × 単位 | — | `kapi_out_mul(KAPI_OUT_LEN_S(buf, count), 512u)`、あふれたら `0xFFFFFFFF` = 必ず拒否 |
| 7 | 出力が 3 本 | — | `ring3_user_ranges_writable` を 2 回 (2 本ずつ)、**どれも target を呼ぶ前**。1 本目だけ書かれる壊れ方をしない |
| 8 | `"none"` / `"target"` | — | wrapper に 1 行も出さない (書かないポインタ / target 自身が見ている) |
| 9 | 検査に落ちた生成 | — | 生成物を **1 バイトも書かない** (半分だけ新しい木を残さない) |

実行結果 (2026-09-23):

```
CASE interpretation PASS
  out 無し                       RED (落ちた)
  arg が引数に無い                   RED (落ちた)
  len が引数に無い                   RED (落ちた)
  出力が 1 本足りない                  RED (落ちた)
  len と size の両方               RED (落ちた)
  size が 0                     RED (落ちた)
  len が const ポインタ引数           RED (落ちた)
  ポインタが無いのに out がある            RED (落ちた)
CASE refuse PASS
CASE real PASS (非 const ポインタを持つ 45 本すべてに out)
```

否定側 (`--mutate`、生成器の検査そのものを壊す。書き換えて戻すので `make check` では回さない):

```
MUTATION 1 RED (1 件): out の書き忘れを見逃す
MUTATION 2 RED (1 件): 出力の数え落としを見逃す
MUTATION 3 RED (1 件): len と size の同時指定を見逃す
```

## この試験が見ていないもの

- **実際に kill されること** — CPL=3 から共有ライブラリの `.text` を出力に渡す経路は
  ホストでは作れない。票 §3 の G2 / G3 (NP21/W + 試験バイナリ) が見る。
- `"none"` が本当に「書かない」か — これは人の申告で、機械には確かめられない。
  `sys_ls` / `gui_register` の関数ポインタがコード帯にあることの確認は票 §3 の G3。
- `dev_blk_read` の `unit` は**最小のセクタ長 512**。1024 (FD) / 2048 (CD) の装置では
  後ろが未検査のまま残る (`dev->sect_size` は wrapper からは引けない)。
