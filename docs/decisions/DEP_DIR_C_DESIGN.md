# DEP_DIR_C — メモリの検出と属性を platform へ — 設計

> 状態: **現行 (2026-10-27)** — 確定 (astra 起草・Opus 2 往復)。段の状態は票 TASK_DEP_DIR が持つ。
> 出所: ~/os32-tmp/run/depdir-c/DESIGN.final.md を 2026-10-27 に写した。冒頭の『r1→r2 の変更』は往復の記録として残す。


## r1→r2 の変更
- C1: test_memory_boot のprobe写し・3か所置換・変異移動・リンク追従をL14bに含める。
- C2: (b)を採用しgeometry/attributesを固定名ヘッダのstatic constへ置く。理由: paging hostのリンク変更を避けられるため。
- C3: memory_boot_page_countsのlegacy上限もgeometry.legacy_endへ置換する。
- C4: low_device.endをbios_rom.endと同じ正典値にし、kernelでKERNEL_LOAD_ADDRとの一致をSTATIC_ASSERTする。
- C5: v86_mem/kselftestの既存マクロ参照を独立照合側として維持する。

- 実物: `/home/hight/os32-v3` main `e59aa4e9`。調査表: 同階層 `SURVEY.md`（d0f5b0e8）、計画: `PLAN.final.md` §2 L14/L15・§3。
- 設計のみ。実装・ビルド・host/NP21/W/Ra266 の試験は未実施。単一利用者・単一タスク・協調切替を前提とする。
- カーネル→機種実装だけを切る。逆向き依存は残す。KAPI 番号・ABI、配置世代、検出アルゴリズム、番地・帯域・PTE・台帳配置は不変。
- C11・整数演算・既存定数の三層・P2V 系を維持。新しい防御機構、動的登録、9801 対応は入れない。

## 1. 実物の確認

|SURVEY #52 の記述|今の main の実物（リポジトリ相対 file:line）|判定|
|---|---|---|
|paging.c:332 の PC98_NATIVE_VRAM|`mm/paging.c:332`、監査側にも `:1620`|一致。監査側も移す対象|
|memory_boot.c:13 の15MiB上限|`mm/memory_boot.c:13`、実際の適用は `:316`|一致|
|memory_boot.c:97 の BIOS 情報|`mm/memory_boot.c:97`、保存済 BDA を `:96` で取得|一致|
|機種のメモリ領域・属性を提供へ|`mm/paging.c:329,379,1622`、`mm/memory_boot.c:176,298` に除外・ROM・背景登録|概要の具体化。矛盾なし|

- **SURVEY との食い違いは0件**。行番号ずれを推測で作らない。以下は表に省略された境界条件。
- `memory_boot.c:110–116`: 保存するのは低位ラップ先1語だけ。ラップ検出時だけ復元。高位の試験語は成功時も復元しない。`：122–127` の2巡目で別名を検出する。
- `memory_boot.c:179–185`: 高位RAM追加時だけ物理モデルへ穴と上限を登録。`：298–305` の台帳背景登録とは別の処理であり統合しない。
- `include/pc98.h:366`: UC対象は `[A0000,A4000)`、`[A8000,C0000)`、`[E0000,E8000)`。CG窓・ROMまで一括UCにしない。
- `memory_boot.c:210–218` の台帳は `[A0000,100000)` 全体を FIXED/UC とする。PTEのUC区間とは異なる仕様を保存する。
- `include/memmap.h:362–374`: システム空間 `[15,16)MiB`、高位開始16MiB、RAM上限2GiB。`：97–98` は両端包含のROM定義。
- 現 platform/pc98 のソース: boot/boot_disk/boot_names/d2_bind/pci_resources/clock/timer/pic/system.c、boot系ヘッダ・platform_io.h・tick_order.h。memory系は未設置。
- `platform/pc98/boot.c:243,256` は既存bind/登録、`build/kernel.mk:34–35,75` は常駐ソースとplatformコンパイル規則。

## 2. 共通の境目と所有

