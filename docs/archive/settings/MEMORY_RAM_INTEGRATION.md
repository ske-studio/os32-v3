# RAM 統合 Phase 2 — 次のコード実装票

> 発行: PM (2026-09-09) / 状態: **撤回 (2026-09-30)** — **U6 決定 (ユーザー 2026-09-30、TASK_MEMMAP_V3 D32)**: A / B の大半は K6 (2026-09-10〜12、`kernel/memory_boot.c` / `pgalloc.c` / `physmem.c`) で着地済み、残り (A3 の exec 永久 claim・C・D) は TASK_MEMMAP_V3 D3 で「撤去」と決まった機構の完成形なので v3 では作らない。残る 2 点 (8MB 機もモデル経路 = legacy `pgalloc_init` fallback の撤去、高位 RAM の検出源 0594h の機種資料との照合) は TASK_MEMMAP_V3 §6 **T1 の受入**へ移した。仕分けは U6_PENDING_REVIEW §1-4。本文は 09-13 のままで書き換えない (根拠行の hotdeploy 窓・Cirrus 16MiB 窓・physmem 未リンクは古い)。
> それまでの状態: 計画 (2026-09-13)

計画・未実装。現在の作業ツリーの `physmem` と受入対象の sparse paging を前提にする。今回作成するのは本票だけ。既存の dirty/untracked 変更は保持し、コミット、コード変更、ビルド、配備、エミュレータ起動/API 呼出しは行わない。

## 1. 決定する成果物

次の実装単位は **physmem → 起動時配置 → pgalloc → RAM mapping → sys/exec → ドライバ activation を一緒に接続するコード** とする。モデルだけを追加して終えない。

- 非 PAE のコード上限は PFN 半開区間 `[0, 1048576)`、最後のフレームは `0xFFFFF000`。4GiB を exclusive な `u32` バイトアドレスにしない。
- 16MiB は旧ローダー入力の信頼範囲上限として残すが、allocator の容量上限からは除く。上位 RAM は確認済み機種ソースが渡した区間だけ。総量、最大アドレス、PTE PRESENT、設定値を RAM 検出の代用にしない。
- bitmap は確認済み bootstrap RAM から動的に切り出す。カーネル BSS に 4GiB 分の bitmap/PT を置かず、kmalloc の固定ヒープにも置かない。
- 当面の exec は低位の連続 identity arena のまま。高位 RAM が追加されてもロード位置・ヒープ・スタック・hotdeploy を高位へ動かさない。
- 15–18MiB の一律 MMIO 穴や「念のため 16MiB 直下 1MiB」を導入しない。実在する既存穴は機種情報から除外し、未稼働デバイスの窓は activation の予約判定で扱う。
- 高位検出の実機確認は別ゲート。Phase 2 は synthetic host model を通じて実コードの高位確保を検証できるが、製品で架空の RAM を追加して起動成功を装わない。

## 2. 現行ソースからの根拠

行番号は本票作成時の作業ツリー。古いコメントより実装を優先する。

