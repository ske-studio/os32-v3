# 公開 KernelAPI 表の拡張と配布世代の切替

> 出所: `~/os32-tmp/run/kapi-cap-design/DESIGN.final.md`
> 状態: **現行 (2026-11-21)** — KAPI v89 の表の容量と配布世代。実装は 2026-11-21 に main に入った (10c6a5d3c、CI の check・build success、NP21/W の受入 PASS)。

## r2→r3 の変更

- P3 配置表に v88 の容量・予約口・データ変位を併記した。
- P1 PPC の exec 無効時はゲートの 1 頁だけを公開する。
- P1 構築・公開・写像の契約と両機種の起動停止点を定めた。
- P2 アプリ・シェル・shlib の診断順と要求版超過時の案内を揃えた。
- P1 部品の包装と照合を対応付け、A→B の互換対象を限定した。
- P1 hostdrv と hsync は全候補の照合後に転送を始める。
- P2 PPC の既存包装経路、再構築依存と旧 ELF の試験を明記した。
- P2 既存検査の現行期待値、旧入力、変異の担当を定めた。
- P2 原本ごとの小段と生成順、容量・比較・復旧の統合条件を定めた。

2026-11-21 の決定（DECISIONS.md）に従う。容量と世代を v89 で一括して切り替え、AV0 の追加を v90 として別に受ける。

## 1. 新しい配置

現行 v88 の容量原本は sdk/kapi.json の func_capacity=300。実装 298 口、予約は 298・299、データ欄は 0x4b8・0x4bc。A では実装順を保ち、予約は 298–383、データ欄は 0x608・0x60c に移る。

容量は 384 口を推奨する。現行 298 口に AV0 の 39 口を足すと 337 口となり、47 口を次の末尾追加に使える。32 bit の両 CPU で実表は 1552 B。x86 の KAPI_ADDR の 4 KiB 枠にも収まる。カーネルの実表とユーザー表でメンバーの変位を共通にする。

|先頭からの変位|内容|
|---|---|
|0、4|magic、version|
|8 + 4i（0 ≦ i < 384）|関数 i。slot 297 は 0x4ac、300 は 0x4b8、383 は 0x604|
|0x608、0x60c|sbrk_heap_limit、shm_base（各 u32）|

ユーザーへ渡す基点 B は 4096 B 整列。exec.c の静的配列を 8192 B・ページ整列にし、配列自体の物理先頭を使う。以下は B 相対の半開区間。

|区間|用途|
|---|---|
|[0,0x610)、[0x610,0x710)|ユーザー表、NUL を含む文字列写し場 256 B|
|[0x1000,0x1c00)|スタブ 384 本。i の入口は B+0x1000+8i|
|[0x1c00,0x1cc0)|sys_ls shim。PPC の共通 sc は +0x1c08|
|[0x710,0x1000)、[0x1cc0,0x2000)|ゼロ。隣の BSS を含めない|

実表の予約口をゼロ初期化し、dispatch は実装数を上限としてから word[2+i] を読む。ユーザー表には各スタブの番地、sys_ls には shim の番地を置く。データは実表から転記し、起動・親への復帰の双方で現在のヒープ上限を反映する。layout と emit は表末端 ≤ 写し場先頭、写し場末端 ≤ 4096、スタブ末端 ≤ shim 先頭、shim 末端 ≤ 8192 を減算で検査する。全域を初期化してから公開し、PPC ではコード全域のキャッシュを同期する。

exec 有効時は両機種とも最初のユーザー AS の前に B と B+4096 をそれぞれ同じ物理番地へ USER/RO で写す。範囲全体がカーネルの予約域内にあることを先に確認し、写像失敗時は起動を打ち切る。子 AS でも両頁を継承する。アクセス検査は VA と物理フレームの対応を各頁で照合し、ユーザー書込みを両方で拒否する。起動監査では 2 枚とも照合し、前後の頁への USER 漏れを検出する。

