# HW-BASIC の共通の HW_OP と機種の後端 — 確定設計

> 出所: ~/os32-tmp/run/hwop-common-design/DESIGN.final.md (astra 起草、sol レビュー 3 回、PM が確定 2026-11-23)。
> 状態: **現行 (2026-11-23)** — 段の状態は票 TASK_HW_BASIC が持つ。

# HW-BASIC の HW_OP を PC-98 と PowerMac で共有する設計案

| REVIEW1 の指摘（先行6件） | 修正先と決定 |
|---|---|
| P1 共通検証と部品リンク | §2.1 で翻訳単位、両機種の所有者、PPC のリンクと import を指定 |
| P1 実アクセスの DSI | §2.4 で回復条件、復帰先、隔離を定義。§3.6 で故障を注入 |
| P1 装置順の情報源 | §2.3 で BAR と raw phys が共用する物理区間表と失敗値を定義 |
| P2 幅別の構成取引 | §2.2 で BDF、橋、lane、不在応答を定義 |
| P2 BAR の全区間 | §2.3 で実 BAR 基点と OF 資源を照合し、反復前に全長を検査 |
| P2 受入と開発方針 | §3.6–7 に故障・幅・装置順・越境の判定、§4 に現行方針を記載 |

| REVIEW2 の指摘 | 修正先と判定方法 |
|---|---|
| P1 raw phys の全域検査 | §2.1 の import 表に action 8 の配送先を追加。§3.7 で検査時の実アクセスを数える。 |
| P1 OF 資源の保存 | §2.3 で OF 利用中の全 PCI 部分木の捕捉、橋の親子関係、起動後の表の生成元を指定。§3.4 で OHCI を照合する。 |
| P1 橋の隔離の共有 | §2.2 に橋ごとの状態と既存利用者の接続先、§3.6 に解除障害後の経路別検査を追加。 |
| P2 不在と障害の判定 | §3.7 の表で各幅の status、値、バス取引を区別する。 |

コードの照合基点は作業木の 2b705d0e2、開発方針は main の a604bf143。命令・幅・観測順・結果・ログの基準は PC-98 の実装と docs/decisions/realhw/HW_BASIC_DESIGN.md §§3–4 および末尾の block 拡張とする。POLICY_DEV §10 に従い、PowerMac の違いは構成取引、番地変換、窓、装置のバイト順、ポートの有無で吸収する。公開 KAPI のスロット・版・SDK に変更は要らない。

## 1. 現在の経路と PowerMac の接続点

| 役目 | x86 の実コードと動作 | PowerMac の現状 |
|---|---|---|
| 入口 | userland/shell/cmd_hw.c shell_cmd_hw → hwb_exec.c の hwb_load / hwb_step。hw_find_id は pci_count / pci_get、BDF 指定はシェルで組み立てる。 | resident shell に同じソースが入る。PM の調査では最初の POINT が通らない。 |
| 特権呼出し | cmd_hw.c hw_access → sys/kapi/kapi_generated.c の wrap_kdebug_call → sys/kern/module/kdebug_call.c kdebug_call → ops.dbg_call → sys/platform/pc98/kdebug/kdebug_main.c recovery_call の HW_OP 分岐 → sys/kern/kdebug/hwb_access.c の hwb_access。 | sys/platform/powermac/module_diag.c の diag_call は現在 PROBE_OP の LIST のみ。HW_OP を同じ hwb_access に渡し、LIST は既存経路を使う。 |
| PCI | hwb_exec.c he_access → hwb_access → sys/dev/pci/pci_hwb.c の pci_hwb_read/write → access_cfg。IRQ 保存、CF8 保存、2 パターンで機構確認、アドレス選択、CFC + lane で指定幅の取引、CF8・IRQ 復元。 | sys/platform/powermac/pci_config.c transaction は UniNorth の Type 0/1 と LE の 32 ビット取引。pci_backend.c の pci_config_write16 は Command 用。PC-98 でも sys/platform/pc98/pci_config.c の列挙用口と HW-BASIC の E7 は別経路。 |
| BAR | hwb_exec.c he_bar が利用の都度 header type と BAR を実読。型別の本数、64 ビット BAR の対と上位、空間を検証し属性をマスク。he_address が offset を足し、末端・整列を検査してログ番地にする。 | OF の PCI バス番地と CPU 物理番地が異なり得る。生の pciread の答えは両機種とも構成値。 |
| PHYS / IO | hwb_access.c の hwb_phys / hwb_block / hwb_io。sys/arch/x86/kdebug/kdebug_window.c kdebug_window_begin と recover_window_cleanup が peek・recovery と所有権を共有。sys/platform/pc98/kdebug/kpeek.c の kpeek_mmio もこの窓を使う。kpeek_call は別 op の配送先。 | 既存の装置別マップはあるが HW_OP の一時窓は未接続。PC-98 のポート空間に相当する資源は無い。 |
| 中止と記録 | hwb_exec.c he_point → cmd_hw.c hw_point → HW_POINT → exec_hwb_abort_take。hwb_step・he_wait_poll・fill 内の判定、hwb_log.c の begin/end/poll/finish → cmd_hw.c の hw_line / hw_log_flush が端末と -o を扱う。 | exec の印とログ整形を共有し、入力から中止要求を立てる部分とモジュールの型付き import を接続する。 |

