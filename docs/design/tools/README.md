# tools/ — 設計
> 状態: **現行 (2026-10-31)** — 出所: MEMMAP_V3・SLIM_KERNEL_DESIGN・SLIM_KERNEL_IMPORTS・VER_INFO_DESIGN・CRT31K_DESIGN・GF6200_4A_DESIGN・HWDEV_CORE_API・HWDEV_DESIGN・SLIM_HWDEV_DESIGN・API_CONTRACTS・GFX_MODES_DESIGN (D1 で移した)
ソース: `tools/`

| OPERATIONS / DERIVE_OPERATIONS | `crt31k-on: {'CRT31K':'true'}`、`crt31k-off: {'CRT31K':'false'}`。他鍵は変更しない。 |

### mkmod

| ビルド | `ld -r` で 1 本にまとめず、**ベース 0 で最終リンク** (`-Ttext=0 --emit-relocs`、モジュール用 `.ld`)。`--emit-relocs` は**解決済みの内部 PC32 も出力する**ので、`mkmod.py` は (1) 参照先がモジュール内 (シンボルがモジュールのセクションに定義) の `R_386_PC32` は**位置独立なので検証して実行時表から除外**、(2) モジュール内への `R_386_32` は実行時表へ、(3) 参照先がモジュール外のもの (未解決) と (1)(2) 以外の種別は**拒否**、の規則で表を作る。外部関数はインポート用スタブ経由なので (1) に落ちる。**メモリサイズ = text+data+bss を整列込みで確定**し、ヘッダに `mem_size` と `bss_zero` 範囲を持つ (画像長 `raw_size` だけで隣を決めない) |

`mkmod.py` は外部データへの `R_386_32` を見つけたら**生成を拒否**

ELF32、R_386_32/PC32、未解決参照拒否、生成 import 型契約、

### gen_sqlite_imports

**インポート表 (名前の並び) とスタブ (`vfs_validate_sqlite: jmp [slot_n]`)** を生成してリンクに混ぜる。

> [要確認] 元文の生成器名 tools/mkmod.py は現行と異なる。インポート表・stub は tools/gen_sqlite_imports.py が生成する。

contracts の文字列 FNV により CIRRUS_MODULE_ABI_GEN は自動更新（tools/gen_sqlite_imports.py:41–44）。kernel と cirrus.mod を同じビルドの対で配る。

### gen_kdebug_imports

- GF 専用枠は番号でなく既存 4 symbol で同定する。check_nm は実リンク map の移植 4.o を S2 の参照解決にも加え、S6 は accessor 名で照合して同じデータの GETTER と SETTER を区別する。
- 関数番地は付表どおり 2 記号の KDI_ADDR/KDI_INDEX を、その FUNC が追加された時点で生成する。manifest は symbol と使用する移植 object を対応させ、実リンク対象のソースにある番地参照を nm 照合へ加える。

`kapi_reserved_nosys` の宣言は既存の exec.c に残し、生成器の source=.c 経路で取得する。

- 宣言は既存ヘッダから抽出。ring3_ls_start/endはexec/ring3_ls.hのconst u8配列、__kernel_resident_endはinclude/memmap.hのu32単体としてPTR生成。

- kapi_reserved_nosysの宣言が元.cだけにある分は既存生成器のsource=.c経路を使う。

- 型付き `KDI_ADDR_name` を必要な2記号だけ生成: `((__typeof__(&name))(uptr)kdebug_import_slots[KDI_INDEX_name])`。KDI_INDEXは生成#define。

- check_nmはこの2件のアドレス利用もmanifestで照合する。stubを比較して常時FAILにしたり、未解決参照がないことを理由にimportを消したりしない。

### gen_build_id

tools/gen_build_id.py:45–55 の短縮 commit・追跡ファイルの dirty・失敗時 unknown を維持。

### np21w_ini

| 共通変換 | EXTRAにCRT31K、_extra_valueにtrue/false検証を追加。ALLOWEDには入れず既存操作・receiptを維持。 |

### gfinit_source

0x58はE299/E2AB/D33Fの3件。資料冒頭のCOPY_NV_REG位置「E80B」は本文のバイト列・前後pcからE7FB（22B、次E811）と扱う。

### hwdev_audit

`build/kdebug.mk` が同じリンクで map を出し、`python3 tools/hwdev_audit.py` が
map の LOAD 一覧・各 .o の size・最終 ELF・KMOD header を照合する。

`hwdev_audit.py` の計測対象は HWDEV の map/ELF/header とし、GF 部品オブジェクトと
部品接続部分を分ける。

### hwdev_layout

配置 inventory は `tools/hwdev_layout.json`

### gen_hwdev_pair

- `build_id` は部品生成時の u32 世代 (v1 は `contract_crc`)。`tools/gen_hwdev_pair.py` はビルド済み kernel/KDEBUG の対と IMAGE_CRC を検証し、manifest 用の `build/out/hwdev/hwdev_pair.h` を生成する。manifest の自己申告で loader の handle/generation/base/size/CRC を置き換えない。kernel_pair_crc は KMOD PAIR_CRC、core_image_crc は依存先 KDEBUG の KMOD IMAGE_CRC と一致。部品自体の CRC は loader が検証する (manifest へ自己 CRC を循環埋込みしない)。

### gen_recover_tables

- 新規 gen_recover_tables.py は oracle の機械抽出結果を入力として格納表を生成し、`--check` で再現性を検査。
- 抽出はCの初期化子を独自解釈せず、host harnessで評価して数値・参照配列を出力する。シンボル名を別に保持してC参照を生成。

- 生成器の既定入力は S2a の固定 oracle だけを C で評価した schema=1 レコードとし、`--input` では保存済み抽出 JSON も受け取る；製品未生成でも生成でき、`--check` は全欄比較で採取した oracle JSON から再現を確認する。

- 補助配列と式関数は recover_gf.c に残し、生成 include は 3 表と件数だけを持つ；範囲の結合には kinds の参照名も一致させ、ポインタの参照先と順序を保存する。

### check_gui_proto

`tools/check_gui_proto.py` が両者の
数表を突き合わせる。

### gen_memmap

- gen_memmap.py:113–119 は memmap.h の include を外して cpp で解くため、容量式を gfx_format.h 等への依存にしない。

## 索引

- [tools/accept/README](accept/README.md)

- [tools/checks/README](checks/README.md)

- [tools/gen_font16](gen_font16.md)

- [tools/gen_hwdev_contract](gen_hwdev_contract.md)

- [tools/gfinit_mode_tables](gfinit_mode_tables.md)

- [tools/host_agent](host_agent.md)

- [tools/mkd88](mkd88.md)

- [tools/module_schema](module_schema.md)

- [tools/nhd_deploy](nhd_deploy.md)

- [tools/prototypes/README](prototypes/README.md)

- [tools/rshell_recover](rshell_recover.md)

- [tools/tests/README](tests/README.md)