呼出側は paging_boot_user_shared(base, pages) に PC-98 と PPC exec 有効時は (B, 2)、PPC exec 無効時は (user_gate, 1) を渡す。後者はトランポリン構築を通らない。整列・枚数・範囲を検証し、写像成功は 0、失敗は負値。途中失敗なら追加済みの写像を外す。子 AS・監査・アクセス検査は成功時に記録した base と pages を使う。

起動順は、静的領域に arch_syscall_layout(384, &layout)、arch_syscall_emit(B, Bのユーザー番地, 8192, 384)、表と文字列の初期化、arch_syscall_publish(B, 8192)、写像の順とする。layout・emit は成功時非ゼロ、失敗時 0。publish は void で必要なキャッシュ同期の完了まで戻らない。構築を exec_init から早期写像の直前に移し、ring3_trampoline_init の成功は 0、失敗は負値として呼出側に返す。後段の exec_init で再構築しない。PC-98 は kernel_boot_memory.inc、PPC は ppc_kernel.c で構築または写像の失敗を記録し起動停止。どちらもユーザー AS 作成へ進まない。

## 2. 版と拒否

v89 は JSON の version=89、func_capacity=384、kapi_abi=2、crt_kapi_symbol=os32_kapi_v89 とする。api は現行 298 口順のまま。OS32X 形式 5、メモリ配置世代 4、shlib protocol 2 を引き継ぐ。v90 は同一世代・同一 CRT 実名で AV0 本体 33 口を slot 298–330、追補 6 口を 331–336 に末尾追加する（AV_HAL_DESIGN.md の宣言順）。両段で共有ヘッダの KAPI_VERSION を同期する。v90 の口を使うものは min_api_ver ≥ 90 とする。

包装時はリンク済み ELF の .os32_kapi_layout から 0x608 を読み、OS32X の +44 に刻む。+48 に ABI 世代 2 を刻む。旧 ELF のヘッダだけを更新する経路を許さず、C/ASM の各単位・アーカイブメンバー・Rust crate の世代記録をリンク前に検査する。世代 1 の入力は rebuild 要求で失敗し、旧 CRT 実名への別名を用意しない。

|対象|拒否点と利用者への結果|
|---|---|
|通常アプリ|exec_launch.inc が AS 確保・entry 前に os32x_layout_check。旧 0x4b8 / 世代 1 は OS32X_LAYOUT_MISMATCH → EXEC_ERR_INVALID。パス、bin/kernel の変位と「KAPI data layout mismatch; rebuild required」を表示。変位だけ一致して世代違いなら「KAPI ABI generation mismatch; rebuild required」。|
|常駐シェル|同じ入口で g_layout_reject を立てる。旧 gshell は rebuild required を表示して CUI へ。旧 CUI シェルも実行せず、起動媒体のフォールバックでも同じ照合を行う。|
|GUI 共有ライブラリ|sys/kern/mm/shlib.c の shlib_init が頁確保前に SHLIB_REJECT_LAYOUT、戻り値 -1。PC-98 は「GUI shlib: rebuild required (KAPI data layout) -> CUI shell」を表示。要求版超過は SHLIB_REJECT_MIN_API でカーネル更新を案内。|
|逆向き|世代 2 のアプリを v88 が配置違いで拒否する。v89 上の v90 必須アプリは要求版超過で EXEC_ERR_INVALID。|

既存の exec_run（FD シェルも通る）と shlib_init の早期照合を使う。有効な形式の入力では変位、ABI 世代、要求版の順に最初の不一致を報告する。前二者は再構築、要求版だけが高ければ要求版と実行中の版を示してカーネル更新を案内する。シェルのフォールバックも理由を保持する。