## 2. 共通の契約と後端

sys/kern/kdebug/hwb_wire.h をシェルと kdebug の私有 wire とする。HW_OP=18、version=1、通常要求は u32 8 語で 32 バイト、BLOCK8/16/32 のみ count・increment を末尾に持つ 40 バイト。同じ像の呼び手とモジュールが CPU の整数表現で渡す。装置のバイト順変換はこの境界の外に置く。

| 所有者・口（新設名は案） | 型と責任 | 失敗と値 |
|---|---|---|
| kdebug 共通 hwb_access | i32 (void *buf, u32 size)。NULL・サイズ・版を調べてから action を解釈する。status 入力 0、READ 入力値 0、幅 1/2/4 バイト、WRITE の値幅、space と BDF を検査。PCI は BDF 16 ビット、offset+幅が 256 以内かつ整列、IO は末端 0xffff 以内、PHYS は整列・末端 u32 以内。 | 壊れた外枠は OS32_ERR_INVAL、部品・op 不在は OS32_ERR_NOSYS。呼出し失敗なら出力を使わない。受理後は return 0 と status を別に返す。 |
| 同上、値と POINT | HW_OK=0、HW_ARG=1、HW_NOSYS=2、HW_WINDOW=3、HW_IO_ERROR=4。READ 成功は幅でゼロ拡張、失敗は値 0。WRITE は入力値を保ち読戻しは明示命令のみ。POINT は未使用欄が全て 0 のとき印を一度消費し value=0/1。 | 不正な内容は ARG。ロード時に古い印を捨て、実行中は既存の点で ABORT。TIMEOUT・ABORT およびシェル終了値への変換はシェルに残す。 |
| sys/include/pci_hwb_contract.h | 現行の u32 pci_hwb_read(u32 bdf,u32 off,u32 width,u32 *out) と u32 pci_hwb_write(u32 bdf,u32 off,u32 width,u32 value)。装置側 pci_hwb.h も契約を読む。利用側には CPU 順の整数。 | 無効 ARG、機構無し NOSYS、機構ありで不在の関数を読むと OK と幅全部 1。取引自体の障害は IO_ERROR。 |
| 両 platform ディレクトリの固定名 platform_hwb.h、PCI 取引 | 読み書きの引数・戻りは上記と同型。PC-98 は E7 の probe と実幅取引、PowerMac は host bridge 選択と排他下の実幅 LE データ取引。8/16 ビット書込みは指定 lane のみを更新する。 | 機構の有無と装置の不在を区別する。32 ビット RMW は隣の W1C status まで書くため使えない。 |
| 同ヘッダ、BAR 番地変換 | u32 platform_hwb_bar(u32 bdf,u32 bar,u32 space,u32 base,u32 offset,u32 bytes,u32 *addr)。共通の BAR 解読後、マスク済みの実 BAR 基点、相対 offset と全長を渡す。PC-98 は恒等。PowerMac は BDF の親橋と OF assigned-addresses / ranges を照合し、全区間を収める窓から CPU 物理番地を算出。 | 不正 BAR、幅・加算の桁あふれは ARG。対応窓無し・変換不能は WINDOW、PowerMac のポート要求は NOSYS。成功時のみ出力を使う。 |
| 同ヘッダ、物理アクセス | u32 platform_hwb_phys(struct hw_req *r)、u32 platform_hwb_block(struct hw_block_req *b)。検証済みの番地・幅で窓を所有し、アクセス後に解放。バーストの count は 1..4096/幅、increment は 0/1。全長・整列・初期値幅を共通で検証。 | 窓の確保・復元失敗は WINDOW（復元失敗が優先）、アクセス失敗は IO_ERROR。失敗した read の部分値を公開しない。 |
| 同ヘッダ、ポート | u32 platform_hwb_io(struct hw_req *r)。PC-98 は従来の in/out を指定幅で一度行う。PowerMac は検証済み要求に HW_NOSYS を返す。 | ESCC の MMIO 別名への転送はしない。READ の値は 0、WRITE の値は保存、シェルの命令結果は NOSYS、終了値は 3。 |

