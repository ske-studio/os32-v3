# HWDEV の核の API・private ABI・台帳 — 参照の表

> 状態: **確定 (M1 で §7〜§9 を追記)** (段の状態は票 TASK_HWDEV が持つ)。
> 出所: ~/os32-tmp/run/devmod/DESIGN.final.md を 2026-10-22 に粒度別に分けて写した。

核 (kdebug) と装置部品 (`/boot/hwdev.mod`) の境界の番号・型・定義だけを置く。分け方と理由は [HWDEV_DESIGN.md](HWDEV_DESIGN.md) §4。i386 cdecl、u32 固定、size/version は完全一致で照合する。

## 1. private ABI の構造体

```c
/* 新 private ABI の骨格 (include/hwdev_private.h)。 */
struct hwdev_identity {
    u32 abi, size, kind, build_id, profile_version;
    u32 kernel_pair_crc, core_image_crc, contract_crc;
};
struct hwdev_lease { u32 slot, generation, session; };
struct hwdev_bundle {
    const struct recover_profile *profile; /* targets/rules/guards/exprs/layouts */
    const struct recover_plan *plan;       /* capture/依存/全辺も既存形式 */
    struct recover_plan_buffers buffers;  /* checked 範囲・長さは別途検査 */
};
struct hwdev_ops {
    u32 abi, size;
    int (__cdecl *call)(u32 op, void *copied_req, u32 size,
                       const struct recover_ops *base,
                       struct recover_transport *transport,
                       const struct recover_profile *s0);
    /* G1: gfinit_call (kdebug/gfinit.h:20-21) と同じ 6 引数。wrapper は名前を変えずに素通しする
       (GF は transport と S0 profile を受けて開始する、gfinit.c:154) */
    void (__cdecl *release_work)(void); /* cleanup 後のみ、HW 操作なし */
};
/* 式は既存 recover_expr の id/argc/kinds/eval。eval は検査済み整数のみ。 */
/* core API は §2 の既存宣言から生成。関数名・型を変えない。 */
struct hwdev_core_api { u32 abi, size, contract_crc, layout_crc; /* §8: 生成済み型付き関数表 */ };
/* kernel/module/module.h: kdebug_ops 末尾へ追加 (§5) */
const struct hwdev_core_api *(__cdecl *hwdev_core_get)(void);
/* facade 内の接続順序 (擬似コード、各失敗は登録前に拒否) */
if (kdebug.state != KDEBUG_RUNNING) return 0; /* ログ・load/start なし */
core = kdebug.ops.hwdev_core_get();
check_core_contract(core); /* size/version/contract_crc を完全一致で照合 */
h = hwdev_load_validate(); /* loader の型/CRC/PAIR 検査、依存 handle 保持 */
fill_hwdev_import_slots(h, core); /* 生成 slot 順・型でコピー、未解決は拒否 */
hwdev_start_initialize(h); /* slot 充填後に開始し call/release_work を核へ登録 */
/* loader 由来の handle/base/size/CRC は部品登録情報に保持する。 */
```

装置境界は `recover.h:200–219` の recover_ops/opaque を再利用し、HWDEV 固有の口は call/release_work だけ。

## 2. 核の関数の slot (26 関数、既存宣言と同型)

slot は 0 始まり、追加は末尾だけ。この表を `tools/hwdev_audit.py` が読み、GF 部品オブジェクトの未解決参照から内部参照と kernel imports を除いた集合と完全一致で照合する。型と呼出し元は宣言元・nm から JSON に生成する。