|部品|包装する MI_KAPI / MI_LAYOUT|MI_ABI とロード時の契約|MI_PAIR_CRC|
|---|---|---|---|
|kdebug|KAPI_VERSION (A=89、B=90) / 4|kdebug_imports.json の module_abi_gen と KDEBUG_ABI_GEN、ops 版・imports を照合|対応 vmkernel.lz4 の pair_crc|
|FEP|同上|fep_imports.json の module_abi_gen と FEP_MODULE_ABI_GEN、ops 版・imports を照合|同上|
|HWDEV|同上|hwdev_imports.json の module_abi_gen と HWDEV_CONTRACT_CRC、ops 版・imports を照合。core の契約・layout CRC も一致|同上|

各ローダーの policy は実行中の KAPI_VERSION、メモリ配置世代、platform_module_pair の有効な CRC を渡し、module_image が init 前に照合する。部品の MI_KAPI は公開 ABI 世代 2 と別の値である。

hostdrv は絞り込み後の全転送候補を列挙し、各 OS32X 本体の変位・世代を配布対象の期待値と突き合わせる。--no-manifest でも同じ事前走査を通し、全件成功後にコピーし名札にも両値を記す。hsync は対象版を配布一式から確定し、選択された全件の名札と転送元ヘッダを読んで照合してから最初の作成・切詰め・書込みへ進む。対象版や必須の照合情報を確定できなければ転送前に拒否する。

## 3. ファイル別の担当段

段 A は v89、段 B は v90。共通ファイルは本線が所有し PPC は同じ差分で受ける。表の担当段は初回の変更・生成または照合の時点。B でも版同期・生成・契約照合を一式繰り返す。A の途中を単独配布しない。

|ファイル|担当段|変更または照合|
|---|---|---|
|sdk/kapi.json|本線 A|容量・世代・CRT 実名を定義|
|sdk/include/os32/os32_kapi_shared.h|本線 A|版を同期|
|sdk/include/os32/os32_kapi_generated.h|本線 A|JSON から再生成。構造体・予約・刻印・CRT 参照を検査|
|sdk/include/os32/os32_kapi_slots.h|本線 A|JSON から再生成。構造体・予約・刻印・CRT 参照を検査|
|sys/kapi/kapi_generated.c|本線 A|JSON から再生成。構造体・予約・刻印・CRT 参照を検査|
|sys/kern/exec/exec_kapi_init.inc|本線 A|JSON から再生成。構造体・予約・刻印・CRT 参照を検査|
|sys/include/kapi_call_generated.h|本線 A|JSON から再生成。構造体・予約・刻印・CRT 参照を検査|
|include/ppc_kapi_adapter_generated.h|本線 A|JSON から再生成。構造体・予約・刻印・CRT 参照を検査|
|sdk/rust/os32api/src/kapi_generated.rs|本線 A|JSON から再生成。構造体・予約・刻印・CRT 参照を検査|
|sdk/include/os32/os32_generations.h|本線 A|JSON から再生成。構造体・予約・刻印・CRT 参照を検査|
|sdk/os32_generations.py|本線 A|JSON から再生成。構造体・予約・刻印・CRT 参照を検査|
|sdk/rust/os32api/src/generations.rs|本線 A|JSON から再生成。構造体・予約・刻印・CRT 参照を検査|
|sdk/include/os32/os32_unit_stamp.h|本線 A|JSON から再生成。構造体・予約・刻印・CRT 参照を検査|
|sdk/crt/generations.inc|本線 A|JSON から再生成。構造体・予約・刻印・CRT 参照を検査|
|sdk/link/generations.ld|本線 A|JSON から再生成。構造体・予約・刻印・CRT 参照を検査|
|sdk/link/app.ld|本線 A|JSON から再生成。構造体・予約・刻印・CRT 参照を検査|
|sdk/link/app_sys.ld|本線 A|JSON から再生成。構造体・予約・刻印・CRT 参照を検査|
|sdk/link/shlib.ld|本線 A|JSON から再生成。構造体・予約・刻印・CRT 参照を検査|
|sdk/link/ppc/app.ld|本線 A|JSON から再生成。構造体・予約・刻印・CRT 参照を検査|
|build/os32.ld|本線 A|JSON から再生成。構造体・予約・刻印・CRT 参照を検査|
|sdk/crt/crt0_c.c|本線 A|新 CRT 実名で定義・参照を照合。Rust の export_name を同期|
|userland/rust/libos32gui/src/cfgro.rs|本線 A|新 CRT 実名で定義・参照を照合。Rust の export_name を同期|
|userland/rust/libos32gui/src/shlib.rs|本線 A|新 CRT 実名で定義・参照を照合。Rust の export_name を同期|
|sys/include/syscall_code.h|本線 A|§1 の layout・emit・publish の宣言と成功規約|
|sys/kern/exec/exec.h|本線 A|早期構築の返値を機種側へ公開|