- D3確定設計 §3 の `boot_platform_register` / `boot_platform_run` / boot_context / step一覧には手を加えない。
- B確定設計 §2 と同じ、リンク時固定の内部口とする。新しい登録関数0、ops表0、初期化フェーズ0。
- L14はBDA読出し・既存probe・機種の地図をplatformへ。モデル作成・台帳・ページ割当方針はkernel所有。
- L15は固定の機種記述を追加し、pagingが汎用の区間操作で使う。CPUのPTEビット生成・CR3・TLB操作はkernel所有。

```c
/* 新規 include/platform_memory.h: 内部専用、全区間は半開区間 */
struct platform_memory_geometry {
    u32 legacy_end, system_first, system_end, high_first, ram_ceiling;
};
/* geometryの実体は下記の固定名ヘッダで定義 */
u32 platform_memory_probe_high(void); /* 確認済み高位RAMの終端PFN、無し=0 */
/* L15で追加。PTEビットや台帳enumを機種記述へ埋め込まない。 */
struct platform_memory_span { u32 first, end; };
struct platform_memory_attributes {
    struct platform_memory_span native_uc[3];
    struct platform_memory_span bios_rom;
    struct platform_memory_span low_device; /* 台帳FIXED/UC、PTEとは別 */
};
#include "../platform/pc98/platform_memory_desc.h" /* 型定義後、guard付き */
```

|状態・値|所有者|文脈|
|---|---|---|
|geometry / attributes|platform/pc98/platform_memory_desc.h の static const、確保なし|登録前から参照可能、IRQ操作なし|
|probe のreported/accepted/saved等|platform関数の自動変数|paging前、IF=0、起動1回、IRQ内ではない|
|boot_high_end / boot_ram_kb / boot_memory / meta境界|mm/memory_boot.c、従来のstaticを維持|detect/initの既存位置|
|PD/PT・boot_identity_end・監査状態|mm/paging.c|initの既存irq_save/restore、監査は既存文脈|
|BDAの保存領域|既存v86側|platformはv86_bios_real_lowmemを呼ぶだけ|

## 3. L14 — 検出・除外領域の移動

```c
/* platform/pc98/platform_memory_desc.h: L14からgeometryを定義。15MiBの重複defineは廃止 */
static const struct platform_memory_geometry platform_memory_geometry = {
    MEM_SYSTEM_SPACE_BASE, MEM_SYSTEM_SPACE_BASE, MEM_SYSTEM_SPACE_END,
    MEM_HIGH_RAM_BASE, MEM_PHYS_RAM_CEILING
};
/* mm/memory_boot.c: 公開名と戻り値契約を保存 */
u32 memory_boot_detect(u32 mem_kb)
{
    u32 end = platform_memory_probe_high();
    if (!end) return mem_kb; /* boot_high_endを新たに消さない */
    boot_high_end = end;
    return end * (PAGE_SIZE / KIB);
}
```

- `platform_memory_probe_high` は現 `memory_boot_detect` のBIOS読出しから2巡目までを移植。最後だけ `accepted ? high_first/PAGE_SIZE + accepted*(MEM_1MB/PAGE_SIZE) : 0` を返す。
- BIOSのlow byte→high byte合成、上限clamp、1MiB刻み、addr XOR、24bit mask、volatile、P2V_BOOTを保存。probe語の新規保存やcache操作は追加しない。
- 固定するアクセス列: `low読(saved) → cell書(pattern) → low読比較 → [差ならlow復元してbreak] → cell読比較 → accepted++`、その後 `accepted` 個を先頭から再読。早期break後のアクセスも増やさない。
- `pc98.h` / `v86_bios.h` の機種依存includeをmemory_boot.cから外す。probe定数2個もplatformへ移す。公開ヘッダ memory_boot.h は変更不要。
- memory_boot側のlegacy clamp、高位fit/add/sum、上限clamp、物理モデルの除外、背景登録の番地はgeometry参照へ。memory_boot_page_countsのMEMORY_BOOT_LEGACY_END（:436–437）もgeometry.legacy_endへ置換し重複defineを除去。式・呼出順・失敗時returnはそのまま。
- 穴を全RAM供給源の一律拒否規則に変えない。高位attestationなしにローダ値だけからRAMを追加しない。UNKNOWNとRESERVED/MMIOを混同しない。
- FIXED/ARENA_TOPの分岐、metadata/workspace算式、固定予約表、DMA適格性、台帳type/owner/cacheはL14で変更しない。
- `init/kernel.c:122–132` のBDA保存→detect、`:267` paging_init、`:287` memory_boot_init の位置・ログ・IFは維持。
- **L14のRa266シリアル受入を完了してからL15に進む**。NP21/Wだけでは依存を解除しない。