### 2.1 翻訳単位・リンク・型付き import

両機種で同じ `sys/kern/kdebug/hwb_access.c` をコンパイルする。外枠、通常要求と block の内容検査、POINT、status/value の確定をこの翻訳単位に残す。固定名 `platform_hwb.h` 越しに検証済み要求を渡す。PC-98 の窓・生成 import のマクロ・ポート・stos の呼出列は `sys/platform/pc98/platform_hwb.h` 内の私有実装へ移し、同じ hwb_access.o に展開する。窓の所有状態は従来の kdebug_window.c に一つだけ置く。PCI E7 の提供元は drivers/pci、x86 のポート機構を選ぶのは PC-98 側。共通 TU から PC-98 ヘッダや生成 import を直接読まない。

PPC の build/targets/ppc-powermac.mk の kdebug.elf は diag.o、共通ソースからモジュール用に組む hwb_access.o、sys/kern/kstring_c.o をリンクする。PowerMac の platform_hwb.h は型付きスロットを呼び、IO は NOSYS を返す。OF 資源・PCI 排他・窓は同機種のカーネル、例外復帰命令は arch/ppc に置く。カーネル側とモジュール側で同じスロット構造宣言を使う。

sys/platform/powermac/kdebug_imports.json の index 0 の snapshot を保ち、順に `u32 (*abort_take)(void)`、表の pci_hwb_read/write と同型の cfg_read/write、platform_hwb_bar と同型の bar、phys、block、phys_check を追加する（phys/block の型も表の通り）。abort_take の提供者は exec_hwb_abort_take。kdebug_imports.h の単一 snapshot 型の配列を異種の関数型を保持する export 表へ改める。各フィールドの型・順とスロット offset をビルドで検査し、梱包した import の index・名前の hash・kind・type_ver をローダで照合する。必須スロット不成立なら dbg_call を公開しない。公開 SDK や x86 の import 表への追加は不要。

| PPC import index | 型付き slot（関数型は上表と共通） | カーネルの提供者・用途 |
|---|---|---|
| 0 / 1 | 既存 snapshot / abort_take | 既存 LIST / POINT |
| 2 / 3 | cfg_read / cfg_write | UniNorth の幅別取引 |
| 4 | bar | action 7 の割当照合・変換 |
| 5 / 6 | phys / block | 単発アクセス / 連続ストア |
| 7 | `u32 (*phys_check)(u32 addr, u32 width, u32 bytes)` | action 8。カーネルの不変資源表で全域を照合し status を返す。窓の取得、PCI 取引、MMIO は行わない。 |

