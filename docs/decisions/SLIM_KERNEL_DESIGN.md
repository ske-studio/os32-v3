# SLIM_KERNEL — カーネル常駐の自己試験を外へ・長い関数を分ける — 設計

> 状態: **現行 (2026-10-25)** — 確定 (astra 起草・Opus 2 往復・PM が条件 N1 と段の順を反映)。段の状態は票 TASK_SLIM が持つ。付表は [SLIM_KERNEL_IMPORTS](SLIM_KERNEL_IMPORTS.md)。
> 出所: ~/os32-tmp/run/slim-kernel/DESIGN.final.md を 2026-10-25 に写した (往復の版と REVIEW はそこに残る)。本文の r2 の見出しと『r1→r2 の変更』は往復の記録として残す。

# SLIM_KERNEL r2 — S3 / S4 詳細設計（2026-10-25）

## r1→r2 の変更

- R1（指摘1）: 試験別の記号・種類・追加数をIMPORTS.r2.mdへ確定し、データ代入・関数番地の置換許可を追加。
- R2（指摘2）: IME候補処理の戻り値を直返しし、-1を呼び手へ返す経路を保存。
- R3（指摘3、PM-b）: 冗長なreader代入と専用口を削除、状態操作は3口、検出可能な変異へ変更。
- R4（指摘4、PM-a）: 旧5枠を削除・番号圧縮、stage/export表/check_nmを追従、旧像は既存pair CRC拒否で確認。
- R5（指摘5）: P2V許可表、memmap/lease/r1 fixture、KAPI layout試験の追従を追加。
- R6（指摘6）: kernel補助はmain後に置き、文字位置を読む既存19試験を検査対象化。
- R7（指摘7、PM-c）: S4はobjdumpとゲスト起動を基本とし、新hostログ基盤を作らず差が出た場合のみCFG照合。
- R8（指摘8）: kapiラッパーと新口の__cdecl指定を撤回し、状態所有ファイルに置く。
- R9（指摘9）: FD第1段の16bit BP折返しを事実化し、LOADER.BIN < 32KiBを着手ゲートにする。

- 対象: `/home/hight/os32-v3` の読取時点。実装担当 astra。ここでは設計だけを作成した。
- PC-9821、単一利用者・単一タスク・協調切替、整数演算。既存の実行・復旧・試験の意味を変えない。
- 権限分離や攻撃対策は追加しない。公開ヘッダの新定数は `#define`。CLAUDE.md [C1]〜[D4] を適用。
- 調査は指定箇所と成果物のnm/size/FAT集計。r2ではレビューに関係する呼出し・再配置・生成器・FD第1段だけを追加確認した。

## 1. 実物の確認

| 票との差（4件） | 実物・根拠 |
|---|---|
| E1: カーネル344KB | `docs/tasks/v3/TASK_SLIM.md:14` に対し `size build/out/kernel.elf`: text=339,219、data=30,519、合計369,738 B（361.07 KiB）。BSS=210,680 Bは別勘定 |
| E2: 自己試験42関数 | 同`:19`に対し `nm -S --size-sort build/out/kernel.elf` の `selftest` を含む t/T は27記号13,510 B。本体16本12,912 B、補助等11記号598 B。42本は再現しない |
| E3: 呼び手はkselftestだけ | 同`:19`に対し `drivers/lgy98.c:136`→link、`gfx/backend_pegc.c:591`→PEGC。後者は検出結果を決める実動作 |
| E4: FDへの効果の前提 | 同`:24`のFD節約は構成依存。現物 `images/os32_boot{,144}.img` の全ディレクトリにkdebug.binもhwdev.modも無い。kdebug入りFDの空きとして使ってはいけない |

- 確認一致: `kdebug/kselftest.c` 2,494行、`boot/loader_fat_new.asm` 1,050行。
- `build/out/kdebug.bin`=111,172 B（108.57 KiB）、`build/out/kdebug/kselftest.o` text=35,115 B、BSS=2,368 B。
- 以下は **nmに存在する本体の全一覧**。B単位、M=移動、R=常駐。static欄は直接参照（括弧内は静的補助関数）。

