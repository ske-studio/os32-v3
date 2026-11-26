# userland/diag/cfg_bench.c — 設計

> 状態: **現行 (2026-11-18)** — 出所: TASK_S5 の契約を今のソースと照らして要約
ソース: `userland/diag/cfg_bench.c`

設定レジストリの実測用の CPL=3 アプリ。`libos32cfg` を静的リンクし、接続 1 回あたりの tick と SQLite のプールの使用量を測る。library の約束 (open から close の間に yield しない) は `docs/design/userland/lib/cfg/libos32cfg.md` の「設定レジストリの全体の約束」。

```
cfg_bench [n] [m]    読み: n 回 (既定 50) の「open(RO) → m 件 (既定 20) の get → close」
cfg_bench -w [n]     書き: n 回 (既定 20) の「open(RW) → begin → set_int → commit → close」
```

## 何を引き、何を書くか

- 読みは `gshell` scope の `desktop/color` (int)・`desktop/wallpaper` (text)・`taskbar/clock_24h` (int) を巡回し、4 件に 1 件 (`j % 4 == 3`) を必ず無い `nosuch/key` にする。m = 20 なら無いキー 5 件・各キー 5 件。
- int は `cfg_read_int` で取り、既定値を埋め戻す (`cfg_get_int` は負の戻りが値と区別できず、失敗を数えられないため)。
- 書きは `app:bench` の `counter` に回数を `cfg_set_int` する。

## 引数の上限

`n` は 1〜`BN_MAX_N` (10000)、`m` は 1〜`BN_MAX_M` (1000)。上限は集計の幅から決める (1 回で数えうる失敗は m + 6、総数は u32 に収まる)。集計は u32 の飽和加算 (`bn_add_sat`) で、飽和したら `saturated 1` を出して失敗にする。

## 出力 (書式を固定)

```
cfg_bench read n 50 m 20
iter 10 ticks 3 pool 123456 B
...
ticks min 2 max 9 avg 6 total 11 n 2
pool start 4096 peak 200704 end 4096 B
status 0 OK
failures 0
```

- 書きは 1 行目が `cfg_bench write n 20`。
- `iter` 行は 10 回ごとと最終回。
- avg は四捨五入 (商と余りで丸め、32 bit で桁あふれしない)。`total` と `n` も出す。
- pool は close の直前 (接続を持っている間) と直後の 2 点を読み、peak は両方の最大、end は最後の直後の値。start はループの前の 1 回。peak は 2 点観測の最大で、prepare・step・commit の途中の一時確保を含む真の最大ではない。
- `status` 行は操作を終えて close する直前の `cfg_status` (数値と名前)。get の途中で `CFG_ERROR` に変わったのも見える。

## 失敗の数え方と終了コード

失敗に数えるのは open の負・get の負 (`NOTFOUND` を除く)・begin・set・commit・close の負・`cfg_status != CFG_OK` (1 回につき 1)。失敗 0 かつ飽和なしで終了コード 0、それ以外は 1。

## 直列化と出力

- 計測の窓は `cfg_open` の直前から `cfg_close` の直後まで。窓の中で yield しない。
- コンソールへの出力は全部窓の外。1 KB ごとに `sys_yield` を挟み (GUI 端末の con_sink は 8 KB の環で、満杯になると古い行を捨てる)、端数は最後に吐き切る。
- 読み捨てる値は `static volatile u32` に符号なし XOR で畳む (符号付きの桁あふれを起こさない)。

## 検査

`test_cfg.py (旧版 f84b12760091)` の `s5_pure` (引数・巡回・失敗の数え方・集計・整形の純関数)、`s5_bench` (実 DB の上の通し。接続を持っている間の yield が 0、短い走りで yield 1 回・1 KB を超える走りで 2 回以上、close 後にプールが開始値へ戻る、get 中の ERROR 遷移が `status 4 ERROR` に出る)。`--sanitize` に符号付き桁あふれの検査が入る。ホストの `u32` は 64 bit なので、平均の丸めの 32 bit 桁あふれはホストでは踏まない。
