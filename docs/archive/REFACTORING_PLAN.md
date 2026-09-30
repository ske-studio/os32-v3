# OS32 コード評価とリファクタリング計画

作成日: 2026-08-06(Claude Code による全域調査: kernel/lib/boot/gfx、fs/drivers、exec/kapi/build/tools、shell/cmds/apps/system/tests、programs/lib* の5系統並列レビュー)

> **更新 2026-08-07:** 決断 **D1 (fat12.c 廃止) は実施済み**。`fs/fat12.c` / `fs/fat12.h` を
> 削除し FatFs に一本化した (§3 D1 参照)。あわせて FatFs 側の堅牢化 (pdrv 単位の
> 二重マウント防止、f_mount 失敗時のアンマウント、タイムスタンプ変換) も入れている。
> D2 は据え置き。
>
> この計画とは別に `archive/feat-vdm` および Windows 作業ツリーからの機能取り込みを
> 実施した (KAPI v31 → v35)。経緯は
> [`tasks/wintree_port/PORT_PLAN.md`](../tasks/wintree_port/PORT_PLAN.md) を参照。

---

## 1. 総合評価

**設計は良い。実装の規律にムラがあり、正当性バグと重複が蓄積している。**

| 領域 | 評価 | 一言 |
|---|---|---|
| fs/vfs (VFS層) | ◎ | ツリー内で最良の設計。18スロットのops表、特別扱いゼロ |
| KAPI生成機構 | ◎ | 4生成物が完全自動生成・ドリフトゼロ。ただしバージョン強制は機能していない |
| lib/ (kstring/utf8/lz4) | ○ | 小さく明快。ただし kprintf が console 依存で「カーネル非依存」の掟を破る |
| programs/lib* (21個) | ○ | DB系9ライブラリのAPI形状はほぼ統一。db_finalize リークが系統的 |
| shell/ | △ | 骨格(コマンド登録機構)は良いが、static バッファ×再帰で二重free |
| kernel/ | △ | kernel_main が350行の神関数。ISRハンドラ2本が110行コピペ |
| ~~fs/fat12.c~~ | — | **2026-08-07 に削除済み** (最大の問題ファイルだった。FatFs と二重実装、マルチインスタンス偽装、サブディレクトリ書き込み不能) |
| exec/exec.c | ✕ | ローダ検証がほぼ無い。ネストoff-by-one、saved_esp 共有など実バグ複数 |
| build/ | △ | 分割は良いがヘッダ依存追跡が皆無 → 「make clean 必須」はこれが原因 |
| tests/ | △ | アサーション式テスト13本の素材はあるが自動実行手段ゼロ |

ドキュメントの重大なズレ: **v86/VDM は完全撤去済み**(全リポジトリで参照ゼロ)なのに GEMINI.md:88,102,251 に18ファイル構成として現存する体で記載。KAPI は「162エントリ」ではなく実際は **150関数(v31)**。`docs/KAPI_TABLE.md` は存在しない。

---

## 2. 発見された重大バグ(リファクタリング以前に修正すべき正当性問題)

### カーネル/FS
| # | 場所 | 内容 |
|---|---|---|
| B1 | `fs/ext2_file.c:34` | `ext2_read_file` が最終ブロックで呼び出し元バッファを最大1023バイト**オーバーラン**(`ext2_read_block` は常に1024B書く)。`ext2_read_stream` は正しくガード済み |
| B2 | `fs/vfs.c:254` + 各FS | `dev_type` エンコード `(type<<8)|id` を fatfs_vfs 以外がデコードしない。fd0 への ext2 マウント試行が **hd2 のセクタを読む** / iso9660 は `(char)256==0` で cd0 化 |
| B3 | `fs/ext2_vfs.c:115` 他7箇所 | ext2 エラーコードを未変換で VFS に流し、enum が1つズレているため **NOSPC が「file exists」、ISDIR が「invalid」として報告される** |
| B4 | `fs/fat12.c:221` | FATを8セクタに切り詰め。1.44MB(fat_size=9)で **読みはサイレント切断、書きはボリューム破壊** |
| B5 | `fs/fat12.c:942` | `fat12_vfs_write/unlink` が basename だけ使い**常にルートに書く**。`/sys/foo` に書くと `/foo` ができ、読み返すと not found |
| B6 | `drivers/ide.c:299` | `ide_read_sector` がステータスのERRを捨てて常に成功を返す(write側は確認している) |
| B7 | `drivers/fdc.c:75` | `fdc_read_results` のリトライ `i--` が無制限 → コントローラ固着で**無限ループ** |
| B8 | `kernel/ime_dict.c:174` + `ime.h:236` | `u16 cost` に負のコストを代入しラップ、`>= 0` 判定が常に真 → **ユーザ辞書ランキング破綻** |
| B9 | `kernel/snd_engine.c:130` | `snd_bgm_start_note` がループマーカースキップで `notes[SND_MAX_NOTES]` の1つ先を読み得る。IRQ/タスク間の `g_snd` 排他も無し |