| 根拠 | 現在の動作 / 統合で直す点 |
|---|---|
| `kernel/physmem.h:1-19,34-84`、`kernel/physmem.c:90-162` | eligibility 専用、最大 64 区間、UNKNOWN が既定。legacy bootstrap は 16MiB clamp と hotdeploy 除外。`find` は allocation を見ず、`reserve_ram` は解除不能。単独では live allocation/exec collision を検出できない。 |
| `kernel/physmem.c:78-87` | MACHINE は呼出側の保証であり検出器ではない。SYNTHETIC は host-only、kernel flag が優先して拒否。 |
| `build/kernel.mk:10-25`、`kernel/kernel.c:329,371-384` | `physmem.c` はまだ C_KERNEL にない。現行は `paging_init(mem_kb)` → `pgalloc_init(mem_kb)` → hotdeploy/selftest。モデルは製品起動に未接続。 |
| `kernel/pgalloc.c:19-76,127-143,212-246` | 3072 ページの静的 bitmap、16MiB clamp。mark と通常 allocation が同じ bit なので free が予約まで解除する。 |
| `kernel/paging.h:36-48,76-94`、`kernel/paging.c:260-320` | static bootstrap PT は 8 枚、残りは要求時追加。新 PDE は master CR3 / live AS なし。失敗は rollback。ただし `reserve_table()` は `BASE + total_pages * PAGE_SIZE` を上端とみなし 16MiB に切るため、疎な allocator へそのまま接続できない。 |
| `kernel/paging.c:492-546`、`kernel/shlib.c:228-230`、`kernel/v86_mem.c:44` | pgalloc の戻り物理アドレスをそのまま dereference する利用者が存在。高位ページを返す前に identity mapping が必要。 |
| `kernel/sys.c:37-101` | `sys_mem_kb * 1024` の未検査計算、top reservation は数値を減らすだけ。pgalloc の除外と連動しない。 |
| `exec/exec.c:340-354,890-900,986-987` | claim A/B の間に EXEC_DYN_RESERVE。開始時 mark、終了時 free。フル arena と A/B と動的穴を区別する必要がある。 |
| `kernel/shlib.c:87-95,145-176` | 4–5MiB の全帯にロード、原本は帯末尾。失敗時 free。未ロードだからと bootstrap metadata の置き場に使うのは不可。 |
| `kernel/hotdeploy.c:73-95` | descriptor は sys getter の buf_phys と固定 buf_size を公開し、magic を最後に書く。単なる総 RAM 上端へ変更するとホストとの契約を壊す。 |
| `kernel/kernel.c:459-495`、`gfx/gfx_core.c:59-100` | GFX 選択、splash、shlib が allocator 初期化より後。GUI=0 だけでは splash による gfx probe を防げない。 |
| `gfx/backend_pegc.c:185-257` | probe 自体がモード変更、MMIO、リニア窓の書き込み試験を行う。init にだけ予約チェックを置くと遅い。 |
| `gfx/backend_cirrus.c:193-215,271-310`、`drivers/wab_glue_xe10.c:103-126` | probe と glue init の境界を監査する必要がある。glue init は map 成功より前にリニア窓を開く。OFF でも窓が閉じない場合があるとの実装注記もある。 |

## 3. 実装所有と変更範囲

担当は役割名であり、今回は誰も起動・派遣しない。共有ファイルの編集は下表順で直列化する。

| 順 | 所有者 | 許可する次回のコード範囲 / 完了物 |
|---|---|---|
| 1 | RAM 統合担当 | `kernel/physmem.[ch]`, `kernel/pgalloc.[ch]`, `kernel/sys.c`, `include/sys.h`, `include/memmap.h`。起動計画、checked PFN allocator、予約 broker、単一 sys 状態。 |
| 2 | paging 担当 | `kernel/paging.[ch]`。疎な RAM の mapping、PT backing 探索修正、既存 Phase 1 rollback/AS 保護維持。1 の内部契約に合わせる。 |
| 3 | exec 統合担当 | `exec/exec.c`, `kernel/shlib.c`, `kernel/hotdeploy.c`。永続 claim と cleanup、全書込み先の配置境界確認。 |
| 4 | GFX/起動統合担当 | `gfx/gfx_core.c`, `gfx/backend_pegc.c`, `gfx/backend_cirrus.c`, `drivers/wab_glue_xe10.c`, `drivers/wab_cirrus.c`, 必要時 `include/gfx_hal.h`, `include/pegc.h`, `include/wab_xe10.h`。preflight と destructive probe/activation の分離。 |
| 5 | RAM 統合担当（統合所有） | `kernel/kernel.c`, `build/kernel.mk`, `kernel/kselftest.c` と host tests。順序をまとめ、physmem を製品リンクする。`build/os32.ld` は原則変更せず配置検査に使う。 |
| 別票 | 機種検出担当 | §8 の根拠確定後の boot snapshot/decoder。上記コードの完成を検出研究だけで止めないが、製品 high RAM enable はこのゲート待ち。 |