PPC の hwb_access は共通接頭部の長さと版を確認後、action 8 に厳密に 44 バイトを要求する。長さ・版の違反は OS32_ERR_INVAL。末尾を読むのは長さ確定後とし、space=PHYS、width=1/2/4、bdf・bar・offset・value・status=0、addr の自然整列、bytes が正の幅の倍数、bytes−1 ≤ UINT32_MAX−addr を検証する。内容違反なら call=0/status=HW_ARG、slot 呼出し 0 件。合格時だけ platform_hwb.h から phys_check を一度呼び、call=0、value=0、status=検査結果。fill は OK を受けて最初の WRITE に進む。

### 2.2 UniNorth の構成取引

wire BDF を検証後、bus/dev/fn を取り出し内部値を (bus<<16)|(dev<<11)|(fn<<8) とする。列挙と同じドメイン・bus 範囲の橋を選び、現行 GEM 用 target_bdf 制約を汎用口に引き継がない。reg=off & ~3。根の bus は Type 0、dev>=11 で (1u<<dev)|(fn<<8)|reg、下流は Type 1、内部値|reg|1。根の選択不能 slot は不在としてバスに出さない。

IRQ 保存と橋の排他の下、LE のアドレス設定・フラッシュ後、data_va + (reg & 4) + (off & 3) に 1/2/4 バイトで一度だけアクセスし、選択解除・フラッシュ・IRQ 復元。LE 変換はこの後端で行う。汎用 WRITE は Command 専用マスクも dword RMW も使わない。橋の機構なしは NOSYS、正常完了した空 slot の read は OK と幅分の全 1、応答のない slot への正常完了 write は OK。全 1 は正常なレジスタ値にもなるため値で取引失敗を推定しない。DSI や明示的な橋エラーは IO_ERROR。列挙表に無いことだけで不在にしない。

橋の状態は PowerMac カーネルの寿命を持ち、OF の host bridge 識別子で一意に登録する。各 struct pm_pci はその状態への参照を持つ。異なる VA で同じ橋をマップしても同じ参照を設定し、コピーされる対象は参照と関数選択情報だけとする。pci_backend.c の root と列挙用コピー、lan_gem_io.c の hw.pci、pci_config.c の saved_pci、usb_platform.c の pm_usb_pci と一時コピー、および HW_OP を pci_config.c の同じ transaction に通す。従来利用者の戻り型・Command の扱いは入口で維持する。transaction は IRQ 保存→共有ロック取得→隔離確認の順とし、隔離中は最初のアドレス書込み前にエラーを返す。解除障害ではロック保持中に隔離を記録する。ハンドルの再作成で隔離を解除しない。

### 2.3 BAR と装置のバイト順

he_bar の実読を保ち、he_address で BAR 番号、実基点、offset を分けて保持する。PC-98 の platform_hwb_addr.h は従来の恒等変換。PowerMac の同名ヘッダは私有 HW_BAR_TRANSLATE=7 を使用。要求は 44 バイト（req の addr=実基点、value/status=0、末尾 u32 bar,offset,bytes）。成功時 value=CPU 物理先頭、失敗時 0。シェルの送信サイズと PPC の受信サイズ判定を対応させる。既存 action の要求長は維持する。

**OF の捕捉と保存。** 現行 kboot_capture.c の tree は ESCC・MPIC など選択した経路だけを保存する。pm_kboot_capture 内の OF 初期化後、tree の走査に PCI 資源収集を加える。各 host bridge 以下をドライバの有無に依らず訪問し、OHCI を含む各関数の assigned-addresses 全項、compatible・PCI ID、親橋 ID をコピーする。各橋の ranges 全項、親子のセル幅、空間種別、bus-range、親 ID も保存する。OF の作業バッファ・ハンドルによる後日の再読には依存しない。pm_kboot_info に上限付きのレコード群を所有させ、長さ不正・容量超過で完全性を保証できない橋は利用不能と記録する。