| slot | 関数 | 宣言元 |
|---|---|---|
| 0 | `trial_begin` | `kdebug/recover.h` |
| 1 | `trial_step` | `kdebug/recover.h` |
| 2 | `trial_end` | `kdebug/recover.h` |
| 3 | `recover_owner` | `kdebug/recover.h` |
| 4 | `recover_owner_configure` | `kdebug/recover.h` |
| 5 | `recover_safe_point` | `kdebug/recover.h` |
| 6 | `recover_plan_enter` | `kdebug/recover.h` |
| 7 | `recover_plan_leave` | `kdebug/recover.h` |
| 8 | `recover_plan_access` | `kdebug/recover.h` |
| 9 | `recover_plan_check` | `kdebug/recover.h` |
| 10 | `recover_bind_plan` | `kdebug/recover.h` |
| 11 | `recover_budget_apply` | `kdebug/recover.h` |
| 12 | `recover_bar_probe` | `kdebug/recover.h` |
| 13 | `recover_profile_crc` | `kdebug/recover.h` |
| 14 | `recover_access` | `kdebug/recover.h` |
| 15 | `recover_restore_point` | `kdebug/recover.h` |
| 16 | `recover_target_address` | `kdebug/recover.h` |
| 17 | `recover_window_hal_init` | `kdebug/recover.h` |
| 18 | `recover_window_access` | `kdebug/recover.h` |
| 19 | `recover_window_cleanup` | `kdebug/recover.h` |
| 20 | `recover_gf_pre` | `kdebug/recover.h` |
| 21 | `recover_gf_result` | `kdebug/recover.h` |
| 22 | `recover_gf_screen_safe` | `kdebug/recover.h` |
| 23 | `recover_hal_wait_frame` | `kdebug/recover.h` |
| 24 | `kpeek_call` | `kdebug/kpeek.h` |
| 36 | `recover_gf_runner_access` | `kdebug/recover.h` |

P6: 既存 import index 0〜35 は維持し、診断 runner の記録付き bus を index 36 に追加する。
核の型付き表では core 関数だけを表の順に並べる (最後のポインタは 26 番目)。

- **G2**: `recover_hal_wait_frame` (recover.h:373・recover_profile.c:119) は gfinit.c:80 の static const 初期化子で関数ポインタとして使う。アドレスは stub のアドレスで、R_386_32 再配置を受ける。
- M1 で GF オブジェクトの未解決 symbol と生成表を照合し、上表以外の既存 kernel imports (確保等) も型付きで供給する (§3)。部品間の symbol 探索は追加しない。

## 3. HWDEV の import の生成 (G3)

- 核は **26** 関数で上表に不足・余剰なし。`recover_hal_wait_frame` は直接 CALL が無くてもアドレス参照が必要。
- kernel imports は **11** (P4 の本番選択門 `gf_production_selected` を末尾追加)。HWDEV 内の index は25から次表順に続ける。既存 kdebug import の番号は再利用/変更せず、宣言の取得元として照合する。

| HWDEV index | 関数 | 既存 kdebug index |
|---|---|---|
| 25 | kprintf | 0 |
| 26 | kmemset | 69 |
| 27 | pgalloc_alloc_phys | 109 |
| 28 | boot_image_info | 212 |
| 29 | pgalloc_free_n_owner | 214 |
| 30 | serial_get_setup | 245 |
| 31 | pci_cfg_read32 | 259 |
| 32 | pci_cfg_read16 | 260 |
| 33 | pci_cfg_write32 | 261 |
| 34 | pci_cfg_write16 | 262 |
| 35 | gf_production_selected | 308 |

`boot_image_info`・`pci_cfg_write32/16` も要る。`kmemcpy` は現行 GF の未解決参照に無いので入れない。`__os32_abi_1_mem_4_fmt_4` は既存 linker の世代契約で、import slot にしない。

- kdebug ではこれらを生成 stub (`kdebug_import_stubs.S`) が `jmp *kdebug_import_slots+N` で呼ぶので、HWDEV に同じ stub をリンクすると kdebug の slot を指して未解決になる。
- HWDEV は自分の .def から `hwdev_import_stubs.S` と `hwdev_import_slots` を生成し、§2 の核の関数と kernel の関数を 1 つの表に並べる。`kdebug_import_stubs.o` は HWDEV にリンクしない。
- gfinit.c・gfinit_resources.c は `generated/kdebug_import_macros.h` を取り込むが getter マクロは使っていない — 取り込みを HWDEV の生成物に差し替える。
- `tools/gen_hwdev_contract.py` が共通契約と同時に stub・slot・metadata・取り込み用ヘッダを生成する。`HWDEV_MODULE` の GF ビルドだけ取り込み先を切替え、両像で配置 assert をコンパイルする。
- `build/hwdev.mk` は同じ machine/ABI flags で GF 部品オブジェクトを別にコンパイルし、`hwdev_main.o` と専用 imports を `build/out/hwdev/hwdev.elf` (map 同居) へリンク、`build/out/hwdev.mod` へ pack する。`build/hwdev.ld` は HWDEV slot 配列を data 先頭に置く。
- 表は facade が開始前に充填する (§1)。slot 25〜35 は kernel の関数のアドレスで埋め、既存 kdebug index は宣言の取得元だけに使う。依存 handle だけで symbol を解決したとは扱わない。