KAPI/SDK 変更は不要。新関数は kernel 内部だけで、以下の名前は **提案**（既存 API と混同しない）。GNU89、kstring、定数の正典は [C1/C2/C4] に従う。

## 4. 実装手順 A — 起動計画と動的 metadata

### A1. sys が起動状態を一度だけ所有する

`sys_memory_prepare(legacy_kb, verified_ranges)` を内部入口にし、状態を PREPARING → FROZEN → ONLINE とする。再初期化はエラー/no-op とし live model を reset しない。

1. 旧入力を PFN に変換する前に clamp して `physmem_bootstrap_legacy()` を呼ぶ。旧入力は旧ローダーとの互換契約であり、全ページの新しいハードウェア検証ができたとの意味ではない。
2. 正式な機種 provider が存在するときだけ MACHINE 区間を加える。現状の製品 provider は「追加区間なし」。boot の mem_kb 引数を大きく偽装する変更は禁止。
3. 既知の実在穴、カーネル/SQLite/シェル/低位用途、shlib 全帯、hotdeploy を除外する。shlib 全帯はロード失敗でも保持する。model の RESERVED は非 RAM であり元の RAM 由来を失うため、報告用 detected pages と eligibility count は別に保持する。
4. `physmem_legacy_end()` から provisional な連続 exec 上端を得る。0、最低実行レイアウト不足、hotdeploy 全窓が確認済み旧範囲に収まらないケースは fail-closed。小メモリで descriptor に有効 magic を出さない。
5. hotdeploy の基点は確認済み旧範囲の末尾に一度固定する。以下の metadata/PT workspace/PEGC BB を切り出しても移動しない。

### A2. 配置は exec 上端を決める前に切り出す

`physmem_find()` を `[MEM_EXEC_LOAD_ADDR/PAGE_SIZE, provisional_end)` の確認済み連続 RAM の**末尾側の限定窓**へ使い、metadata と bootstrap workspace を取る。先頭から任意 first-fit して後から exec が被せる実装にしない。

- metadata: 全 PFN 番号を添字にできる `eligible` と `allocated` の 2 bitmap。`limit_pfn` は確認済み RAM の最大 end PFN（総ページ数ではない）から決め、各配列は `ceil(limit_pfn / 32) * sizeof(u32)`、全体をページ切上げ。4GiB の場合 2 配列計 **262144 bytes = 64 pages**。常に最大量を取る必要はない。
- 置き場は確認済み legacy bootstrap RAM、既存 bootstrap PTE が supervisor identity RW、PCD/PWT なし、guard でないことも照合する。`physmem_reserve_ram()` 成功後だけゼロ初期化する。metadata は kernel 寿命の永久予約。
- 4–5MiB は shlib が全帯に書くため不可。0–4MiB の「未使用に見える空白」、exec の現在空いているヒープ、EXEC_DYN_RESERVE の穴にも置かない。
- metadata の下に **bootstrap workspace** を切り出し、最終 exec 上端をその下端まで縮める。この workspace は RAM のまま、一般 allocation を公開する前は paging 統合だけが使う。必要数は確認済み RAM を覆う未設置 PDE 数から算出する（同一 PDE を重複計上しない）。高位 RAM がある場合、当面 `reserve_table()` の APP 帯回避を維持できるよう workspace を `MEM_APP_BAND_TOP` 以上に確保する。
- 高位全域を identity map する最悪時の追加 PT backing は **1016 pages = 4161536 bytes**。これは必要 PDE がある場合だけ確保する動的 RAM で、static PT/BSS 増加ではない。boot RAM の不足時に架空 RAM/exec 内へ fallback しない。必要量・実際の空き・失敗理由を返して起動を止める。低位 8MiB しか確認できない等の構成でこの workspace を確保できなければ high-map 起動は未対応として明示する。これは資源条件であり 16MiB/32MiB のアドレス上限を復活させる措置ではない。
- 高位 RAM がない場合の追加 PT は 0 枚でよい。後日の任意デバイス高位 map は通常の mapped-safe allocation から要求時取得でき、資源不足なら従来どおり失敗する。
- PEGC BB は選択候補について boot-only `sys_reserve_top()` を broker 化して確保する。metadata/workspace と同じ配置計画で扱い、hotdeploy 直下という旧数式のまま重ねない。同サイズの再呼出しは記録した同じ base を返す。
- 最終配置 `[exec arena][bootstrap workspace / metadata / 必要な BB][旧 hotdeploy]` は実際の連続 RAM/穴に従い決める。順番は配置記録で明示し、全領域の非重複を検査する。BB 不要なら予約しない。配置計画は model のコピー上で全検査を済ませ、失敗なら公開状態を変えない。