## 4. L15 — 固定の機種属性

```c
/* platform/pc98/platform_memory_desc.h: L15で追加、既存定数のみ */
static const struct platform_memory_attributes platform_memory_attributes = {
    { {TVRAM_CHAR_BASE, TVRAM_CG_WINDOW},
      {GVRAM_PLANE_B, GVRAM_BRG_END}, {GVRAM_PLANE_I, GVRAM_I_END} },
    {MEM_BIOS_ROM_START, MEM_BIOS_ROM_END + 1},
    {MEM_CONV_END, MEM_BIOS_ROM_END + 1} /* bios_rom.endと同じ */
};
/* mm/paging.c: static helper、既存ループ内で使用 */
static int platform_span_has(const struct platform_memory_span *s, u32 a)
{ return a >= s->first && a < s->end; }
static u32 native_cache_bits(u32 a)
{
    for (u32 i = 0; i < 3; i++)
        if (platform_span_has(&platform_memory_attributes.native_uc[i], a))
            return PTE_PCD;
    return 0;
}
```

- 実装では3をヘッダの配列長定数に集約（新規番地定数は作らない）。PWT=0を維持し、WB/UCの再解釈をしない。
- 固定名ヘッダはplatform_io.hと同じ寄せ方で正典ヘッダを内部includeし、include/platform_memory.h経由で参照する。L14からgeometryをstatic constとし、L15でattributesを追加、.cに外部実体は置かない。
- kernel側でMEM_BIOS_ROM_END + 1 == KERNEL_LOAD_ADDRをSTATIC_ASSERTする（Cの定数式として正典マクロを用いる）。
- paging.c:329 と1622のシステム空間判定はgeometry参照。`:332,1620` のPC98_NATIVE_VRAMをnative_cache_bitsへ。paging.cのpc98.h依存を除去。
- `:379` のROM RO設定は記述のfirst/end-1を渡す。`:1311–1312` の期待属性、`:683` のV86対象上端も同じROM境界を参照。NULL/tramp/guard優先順は変更しない。
- low_deviceはmemory_boot_fixedの機種行を置換。既存の低位WB行→機種UC行→残り固定領域の登録順を維持し、行数・型・owner・PFNを固定する。
- 台帳の背景2区間はL14のgeometry、cache/type/ownerは従来値。低位台帳UCをPTEへ流用しない。
- paging_map_physのdevice UC、surfaceのcache指定、keep_cacheのPCD/PWT継承、DMAプールのWB、V86セッション復元は汎用処理として残す。
- PDE/PT枠、aperture/MMIO窓、APP帯、pool、guard、ledger配置は一切動かさない。memmap.h/pc98.hの既存定数・マクロは他の利用者のため残す。
- 同じ記述を初期化と監査に使うため、hostは必ず移動前の固定期待列と比較する。記述自身を正しさの根拠にしない。
- kernel/v86/v86_mem.c:41の区間表とkdebug/kselftest.c:953のPC98_NATIVE_VRAMは既存マクロ側のまま残し、§7.1でも独立に照らす側として使う。

## 5. 大きさの見積もりと上限

|対象|常駐像増減の予測 B|根拠・検証|
|---|---:|---|
|L14|−64～+256|probeは移動で相殺、geometryは1実体20B、wrapper・参照コード・align差|
|L15|0～+384|attributesは1実体40B、区間照合、既存式/固定行の置換|
|群C計|−64～+640、評価予算+1024|BSS増分0、可変状態の複製なし。static constの翻訳単位別複製・最適化後の残存量はmapで実測、予算は維持|
|kdebug.bin|原則0|口・import slot追加なし。kernelとのペア情報は再生成|