| 本体 | B | 定義 file:line | 触るstatic / 決定 |
|---|---:|---|---|
| lease_selftest | 2,152 | exec/lease.c:384 | (tables_image)、永続staticデータ直接参照なし / M |
| con_sink_selftest | 1,689 | kernel/con_sink.c:314 | g_reader、試験専用g_self_buf/g_self_src / M |
| kbd_inject_selftest | 1,661 | kernel/kbd_inject.c:320 | (inj_push)、関数内utf8_a等の試験定数 / M |
| exec_tramp_user_selftest | 1,380 | exec/exec.c:584 | ring3_tramp_page / M |
| appslot_resume_mark_selftest | 1,182 | exec/appslot.c:882 | g_slot/g_cur/g_poll_last_tick、(slot_zero) / R |
| launch_selftest | 788 | exec/launch.c:383 | g_req/g_next_token、(row_clear) / R |
| paging_map_user_keep_selftest | 526 | kernel/paging.c:1210 | page_directory/page_tables/pg_enabled、(selftest_as_begin/end) / R |
| appslot_gfx_owner_selftest | 495 | exec/appslot.c:1081 | g_slot/g_cur、(slot_zero) / R |
| exec_kapi_layout_selftest | 465 | exec/exec.c:522 | kapi/ring3_tramp_page / M |
| paging_memmap_selftest | 450 | kernel/paging.c:1381 | page_directory/page_tables/pg_enabled、(fixed_paging_valid/memmap_want_at/memmap_seen_at) / R |
| link_selftest | 440 | net/link.c:1149 | link_ready、(cstr/link_wait_up/open/status/link_counters_reset) / R |
| paging_app_band_selftest | 369 | kernel/paging.c:1252 | page_directory/pg_enabled、(selftest_as_begin/end) / R |
| appslot_abort_admit_selftest | 363 | exec/appslot.c:1027 | g_slot/g_cur、(slot_zero) / R |
| pegc_linear_selftest | 346 | gfx/backend_pegc.c:599 | s_sys16m_ram、(ff2_locked_write/pegc_stat/mmio_w8/w16/pegc_alias_test) / R |
| paging_pd_clone_selftest | 342 | kernel/paging.c:1161 | pg_enabled/pd_selftest_probe、(selftest_as_begin/end) / R |
| db_v50_selftest | 264 | kapi/kapi_db.c:1318 | sql_copy_buf/path_copy_buf、(db_shm_empty/diag/db_copy_input/shm_row_fits_n/db_journal_name) / R |

- 間接的に触る状態も移植検査対象。例: inj_push→g_inj_ring/dest/head/tail/count、linkの待機→通信状態。
- 名前にselftestを含む残り11コード記号も省略しない（すべてR、試験本体の外出し量に算入しない）。

| 記号 | B | 役割・状態 |
|---|---:|---|
| wrap_v86_selftest | 5 | 既存KAPIラッパー |
| kdi_get_kselftest_fail / kdi_get_kselftest_pass | 6 / 6 | 公開結果の読出し |
| kdi_kselftest_reset | 21 | 公開結果の初期化 |
| kdi_kselftest_count / kselftest_count | 28 / 28 | 公開結果の加算 |
| kselftest_run_audit | 43 | 実行時監査入口 |
| kselftest_audit_v86_return | 66 | V86復帰監査 |
| selftest_as_begin | 83 | 試験AS準備（endは独立nm記号なし） |
| kselftest_skipped | 93 | 未実行の報告 |
| kselftest_run_audit.part.0 | 219 | コンパイラ生成の監査本体 |

- データ記号は別: kselftest_fail/pass、lease_selftest_result、pd_selftest_probe 各4 B。
- `kernel/kselftest_report.c:9`、`kernel/kaudit.c:17`の結果・監査を外へ移さない。KDEBUG=0でも必要。

## 2. S3 の方式の比較と決定

| 方式 | 常駐 / FD / 実装負担 | 判定 |
|---|---|---|
| 全部をkdebugへ、staticをポインタで公開 | 最大約12.9KB減。ただしAS/slot/launchの内部配置と復旧手順が外部へ漏れ、保守負担増 | 不採用 |
| 5本をkdebugへ、状態操作を小さい型付き口に限定 | 本体7,347 B減、kdebug約7〜10KiB増。既存の読込・起動段を利用 | **採用** |
| kselftest全体＋移動5本をselftest.modへ | 約42KiBの試験コードを必要時だけ保持可能。ただし同一FDに載せれば容量は減らずヘッダ等が増える | 今票では不採用 |
| selftest.modをFDから省略しHDD/HostDrvに置く | 約35KiB以上のFD節約が可能だが、単独FDの起動自己診断が欠ける | 振る舞い維持に反するため不採用 |

