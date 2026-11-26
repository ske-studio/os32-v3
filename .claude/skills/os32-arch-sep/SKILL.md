---
name: os32-arch-sep
description: CPU・機種・装置の分離を壊さずにコードを足す・移す。層の検査 (include_layers・link_layers) が落ちた時、共通のソースから機種や装置の物を使いたい時、機種ごとに違う番地・設定を足す時、ソースの置き場を持ち主に合わせて移す時、生成物が変わる段を分離の枝で出す時に使う。
---

# os32-arch-sep — 機種依存の分離を保つ

決まりの正典: 層の規則と直し方は [check_layers](../../../docs/design/tools/check_layers.md) §3・§4・§8、ソースの選択は [select](../../../docs/design/build/select.md)、置き場の移動は [SRC_MOVE](../../../docs/SRC_MOVE.md)、生成物の一致は [sep_identity](../../../docs/design/tools/ci/sep_identity.md)。基準は「共通のソースが機種・装置のヘッダを読む数 0・ビルド 1 系統・入口の骨格 1 本」(ユーザー決定 2026-11-17)。

## 1. 置き場を決める (持ち主はパスで決まる)

| 書く物 | 置き場 | 読んでよいヘッダ |
|---|---|---|
| CPU に依る物 (例外・コンテキスト・命令) | `sys/arch/<cpu>/` | 共通の口、同じ arch。機種のヘッダは固定名 `platform_*.h` だけ |
| 機種に依る物 (起動・番地・BIOS・割り込みの配線) | `sys/platform/<機種>/` | 何でも (同じ像の arch・platform) |
| 装置のドライバ | `sys/dev/` (表示は video/、HWDEV は hwdev/) | 共通の口。arch のヘッダは固定名 `arch_*.h` だけ |
| 共通の実装・宣言 | `sys/kern/`・`sys/fs/`・`sys/hal/`・`sys/net/`・`sys/input/`・`sys/kapi/`・`sys/include/`・`lib/` | 共通と、固定名 `arch_*.h`・`platform_*.h`、`sys/include/<名>_contract.h` |

像に入るソースは `build/select.mk` の所有者集合から像の構成 (`sys/conf/images/`) で選ぶ。新しいソースは、持ち主の集合の一覧に 1 行足す (一覧の置き場と書き方は select の設計書。Makefile に手書きの一覧を作らない)。

## 2. 共通から機種・装置の物を使いたい時

| したい事 | 形 |
|---|---|
| 共通が装置の関数・型を使う | 共通が要る宣言だけを `sys/include/<名>_contract.h` に置き、装置のヘッダがそれを include する。共通は contract だけを読む |
| 機種ごとに実装が違う小さい物 (インライン・定数・表) | 固定名 `sys/platform/<機種>/platform_<名>.h`。`-Isys/platform/$(PLATFORM)` が実装を選ぶ。**両方の機種に同じ名前で置く** |
| 共通が CPU の関数を使う | 契約のヘッダ (`sys/include/cpu.h`・`sys/include/io.h` など) に宣言、実装は固定名 `sys/arch/<cpu>/arch_<名>.h` |
| 共通が機種の定数を使う | 共通の側に機種に依らない名前で値を持ち、機種のソースに `_Static_assert` で一致を固定する |
| 機種だけの番地 | 機種のヘッダに置く (PC-98 の起動と低位メモリは `sys/platform/pc98/platform_boot_layout.h`)。OS の配置は `sys/include/memmap.h`。地図の道具 `tools/gen_memmap.py` は両方を読む (`PLATFORM_HDR_RELS`) |
| `-D` で名前を差し替えたい | しない。口 (関数・固定名のヘッダ) にする |

## 3. 確かめる

```bash
make all && make ARCH=ppc PLATFORM=powermac kernel          # 両方の像が組める
for id in include_layers link_layers src_owner build_sources ppc_common_compile; do
    python3 -B -m tests.infra.ci run --check "$id" || exit 1
done
```

- 共通の置き場のソースは PowerPC のコンパイラでも通ること (登録 ID `ppc_common_compile`)。x86 だけの型の幅に頼らない (PowerPC の `u32` は `unsigned long`)。
- 試験が手本・写しの木を自前で作っている所に追従が要る: host 試験の `-I` の一覧 (`tools/tests/kernel_includes.py` は `build/config.mk` を展開するので自動)、変異の時だけ写すディレクトリの一覧、手本のヘッダの差し替え定義。通常実行 では見つからないので、触った試験は `--mutate` 付き で 1 回回す。

## 4. ソースを移す

`docs/SRC_MOVE.md` §3 の手順: 対応表 JSON → `python3 tools/move_files.py <表> --dry-run` → 適用 → 一覧に出た所を手で直す。移動と中身の直しは別のコミット。

移した後に漏れやすい所 (2026-11-17〜18 に実際に漏れた):

- `tools/source_roots.py` の走査の根 (ファイル単位で挙げている検査がある。外れると、その検査は違反を検出しなくなる — 変異込みでしか気付かない)。
- 検査の登録の `sources` (移動先に合わせ、`python3 -B -m tests.infra.ci lint` で登録を確かめる)。
- 文書の中のパス (`python3 -B -m tests.infra.ci run --check docs_links`)、ソースを特定のコミットと比べている試験。

## 5. 生成物が変わるか

登録検査 `build_identity` が候補の二つの清浄ビルドを照合する。取り込みは os32-land に従う。