- 常駐予算はmemmap.h:230の式で現在 **0x275000=2,576,384B**。__kernel_resident_endとlink ASSERTでBSS/align込みを比較する。
- コード量で浮動heap等の実アドレスが変わり得るが、算式・予約枠・配置世代を変えない。固定番地と記号由来の配置差を分けて記録する。
- 起動FDの2HD/1.44MB、BOOT+MINIMAL集合の収録物を固定して両方生成。kernel.bin、vmkernel.lz4、クラスタ使用量/空き、常駐endを基点比で保存する。
- 圧縮後の増分は未確定。上限へ触れない見込みだが合格は実測後。容量引上げ・収録削除・SHM/heap縮小で通さない。
- kdebug上限はimage/memory 262144B、load span 524288B（B確定設計§6）。既存生成検査で確認する。
- +1024B超なら記述参照・wrapperの重複を調べ、同じ口のまま削減。基点と同一ツールチェーンで通常/fixture/MINIMALを計測する。

## 6. ファイルごとの変更一覧

|段|ファイル（相対）|新規/変更・概算行数|
|---|---|---|
|L14|include/platform_memory.h|新規20、geometryとprobeの宣言|
|L14|platform/pc98/memory.c / platform_memory_desc.h|新規計100、既存コメント・probeとgeometryを分離移動|
|L14|mm/memory_boot.c|変更130、probe削除/wrapper/geometry参照|
|L14|build/kernel.mk|変更2～6、常駐とfixtureのリンク集合|
|L15|include/platform_memory.h / platform/pc98/platform_memory_desc.h|変更各15/15、attributes|
|L15|mm/paging.c / mm/memory_boot.c|変更各45/15、機種属性参照・固定行|
|各段|tools/tests/dep_dir_c_memory_host.c|新規240～320、probeアクセス列・モデル/PTE採取|
|各段|tools/tests/test_dep_dir_c_memory.py|新規180～240、コピー/ビルド/比較/変異|
|各段|tools/tests/fixtures/dep_dir_c/*|新規、manifest/入力/順序列/区間列、行数は採取件数依存（数百～数千）|
|各段|build/checks.d/check-dep-dir-c-memory-host.mk、tools/check_map.d/check-dep-dir-c-memory-host.yaml|新規各20～40、選択検査へ登録|
|各段|既存tools/testsの該当runner/shim、関連tools/check_map.d/*.yaml|変更各0～25、依存パス/コピー/リンク/変異先を追従|
|必要時|kernel/module/kdebug_imports.def等の実装元指定|変更0～数行、移動したsourceのみ、symbol/slot固定|
|記録|docs/tasks/v3/TASK_DEP_DIR.md|変更5～10、実施した受入・保留・証拠のみ記録|

## 7. 受入 → 試験・変異、既存利用者、段分け

### 7.1 基点とhost

- B確定設計§8の方式: e59aa4e9を同じhost shimで一度採取し、入力・採取手順・SHA・期待列をfixtureに固定。実行時git不要、変更後から期待値を更新しない。
- L15着手時にはL14受入済SHAも記録し、e59aa4e9との地図同一性を再確認。数値番地を持つ機種区間と、リンク記号由来の予約区間を分離して比較する。
- shimは物理read/writeとBDAを記録できる形にする。製品に新しい関数表は入れずhostビルド側でP2V_BOOT/対象メモリを差し替える。

|受入項目|host試験|変異（各1行、実行時の比較失敗を要求）|
|---|---|---|
|BIOS・検出範囲|報告0/1/複数/上限/上限超、既存低位8/9/12/15/16MiB入力、返値と全アクセス列|BIOS上位byteのshiftを8→0|
|保存復元・RAMを壊さない|正常/24bit wrap/書込不成立/短いRAM反復、low全語の前後値と2巡目列、従来の高位残存語も比較|low復元を削除（saved参照は比較式に残す）|
|2巡目の別名検出|途中の別名・先頭の破壊・accepted=0、打切後に余分なread/writeが無い|2巡目の比較を逆転|
|RAM地図・除外|生のmem_kbとRAM合計を別比較、高位attestation有無、PFN区間/type/owner/cache/登録順と失敗戻り|legacy上限を16MiBにする|
|台帳・配置・DMA|FIXED/ARENA_TOP境界3073/3074PFN、高位fit、metadata/workspace、DMA予約の前後を固定列比較|背景のsystem_endを1頁減らす|
|全PTE/区間同一|全boot PT、aperture/top PT、PDE、オンライン高位RAM、ROM/VRAM/穴/guard/窓の全頁を比較、境界±1頁も検査|native_ucの終端を1頁伸ばす|
|cache継承・監査|全PTEのPFN/P/RW/USER/PCD/PWT、動的device/V86/再構築前後。ゲスト採取だけA/Dを除外|native_cache_bitsのPCDをPWTにする|
|新ヘッダ可視・変異成立|原本と各変異コピーを同じ-Werrorでビルドし、原本PASS/変異はassert比較でFAIL|ROM終端を1頁縮め、ビルド成功後の差分検出を要求|
|KAPI/容量/配線維持|既存ABI・レイアウト・package・上限検査、boot一覧と検出呼出位置の前後照合|host fixtureのPTEを1bit反転して比較器自身を検証|

- 新runnerはinclude/platform_memory.h、platform/pc98/platform_memory_desc.h、platform実装、既存の推移的ヘッダ/shim/fixtureを変異用段のコピーに含め、include rootとリンク対象を明示する。
- 置換でローカル変数が未使用にならない変異を選ぶ。未使用なら式を保った値/比較の変更へ直す。-Werror無効化、unused抑制、コンパイル失敗を検出成功とする扱いは禁止。
- 全対象PTE比較は境界の抜取りだけにしない。大きい配列は全件比較し、保存fixtureは同一属性の連続区間へ圧縮可能（PFN算式・総頁数も検証）。

### 7.2 NP21/W と Ra266（hostと別欄で判定）

|環境・段|受入・証拠|
|---|---|
|NP21/W・L14/L15各段|同じ設定・媒体の基点とFD両形式/HDD起動、memの量/地図、VRAM描画、DMA、LAN送受信、シリアル、キー/マウスを比較。ゲスト試験一式の成功/失敗/skip件数を記録|
|NP21/W・各段|host agentを起こして疎通後にhost_testを実行、全結果保存。未起動/timeout/未実施をPASSにしない|
|Ra266・L14必須ゲート|シリアルで同一構成の基点/変更後を採取。RAM量、15～16MiBがRAMでないこと、起動ログ、mem全文が従来と同じ。probe後に起動・メモリ試験完了まで確認|
|Ra266・L15/節目C|シリアルで起動・memの同一性、既存VRAM/DMA診断とゲスト試験結果を採取。描画の目視/物理接続が必要なら別の未完項目として残す|

- 変異はhostの模擬メモリだけで行う。RAM破壊を誘う変異カーネルをゲスト/実機へ配備しない。
- NP21/Wは停止→配備→起動。画像/設定の不可逆変更と実機物理操作は既存D1/D2に従う。Ra266のHDD書込みは試験に加えない。
- 実機不在・シリアル証拠不足は保留。L14のR未完ならL15実装へ進まない。失敗時は保存済の基点kernel/kdebug/画像の組へ戻す。

### 7.3 既存利用者と段分け

- 名前だけのgrepで確認した主対象: test_memory_boot.py、test_memmap_boot.py、test_memmap_audit.py、test_paging_bounds.py、test_mem_stat.py、test_device_reservation.py、test_h4_backing.py、test_shlib_high.py、test_app_band_pde.py、test_v86_low.py。
- 対応host: memory_boot_host.c、memmap_boot_host.c、memmap_audit_host.c、paging_bounds_host.c、paging_rebuild_host.c、highram_stage_host.c、dma_boot_host.c、device_reservation_stage_host.c、v86_low_host.c。
- 広い利用者: test_{memmap_gen,layout_defs,vmkernel_layout,boot_named_constants,kdebug_imports,r1_fixture,gfx_boot,appmem,lease,surface_bundle}.py、r1_recovery.py。本文固定/切出し元/差し替え定義は実装時に該当行だけ確認する。
- test_memory_boot.pyはL14bでmemory.cを写しに追加し、detect_capのP2V_BOOT 3か所確認・host_boot_ptrへの置換をその写しで行う。detect-cap-removedの当て先をmemory.cのreported上限へ移し、写しをリンクに追加、host宣言と固定名ヘッダのinclude/copyを揃える。
- C2(b)によりpaging hostは新規リンク不要。test_{access_walk,app_bb_overlap,exec_budget,gfx_attach,gfx_kernel_fb,ring3_str,shm_user,surface_lease,surface_query,unicode_surface}も固定名ヘッダの可視性と原本PASSを確認し、必要なコピーだけ追従する。
- test_surface_bundle.py:31の変異は`native_cache_bits(phys)`を`0`に置換する当て先へ更新し、旧式消失による当て先なしを防ぐ。ビルド成功後に既存のinitial native UC比較でFAILすることを確認する。
- 既存試験は期待値・成功件数を維持し、実装移動によるinclude/リンク/コピー/置換先だけ追従。既存stubと本物の二重定義を避け、各runnerの所有する差し替えを明示する。
- 選択検査の入力に新ヘッダ・platformソース・fixture・runner・mk/YAMLを追加。memory/paging/memmapに触れる既存検査の選択集合を減らさない。kdebugのsource指定、P2V検査の許可表も既存道具で照合する。

|小段（各codex2時間以内）|完了点・時間目安|並列・依存|
|---|---|---|
|L14a|基点採取・host shim/固定列・比較器、90～110分|単独。コード移動前に固定|
|L14b|probe/geometry移動、test_memory_bootの写し/3か所置換/変異先/リンク追従、常駐/fixture・直結host、90～110分|a後|
|L14c|既存runner追従・全変異・容量/選択検査、90～110分|b後、同一試験の並列編集不可|
|L14受入|NP21/W一式・host_test、Ra266シリアル判定、証拠固定|c後。実機待ちは作業枠外、未完を残す|
|L15a|固定名ヘッダへ属性追加・paging/台帳参照・全PTE固定列比較、90～110分|L14のRa266合格後のみ|
|L15b|既存利用者追従・変異・容量/全体検査、90～110分|a後|
|L15受入/節目C|NP21/W一式・host_test、Ra266 RAM/VRAM/DMA、差分記録|b後、各作業枠110分以内に分割、待ちは別記|

- L14とL15は並列不可。各小段を戻せる差分として保存。固定列採取後の独立host実行は並列可だが、実装とfixtureの期待値更新を同時にしない。
- 2時間を超えそうなら既存runner群の追従だけ別小段へ分割し、未検証の実装を受入済扱いにしない。

## 8. 未決

- **0件**。ユーザー判断を要する新しい仕様分岐はない。既定のリンク時固定口を推奨として採用する。
- Ra266受入待ちや実測容量は作業上の未完であり設計の未決と数えない。L14→実機確認→L15の順序と既存の物理操作承認を維持する。

反論: 0件。

## 確定 (2026-10-27、Opus 5.5 の第 2 回で承認)

- r1 の C1〜C5 は r2 で直った (REVIEW.r1.md)。C2 は (b) 固定名ヘッダの static const (設計者が選択)。
- N1 (依頼文に 1 行): 新しい runner の写しは `d/kernel/` と `d/platform/pc98/` の木の形で置き、`-I d/kernel` を先頭にする (平らに写すと `../platform/pc98/platform_memory_desc.h` が原本を拾い、写しに当てた変異が効かない)。
- N2 (所見): static const の複製は高々 3 翻訳単位で 60B×3 未満、kdebug は読まない。大きさは実測の map で確かめる。