PowerMac カーネル初期化時、diag 公開前に保存レコードから BDF/BAR 別の割当表と、全親橋を通して変換した物理区間・装置順表を生成する。橋の所属と割当長を保持し、登録不能な橋・不完全な資源は WINDOW とする。不完全な入力で重複を排除できない物理範囲は raw phys にも公開しない。

特権側は BDF、BAR 番号・空間・幅・整列・長さを検査し、対応する OF ノードの assigned-addresses の BDF・BAR レジスタ・空間が一致する項目を選ぶ。実基点と割当基点の不一致・未登録・曖昧は WINDOW。offset と bytes が割当長内に収まることを減算で検査し、越境・算術範囲外は ARG。各親橋で同一空間の ranges 一項に半開区間全体が入ることを確かめ、親基点へ変換する。窓無し・競合は WINDOW。OF の多セル値は広い整数で扱い、末尾まで u32 物理番地に収まって初めて成功。PCI BAR を書き換えて長さを測定しない。

単発・poll は要素幅、fill/block は count が決まった時点で積のあふれを調べ、総バイト数で変換する。最初のストア前に区間全体を検査し、後段の単発検査だけに任せない。raw phys の fill にも PPC 私有の全域検査を設ける（同じ 44 バイト形式の action 8、PHYS、addr=物理先頭、bar/bdf/offset=0）。PC-98 の構成取引・ログの回数は従来どおり。

PowerMac 後端が OF で変換した物理資源に、機種の既知の装置プロファイル（compatible/PCI ID とレジスタ範囲、仕様で定めた LE/BE）を結び付けた区間表を公開する。OF の ranges 自体は装置順の根拠にならない。BAR 変換時・raw phys の全域検査時・実際の HW_PHYS/block のいずれでも、同じ不変の表を物理区間で検索する。前回変換の状態に依存しない。全域が単一の確定資源かつ同一順なら成功、未知・重複して所有者や順序が競合・異なる区間をまたぐ場合は HW_WINDOW でアクセス 0。既知の OHCI は LE。ログと wire の数値は CPU 順、装置との変換は実アクセスの後端だけが行う。

### 2.4 物理窓と障害復帰

PC-98 は 0xFE000000 の窓で IRQ・元 PTE を保存し、PCD、書込み時だけ RW。最大 4096 バイトは開始位置によって 2 ページを要する。部分マップ失敗でも復元し、復元不能なら既存どおり窓を使用不能にする。block は初回ストア前に全域をマップし、既存の stos 列・DF 復元を保つ。fill は中止点を持つ単発反復。

PowerMac は一時窓を排他し cache-inhibited・guarded でマップ、各出口でマップと割込み状態を復元する。CPU 順と装置順の変換、順序保証は後端で幅ごとに一度行う。PCI と OHCI の LE レジスタは PPC のネイティブロード値をそのまま返さない。block は全域を先に確保し、指定幅で連続ストア、値は幅内で巡回する。要素間の再マップ・関数呼出し・暗黙読戻しを挟まない。

sys/arch/ppc/kernel_trap.c の既存 onfault_data は一つの命令 PC・一つの DAR との完全一致である。単発 load/store と PCI の選択書込・フラッシュ・データ・解除の各命令は、EE=0 で命令ラベル、対象 VA、専用エラー復帰ラベルを arm してから実行する。保護枠が使用中なら未アクセスで失敗し、他者の枠を clear しない。正常出口は自分の登録を解除する。対象 supervisor DSI（miss/protect/io）のみトラップが登録を消費し SRR0 を復帰ラベルへ向ける。

block はループ前に一度、実ストアの PC とマップ済み VA 区間・幅・store 条件を登録する専用の範囲付き onfault を arch/ppc に追加する。DAR が対象要素の整列した番地で一致する場合だけループ外の cleanup へ戻す。無関係な PC・DAR・例外は通常の trap 経路へ渡す。失敗後は続行・再実行しない。既に完了したストアは残り、read は部分値を返さない。

