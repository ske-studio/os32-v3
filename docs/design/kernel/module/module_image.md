# kernel/module/module_image.c — 設計
> 状態: **現行 (2026-11-18)** — 出所: TASK_DBGREF (§3-2・§4) の契約を今のソースと照らして要約
ソース: `kernel/module/module_image.c` (`module_image_header`・`module_image_validate`・`module_image_relocate`)・`kernel/module/module_image.h`・`kernel/module/module_schema.h` (生成元 `tools/module_schema.py`)

`/boot` の部品 (KMOD v1) を検査し、再配置する。ローダ (`kernel/module/module_runtime.c`) とホスト試験が同じ実装を使う。
容器の並び・ヘッダの語・**拒否する条件の一覧の正典は `docs/08_build.md` §8-2「起動時に読む部品の形式 (KMOD v1)」**。ここは関数の約束だけを書く。

## 呼び手が決める期待 (`struct module_image_policy`)

- 期待する `module_id` は **呼び手が渡す** (像の自己申告から決めない)。ID に対応する行 (`module_component_policy`: phase・必須 / 許す flags・stop の可否・ops の版と口数・image / mem / load_span・reloc / export の上限) が `MODULE_COMPONENT_POLICIES` に無ければ `MODULE_BAD_HEADER`。像の ID が期待と違っても `MODULE_BAD_HEADER`。
- KAPI 版・layout 世代・`module_abi_gen`・ops の版・import の件数は本体の生成値と完全一致 (違えば `MODULE_ABI_MISMATCH`)。対の CRC が必須の部品は本体の Image CRC が有効で一致すること (`MODULE_NO_IMAGE_CRC` / `MODULE_PAIR_MISMATCH`)。
- import / export の契約 (名前の FNV-1a・版・種別) は生成した表 (`imports`・`exports`) で渡す。

## 3 つの関数

| 関数 | 読むもの | すること |
|---|---|---|
| `module_image_header(header, header_bytes, file_size, policy, out)` | 先頭の 144 B だけ (`file_size` がもっと大きくても) | ヘッダの全欄・大きさ・節・表の範囲・世代を検査し、`load_span` を出す。ローダはこれが通ってからページを取る |
| `module_image_validate(image, size, policy, out)` | 容器の全体 | ヘッダの検査に加えて CRC (語 32 を 0 として全体の CRC32)・import・export・名前表・reloc を検査 |
| `module_image_relocate(image, size, policy, base, out)` | 容器の全体 | validate が通ったら `base + mem_size` のあふれを断り、`[payload_size, mem_size)` をゼロにし、reloc の各語 (非整列可) に `base` を足す |

- **出力 `out` は成功したときだけ書く** (失敗では呼び手の値をそのまま残す)。`out` は NULL でもよい。
- header と validate は入力を書き換えず、確保せず、番地を解決せず、部品のコードを呼ばない。relocate も拒否のときは像を 1 バイトも変えず、成功のときに書くのは reloc の語と BSS だけ。呼び手は `load_span` 分の書ける領域を、整列した番地で用意する。
- 拒否の戻り値は `enum module_image_result` (`BAD_HEADER` 3・`PAIR_MISMATCH` 4・`NO_IMAGE_CRC` 5・`ABI_MISMATCH` 6・`BAD_CRC` 9・`RELOC_INVALID` 11・`IMPORT_INVALID` 12)。ローダはこの値をそのまま部品の状態に残す。

検査: `make check-module-image-host` (`tools/tests/test_module_image.py`、本体 `tools/tests/module_image_host.c`)。独立に組んだ容器と信頼する import / export の契約で、正常品 (非整列・未整列順の reloc、隣接する書込区間、one-past の値、payload の最後の語の reloc) を受け、拒否の群 (未来版・header_size・image_size・表の範囲・表の重なり・reloc の重なり・reloc の範囲・import slot・export slot・名前の hash・CRC・対・`image_valid`・ABI / ops の不一致、ほかに ID・部品の ops・image / body の大きさ) がそれぞれ指定の戻り値で断られることを見る。header と全体の両方で、入力が変わらない・失敗では出力の番兵が残る・成功の計画が独立に読んだ語と `load_span` に一致する、を確かめる。`--mutate` は検査の述語を写しで 1 つずつ壊す。