### exec ローダ
| # | 場所 | 内容 |
|---|---|---|
| B10 | `exec/exec.c:240-280` | ヘッダサイズ・`code_off+text_sz`・`entry_off` を一切未検証。4バイトのファイルで**前回プログラムの残留ヘッダを読む**パスあり |
| B11 | `exec/exec.c:74,166` | ネストガードがレベル3で入場許可→インクリメントで4→ `exec_ctx_stack[4]` に**40バイト域外書き込み** |
| B12 | `exec/exec.c:366` | `static u32 saved_esp` がネストで共有され、親のESPが子の値で上書き(現状はGCCのエピローグで偶然マスク) |

### ユーザランド
| # | 場所 | 内容 |
|---|---|---|
| B13 | `shell/main.c:539` | `execute_single` の static バッファ(`allocated_strings[]`)× local `alloc_count`。`if`/`source`/バッチ経由の再帰で**二重free** |
| B14 | `shell/ui.c:194→317` | tab補完がスコープを抜けた `pctx.name_store` への**ダングリングポインタ**を参照 |
| B15 | `cmds/du.c:51`, `find.c:68` | コールバック内から `sys_ls` 再帰 → ext2 の共有グローバルバッファ破壊で**エントリ欠落**(cmd_file.c は collect-then-recurse で正しく回避済み) |
| B16 | `system/hsync.c:147`, `install.c:218` | 空文字列パスで `dst_path[-1]` 参照。加えて `str_cpy/str_cat` が長さ無制限 |
| B17 | `programs/lib*` 全域 | `db_finalize` 呼び忘れが系統的(`chem_spawn` は1オブジェクトごとに1ステートメントリーク)。準拠は libos32text のみ |
| B18 | `apps/vbzview/vbzview.c:442` | 起動のたびにガードページ+未マップ領域を**意図的にデリファレンス**するデバッグコードが本番パスに残存 |

---

## 3. リファクタリング計画

### 決断が必要な2点(先に決める)

**D1: fat12.c を廃止して FatFs に一本化するか**(最重要) — ✅ **廃止で決着・実施済み (2026-08-07)**
- FatFs はビルド・登録済みで機能的に上位互換(LFN、FAT16/32、サブディレクトリ書き込み、rename、mkdir)。fat12.c は 1,152 行で B4/B5 含む問題の巣だった。
- 実施内容: `fs/fat12.{c,h}` を削除。`kernel.c` の `fat12_init()` と FDD ブートマウントを
  `root_fs = "fat"` へ、`exec/exec.c` の `fat12_is_mounted()` フォールバックを削除、
  `fs/vfs.c` と `build/kernel.mk` から参照を除去。
- あわせて FatFs 側を堅牢化した(ブランチには無い新規対応):
  `fatfs_vfs_mount` に pdrv 単位の busy フラグを追加して fd1 の自動マウント試行が
  fd0 のマウントを壊す use-after-free 経路を遮断、f_mount 失敗時に `kfree` の前に
  アンマウントする、`dos_time_to_epoch()` でタイムスタンプを変換する。
- これにより Phase 2 の fat12 修繕(B4/B5/コンテキスト化/チェーンループ統合)は不要になった。
- ホスト側の `tools/mkfat12.py` はイメージ生成ツールとして残存。

**D2: ゲームエンジン系11ライブラリの位置づけ**
- libos32ai/battle/board/chem/econ/ecs/event/inv/map/text/asset は**テストプログラム以外の消費者ゼロ**(出荷ゲーム未存在の投機層)。品質は良いので削除不要だが、リファクタリング投資は最小限(リーク修正のみ)に留め、実ゲーム着手時に本格整備するのが合理的。