単発・block の復帰先は取得済み PTE の逆順復元・TLB 無効化・排他解除・保存 IRQ 復元を行い IO_ERROR。復元を確認できなければ窓を隔離し、当該要求と以後の窓要求は WINDOW。PCI の復帰先は選択状態が変更された可能性があれば保護付きで解除・フラッシュし、排他と IRQ を戻す。解除自体の障害は再帰的に解除せず橋を隔離、以後その橋の構成取引を列挙側も含めて禁止し IO_ERROR。隔離フラグは排他を解く前に立てる。

## 3. 段と受入条件

1. **本線、x86 の分離。** 現行翻訳単位とリンク順を維持し、宣言を契約に出し、固定名ヘッダのインラインで従来の式・命令列を保つ。新 action の配送と新 import はこの段に含めない。基点と候補を同じ条件で build_compare（tools/ci/sep_identity.py）に通し、x86 生成物の差 0 を条件にする。差が出たらオブジェクト・逆アセンブルで最初の差を調べ、分割位置を直す。
2. **本線の回帰。** make all、層・ソース選択の検査、PPC common-compile。tests/dev/ の test_hwb_{wire,access,parse,exec,shell,log,probe,scripts}、test_pci_hwb、test_exec_hwb_abort、test_rshell_hwb に対応する check を変異込みで通す。既存の期待値・実アクセス列・ログを保持し、移動した変異アンカーは同じ故障で実行時 RED を確認する。最後に checks/run.py --lint。全変異は CI。
3. **PowerPC 担当。** diag_call に共通 HW_OP を接続し、必要な型付き import をロード時に解決。既定 PCI ドメインの橋・窓を GEM の起動条件から独立して取得する。PowerMac のみで使う変換 action は固定名ヘッダで配送し、PC-98 の配送は既存コードへ畳み込む。build/select.mk の所有者集合で選ぶ。x86 比較を再実施し、生成物変更の申告は PPC のみ。PROBE_OP の LIST と部品無しの動作も確認する。
4. **QEMU mac99。** FEATURE_KDEBUG を有効にした像の常駐シェルで、列挙された OHCI を選ぶ。hpt370_probe.hwb の CFG_OFFSET/CFG_VALUE 反復を用い、ID、class と BAR0 を pciread で記録する。window を BAR0 に結び mmioread32 で +0 の HcRevision を読む。終了 0、ID は列挙結果、ログの addr は OF から独立に算出した物理番地、値の下位 8 ビットは既存 OHCI 初期化の revision と照合する。HPT370 の ATA 書込み・ポート番号は転用しない。
5. **失敗と停止。** 別台本で有効な port に ioread / iowrite を幅別に各一件実行し、各ログの NOSYS と終了 3 を確認。wire 試験で call=0/status=2 と READ 値 0 を確認し、モジュール無しと区別する。wait/poll 中に停止入力を入れ ABORT、次回のロードは正常。窓の二ページ目のマップ失敗と復元失敗は後端試験で注入し、先行ストア 0 と復元結果を検査する。

6. **注入試験。** マップ成功後、単発の load/store、block の途中の store、PCI の選択・フラッシュ・データ・解除で同期 DSI を注入する。SRR0 の復帰先、read=0、失敗要素以降のストア 0、先行要素の一度だけの更新、onfault・IRQ・PTE・橋選択の終了状態を検査する。正常に復元した直後の要求は成功、復元失敗の次の要求は隔離 status とアクセス 0。対象外 PC/DAR は捕捉しない。
   解除書込みとそのフラッシュを個別に故障させ、注入後のカウンタを基準に、同じ橋への HW_OP、列挙、GEM（保存コピーも含む）、USB の構成アクセスを順に実行する。各入口で失敗し、各回の address/data の読書き増分はすべて 0。別橋は正常、同じ橋を新しいコピーから選んでも隔離が続く。