|sys/kern/exec/exec.c|本線 A|新配置の §1 の式、確保・公開範囲、x86 shim の表参照を照合|
|sys/kern/exec/ring3_str.h|本線 A|新配置の §1 の式、確保・公開範囲、x86 shim の表参照を照合|
|sys/kern/exec/exec_tramp_init.inc|本線 A|新配置の §1 の式、確保・公開範囲、x86 shim の表参照を照合|
|sys/kern/exec/exec_tramp_strings.inc|本線 A|新配置の §1 の式、確保・公開範囲、x86 shim の表参照を照合|
|sys/kern/exec/exec_finish.inc|本線 A|新配置の §1 の式、確保・公開範囲、x86 shim の表参照を照合|
|sys/arch/x86/kernel/syscall_code.c|本線 A|新配置の §1 の式、確保・公開範囲、x86 shim の表参照を照合|
|sys/arch/x86/kernel/ring3_ls.S|本線 A|新配置の §1 の式、確保・公開範囲、x86 shim の表参照を照合|
|sys/kern/mm/paging_map.inc|本線 A|両頁の写像・権限・監査を §1 に揃える|
|sys/kern/mm/paging.c|本線 A|両頁の写像・権限・監査を §1 に揃える|
|sys/kern/mm/paging.h|本線 A|両頁の写像・権限・監査を §1 に揃える|
|sys/kern/exec/access_walk.c|本線 A|両頁の写像・権限・監査を §1 に揃える|
|sys/kern/kaudit.c|本線 A|両頁の写像・権限・監査を §1 に揃える|
|sys/platform/pc98/kernel_boot_memory.inc|本線 A|両頁の写像・権限・監査を §1 に揃える|
|sys/platform/powermac/ppc_kernel.c|PPC A|2 頁の初期化引数、PPC 分岐・publish、実表サイズを照合|
|sys/arch/ppc/syscall_code.c|PPC A|2 頁の初期化引数、PPC 分岐・publish、実表サイズを照合|
|sys/arch/ppc/exec_kapi.c|PPC A|2 頁の初期化引数、PPC 分岐・publish、実表サイズを照合|
|sdk/link_guard.py|本線 A|旧入力を拒否し新 ELF の刻印を配布まで照合。定数は生成値を使用|
|sdk/rustc_stamp.py|本線 A|旧入力を拒否し新 ELF の刻印を配布まで照合。定数は生成値を使用|
|sdk/os32x_hdr.py|本線 A|旧入力を拒否し新 ELF の刻印を配布まで照合。定数は生成値を使用|
|sdk/mkos32x.py|本線 A|旧入力を拒否し新 ELF の刻印を配布まで照合。定数は生成値を使用|
|tools/mkshlib.py|本線 A|旧入力を拒否し新 ELF の刻印を配布まで照合。定数は生成値を使用|
|tools/hostdrv_deploy.py|本線 A|旧入力を拒否し新 ELF の刻印を配布まで照合。定数は生成値を使用|
|userland/system/hsync.c|本線 A|旧入力を拒否し新 ELF の刻印を配布まで照合。定数は生成値を使用|
|build/ppc-user.mk|PPC A|既存の mkos32x.py 共通経路で ELF のターゲット順を読み LE 包装。PPC CRT・ASM の各 object は実際に使う生成ヘッダ・刻印に依存させ、世代変更のみでも再組立・再リンク|
|sys/kern/exec/os32x_hdr.c|本線 A|§2 の拒否経路と案内を照合|
|sys/kern/exec/exec_launch.inc|本線 A|§2 の拒否経路と案内を照合|
|sys/kern/mm/shlib.c|本線 A|§2 の拒否経路と案内を照合|
|sys/platform/pc98/kernel_gui_shlib.inc|本線 A|§2 の拒否経路と案内を照合|
|sys/platform/pc98/kernel_shell_fdd.inc|本線 A|§2 の拒否経路と案内を照合|
|tools/hwdev_layout.json|本線 A|予約を含むメンバー一覧を更新し DWARF から layout・CRC を再生成|
|sys/dev/hwdev/generated/hwdev_contract.h|本線 A|予約を含むメンバー一覧を更新し DWARF から layout・CRC を再生成|
|sys/dev/hwdev/generated/hwdev_layout.txt|本線 A|予約を含むメンバー一覧を更新し DWARF から layout・CRC を再生成|
|hwdev_contract_fixture.py (旧版 54003f375ca6)|本線 A|生成差分をレビューして HWDEV・FEP の変化した指紋を更新|
|test_ime_display_boundary.py (旧版 24ad6274535d)|本線 A|生成差分をレビューして HWDEV・FEP の変化した指紋を更新|
|test_kapi_layout.py (旧版 ff44d589cebf)|本線 A/B|現行期待は JSON と配置式から導出。300・0x4b8・旧 CRT 実名は v88 拒否 fixture に限り、関連する変異の目印を更新|

