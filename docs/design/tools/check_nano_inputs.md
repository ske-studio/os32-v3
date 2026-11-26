# tools/check_nano_inputs.py — 設計
> 状態: **現行 (2026-11-18)** — 出所: TASK_T2D_T2H (§3-4・§3-5 f1a) の契約を今のソースと照らして要約
ソース: `tools/check_nano_inputs.py` (台帳 `sdk/allocator/nano_inputs.json`、表示 `sdk/allocator/nano.LICENSE`)

SDK の allocator が使う newlib nano の入力が、台帳に記録したものと同じであることを検査する。由来・版・設定の背景は `docs/08_build.md` §8-5「nano allocator の入力台帳」。

## 検査すること (通常の実行)

- 台帳の schema は 2。
- toolchain の `libc.a` などの archive の SHA256、`libc.a` の各 member の SHA256 が台帳 (receipt があれば receipt) と一致する。台帳が「無いはず」とする archive (`libc_nano.a` など) が無い。
- `i386-elf-gcc` の版と target が台帳と一致する。
- 台帳の各 symbol をちょうど 1 つの台帳どおりの member が提供する。dlmalloc の印 (`__malloc_av_`) が無い。
- 実際の `-L` の順で `-lc` を解決したリンクの map で、各 symbol が台帳どおりの `libc.a(member)` から来る (先頭の `-L` に置いた偽の libc.a を拒否する)。
- receipt がある場合: receipt の upstream と configure が台帳と一致し、receipt を作った構築スクリプト (`tools/ci/build_cross.sh`) の hash が今のものと一致する。食い違いには復旧の案内 (`--record-built` で receipt を作り直すか、再構築) を付ける。receipt が無いまま hash が違うときも同じ案内を付ける。GCC の版・provider・dlmalloc・link map の失敗には receipt の案内を付けない。

## receipt の作成 (`--record-built --source --tarball --config`)

3 つ全部が要る。tarball の SHA256、tarball 内の各ソースと構築に使ったソースの一致、`nano.LICENSE` が tarball の該当ソースの先頭 27 行と一致、configure の必須 flag があり nano を無効にする flag・multilib が無い、Makefile の `CFLAGS` と target が台帳どおり、toolchain の patch が空。通ったら inventory と合わせて `$(CROSS_DIR)/os32-nano-build.json` に書き、続けて通常の検査を行う。

## SDK の入力 (`sdk_build` 節)

SDK のソースの hash、SDK が使う upstream のソースの hash が toolchain の台帳と一致すること、SDK の member が toolchain の台帳にあること、SDK の patch が空であること。SDK の入力を足しても toolchain の再構築・receipt・cache key は変わらない。

## cache key (`--cache-key`)

upstream・configure・gcc・target・cflags・patches・構築スクリプトの hash と、member・archive の**名前**の集合 (並べ替え済み) から作る。名前の追加では変わり、hash 値や列挙の順序の変化では変わらない。

## 結果

成功は `nano inputs PASS: build receipt` か `nano inputs PASS: pinned local ledger` で rc 0。失敗は `nano inputs FAIL: <理由>` で rc 1。

## 検査

`test_nano_inputs.py (旧版 027d3b5ba486)` が実 toolchain の台帳を正常対照にし、入力を 1 つずつ変えたケースの拒否と、拒否を外す変異の実行時 RED を確かめる。