### Phase 0 — 正当性修正(即着手可・低リスク・各修正が独立)

上記 B1〜B18 の修正。特に安い順:
1. B1 ext2 オーバーラン(~6行、read_stream と同じガードを移植)
2. B3 `ext2_to_vfs()` 変換層追加(fatfs_vfs.c:33 の `ff_to_vfs()` が手本)
3. B6/B7 ide ステータス確認・fdc リトライ上限(各2〜3行)
4. B10/B11/B12 exec ローダ検証(~15行)+ネストoff-by-one+`saved_esp` を `ExecContext` へ
5. B13 shell の static → local 化(SCRIPT_MAX_DEPTH=4 なのでスタック増は許容範囲)
6. B8 `cost` を i32 へ
7. B2 各 `mount()` に dev_type 検査を追加(D1 の決断後に)
8. B17 `db_query_one()`(1行取得+自動finalize)を libos32db に追加し ~30箇所を置換
9. B15 du/find を collect-then-recurse へ(cmd_file.c:107 のパターンを流用)
10. B18 vbzview のデバッグコード削除、ext2 ブロックサイズ検証(1行)、iso9660 の ctx 無視修正

### Phase 1 — ビルド基盤(全後続作業の効率を左右するので早期に)

1. **`-MMD -MP` + `-include` による自動ヘッダ依存追跡**。リポジトリ全体で最高価値の1手。「KAPI変更後は make clean 必須」(POLICY_DEV.md:123)が消滅する。実は `include/os32_kapi_generated.h` がどの makefile にも prerequisite として現れていないのが根因。`programs.mk:317` のレシピ無しパターンルールは no-op なので削除
2. `git rm --cached cat`(唯一の追跡済みゴミ)、`nhd_deploy.py:823` の `tmp_upload.bin` を tempdir へ
3. `math_test_rs_rust` を `programs` ターゲットへ追加(現在 deploy.yaml にあるのにビルドされない)、`programs/%.bin` に `build/app.conf` を prerequisite 追加
4. `tools/dump_kapi.py` 削除(`/mnt/c/WATCOM/...` の SSOT を上書きする危険な死にスクリプト)
5. `gen_kapi.py` に検証追加(重複名・未知型はエラーに。kapi_rust_gen の「未知型→u32」サイレントフォールバックはABI破壊の温床)

### Phase 2 — 死物削除とドキュメント同期(~2,000行削減、リスクほぼゼロ)

1. GEMINI.md 修正: v86 記載(88,102,251行)削除、KAPI 150関数/v31 に訂正、KAPI_TABLE.md 参照削除、「main() を先頭に」ルール削除(app.ld の `.text.startup` 配置により技術的に不要で、実際ほぼ守られていない)
2. `boot/loader_fat.asm`(458行、`loader_fat_new.asm` に置換済みの死物)+ boot.mk エントリ
3. `lib/fep_engine.{c,h}`(505行、SQLite版IMEに置換済み。唯一の消費者 fep_test.c はビルド不能な死物)
4. `libos32snd`(70行、消費者ゼロ、NULLガード無し)— 削除 or sndctl系を経由させるか決定
5. `dev.h` の DEV_CHAR/chr_read/chr_write/ioctl/priv(未使用)、`hostdrvfs_get_ops`、`vfs.c:12` の fat12.h include、各所の墓標コメント
6. ~~D1 決断後: `fs/fat12.c` 廃止(1,152行)~~ ✅ 実施済み (2026-08-07)

### Phase 3 — 重複統合(挙動同一の機械的抽出)