### A3. exec の所有を boot で固定する

最終 exec 上端から現在の `exec_child_claim()` と同一の A/B、動的穴を一度計算する。式は共通化し、unsigned underflow と guard/最低 heap/stack を先に検査する。

- A/B は起動時に permanent reservation として model に反映し、pgalloc の eligible から落とす。子がいない時間も解放しない。
- A/B の間の EXEC_DYN_RESERVE は RAM eligible のまま。V86/shlib instance/PD/PT が使える従来の動的穴を残す。
- ドライバ衝突用の **legacy arena claim** は穴を含めた `[MEM_EXEC_LOAD_ADDR, final_exec_end)` 全域。A/B bit だけの検査では不十分。
- `physmem_legacy_end()` は自身の A/B 永久予約を入れると縮むため、予約後の model から sys の exec 上端を再計算しない。sys の確定済み配置記録を使う。
- `sys_usable_mem_end()` はこの frozen exec 上端の互換 getter。highest RAM や total pages ではない。`sys_get_mem_kb()` の旧報告と、管理 eligible/free pages は分ける。既存の総量 API を配置計算に使う呼出側を除去する。

## 5. 実装手順 B — pgalloc と sparse paging を実際につなぐ

### B1. allocator の内部契約

提案 `pgalloc_init_model(model, metadata, metadata_bytes)` は容量検証後に初期化し、PREPARING 中は統合コード以外へ公開しない。

- `eligible[pfn] = 1` は正規化 model の RAM かつ allocator 対象だけ。`allocated[pfn] = 1` は通常確保だけ。UNKNOWN/MMIO/RESERVED は eligible=0。model と eligible を二重に外部編集させず、以後の変更は sys の reservation broker に限定する。
- alloc は eligible && !allocated の連続 PFN のみ。疎な穴、reservation、alignment をまたがない。`alloc_page/alloc_n` と range 版を同じ checked core に揃える。全 mutator を irq_save/restore で原子的にする。
- 提案 `pgalloc_alloc_n_pfn(n, first, end)` を PFN range の核にする。既存の byte range 版は整列を厳密に検査して変換する互換 wrapper（hi=0 を 4GiB と解釈しない）。最終ページは PFN 版で指定する。
- free は eligible かつ allocated のみ。free_n は全範囲を先に検査し、予約/非整列/範囲外/二重解放混在なら全件不変。`first + n` ではなく `n <= limit-first` で検査する。予約を「使用中 bit」と同列に扱わない。
- `pgalloc_mark_used()` の void/冪等/無差別 mark を ownership API として残さない。全 call site を A3/C に移し、残す場合も内部 checked reservation wrapper に限定する。free でその予約は解除不能。
- `total_pages()` は eligible の union count、`free_pages()` は eligible-allocated。high-water は別の limit_pfn。上端を `BASE + total_pages*PAGE_SIZE` から復元するコードは禁止。