- #7の決定は「kselftestをkdebug.binに残す」。ファイルを分けるだけでFD空きが増えるとは数えない。
- appslot 3本合計2,040 Bはスロット保存・初期化・current・poll時刻等の可変状態が多いので残す。
- paging 4本合計1,687 BはマスターPTE書換え・固定地図判定・AS片付けまで外部契約化する価値が小さい。
- launch 788 Bは要求表/tokenの初期化復旧を外へ出さない。db 264 Bは文字列・SHM内部補助の口の方が大きい。
- link/PEGCはKDEBUG非依存の機能検査。現状の呼出し・タイムアウト・VRAM復元を維持する。
- leaseはtables_imageごと移す（公開PD物理番地からの走査）。lease_next_tokenは公開せず、既存副作用を保存。
- con/kbdはリング配列を公開しない。試験専用配列はkdebug BSSへ、既存のdisable/reset/drop復元手順はそのまま移す。
- execは「計算できるtrampアドレス」と「初期化済みring3_tramp_page」を区別する。
- `exec/exec.c:510` のexec_tramp_page_addrはraw配列の整列番地を返す。初期化済み判定の代用は禁止。

新しい状態口（`include/kselftest_probe.h`、内部私用ABI。型KernelAPIは既存宣言を使う）:
```c
#define KSELFTEST_PROBE_VERSION 1u
struct kselftest_exec_view {
    u32 tramp_page;              /* ring3_tramp_pageそのもの。未初期化なら0 */
    const KernelAPI *api;        /* kapiそのもの。読取用、所有権を渡さない */
};
void kdi_selftest_exec_view(struct kselftest_exec_view *out);
u32 kdi_selftest_inj_push(const u8 *p, u32 len, int dest);
void kdi_selftest_lease_result(u32 result);
```
- 新3口はexec/exec.c、kernel/kbd_inject.c、exec/lease.cに直接実装。C3は外部プログラム向けで、この内部import用にkapi層や__cdeclを足さない。
- exec実体は2フィールドの写し、kbdはinj_pushと同じ呼出し条件、leaseは既存観測変数への1代入。conはowner_exit(saved_owner)後の冗長なg_reader代入を削除する。
- 新口から試験を実行しない。汎用のread/writeメモリ口・状態識別子switch・新規KAPIスロットは作らない。
- 口は起動自己診断の既存文脈だけで使用。IRQマスクや待機を新たに足さず、元の状態復元順序を保持する。
- 試験ごとの全import（記号・種類・既存/追加・間接依存）は [SLIM_KERNEL_IMPORTS](SLIM_KERNEL_IMPORTS.md)。確定時は本文と一緒に写す。追加44契約（FUNC38/GETTER1/SETTER2/PTR3）、全体308枠。
- 移植時の未解決記号とデータ代入を付表と照合。新3口、既存関数import、生成SETTERへの代入変更、PTR参照、inline依存の公開FUNC化、関数番地2件、冗長代入削除だけを許可する。

kselftest側の形（既存のcheck件数・名称・ビット解釈は変えない）:
```c
/* kdebug/selftest_internal.h: 移植先の通常C関数、importマクロ対象外 */
u32 kst_con_sink(void); u32 kst_kbd_inject(void);
int kst_lease(void);
u32 kst_exec_tramp_user(void); u32 kst_exec_kapi_layout(void);
/* kdebug/kselftest.c:399相当 */
u32 bad = kst_con_sink(); /* 以降のcheck列は現状のまま */
/* exec試験内: 状態を一度取得し、従来と同じ分岐・検査に使う */
struct kselftest_exec_view view;
kdi_selftest_exec_view(&view);
/* lease試験末尾: 従来の公開結果変数への書込みを保持 */
kdi_selftest_lease_result((u32)rc);
```