|docs/KAPI_SPEC.md|本線 A|版表記・予約・関数表を同期し旧データ位置を現行仕様から除く|
|docs/INDEX.md|本線 A|版表記・予約・関数表を同期し旧データ位置を現行仕様から除く|
|README.md|本線 A|版表記・予約・関数表を同期し旧データ位置を現行仕様から除く|
|build/app.conf|本線 B|AV0 利用物の要求版を指定|

実装順は次の通り。各群内でも原本 1 ファイルを 1 小段とし、担当表の各行に対応させる。生成物は生成小段にまとめ、直接編集しない。

1. A の sdk/kapi.json → 共有版ヘッダ → 必要な生成器変更を各 1 小段 → KAPI・世代・Rust の生成。
2. CRT・包装・ビルド依存 → syscall 契約 → CPU 実装 → exec 構築 → paging 契約・実装 → 機種起動 → 監査 → ローダー・配備の順に、各原本を個別に変更。
3. HWDEV 配置原本 → DWARF 生成 → 契約生成 → 依存部品包装 → fixture・検査・文書の各原本 → A 統合。
4. A 統合後に B の JSON → 共有版ヘッダ → 同じ生成 → AV0 実体・登録・要求版の各原本 → 契約再生成 → 部品再包装 → B 検査・統合。

A では新しい ABI の原本を増やさない。B の新規 AV0 実体・ヘッダは B のソース一覧と依存に登録し、追加検査は作成した段で CHECK の入力・basis・変異を登録する。既存 298 口の引数・戻り型・意味は維持する。HWDEV・FEP・kdebug を含む配布部品は同じビルドから揃え、HWDEV 契約生成の後に依存部品を作る。外部 apps/・game/ は H-7 の再開条件と決定が揃った時点で新 SDK から全再構築する。

## 4. 受入