### B2. 高位ページを返す前に mapping を完了する

1. `paging_init()` の 8 bootstrap PT と既存低位保護を保持。旧 mem_kb clamp は bootstrap 専用と明記し、一般 RAM 上限との名前・用途を分離する。
2. `reserve_table()` は total 由来の上端を廃止し、model/allocator の RAM 区間と既に安全に書ける PTE を走査する。BOOTSTRAP 中は A2 workspace に限定する。通常時も APP 帯、USER、PCD/PWT、非 identity、不在 PT/PTE を除外する現在の安全条件を維持する。範囲付き PFN allocator を使い、探すだけの `physmem_find()` で live PT を取らない。
3. master CR3、live AS=0 で、確認済み RAM の各区間を supervisor `PAGE_RW` で identity map する。低位固定用途・guard・shlib 等に一括 RW を上書きしない。高位 UNKNOWN/予約/MMIO をまとめて PRESENT にしない。既存 16–32MiB PT 内の RAM も忘れず map する。
4. `paging_map_phys(base, base, npages, flags)` の page-count 入口を使い、4GiB end をバイト化しない。新 PDE の pending list/rollback/one-shot paging_init は保持。必要 PT は検証済み low workspace から得るので「高位 PT をゼロクリアするためにその PT が必要」という再帰を作らない。
5. 全必要 map 成功後のみ allocator/sys を ONLINE にし、一般利用者が高位 page を取得可能になる。途中失敗で部分的に高位 allocator を公開しない。停止する失敗経路と、個々の map API が全件 rollback する保証を区別する。
6. workspace 内で割り当てた master PT は master 寿命まで allocated。残りは一般 RAM として利用可能。以後の PT backing は映った高位 RAM からも得られるので 16MiB 制限を残さない。
7. PD/PT、V86 backing、shlib instance は通常 alloc/free を継続できる。ただし任意の DEVICE/DMA 用途は本変更で全物理域対応と宣言しない。DMA 制約は [HW2] と利用者固有の range 制約を維持する。

## 6. 実装手順 C — exec / hotdeploy / 起動順

- `exec.c` の child A/B mark/free を撤去し、boot 固定 claim が存在することを入口で検査する。成功、`exec_launch_abort()`、longjmp、CTRL+STOP、ネスト戻りのいずれでも永久 claim を解放しない。通常 allocator で取った子固有資源のみ既存 owner cleanup で解放する。
- exec がファイルを読み込む最初の書込みから final arena 内であることを検証する。890 行の memmove 直前だけの検査では遅い。shell 切替、FORCE_CPL0、Ring3 の固定 heap/stack 上端との min、ネスト計算も確認する。高位 RAM 追加で user mapping を広げない。
- shlib 全帯は boot 予約を使用し、ロード失敗時の `pgalloc_free_n(MEM_SHLIB_BASE, band_pages)` を削除する。per-AS `.data/.bss` 複製の alloc/free は別の通常 allocation として保持する。
- `sys_reserve_top()` は FROZEN 後の新規予約を拒否。同サイズの既存 BB 利用だけ許可。引数の切上げ overflow、最低 exec サイズ、他予約との重複、model 容量を全件検査する。live exec の上端を変更しない。
- hotdeploy descriptor は確定済み base/size から一度 publish。metadata や high RAM 導入では HD_VERSION/descriptor layout を変えない。窓の全範囲に正しい RAM mapping があることを確認してから magic を設定する。

起動順の完成形:

1. 旧 loader 引数保存、必要なら後続の正式 provider 用の実 BDA snapshot（§8）。低位基本初期化と bootstrap paging。
2. VFS が使える段階で GFX 設定読取りを現行の splash 直前より前へ移す。GUI=0 と GFX=pc98 を混同しない。
3. sys PREPARING、確認済み RAM/実在穴を投入。GFX 候補の非破壊識別と予約計画、BB 必要量、metadata/workspace、最終 exec A/B を確定する。一般 allocator 利用/selftest/exec/splash はまだ走らせない。
4. model/配置 freeze、動的 metadata に allocator 初期化、master RAM mapping、ONLINE。
5. 予約済み候補について destructive probe/init。失敗時の規則は §7。成功後 hotdeploy publish、allocator を使う selftest、exec 初期化、shlib、splash/シェルへ進む。既存 init の移動による依存を `kernel.c` で一箇所調停する。

## 7. 実装手順 D — activation 前の予約衝突判定

提案 `sys_memory_reserve_device(owner, spans)` を唯一の broker にする。一般の `physmem_exclude()` をドライバから直接呼ばせない。

- span は既存 PEGC/Cirrus/glue 定数から列挙。PEGC の 512KiB 窓と制御窓、Xe10 の bank window と **16MiB からの全 2MiB linear aperture** を対象にする。表示する 300KiB だけではない。実際に enable する全窓を preflight に含める。
- span 全件について PFN bounds、live allocated、永久 reservation の owner、**動的穴を含む全 legacy exec arena**、metadata、bootstrap workspace（計画中を含む）、hotdeploy、BB/shlib と衝突検査。対象が UNKNOWN でも MMIO 予約はできるが RAM には変えない。
- boot の provisional arena と衝突する候補は拒否する。MMIO を使うために無言で legacy arena を切り詰める政策は入れない。metadata/BB の計画的 carve-out は sys の別の boot 配置操作であり、activation が勝手に行ってよい操作ではない。
- model のコピーへ全 span を overlay し、容量不足等を含めて成功してから owner 記録と eligible を同時 commit。すでに確保済み RAM を eligible=0 にするだけでデバイスを開いてはならない。同一 owner の同一 span 再利用だけ冪等に許可。
- 予約判定に必要な非破壊識別を `identify` として分離する。**予約失敗時は現行 probe callback 自体を呼ばない**。PEGC のモード切替/linear_selftest より前、Cirrus の chip probe/glue init/linear_enable より前にゲートを置く。boot splash、auto、強制選択、実行中再 init の全経路を同じ入口へ通す。
- mapping/PT 資源の準備を aperture enable より前に済ませる。device mapping は supervisor+PCD、USER は既存の client 面だけ。再 init で USER 属性を消さない。
- 機種の実際の decode 状態（既に窓が開いている、RAM がそこへ出ない等）は machine の実在穴の話。GFX=pc98 だけで物理的に存在する窓を RAM と認定しない。
- destructive probe/enable に到達した予約は失敗・shutdown でも永久保持する。OFF が実際に閉じる保証がないため free しない。probe 失敗後 CUI に戻れても、起動時 RAM を失った理由を報告する。識別失敗で予約前なら何も除外しない。任意の解除・再利用はこの票の対象外。
- FROZEN 後の新規デバイス予約は idle/master/live AS=0 の場合だけ broker で可能。exec/top を移動しないため、初回 BB を必要とする後発 PEGC は安全に拒否して再起動時の設定変更を要求する。以前の予約を再利用する init は許可する。

## 8. 別ゲート — PC-98 の安全な >16MiB 検出ソース

**今回の許可範囲のリポジトリコードから、安全な高位 RAM detector は見つからなかった。候補の手掛かりと、保証できる事実を分ける。**