import表と既存利用者:
- 正典は `kernel/kdebug_imports.def`。生成JSONは編集しない（`tools/gen_kdebug_imports.py:2`）。現物269枠、stage S5。
- 旧FUNC枠27(con)、50(exec layout)、52(exec tramp)、67(kbd)、86(lease)を削除して番号を詰める。内部importはKAPIのABI2対象外、予約枠・NULL・reserved機構を追加しない。
- STAGE_KINDSのS2 FUNCを134→129、新S6は付表の種類列どおり（データ種類を含めてよい）。count/ABI hash/JSON/マクロ/カーネル表を同時再生成する。
- build/kdebug.mk:6のKDEBUG_STAGEをS6へ。generate():246のS6 export slotsはS5と同じ[1..16,18,19,20]。GF_ONLY_SLOTSも番号圧縮に追従する。
- check_nmはS2の旧5記号を期待集合から除き、移植4.oを実リンクmapに含めてS6まで照合。GETTER/SETTER/PTR正規化をS6にも適用、アドレス利用は付表のmanifestで検査する。
- hwdev等も再生成・同時ビルドし、新しいindexと契約の整合をtest_hwdev_moduleで確認。ハードコードされた個数・成功表示も追従する。
- 旧kdebugは既存pair CRCで拒否されることを混在像で再確認（新しい保証ではない）。同版一式では従来の試験数・観測結果を維持する。
- ホスト試験が旧selftest名を呼ぶ場合、ホストfixtureだけの転送関数でkst_*へ渡す。本番に旧名の成功stubを残さない。
- lease_selftest_result、kselftest_pass/fail等の観測シンボルはカーネルに残す。道具の番地は毎回現物ELFから解決する。
- 別部品案ならmodule_load→module_start→module_exports→4起動段→module_abortが必要。
- 根拠: `kernel/module_runtime.c:36/120/22/161`。owner・依存・参照数・quiesceを通す必要があり、単純freeではない。
- post_exec/post_unicode/post_probeより前に解放できず、ロード失敗をpassにもできない。採用案ではこの経路を増やさない。

## 3. 調べもの #4 の結論（この票では実装しない）

- **PM後のVK32/CRC32/LZ4だけはC共有可能。リアルモードのFAT/BIOS読込はasmのまま。**
- 根拠: `boot/loader_fat_new.asm:70` org=0x8000、`:312` bits32、`:321` ESP=0x9fffc、`:324`以降がVK32呼出し。
- ファイルは`:19`の0x10000以降へ直読、MAX_IMAGE_SIZE=0x7f000（`:406`）。Cをこの圧縮入力域へ置いてはいけない。
- FAT/ルートバッファは0x6000〜0x77ff、bootinfoは0x7e00（`:95`）。ローダの物理上限は少なくとも0x10000未満。
- 現物loader_fat_new.bin/loader_fat144.binは各2,701 B。第1段boot/boot_fat.asm:188–207はBP=0x8000からFAT鎖をEOCまで読み、長さ上限検査はない。
- 将来案: 16bit入口＋32bit glueをELF化し、`vk32_boot.c`/`lz4_mini.c`をfreestanding -m32で同じ像にリンクする。
- `boot/vk32_boot.c:43`の引数(file,size,window,out_crc)をPM glueから渡し、戻り値とCRC格納を現状通り扱う。
- CRC実装は同`:24`のcrc32_core.inc、strerrorはFD像から除去。BSSが出ればPM開始時に明示初期化、libgcc未解決も検出する。
- 見込みはローダ全体4〜8KiB（未リンクの予算値）、現物比+1.3〜5.4KiB。1KiB/512Bクラスタ単位でFDを圧迫する。
- BPは16bitなので0x10000で0へ戻り、続く転送は0:0000のIVTを上書きする。ゲートは **LOADER.BIN < 32KiB**、かつセクタ丸め後の配置が0x10000を越えないこと。
- 実装着手条件: 生成規則・linker mapで上記容量ゲート、0x10000境界、スタック、GDT、A20との非重複を証明する。
- 従って「C共有は技術的に可能、現物には容量余地があるがCリンク後の適合は未測定」。今票では仕様共通の試験を維持するだけ。
- 将来検査は `tools/tests/test_vk32_crc.py`（C/asm同一fixture、`vk32_boot.c:6`）＋両FD起動、CRC/長さ/LZ4エラーの同一性。

## 4. S4 の分け方

- 新しい補助はすべて元の翻訳単位のstatic。kernelの補助定義はkernel_mainの後、前方宣言だけ前に置く。例外・戻り値・I/O・ログ・初期化順序を変えない。