## 4. contract_crc (G4)

生成入力・正規化・照合の方式は §8 を正とする。

## 5. 部品の台帳の 11 枠目 (PM 決定 2026-10-22)

```python
# tools/module_schema.py: 既存 ID/slot は維持。11 番を一つ追加。
MODULE_RECORD_CAP = 11  # 10→11、生成 C・台帳試験も更新
# COMPONENTS 末尾へ (既存 schema の dict 形式)
dict(name='HWDEV', path='/boot/hwdev.mod', dependencies=(1,), required=False,
     module_id=11, phase=CONSTANTS['PHASE_POST_ROOT'],
     required_flags=CONSTANTS['FLAG_PAIR'], allowed_flags=CONSTANTS['FLAG_PAIR'],
     stop_allowed=0, ops_version=1, ops_slots=1,
     image_max=196608, mem_max=196608, load_span_max=262144,
     reloc_max=16384, import_max=128, export_max=1,
     init_symbol='hwdev_init', imports_symbol='hwdev_import_slots',
     ops_symbol='hwdev_ops', ops_names=('initialize',))
```

- 大きさの上限の由来は [HWDEV_DESIGN §5](HWDEV_DESIGN.md#5-大きさメモリの見積もり)、全部品の上限・由来・今の余裕の表は [08_build §8-2](../../08_build.md#起動時に読む部品の大きさの上限と由来)。
- schema:23–24 の注記と、台帳試験 T5c-6 の「11 番目を拒否」を HWDEV の受入へ変える (未定義の 12 番目・容量超過の拒否は維持)。

## 6. KDEBUG の ops の版 3→4・slot 19→20 と波及 (G5)

- KDEBUG_OPS_NAMES の末尾に `hwdev_core_get` を足し、ops_version 3→4、ops_slots 19→20。既存 19 口の順序は維持。
- 揃えるもの: kernel/module/module.h の構造体、KDEBUG ops の初期化子、生成の MODULE_OPS_SLOTS / KDEBUG_OPS_VERSION。module.c:16 の STATIC_ASSERT を版 4 の 20 口 + version で成立させる。
- **G5 (M2 の依頼文に書く)**: KDEBUG_OPS_VERSION は kernel/module/module.h:7 の手書きの定義で、GF の identity (gfinit.c:148 → recover.c:130 の CRC) にも入る。新しい口には `kdebug_export_contracts[MODULE_OPS_SLOTS]` (gen_kdebug_imports.py:213) の型契約も 1 行要る。identity の CRC が変わるのは想定どおり。
- kdebug の既存 import は未使用になっても削除・並替え禁止、必要分だけ末尾追加。内部 ops 版 / 契約 CRC も更新。
- 公開の入口 (旧 S7) は既存の kdebug_call のまま。KAPI は生成元だけを変え、[ABI] の版更新・clean → all を守る ([KAPI_SPEC.md](../../KAPI_SPEC.md) §3-1)。

## 7. M1 の同一像計測 (2026-10-22)

基点 `b0905cd` の S6 ソース、既定 `build/config.mk` の i386/GNU11/-O2、
`CROSS_DIR=/home/hight/opt/cross`、`make all` で再構築。
`build/kdebug.mk` が同じリンクで map を出し、`python3 tools/hwdev_audit.py` が
map の LOAD 一覧・各 .o の size・最終 ELF・KMOD header を照合する。
GF は `gfinit*.o` 7 個と `recover_gf.o`、核は残るリンク入力全体 (selftest・imports を含む)。

| 帰属 (B) | text | rodata | data | BSS (file 外) |
|---|---:|---:|---:|---:|
| 核 | 77,146 | 14,260 | 1,108 | 30,364 |
| GF | 36,195 | 86,529 | 0 | 2,760 |
| リンク整列 | 31 | 39 | 24 | 80 |

- file = 核 **92,514** + GF **122,724** + その他 **24,670** = **239,908B**。
- その他 = prefix 24,576 + payload の整列 94。prefix は header 144、reloc 11,780 (2,945件)、import 3,216 (268件)、export 216 (18件)、名前 5,399、表間/末尾整列 3,821B。
- payload 215,332B、BSS 33,204B、mem 249,856B (末尾頁整列 1,320B)。BSS/prefix は GF に一括帰属させない。
- M1 の最大 node 検査追加後は核92,514 + GF122,684 + その他24,646 = **239,844B** (payload215,268、reloc2,944件、mem249,856B)。上表のS6基点とは分けて計測する。
- これは現行単一像の帰属であり、分離後の核の file サイズではない。分離で import/reloc/prefix が変わるので M5 で再計測する。旧像との差を GF 量にしない。

## 8. M1 固定の G4 生成・照合手順 (M2/M3a の入力契約)

1. 生成入力は `hwdev/hwdev_imports.def` (§2 の順序・§3 の kernel import 順と完全一致を検査)、共有ヘッダの umbrella、配置 inventory。既存宣言を取得する方式は `gen_kdebug_imports.py` を再利用し、型の独自 parser は追加しない。
2. umbrella は `recover.h`, `recover_gf.h`, `gf_stage_state.h`, 辞書順の `gfinit*.h`, `kpeek.h`, 新設する `hwdev_private.h` と kernel import の宣言元を各一度 include。両像とも同じ生成物と i386 の machine/ABI flags、`__KERNEL_BUILD__` でコンパイルする。`RECOVER_MODULE` 等の像固有定義は契約生成に渡さない。
3. 配置 inventory は umbrella が宣言する共有の全 struct/union と全メンバ (配列自体、関数ポインタも含む)、全 enum の列挙子。`recover` から到達する型だけを推測で間引かない。新 private 型も対象。名前の inventory をヘッダ変更と同時に更新し、コンパイラの DWARF (`-g -fno-eliminate-unused-debug-types`) と突き合わせて欠落・余剰を失敗にする。DWARF は列挙の照合用で、型の再実装には使わない。
4. inventory から `sizeof(T)`, `__alignof__(T)`, `offsetof(T, member)`, `sizeof(((T *)0)->member)`、enum 値の定数表を C で生成し、対象クロスコンパイラが出す ELF32 の専用 section から little-endian u32 を読む (ゲスト実行不要)。名前は型名/メンバ名の ASCII 辞書順、enum は列挙子名順。負の enum は u32 の2の補数。値から `STATIC_ASSERT` 全行を生成し、両像の実ビルドで include する。値の変化を片側だけ再生成して通してはいけない。
5. 配置の正規形は UTF-8/LF、各行 `kind\tname\tdecimal-u32\n` (kind は size/align/offset/member_size/enum)。`layout_crc` はその全 byte の CRC-32/ISO-HDLC (`zlib.crc32(data) & 0xffffffff`)。これは配置表の指紋であり、テキスト CRC と別に保持する。
6. umbrella を同じクロスコンパイラで `-E -P`。行番号とコメントは含めず、文字列/文字リテラル内部を維持して外側の空白を除く。リテラルの escape は展開・変更しない。日時・絶対パス等のビルド依存マクロを共有ヘッダで使わない。正規化 slot 宣言は `core:<index>:<symbol>:<declaration>\n` と `kernel:<index>:<symbol>:<declaration>\n` の順 (declaration は同じ空白正規化)。最後に正規化ヘッダテキストを連結した UTF-8 byte 列の同じ CRC-32 を `contract_crc` とする。署名の const・引数順・callback 型、enum・配列長の変更も対象となる。
7. `hwdev_identity` は §1 の32Bを維持。`hwdev_core_api` の固定 prefix は `u32 abi, size, contract_crc, layout_crc` (16B) に確定し、その後に §2 の26ポインタ (計120B、abi=1)。manifest にも `layout_crc` を持たせ、M2 は version/size、両 CRC、PAIR と core image CRC の完全一致を登録前に検査する。CRC 値 0 を未指定/wildcard にしない。
8. M2 の生成器は `tools/gen_hwdev_contract.py`、配置 inventory は `tools/hwdev_layout.json`、共通生成物は `hwdev/generated/`。KMOD の `module_abi_gen` は `contract_crc` と同値。kernel の facade も同じ CRC・配置 assert を使い、core 表全体の RO span と26関数の text 範囲を照合してから slot を充填する。module runtime は検証済みの RO 範囲と IMAGE_CRC を record に保持する。
9. M3a の受入は生成の再実行で差分0、両像の assert 通過、関数順・署名・enum 値・同サイズのメンバ交換・callback の const を片側で変更した負例が照合失敗。生成器はどちらかの像だけを正として自動的に追認せず、一組の契約を生成する。

## 9. 部品の寿命 manifest v1 (M2/M3a の入力契約)

すべて u32、i386 cdecl、自然整列4。構造体は `hwdev_private.h` の生成/宣言対象とし、M1 では未使用の実コード定義を追加しない。

| offset | `hwdev_manifest` のフィールド | 固定値/照合 |
|---:|---|---|
| 0 | `struct hwdev_identity identity` | 32B、abi=1、size=32、kind=1 (GF6200)、profile_version は RECOVER_PROFILE_VERSION |
| 32 | `u32 version, size` | 1、68 |
| 40 | `u32 layout_crc` | §8 の配置表 CRC |
| 44 | `u32 lifetime_flags` | 7 = BOOT_BODY(1) / SYNC_CALLBACKS(2) / NO_DMA(4)、完全一致 |
| 48 | `u32 work_max_pages, dma_max_pages` | 269、0 (下記の試行 RAM 上限、DMA なし) |
| 56 | `u32 ops_abi, ops_size` | 1、16 (`hwdev_ops`: prefix 8B + call/release_work 各4B) |
| 64 | `u32 reserved` | 0 |

- `build_id` は部品生成時の u32 世代 (v1 は `contract_crc`)。`tools/gen_hwdev_pair.py` はビルド済み kernel/KDEBUG の対と IMAGE_CRC を検証し、manifest 用の `build/out/hwdev/hwdev_pair.h` を生成する。manifest の自己申告で loader の handle/generation/base/size/CRC を置き換えない。kernel_pair_crc は KMOD PAIR_CRC、core_image_crc は依存先 KDEBUG の KMOD IMAGE_CRC と一致。部品自体の CRC は loader が検証する (manifest へ自己 CRC を循環埋込みしない)。
- KMOD export の `initialize` は `int (__cdecl *)(const struct hwdev_manifest **manifest, const struct hwdev_ops **ops)`。slot 充填後に一度呼び、静的な RO の2ポインタを返すだけ。登録・確保・I/O・core 呼出しをしない。失敗時は公開しない。manifest/ops の全 span が当該 body の RO 内、call/release_work が text 内であることを facade が checked 範囲検査する。
- `hwdev_ops.call` の第5引数は実宣言どおり **`struct recover_transport *`** (const なし)。GF が max_polls を更新する。第4・6引数の const は維持。M3a は既存 `gfinit_call` への型照合を生成する。
- 登録した body・ops・expr callback・静的許可表は boot 寿命、依存 KDEBUG handle を保持、stop=0、再初期化/差替え/hot unload なし。登録失敗は module rollback/隔離へ返し、公開前の参照だけを取り消す。
- owner の試行 RAM 上限は **269頁 = 1,101,824B (1076KiB)**。2048 node で decode 69 + plan 40 + ROM 32 + flow 128頁を個別に丸める。旧251頁は1790 node時の値で、32MB機の試行中だけ約3%を所有する (PM 決定 2026-10-22)。**この上限と、計画の表の大きさ (GF_PLAN_TARGETS・GF_PLAN_RULES・GF_PLAN_LAYOUTS・GF_INIT_ARGS・RECOVER_CAPTURE_MAX など) は、物理的な制約 (起動 FD に入らない・部品の像の上限・番地の配置) に当たらなければ広げてよい** (ユーザー決定 2026-10-28「特に物理的な（フロッピーディスクに入らない）などがなければ拡張していいですよ」) — 枠に収めるために既存の利用を削らない。小さい静的 control は常駐 BSS に計上し、旧 SIZE_BARS flow は最大 flow の確保前に返す。
- ROM/decode/plan/flow は専有 trial RAM。plan は decode の node/arg を借用し、profile は compiled の表を借用。PAUSED は bundle・lease・両 handle を保持。SIZE_BARS 成功から PREPARE までの保持も参照中として数える。同期 callback が帰る前に解放しない。
- 終端順は局所 cleanup → 共通終端 → 核に結果保持 → plan/profile/expr/opaque の作業 RAM 参照解除 → release_work (I/Oなし・二重呼出し安全) → lease 解放。SIZE_BARS 成功の保持例外は解除/free を次の開始・中止まで送る。GF_STAGES T1 の初期化完了後は [GF_STAGES_DESIGN §3・§10](GF_STAGES_DESIGN.md) に従い、S0 後も init・作業域・owner/lease を保持する。外部 recover、開始途中失敗、二重終端も同じ所有規則を使う。
- FAILED/cold・PTE破損ラッチ・最後の結果・session 世代は核の boot 寿命で、部品の解放/再登録で初期化しない。核の S0 ops/profile に戻すまで部品作業 RAM を返さない。これらの実装は M4、M1 は契約の固定だけ。
- M3a の body は `gfinit_call` と `gfinit_recover_end` を call/release_work に型照合して結ぶ。後者は既存の終端状態でだけ作業 RAM を返す実装を維持し、owner/lease の接続と全出口の整理は M4 の契約に従う。

## 10. M3b の試行入口と外部 recover

| 境界 | 契約 |
|---|---|
| kdebug import 268 | `const struct hwdev_ops *hwdev_module_ops(void)` (`kernel/module/hwdev_module.h`)、S5 の末尾追加。既存 0〜267 は不変、private import 契約 CRC は生成器で更新 |
| op 5〜10 | 公開済み ops を取得し、未読み込み・非公開なら `-1`、要求を書き換えず recovery_attach も実行しない。取得後は既存の attach と 6 引数を維持して `ops->call` |
| op 3/4 | S0 の attach・recover と結果を維持。op 4 の recover が成功または状態遷移した後だけ、公開済みなら `ops->release_work`。部品なしでは解放呼出しなし |

KDEBUG の ops 版 4・20 口と HWDEV ABI は変更しない。GF 本体は M3c で核から除外した。

## 11. M3c の旧 import 保持と像の境界

| kdebug slot | symbol | 扱い |
|---:|---|---|
| 264 | pci_cfg_read32 | retained |
| 265 | pci_cfg_read16 | retained |
| 266 | pci_cfg_write32 | retained |
| 267 | pci_cfg_write16 | retained |
| 308 | gf_production_selected | retained (P4、HWDEV の診断入口だけが呼ぶ) |

GF だけが使っていた上の口は、既存 201 と同じ `.def` の `retained` で保持する。
269 slot の順序・型・実装を維持し、private 生成契約のみ再生成する (KAPI と ops の版は不変)。
生成器の nm 検査は実際の kdebug map の LOAD 入力を読み、GF オブジェクトの混入・
核の未知参照・上の口への核の参照・retained の欠落・kernel 側の旧実装欠落を拒否する。
出力ディレクトリに残る古い GF オブジェクトはリンク入力として数えない。
`hwdev_audit.py` の計測対象は HWDEV の map/ELF/header とし、GF 部品オブジェクトと
部品接続部分を分ける。§7 の M1 単一像実測は過去の基準値として維持する。

## 実装の後の変化 (2026-10-23、PM 注記)

- §6 の「公開の入口 (旧 S7) は … KAPI は生成元だけを変え、[ABI] の版更新」は、`recover` の先例 (`userland/shell/cmd_recover.c` が `kdebug/recover_wire.h` を直接読み KAPI を変えない) に揃える — **2026-10-23 確定** ([GF6200_S7_SHELL.md](GF6200_S7_SHELL.md)): 公開の入口は既存の kdebug_call のまま、shell は private な `kdebug/gfinit_wire.h` を直接読む。sdk/kapi.json・KAPI の版・slot・SDK の生成物は変えない。§6 の KDEBUG の内部 ops の版 4・20 口の説明はそのまま。