- `boot/loader_fat.asm:247-280`、`boot/loader_hdd.asm:266-284`、`boot/loader_fat_new.asm:277-295` はいずれも 1MiB から 512KiB 刻み、16MiB で止まる書込み probe。終了条件を 4GiB に広げるのは不可。MMIO、alias、cache、失敗時復元、機種別 decode を保証せず、NMI mask のコメントもその保証にはならない。
- **独立した非破壊入力候補は実 BDA の起動時 snapshot。** `kernel/v86_bios.c:285-319` に、実 IVT/BDA の保存コピーと、ゲスト用に `0x0401` をゼロ化し、`0x0594` の word を「16MB を超える分」としてゼロ化するコードがある。これは場所の手掛かりであり、単位・基準点・有効機種・連続性・穴の decode を証明しない。`0x0594` の値に推測の倍率や 16MiB 加算式を当てない。
- 同箇所は `0x0413` を IBM PC の流儀、`0x05AE` を FDD 対応 bitmap と明示する。これらを PC-98 RAM 容量として使わない。V86 内で書き換えた BDA をホスト物理 RAM の根拠にもしない。
- コードが参照する `np21w-src/src/bios/biosmem.h` はリポジトリ外であり、今回読んでいない。コメント中の参照先を検証済み根拠に昇格しない。`boot/`, `kernel/`, `include/`, `drivers/`, `lib/`, `tools/` の対象ソース検索でも正式な高位 decoder/PC-98 firmware map 入口は確認できなかった。

別票の実装可能な境界:

1. 読取り専用の `pc98_mem_snapshot` と純粋な `pc98_mem_decode` を分ける。snapshot は実 BDA が V86 用に差し替わる前、保存先は低位用途と重ならない小さな kernel 構造体。現状は raw 値を保存しても **RAM 区間を一切追加しない**。
2. 正式に許可された機種根拠から各フィールドの意味/有効性/予約 decode/alias 条件を確定し、根拠付き decoder tests を追加する。実ハードの読取り結果と構成情報の照合は後日行う。未対応機種、矛盾、欠落、overflow は「上位 UNKNOWN」で返す。
3. 出力は総 KB ではなく確認済み `[first_pfn,end_pfn)` と実在穴。kernel 内の一箇所だけが MACHINE として投入する。settings.db、system.cfg、エミュレータ設定値から size を読んで trusted にしない。
4. 正式 provider の完成まで製品の high RAM はゼロ追加を維持する。一方、以下の host integration test は今すぐ実装でき、物理 allocator の 16MiB cap 除去を先に完了させられる。

## 9. 受入テスト — 実装順に RED → GREEN

既存テストの実行記録と、新規に必要なテストを混ぜない。本票作成時にはテストを実行していない。

### H1: host 純粋 model と allocator 実ソース（RAM 統合担当）

既存 `tools/tests/test_physmem.py`, `test_pgalloc_range.py` を基線に、ILP32/GNU89 の実 `physmem.c + pgalloc.c + sys` 配置核を結合する。ホストの backing 配列はテスト専用。製品に synthetic source を混入させない。

- 旧 8/16MiB と極端な mem_kb: legacy clamp と page 切捨て、hotdeploy base の一致、multiply/round-up underflow/overflow 拒否。
- synthetic の 32MiB/64MiB/疎な高位/末尾 PFN を投入。16MiB 超を実 alloc/free でき、穴を跨ぐ alloc_n は失敗。未投入範囲は何をしても取得不能。
- `[1048575,1048576)` の確保/解放、end 超過、0/負 n、byte hi=0、misalignment。失敗で bitmap/counters/model 不変。
- metadata sentinel、shlib、hotdeploy、BB、A/B への誤 free/free_n/mark でも予約が解放されない。mixed free_n は全件不変。再 init が既存状態を消さない。
- 予約全件 transaction、range table 満杯、metadata 不足、低位 workspace 不足、最低 exec サイズ不足に fault injection。公開状態/descriptor magic は不変または無効。
- alloc/free/reserve の IF 保存、探索から commit の不可分性。eligible/allocated/UNKNOWN の集計と疎な highest PFN を独立に照合する。
- default/kernel/kernel+host flag の SYNTHETIC 拒否を既存 tests と同じく検証する。MACHINE flag を付ければ検出が済むというテスト記述は禁止。

### H2: paging 結合（paging 担当）