| 対象・境界 | 補助名と引数 / staticの置場 / 残す範囲 |
|---|---|
| kernel_main (`kernel/kernel.c:184`) | `static void kernel_shell_loop(void)`へ`:695`以降のシェル実行ループ全体（失敗時末尾フォールバックの前まで）を抽出。ループ固有ローカルも移す。boot globalsは元位置、mem_kb/boot_drive依存の初期化はmainに残す |
| kernel_main 第2分割 | `static void kernel_mount_children(void)`へ`:517`の列挙〜`:565`付近のブロックをそのまま抽出。ルートマウント・services起動順は変更しない。捕捉が必要なローカルは入力引数にし、共有context構造体を新設しない |
| exec_launch (`exec/exec.c:1711`) | **今回は分けない**。`:2022`setjmp、`:2027`longjmp復帰、`:2096`ESP保存、`:2202`iretがあり、見出しだけでフレーム寿命を分割できない。既存のvolatile caller情報・static状態は不変 |
| ext2_format_range (`fs/ext2_fmt.c:49`) | `static int fmt_write_super(Ext2Ctx *ctx, const Ext2Layout *lay)`へ`:100`〜`:142`のSB作成と書込。必要なlayout計算値はlayから現行式のまま参照、値の導出時点は移さない |
| ext2_format_range 第2分割 | `static int fmt_write_root(Ext2Ctx *ctx, u32 gdt_blocks, u32 inode_tbl_blocks_per_group)`へ`:308`以降のinode2とディレクトリ作成。fmt_ctxは元関数のstaticのままポインタ渡し。ext2_g_blkも元の共用バッファ、スタックへ複製しない |
| ime_process_key (`kernel/ime.c:332`) | `static int ime_candidate_key(u8 ascii, u8 scancode)`へ`:341`〜`:489`。`if (g_ime.state != IME_ST_INPUT) return ime_candidate_key(ascii, scancode);` と直返し、残りは `return ime_input_key(ascii, scancode);`。g_imeと候補表示staticは元ファイルに残す |
| serial_init_ex (`drivers/serial.c:215`) | **分けない**。211行には長い理由コメントが含まれる。既にserial_plan_outがあり、`:225`〜`:410`のIRQ/PIC/モード変更と復元、`:413`の区間外報告を一体で読める価値を優先 |
| disk_read_common / disk_write (`kernel/v86_bios.c:506/720`) | **両方とも分けず共通化しない**。readはtransfer=0のverifyとD88の実トラック→論理トラック再探索（`:605`）、writeは保護拒否（`:776`）と端数RMW（`:798`）。見出しの類似だけでは同型でない |

- ext2の新補助が返すのは既存のI/O結果。呼元は元と同じ位置で元と同じエラーを返す。書込み回数・順序を固定する。
- IMEの`:477`は候補確定後にreturn -1（`:488`）。呼び手`:769–771/820–822`が生キーを通す。候補処理からime_input_keyへ流さず、0/1/-1をそのまま返す。
- 境界の行番号は目印。実装冒頭の自由変数チェックで引数型を確定し、親のローカルをstaticに昇格して帳尻を合わせない。
- objdump: 同じコンパイラ・旗・生成ヘッダで変更前後の対象.oに `objdump -drwC`。Build IDやリンク配置差を除外。
- 最初は補助をstatic inlineとして抽出し、親の命令列・再配置先・分岐先の対応が同じになるかを見る。
- 正規化は番地/ローカルラベルのみ。即値、I/O命令、volatileアクセス、外部呼先、CR3/IF操作は消さない。
- -O2の一度だけ呼ぶstatic抽出はobjdumpの同一とゲスト起動で確認。差が出た場合だけ補助を展開したCFGで条件・呼出し・書込順を照合し、説明できない候補は戻す。新hostログ基盤は作らない。
- 比較用だけalways_inlineで同一性を調べても、本番成果物の同一性の証拠とはしない。本番のスタック増分も確認する。

## 5. 大きさの見積もり

| 項目 | 現物 | 採用案の差（測定前の予算） |
|---|---:|---|
| kernel text+data | 369,738 B | 試験5本−7,347 B、tables_image除去分は未加算。3口・データaccessor・表・型情報+0.5〜1.5KiB、純減約5.7〜6.7KiB |
| kernel BSS | 210,680 B | con専用配列が減る。公開結果は残る。実装時に配列サイズを別計測 |
| kdebug.bin | 111,172 B | 本体＋tables_image＋importの増分で約7〜10KiB増、115.6〜118.6KiB程度を予算化 |
| kdebug内kselftest | text 35,115 B | 元の判定本体は残り、移植した試験は別.oとして加算。元35KBが消えるとは数えない |
| 新部品 | なし | selftest.modを作らない。比較案ならtext約42KiB＋既存BSS2,368 B＋移植BSS＋ヘッダ/再配置/ページ端数 |
| 1.23MB FD | 空き133,120 B | 現物はkdebug無し。カーネル圧縮差だけ変動。kdebugを新規同梱するなら118,784〜121,856 B消費し、余裕は11,264〜14,336 Bから圧縮差分を加減 |
| 1.44MB FD | 空き351,744 B | 同条件で229,888〜232,960 B残る見込み。hwdev等追加分は別途差し引く |

