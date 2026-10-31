# tools/gen_hwdev_contract.py — 設計
> 状態: **現行 (2026-10-31)** — 出所: GF6200_S7_SHELL・HWDEV_CORE_API (D1 で移した)
ソース: `tools/gen_hwdev_contract.py`

gfinit.hはwireをincludeし40B assertと関数宣言を保持。

- gfinit_wire.hはgfinit.h経由でDWARF収集に含まれるため、generatorのglobから直接includeだけを除き、冗長な1行追加を防いで生成物・inventoryの差0を維持する。

```c
/* core API は §2 の既存宣言から生成。関数名・型を変えない。 */
struct hwdev_core_api { u32 abi, size, contract_crc, layout_crc; /* §8: 生成済み型付き関数表 */ };
const struct hwdev_core_api *(__cdecl *hwdev_core_get)(void);
```

- kdebug ではこれらを生成 stub (`kdebug_import_stubs.S`) が `jmp *kdebug_import_slots+N` で呼ぶので、HWDEV に同じ stub をリンクすると kdebug の slot を指して未解決になる。
- HWDEV は自分の .def から `hwdev_import_stubs.S` と `hwdev_import_slots` を生成し、§2 の核の関数と kernel の関数を 1 つの表に並べる。`kdebug_import_stubs.o` は HWDEV にリンクしない。
- `tools/gen_hwdev_contract.py` が共通契約と同時に stub・slot・metadata・取り込み用ヘッダを生成する。
`HWDEV_MODULE` の GF ビルドだけ取り込み先を切替え、両像で配置 assert をコンパイルする。

## 4. contract_crc (G4)

## 8. M1 固定の G4 生成・照合手順 (M2/M3a の入力契約)

1. 生成入力は `hwdev/hwdev_imports.def` (§2 の順序・§3 の kernel import 順と完全一致を検査)、共有ヘッダの umbrella、配置 inventory。既存宣言を取得する方式は `gen_kdebug_imports.py` を再利用し、型の独自 parser は追加しない。
2. umbrella は `recover.h`, `recover_gf.h`, `gf_stage_state.h`, 辞書順の `gfinit*.h`, `kpeek.h`, 新設する `hwdev_private.h` と kernel import の宣言元を各一度 include。両像とも同じ生成物と i386 の machine/ABI flags、`__KERNEL_BUILD__` でコンパイルする。`RECOVER_MODULE` 等の像固有定義は契約生成に渡さない。
3. 配置 inventory は umbrella が宣言する共有の全 struct/union と全メンバ (配列自体、関数ポインタも含む)、全 enum の列挙子。`recover` から到達する型だけを推測で間引かない。新 private 型も対象。名前の inventory をヘッダ変更と同時に更新し、コンパイラの DWARF (`-g -fno-eliminate-unused-debug-types`) と突き合わせて欠落・余剰を失敗にする。DWARF は列挙の照合用で、型の再実装には使わない。
4. inventory から `sizeof(T)`, `__alignof__(T)`, `offsetof(T, member)`, `sizeof(((T *)0)->member)`、enum 値の定数表を C で生成し、対象クロスコンパイラが出す ELF32 の専用 section から little-endian u32 を読む (ゲスト実行不要)。名前は型名/メンバ名の ASCII 辞書順、enum は列挙子名順。負の enum は u32 の2の補数。値から `STATIC_ASSERT` 全行を生成し、両像の実ビルドで include する。値の変化を片側だけ再生成して通してはいけない。
5. 配置の正規形は UTF-8/LF、各行 `kind\tname\tdecimal-u32\n` (kind は size/align/offset/member_size/enum)。`layout_crc` はその全 byte の CRC-32/ISO-HDLC (`zlib.crc32(data) & 0xffffffff`)。これは配置表の指紋であり、テキスト CRC と別に保持する。
6. umbrella を同じクロスコンパイラで `-E -P`。行番号とコメントは含めず、文字列/文字リテラル内部を維持して外側の空白を除く。リテラルの escape は展開・変更しない。日時・絶対パス等のビルド依存マクロを共有ヘッダで使わない。正規化 slot 宣言は `core:<index>:<symbol>:<declaration>\n` と `kernel:<index>:<symbol>:<declaration>\n` の順 (declaration は同じ空白正規化)。最後に正規化ヘッダテキストを連結した UTF-8 byte 列の同じ CRC-32 を `contract_crc` とする。署名の const・引数順・callback 型、enum・配列長の変更も対象となる。
7. `hwdev_identity` は §1 の32Bを維持。`hwdev_core_api` の固定 prefix は `u32 abi, size, contract_crc, layout_crc` (16B) に確定し、その後に §2 の26ポインタ (計120B、abi=1)。manifest にも `layout_crc` を持たせ、M2 は version/size、両 CRC、PAIR と core image CRC の完全一致を登録前に検査する。CRC 値 0 を未指定/wildcard にしない。
8. M2 の生成器は `tools/gen_hwdev_contract.py`、共通生成物は `hwdev/generated/`。KMOD の `module_abi_gen` は `contract_crc` と同値。kernel の facade も同じ CRC・配置 assert を使い、core 表全体の RO span と26関数の text 範囲を照合してから slot を充填する。module runtime は検証済みの RO 範囲と IMAGE_CRC を record に保持する。

生成器の nm 検査は実際の kdebug map の LOAD 入力を読み、GF オブジェクトの混入・
核の未知参照・上の口への核の参照・retained の欠落・kernel 側の旧実装欠落を拒否する。
出力ディレクトリに残る古い GF オブジェクトはリンク入力として数えない。
