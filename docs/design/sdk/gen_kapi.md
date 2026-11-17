# sdk/gen_kapi.py — 設計
> 状態: **現行 (2026-11-18)** — 出所: TASK_KAPI_OUTPUT_GUARD の契約を今のソースと照らして要約
ソース: `sdk/gen_kapi.py` (`out_errors`・`input_errors`・`check_all_out`・`out_ranges_c`・`out_guard_c`)

`sdk/kapi.json` の出力ポインタの申告 `out` を検査し、ラッパ (`kapi/kapi_generated.c`) の先頭に範囲の検査を出す。
`out` の書式と意味、検査の中身 (`ring3_user_ranges_writable`) の正典は `docs/KAPI_SPEC.md` §3-3。ここは生成器が守る規則だけを書く。

## 申告の検査 — 書き忘れで生成を落とす

仕組みの要は「次に KAPI を足す人が `out` を書き忘れたら生成が落ちる」こと。非 const のポインタ引数 (`*` を含み `const` を含まない宣言) を「書かれ得る引数」と見る。
次のどれかがあると、`sdk/gen_kapi.py` は理由を名指しで標準エラーに出して exit 1 で終わり、**生成物を 1 バイトも書かない** (検査は書き出しより前)。

| 誤り | 例 |
|---|---|
| 非 const のポインタ引数があるのに `out` が無い | `t_len_u: 非 const のポインタ引数 buf があるのに "out" が無い` |
| 非 const のポインタ引数が `out` に並んでいない (出力が 1 本足りない) | 3 本中 2 本だけ書いた |
| 非 const のポインタ引数が無いのに `out` (範囲・`"none"`・`"target"`) がある | const ポインタだけのエントリに `out` |
| `arg` / `len` が引数に無い、`arg` が非 const のポインタでない、`arg` の重複 | `{"arg": "nope", ...}` |
| `len` と `size` の両方、またはどちらも無い | — |
| `len` がポインタ引数 | `{"arg": "cb", "len": "path"}` |
| `size` / `unit` が 0 以下・空の式・整数でも文字列でもない、`unit` を `size` と組む | `"size": 0` |
| `"direct"` のエントリに範囲 (ラッパが無いので検査を置けない) | — |

`out` は非空の配列か `"none"` / `"target"` だけ。`in` (入力の範囲) も同じ形の検査を通す (`input_errors`)。

## 生成する検査の形

- 長さの式: `size` は `KAPI_OUT_LEN(arg, size)`。`len` は長さ引数が符号なし (`u8`・`u16`・`u32`・`u64`・`unsigned`・`size_t`) なら `KAPI_OUT_LEN`、それ以外 (`int` など) は `KAPI_OUT_LEN_S` で、負は 0 に丸める (既存 target の「長さ <= 0 の扱い」を変えない)。
- `unit` は `kapi_out_mul(<長さ>, unit)`。個数 × 単位があふれたら `0xFFFFFFFF` を返し、必ず拒否される。
- `KAPI_OUT_LEN` / `KAPI_OUT_LEN_S` / `kapi_out_mul` の定義は生成物に 1 か所だけ。
- 範囲は 2 本ずつ `ring3_user_ranges_writable` に渡す (3 本なら 2 回の呼び出し、余りは `(0, 0)`)。どれかが偽なら `ring3_fault_kill()`。**全部の検査が target の呼び出しより前**に出る (1 本目だけ書かれる壊れ方をしない)。
- `"none"` と `"target"` のエントリには検査を 1 行も出さない。

## 検査

`tools/tests/test_kapi_out.py` (`make check-kapi-out`)。合成した最小の `kapi.json` で解釈と順序 (`interpretation`)・誤りごとの拒否と「何も書かない」(`refuse`)・実物の `sdk/kapi.json` が `--check-only` を通ること (`real`) を見る。`--mutate` は生成器の検査を写しで壊し、`refuse` が落ちることを見る。