既存 `tools/tests/test_paging_bounds.py`（通常、`--rebuild nonmaster`, `--rebuild rollback`）を維持し、実 allocator/model 結合ケースを追加する。

- master で high RAM map → 実高位 allocation → zero/copy → PD clone → destroy/free。CPU/特権命令のみ host hook とし、model/allocator/mapping 本体を mock に置き換えない。
- PT は低位 workspace から bootstrap、ONLINE 後は既映写高位 backing も使用。総ページ数を上端と誤解したケースを確実に落とす。
- 未知 RAM、device PCD、USER、非 identity、APP 帯、guard、metadata を PT backing にしない。空 RAM run では range 外 fallback しない。
- 複数新 PDE の途中 OOM、nonmaster/live AS 拒否で既存 PTE/PDE/bitmap/統計が不変。最後のページ、複数 PDE またぎ、非 present PT、paging_init 再呼出しも回帰確認。
- 新 RAM mapping が低位 guard/RO/USER/PCD を変更しない。ONLINE 前には一般高位 alloc を呼べない。

### H3: 所有/activation の結合（exec/GFX 担当）

- exec の file read 直前から cleanup まで配置 sentinel を置き、成功・失敗・ネスト・longjmp の全経路で A/B 永久保持、動的穴の通常 allocation は回収される。
- shlib 読込み失敗でも全帯予約維持。per-AS 複製だけ free。high RAM 増加で sys legacy end / hotdeploy base が変化しない。
- `GFX=pc98` は optional probe/予約ともゼロ。auto/forced/splash/re-init の各入口で衝突を注入し、予約失敗なら **probe callback / aperture-enable / VRAM write の回数がゼロ**。
- PEGC 窓、Xe10 bank、linear 2MiB のそれぞれへ allocation/metadata/hotdeploy/BB/exec claim を重ねて拒否。A/B の間の穴が現在空いていても full arena collision として拒否する。
- 確認済み RAM として 15–18MiB を投入した CUI model では、実在穴と active reservation がなければ通常 eligible。無条件除外がないことを検証する。
- probe 失敗後の永久予約保持、同一 owner 冪等、異 owner 拒否、map failure が enable より前、late BB 拒否を確認する。

### H4: target build と実行確認（後続の排他検証担当）

- 実装時に対象 GNU89 `-m32 -march=i386 -Wall -Wextra -Werror` compile、`make kernel` と該当 `make check` を実行。環境/依存で止まったものは失敗・未実施を分けて記録する。
- `kernel.map` の `__bss_start/__bss_end` と固定 heap/stack/SQLite 境界を比較する。bitmap と追加 PT が BSS に最大サイズで増えていないこと、physmem が製品リンクされ host define がないことを実出力で確認する。
- 後日、許可を得た停止→配備→起動の排他セッションで kselftest、CUI 8/16MiB、shell/gshell、exec nest、V86、hotdeploy、PEGC/Cirrus の回帰を確認する [D1/V1/V4]。現在はエミュレータ停止・API 禁止を維持する。
- >16MiB の実機 RAM 認定、機種 decoder、実際の alias/cache/MMIO 境界は別ゲート。host synthetic PASS と実 RAM PASS を別欄に記録する。

## 10. 完了条件 / 今回の検証範囲

Phase 2 CODE 完了は A～D と H1～H3 の統合、および H4 の target build/配置検査まで。単なる bitmap サイズ変更、physmem の単体 PASS、疎 PTE の追加だけでは完了扱いにしない。実機対応完了は §8 の正式 provider と H4 の実機確認が別途必要。

今回確認したのは現在ソース・対象 dirty diff・build 入力・既存テスト構成と設計整合だけ。新規作成は `docs/archive/settings/MEMORY_RAM_INTEGRATION.md` のみ。ネットワーク、環境変数/秘密情報、`docs/hw/`、エージェント起動、エミュレータ/API、コード編集・ビルド・配備は行っていない。