1. v88 の正常起動済み配布一式とテスト入力（.o、Rust アーカイブ、通常アプリ、常駐シェル、shlib）を保存する。A/B 各段で sdk/gen_kapi.py と sdk/kapi_rust_gen.py を実行し、再実行で差分が増えないことを確認する。kapi-abi は基点の既存口すべての順序・署名を検査し、A の容量変更だけに KAPI-Break の理由を記載する。公開 C ヘッダを GNU89 でコンパイルし、両 CPU の offsetof / sizeof と Rust repr(C) を §1 と比較する。
2. make clean → make all、PPC は make ppc-ci-image。HWDEV の DWARF と --layout-only、KAPI の audit・ranges・version、HWDEV・FEP 契約、変更した検査の変異・lint を通す。全体の check + build は CI。x86 の build_compare は v88 対 A でカーネル・ユーザーランド・契約依存部品の差を期待する。A 対 B でも追加ラッパー・実表・版の差が出る。PPC 専用差分を独立させた場合は x86 一致。予期しない差分をファイル別に調べる。
3. 旧 .o 混入、旧 Rust メンバー、旧 ELF の再包装、旧ファイルを混ぜた配備目録をそれぞれ拒否する。hostdrv とゲストの hsync が転送前に不適合を示すことを確認する。NP21/W で旧通常アプリ・シェル・shlib を個別に差し込み §2 の診断、未実行、CUI の復帰または再配備案内を記録する。変位だけ、世代だけ、要求版だけを変えた入力も独立に検査する。
4. 同一 SHA の新しい起動媒体で NP21/W を起動し、シェル、GUI shlib、部品読込、アプリの起動と親への復帰、malloc と SHM、文字列返却と sys_ls を実行する。両頁の読み取り、書込み拒否、隣接頁の保護を各 AS で確認する。PPC はゲストのユーザーモードから実際にスタブ→sc→dispatch→呼出元へ戻し、slot 番号・戻り値・データ欄を観測する。キャッシュ公開後の先頭・末尾スタブの行先も検査する。
5. A は slot 297 の正常呼出しと 298・299・300・336・383 の範囲外処理を区別する。B は 298–336 の各宣言から生成ラッパー・登録先・仮実体までを照合し、正しい引数で NOSYS、出力不変を確認する。同値を返す隣接口の取り違えは登録先または試験用の個別識別値で検出する。337 と 383 は予約のまま。A の OS32X アプリ・shlib を B が受け、B の AV0 必須入力を A が拒否することも確認する。

各段の検査割当てと統合完了条件は以下とする。

- A/B とも test_kapi_layout.py の covers に生成元・CRT・包装経路を含め、旧 ELF を app と shlib それぞれに渡して失敗を確認する。現行 CRT 名・容量を誤らせる変異は現行入力で、世代照合を外す変異は同変位の旧世代入力で RED にする。PPC は旧 big-endian ELF と生成値のみ更新した再ビルドを含む。
- test_paging_bounds.py と test_access_walk.py のホストスタブに枚数・各フレーム・権限を持たせる。2 枚目を落とす変異、ゲートの枚数を 2 にする変異を個別に検出する。両機種で layout、emit、2 枚目の写像の失敗を注入し、ユーザー起動回数ゼロを検査する。
- test_module_image.py は三部品について旧版混在（v88→A、A→B）、同版で異なる契約、同版で別の組の CRC を別々に拒否し、各照合を外す変異で RED。test_hostdrv_manifest.py と test_hsync_inventory_sync.py は末尾に同変位・旧世代を置き、名札なし・対象絞り込みの各経路で不適合候補が選択されたとき転送先の変更ゼロを確認する。各 CHECK の covers に変更した本体とスタブを登録する。
- x86 の build_compare は通常・r1 の kernel.bin、kernel.elf、vmkernel.lz4、kdebug.bin、および boot・programs・userland・SDK・lib・images の対象を保存し、§4.2 の期待と照合する。両 FD 像の空き各 64 KiB 以上、MINIMAL の実像での起動、vmkernel.lz4 の 508 KiB 上限、kdebug のファイル・メモリ各 262144 B 上限を実測する。§5 の復旧用一式について配布物のハッシュ・媒体・タグの SHA を保存記録と照合し、復元起動まで通して統合完了とする。