| 対象 | 内容 | 削減 |
|---|---|---|
| `isr_handlers.c` | 2つの110行フォルトハンドラを `fault_report()` に統合 | ~150行 |
| `gfx_vram.c` | flip-present 3コピー・パレット書き込み3コピーを関数化。1028Bの構造体値コピー×2/フレームをポインタスワップに | 2KB/frame のmemcpy解消 |
| `ide.c`/`atapi.c` | wait_bsy/wait_drq/select_bank を `ata_common.c` へ + バンク所有権の明示(現状 cd0 と hd2/hd3 の交互アクセスでバンク不整合) | ~60行 |
| `ime.c` | getchar/trygetchar/getkey 3兄弟を `ime_pump()` + 薄いラッパへ | |
| ハードウェア定数 | `idt.h`/`gfx_internal.h` の PIC/PIT/GDC 重複定義を `pc98.h` に一本化。`gfx_vram.c` の VSYNC ポート `0x60` vs `0xA0` 不整合を意図的に解決 | |
| `cmds/` | 共有ランタイム `oscmd`(slurp/foreach_file/error/parse_flags)。wc/head/grep の **64KB BSS二重確保**も機械的に解消 | 400〜600行 |
| `less`/`more` | 共有 pager.c 化 | ~250行 |
| キーコード | `include/os32_keys.h` 新設。4種の非互換キー定数(0x1E vs 0x3A の UP など)を API別に文書化して統一 | |
| `system/` | hsync/install/cdinst の手製 str_* を `<string.h>` の strn* へ。3つの再帰ツリーコピーを `os32_walk()` に統合(B15/B16も同時解消) | ~60行+ |
| DB系 | `db_sql_append` 移行の残り3箇所(inv_shop/inv_lottery/econ_core)完了、`db_sql_append` にサイズ引数追加 | ~97行 |
| LZ4 | `boot/lz4_mini.c` を `lib/lz4.c` の二重コンパイル(-DLZ4_FREESTANDING)に統一 | |

### Phase 4 — 構造改善(セマンティクスに触れる。ブート/実機テスト必須)

1. **`kernel_main` 分割**: `boot_init_cpu/devices/fs/userspace()` + ハードコード座標40箇所を `boot_status()` 行カーソル式へ(現在3箇所でラベルが衝突・上書きされている)
2. **`kprintf` のシンク関数ポインタ化**: lib/ → kernel/console の依存を切り「カーネル非依存lib」の契約を回復
3. **gfx の公開/内部境界確立**: `boot_splash.c` の gfx_internal.h 依存解消、`gfx.h` を kernel実装分と libos32gfx 実装分に分割、未実装の `gfx_set_palette_all` 宣言削除
4. **`CmdHandler` を int 返却に**: `$?` とスクリプトの成否分岐が構造的に不可能な現状を解消(~40ハンドラ、コンパイラが全箇所検出)
5. **KAPI バージョン強制**: crt0_c.c に `kapi->version >= ビルド時版数` チェックを注入(現在 `min_api_ver` はデフォルト7のまま16コマンドが v31 構造体にリンクしており、版数は飾り)。app.conf の手書き版数は参照フィールドからの自動導出へ
6. `fat12` 廃止に伴い exec.c の PATH 検索を VFS 一本化(現在 /bin 系フォールバックは fat12_read を通らない非対称)
7. 内部ヘッダ整備: ゲームlib 8個に `x_internal.h`(econ_internal.h が手本)→ その後 `-fcommon` 除去

### Phase 5 — 大物(Phase 0〜3 完了後に判断)

1. **VfsOps のハンドルベース化**: 現在は全opがパス文字列で、512Bリードごとにフルパス再解決。open/close/pread を追加しFSごとに漸進移行(NULL スロットでフォールバック可能な設計に)
2. **ブロックI/Oの dev.c 一本化**: 現在レジストリの実利用者は iso9660 のみ。ext2/fatfs/fat12 の直呼びを統合すればバッファキャッシュ・リトライ・エラー変換の一元点になる
3. **out-of-tree ビルド**(`build/obj/`): 277個の in-source .o と手書き clean リストを解消。Phase 1 完了後のみ
4. **テスト自動化**: `tests/libtest.h`(check/check_eq の13コピー統合)+ `make test` + 生成 .bat + HTTP API(`curl localhost:8032/cmd`)で NG を grep。素材は揃っている
5. `-Wextra` 導入(ノイズ波が大きいので独立タスクとして)
6. Rust ターゲットの ISA 整合(`i686-unknown-none` は CMOV 等を許可し `-march=i386` の C と不一致。実機386/486SXでフォルト)

---

## 4. 全調査結果の所在

本計画は5系統の詳細レビュー(計約100項目、全て file:line 付き)の要約。各項目の根拠・再現条件・修正案の詳細が必要な場合は、該当箇所を指定して質問すること。
