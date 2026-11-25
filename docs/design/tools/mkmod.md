# tools/mkmod.py — 設計
> 状態: **現行 (2026-11-18)** — 出所: TASK_DBGREF (§3-2・§3-6・§4) の契約を今のソースと照らして要約
ソース: `tools/mkmod.py` (`build`・`pair_crc`・`main`)・`tools/module_schema.py` (`COMPONENTS`)

VA 0 で `--emit-relocs` 付きでリンクした i386 ELF から、`/boot` の部品 (KMOD v1) の容器を作る。容器の形式の正典は `docs/08_build.md` §8-2「起動時に読む部品の形式 (KMOD v1)」、読む側の検査は `sys/kern/module/module_image.c`。
作る側の合格は、ディスクから読んだ像が同じ内容である保証にはならないので、読む側も全部を検査し直す。

## 入力と世代

- `build(elf_data, metadata, vmkernel, kapi_abi_gen, layout_gen, *, …)`: KAPI 版と layout 世代は **必須の引数** (省くと TypeError)。CLI も `--kapi-abi-gen`・`--layout-gen` が必須で、省くと出力を書かずに失敗する。呼び手 (`build/*.mk`) は `sdk/kapi.json` の今の値を渡す。
- `module_id` は `tools/module_schema.py` の行 (`POLICIES`) に在ること。無い ID は生成で断る。部品ごとの上限 (image / mem / load_span / reloc / export) もその行から取り、超えたら断る。
- 対の CRC が必須の部品は、組んだ `vmkernel.lz4` (VK32 v2) のヘッダが記録している Image CRC を `pair_image_crc` に埋める (`pair_crc`。ヘッダが壊れている・長さが違う・記録された CRC が実際の CRC (その欄を 0 として計算) と合わない VK32 は断る)。最後に語 32 を 0 として容器全体の CRC32 を `image_crc` に埋める。

## 生成で断るもの (例外 `ValueError`、CLI は非 0 で出力を書かない)

- ELF が i386 の ELF32 でない、節・シンボルの表が壊れている、`--emit-relocs` の無い ELF、許していない節・重複した節、`.text` が VA 0 でない、節の重なり、BSS が payload に重なる。
- 再配置: 種別は `R_386_32` (1) と `R_386_PC32` (2) だけ。未解決のシンボルを指す・範囲外・その他の種別は断る。`R_386_32` だけを容器の reloc 表に載せ (書込位置は非整列でよい)、PC32 は部品の中で解決済みなので載せない。reloc の 4 B どうしが重なるもの、上限を超える件数は断る。
- import: slot の配列が `.data` の先頭で件数 × 4 B ちょうどでない、生成した import の並び (添字) と食い違う、名前の重複、名前の FNV-1a の衝突 (例: `costarring` と `liquid`)、slot が reloc と重なる、slot の初期値が 0 でない。
- export: ops の表の大きさが部品の口数 + 1 語でない、表が payload の外、先頭の版が部品の ops の版と違う (古い版の ELF)、口のポインタに `R_386_32` が無いのに非 0、入口が `.text` の外、名前の重複・hash の衝突。
- init が `.text` の外。

検査: `make check-mkmod-host` (`tools/tests/test_mkmod.py`)。実物の i386 ELF (生成した import slot とスタブ、非整列の絶対再配置、内部の PC32 呼び出し、export 2 本) から容器を作って C の検査器に通し、埋めた対の CRC と容器の CRC、CLI の出力と API の結果の一致を見る。壊れた ELF・対応しない / 未解決の再配置・不正な import・壊れた VK32 は失敗すること。`--mutate` は絶対再配置の取りこぼし・PC32 を残す・対の CRC が 0・export の slot の誤りを写しに入れる。import の集合と `.def` の一致は `tools/tests/test_kdebug_imports.py`。