## 5. 戻し方

ROLES §6 に従い、本計画の承認後、実装前に PM がその時点の main に注釈付きタグ（推奨名 pre-kapi-v89）を付け、push と参照先 SHA を確認する。タグと対応する v88 カーネル、シェル・全プログラム・shlib・部品を含む配布一式、媒体のバックアップを紐付けて保存する。

復旧は旧媒体から起動するか、停止中の対象媒体へタグに対応する旧一式を復元して再起動する。混在状態のシェルや hsync に復元を依存させない。ver、旧シェル、GUI、malloc、SHM の往復を照合する。ソースから復元するときもタグの別作業木で clean ビルドして一式を揃える。完了報告に実際のタグ名、SHA、配布一式の識別子、復旧手順とゲスト受入結果を記す。

## 6. 未決

1 件。容量 384 を含む本計画で大きな書き換えに着手するか、ユーザーの承認を受ける。配置方式の選択は決定済み。

## 確定 (2026-11-21、sol 第 2 回「4 点を反映すれば可」、PM が反映)
以下は本文に優先する。
- **P1 初期化の順**: 早い構築が kapi を読むが、実の表の取得と初期化は両機種とも後の exec_init() にある (sys/kern/exec/exec.c、sys/kern/exec/exec_tramp_init.inc、sys/platform/powermac/exec_boot.c)。実の表の初期化を exec_init() から分け、「実の表の初期化 → トランポリンの構築と公開 → 写像」の順を両機種で固定する。
- **P1 hsync・HostDrv の事前の照合**: 期待する変位と世代の取得元を経路ごとに決める — 名札 (manifest) がある時はそれ、名札が無い時 (HostDrv の絞り込みの配備で名札を消す時・hsync の名札なしの経路) は配る物の os32x のヘッダの世代と変位を読んで照らし、合えば通す。受入は正常の転送と、不適合の時の書き込み 0 の両方を試す。
- **P2 部品の組の CRC**: 今の照合は MODULE_FLAG_PAIR が立つ時だけ。kdebug・FEP・HWDEV の 3 つの包装ではこの旗を必須にし、CRC の不一致の試験は旗つきの入力で行う。
- **P2 境界の検査**: test_kapi_layout.py (旧版 ff44d589cebf) の旧い 1 頁の式 `12R + 272 <= 4096` は R=384 で落ちるので、§1 の表と文字列の頁・スタブと shim の頁のそれぞれの境界の検査に置き換える。
- **着手**: ROLES §6 の大きな書き換え — ユーザーの承認の後、今の main に注釈付きのタグを打って push してから始める。
- **ユーザー決定 (2026-11-21、着手の承認)**: 「HAL とこの KAPI 群が整理されるまで、動かしたいもの以外動かせなくて良いです。つまり、後方互換性は考えなくて良いと言うことです。ビルドし直してイメージの入れ替えはそんなに気にしない」— 案 2 で着手する。
- **PM 決定 (同日、上のユーザー決定を受けた簡素化)**: 旧いバイナリの拒否は、今ある exec の早い照合 (exec_launch・shlib_init の配置と世代の照合) で起動の前に止めるだけにする。**HostDrv・hsync の転送の前の全件の照合 (tools/hostdrv_deploy.py・userland/system/hsync.c の変更) は作らない** — 像は全部を作り直して入れ替える運用なので要らない (この 2 行は担当の表から外す)。
- **段 A の PM 決定 (2026-11-21)**: kdebug の起動の自己試験は予約の口 298〜383 がすべて 0 であることを確かめる形に変え、kapi_reserved_nosys の import を kdebug の import の契約から外して生成器で再生成する。