7. **幅・区間の後端試験。** Type 0/1 両方でアドレス値、上下の dword、全 byte lane と halfword lane を記録する。W1C status を立てた模擬構成空間で Command へ 8/16 ビット書込みし、対象値のみ更新、隣接 lane と W1C の保持、データ取引の回数・幅を検査する。LE/BE の既知窓に異なるバイトを並べ、BAR と raw phys の read/write と block の値・生バイト列を照合する。割当末尾ぴったりは成功、1 要素越えは ARG、実基点不一致・途中の親橋越境・未知順・競合は WINDOW。fill/block の最後だけ越える場合もストア数 0。

   action 8 は正常・未知区間とも窓取得とバスアクセスが 0 件。短い要求・余分な末尾・不正欄・長さの桁あふれでは slot 呼出しも 0 件。起動試験では GEM を使わない構成でも捕捉した OHCI の割当と親 ranges が OF 終了後に照合できることを確認する。

| 構成 read の条件（各幅 1/2/4） | status と value | 観測する取引 |
|---|---|---|
| 根の bus の選択不能 dev < 11 | HW_OK、幅分の全 1 | アドレス選択を含む全取引 0。write も OK で取引 0。 |
| 選択可能な空 slot が正常応答 | HW_OK、0xff / 0xffff / 0xffffffff | 対応幅のデータ read 1 件、選択と解除が完了。 |
| 橋の構成機構なし | HW_NOSYS、0 | 全取引 0。 |
| 利用可能な橋でデータ取引に障害を注入 | HW_IO_ERROR、0 | 指定幅で試行 1 回、再試行 0。解除と隔離は §3.6 の判定による。 |

## 4. 実装前に詰める点

- PPC 担当は停止入力を常駐シェルの中止印につなぐ入力経路と、最初に登録する実機の装置 ID・レジスタ範囲を確定する。同期 DSI 以外の橋固有の異常通知と判別法は対象機種の資料で確認する。
- docs/POLICY_PCIDEV.md §1（main a604bf143、2026-11-23）に従い、本線・PowerPC ともドライバに記録を足す調査版の前に HW-BASIC 台本で実レジスタを読む。台本冒頭にメーカー資料・仕様と BSD/MIT 実装の file:関数を記す。
- 本稿は設計の静的照合。各段の x86 build_compare 差 0、変異試験、PowerMac QEMU と実機での観測は実装時に採取する。

## PM の確定 (2026-11-23、往復 3 回の後)
- sol 第 3 回の残り 1 件 (P1、§2.2・§2.4・§3.6) は争点でなく足りない手順なので、PM が本書に取り込んで確定とする: 共用の構成の取引が DSI をエラーに変えるようになる段で、`pm_gmac_early_quiesce` (sys/platform/powermac/pci_config.c) は GEM の停止の確かめ中に構成の取引 (Command の読み・bus master の解除の書き込み) が失敗したら起動の側へ負の値を返し、呼び出し (sys/platform/powermac/ppc_kernel.c) は `memory_boot_init` に進まない。受入に、読み・解除の書き込みのそれぞれへ障害を注入して `memory_boot_init` に到達しないことを足す。
- 段の担当: 本線 = 共通の HW_OP と x86 の後端の分離 (x86 の生成物の差 0)。PowerPC = PowerMac の後端 (UniNorth の構成の取引・OF の資源の表・物理の窓・DSI の回復・橋の隔離の共有・GEM の early quiesce の負の値)。

## PM の追記 (2026-11-23、本線の段の取り込みの時)
- 本線の段の完了の条件「x86 の生成物の差 0」を改める: sol のレビュー (第 1 回 P1・P2) で、後端が最初の書き込みの前に全区間を検査できるよう fill/block の count の確定と BAR の全区間の受け渡しを BAR の変換の前に移すことと、abort_take・phys_check の口と action 8 の配送を本線の段で持つことになった。これで x86 の shell (hwb_exec)・kdebug (hwb_access)・hwdev の対の値と配布の像に差が出る (vmkernel は同じ)。x86 の振る舞いの変化は 2 つだけ: 不正な count は BAR の PCI の読みより先に拒否 (読みの順とログが変わる)、正常な 44 バイトの action 8 は INVAL から HW_NOSYS。既存の台本・試験はこの差を前提にしていない (sol 第 2 回)。PowerMac の後端はこの口の上に作る。