- 現物VMKRNL.LZ4=224,754 B。非圧縮−7KBをそのままFD空き+7KBとしない。両FDのFAT空きクラスタ数で確定する。
- S4は容量削減を約束しない。対象text増加は合計512 B以内を目安とし、超えればinline/抽出境界を見直す。
- S3の採否ゲート: カーネル純減5KiB以上、追加常駐コスト1.5KiB以内。超過した試験はその移動だけ戻し測定根拠を残す。

## 6. ファイルごとの変更一覧（概算、生成物は自動）

| 新規/変更 | 内容・規模 |
|---|---|
| 新規 include/kselftest_probe.h | 型と3宣言、約30行。実体は状態所有ファイル（kapi/kapi_selftest.cは作らない） |
| 新規 kdebug/selftest_internal.h | kst_*宣言、約20行 |
| 新規 kdebug/selftest_con.c / selftest_kbd.c / selftest_lease.c / selftest_exec.c | 順に約130 / 140 / 120 / 170行、元の試験と専用補助を移動 |
| 変更 kernel/con_sink.c、kernel/kbd_inject.c、exec/lease.c、exec/exec.c | 元試験削除、con以外へ新3口を各5〜15行。元ヘッダの試験宣言削除、conは冗長reader代入の削除だけ |
| 変更 kdebug/kselftest.c、kernel/kdebug_imports.def、tools/gen_kdebug_imports.py | 呼先5種置換、旧5枠削除・圧縮、S6追加・生成表/check_nm追従（計約100〜180行） |
| 変更 build/kdebug.mkとkernelの対象リスト | 新.o追加とKDEBUG_STAGE=S6（約10行）。生成JSON等は同時再生成 |
| 変更 kernel/kernel.c、fs/ext2_fmt.c、kernel/ime.c | 純粋抽出、各約180 / 100 / 250行の移動、増分各10〜30行 |
| 変更 tools/tests/test_{con_sink,kbd_inject,lease,kdebug_imports,kdebug_init}.py と関連fixture | 移植.o・host用接続、旧名互換、生成契約、各10〜60行 |
| 新規 tools/tests/test_slim_selftest.py | 状態復元・実import経由のデータ/関数番地照合、100〜200行。S4用hostログ基盤は作らない |
| 変更 tools/check_p2v_allow.txt、tools/tests/memmap_audit_host.c、lease_host.c、test_kapi_layout.py、r1関連fixture | exec試験2本の許可を移植先へ、lease呼先/リンク入力・ソース検査を追従（各5〜30行） |
| 変更 docs/tasks/v3/TASK_SLIM.md | S3/S4実測・残置・#4結論、各段10行以内。bootソースは変更なし |

## 7. 受入 → 試験・変異・実装段

| 受入項目 | host / ゲストで確かめること | 変異（1行） |
|---|---|---|
| S3 既存判定が同じ | 既存con/kbd/lease hostに移植.oをリンク、ONゲストの4起動段のpass/fail・ビット値・順序を前版と比較 | inj_pushの戻りを1減らし該当判定だけ失敗する |
| S3 状態を壊さず戻る | owner、ring drop/read状態、CR3、free pages、lease結果を前後比較。post_exec再実行も同じ | con_sink_disable（377行相当）を省き、後続push/statの既存bit4検査を失敗させる |
| S3 exec未初期化 | 新host fixtureでview.tramp_page=0を供給、従来の未初期化失敗bitを維持 | snapshotをexec_tramp_page_addrへ置換し負例が落ちる |
| S3 ABI/道具の退行なし | test_kdebug_imports/init、現行一式起動、既存pair CRCの混在拒否、KDEBUG=0のskipped/監査、公開結果シンボル | S6のSETTER種類をGETTERへ変え契約検査が落ちる |
| S3 容量 | nm/size/mapと両FDの空きクラスタ、同梱一覧、増加import数を段ごとに保存 | 移動した本体.oをカーネルに戻し容量/記号チェックが落ちる |
| S4 起動・復旧 | objdump同一＋既存19試験、ゲスト起動/root/サブマウント・CUI/GUI切替・GUI失敗→CUI・shell失敗 | サブマウントをroot前へ動かし命令順序差分とゲスト起動差を検出 |
| S4 ext2 | objdump同一、差が出た時だけCFGでI/O順序/戻り値照合。ゲスト起動後に使い捨て像だけでformat→mount | rootのinode番号またはSB書込先を1変え命令即値差を検出 |
| S4 IME | objdump同一＋ゲスト起動/候補選択・取消・通常文字・UTF-8 BS、候補中のaが生キーで返ること | candidateの-1を通常入力再呼出しへ変え、aがローマ字に積まれる差を検出 |
| 残置部に回帰なし |既存serial_vfast/portc、v86、memory_bootの選択検査とKDEBUG無効起動。未変更部の改修試験は増やさない | 新規変異不要、既存試験の変異を維持 |

- 全段は **astraの1回≤2時間**。超過見込みなら境界・fixture・本体移動を分離し、未検査候補を完了扱いにしない。
- 共通完了検査: `make all`、足した/触った試験を `MUTATE=0`、`check_select.py --lint`。取り込み時check-changed、節目の全体検査。
- S3a: 基準値・付表保存＋S6生成/check_nmの準備とhost検査。旧枠は移動する段ごとに削除・圧縮し、未移植の呼先は維持する。
- 各S3中間段では実際に移動済みの旧枠だけ削除し、新たに必要なimportだけ追加。STAGE_KINDS/countもその段の現物へ合わせ、S3e後に付表の最終308枠となる。
- S3b: con移動＋SETTER import・kselftest置換（新状態口なし）。con/kdebug hostとON/OFF起動。
- S3c: kbd移動。同じ共通ファイルを触るためS3b後に実施。kbd/kdebug hostと起動。
- S3d: lease移動＋tables_image・結果口。test_lease/test_surface_lease/test_memmap_auditとr1 lease fixture（r1_lease_host.c）のリンク追従、前後AS/owner/free pages確認。
- S3e: exec2本移動＋snapshot、P2V許可表追従。test_kapi_layout、新exec fixtureと実import番地比較、kdebug host、4起動段ゲスト比較。
- S3f: 容量ゲート・両FD候補・既存pair CRC混在拒否・道具確認・票反映。配備や既存像上書きはこの設計の作業に含めない。
- S4a: kernel shellループ抽出。S4b: サブマウント抽出。各段objdump・ゲスト起動/復旧・既存19試験（付表末尾一覧）。補助定義はmain後で文字位置を保つ。
- S4c: ext2 SB抽出。S4d: root抽出。各段objdump同一＋ゲスト起動/使い捨て像検査、差が出た時だけCFG照合。
- S4e: IME候補/通常入力抽出＋objdump・ゲスト起動とキー経路確認。差が出た時だけCFG照合、新しいhost記録fixtureは作らない。
- S4a/b、S4c/d、S4eはソースが重ならず並列可。S3は共通import/ビルド/ヘッダ編集があるので直列、S4群とは並列可。
- 並列候補でも最終ビルド・生成物・FD集計・check-changedの統合検査は直列で行う。文書段ではビルドもゲスト実行も未実施。

## 8. 未決（ユーザー判断が要るものだけ）

- **0件**。推奨は本案。FDへのkdebug同梱方針は変更せず、#7の別部品化・#4の共有実装は今票で行わない。
- 実装時に確かめる実import集合・追加text量・C版ローダ容量は技術検査項目であり、ユーザーへの判断依頼ではない。

反論: 0件（指摘1〜9およびPM決定a〜cを採用）。

## 確定 (2026-10-25、Opus 5.5 第 2 回の条件付き承認、PM が反映)

- N1 (P2): HWDEV の kernel import 表は kdebug の index を手書きの文書 `docs/decisions/realhw/HWDEV_CORE_API.md` §3 の第 3 列に持つ (例 `| 26 | kmemset | 73 |`)。`tools/gen_hwdev_contract.py:33-35` と `tools/hwdev_audit.py:50-52` が .def の index と照合するので、番号を詰めると hwdev の生成と監査が落ちる。**§6 の変更一覧と S3b〜S3e の各段に「HWDEV_CORE_API.md §3 の第 3 列を、その段で移した枠の分だけ新しい index に直す」を足す。**
- N2 (P3): KDI_ADDR は `kapi_reserved_nosys` だけに絞ってよい (`shlib_read_page` は stub 経由でも同じ関数に着く)。実装者の判断に任せる。
- PM 決定 (段の順): S3 は S2 (票 TASK_SLIM、`SLIM_HWDEV_DESIGN`) と同じく hwdev の生成物と契約の照合に触れるので、**S2 を main に取り込んでから** S3a を始める。S4 は S2 と並べてよい。S4 の段はゲストの起動を使うので NP21/W を取り合わないよう 1 本ずつ出す。
- 付表 (試験ごとの import の一覧) は同じ置き場の `IMPORTS.final.md`、リポジトリの写しは `docs/decisions/SLIM_KERNEL_IMPORTS.md`。

## S4b の実装で設計者が決めたこと

- 抽出範囲の自由変数は親の引数 `boot_drive` だけなので、補助は `static inline void kernel_mount_children(u32 boot_drive)` とし、`kcg_module_load()` と `SERVICES_ROOT_READY` の間から呼ぶ。列挙・root 判定・サブマウントの既存ブロックをそのまま移す。

## S4c の実装で設計者が決めたこと

- SB 専用の `inodes_count` の写しは抽出後に不要なので削除し、補助では設計どおり `lay` の各フィールドを読む。`ext2_layout_plan` の計算位置は維持し、`fmt_write_super` は `ext2_write_block` の結果をそのまま返し、呼元の同じ位置で `EXT2_ERR_IO` に変換する。

## S4d の実装で設計者が決めたこと

- root 専用の作業変数と `ret` は `static inline int fmt_write_root(Ext2Ctx *ctx, u32 gdt_blocks, u32 inode_tbl_blocks_per_group)` のローカルとし、既存の I/O ごとの `EXT2_ERR_IO` と成功時 `EXT2_OK` を返す。呼元の元ブロック位置で非ゼロを `EXT2_ERR_IO` にし、`fmt_ctx`・`ext2_g_blk` の所有と計算・読書き順序を維持する。

## S4e の実装で設計者が決めたこと

- 現行ビルドの `kernel/ime.c` は `fep.mod` に入るため、指定の `kernel.elf` の .text 比較に加えて、実体の `build/out/fep/ime.o` の命令・再配置と `build/out/fep/fep.elf` の .text も変更前後で比較する。

## S3a の実装で設計者が決めたこと

- 未移植の S3a は S6 を空で開始し、269 枠・旧 5 FUNC・HWDEV §3 の番号を維持する。生成 stage だけ S6 にし、S3b〜S3e で実際の移動に合わせて S2/S6 の種類列と .def を更新する。
- GF 専用枠は番号でなく既存 4 symbol で同定する。check_nm は実リンク map の移植 4.o を S2 の参照解決にも加え、S6 は accessor 名で照合して同じデータの GETTER と SETTER を区別する。
- 関数番地は付表どおり 2 記号の KDI_ADDR/KDI_INDEX を、その FUNC が追加された時点で生成する。manifest は symbol と使用する移植 object を対応させ、実リンク対象のソースにある番地参照を nm 照合へ加える。
- 基準値は基点 25aeb119 を同じ環境で make kernel した成果物から保存し、付表の末尾に数値と証拠の所在を記す。作業木に無い FD 像は未測定とし、S3f の候補像集計へ持ち越す。

## S3b の実装で設計者が決めたこと

- con の移植先は `kdebug/selftest_con_sink.c`、内部宣言は `kdebug/selftest_internal.h` とし、この段では `kst_con_sink` だけを宣言する。S6 は con の 7 FUNC + 1 SETTER、S2 は 133 FUNC、全体は 276 枠とする。
- con の既存ホスト fixture は移植ソースを直接取り込み、`KDEBUG_SELFTEST_HOST` のときだけ生成 import ヘッダを外して既存 owner と SETTER の接続を与える。旧名の転送関数は fixture 内だけに置き、本番の移植先は生成 import を使う。

## S3c の実装で設計者が決めたこと

- kbd の移植先は `kdebug/selftest_kbd_inject.c`、宣言は既存の `selftest_internal.h` に追加する。S2 は 132 FUNC、S6 は con/kbd の 16 FUNC + 1 GETTER + 2 SETTER、全体は 286 枠とし、付表の種類順に並べて HWDEV §3 の第 3 列も追従する。
- `include/kselftest_probe.h` はこの段で必要な version と `kdi_selftest_inj_push` だけを宣言し、実体は元の `inj_push` を直返しする。drop 観測変数の既存実体はカーネルに残し、宣言を `include/kbd_inject.h` に置いて GETTER/SETTER を生成する。
- kbd のホスト fixture は S3b と同じ `KDEBUG_SELFTEST_HOST` で移植ソースを直接取り込み、SETTER と旧名転送を fixture 内だけに置く。既存変異の移設先を追従し、非ゼロ drop 復元と移植 object の import 集合・非常駐を検査する。
