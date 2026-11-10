# TASK_PPC_SEP — PowerPC 移植とアーキテクチャ分離の計画

> 状態: **実装中 (2026-11-10)** — 案 D を採用、MMU の外の 33 件を順に直す。第 1・2 段は受入完了、第 3〜5 段は下の「段の状態」の表。確定設計は [PPC_SEP1_DESIGN](../../decisions/ppc/PPC_SEP1_DESIGN.md)・[PPC_SEP2_DESIGN](../../decisions/ppc/PPC_SEP2_DESIGN.md)・[PPC_SEP3_DESIGN](../../decisions/ppc/PPC_SEP3_DESIGN.md)・[PPC_SEP4_DESIGN](../../decisions/ppc/PPC_SEP4_DESIGN.md)・[PPC_SEP5_DESIGN](../../decisions/ppc/PPC_SEP5_DESIGN.md)。実装と取り込みの決まりは [PPC_IMPL_RULES](../../decisions/ppc/PPC_IMPL_RULES.md)。2026-11-10 から到達点 ② (票 TASK_PPC_KBOOT) を優先し、分離の残り (下表の「未着手」) は ② の後か、本線の受入の待ちが減った時点で再開する。調査の全文は ~/os32-tmp/run/ppc-arch-survey/SURVEY.md。

## 段の状態 (2026-11-10)

受入の区分: x86 の生成物が不変の段は build_compare、変わる段は os32-v3 の PM の NP21/W。積んだ枝は main を取り込み直してから送る。

| 段 | 中身 | 状態 |
|---|---|---|
| 第 1・2 段 | ツール・ソース・生成物の選択、起動・例外・IRQ・時計・V86 の口 | main (受入完了) |
| 第 3 段 C1・C2・D1・D2・D3 | 文脈・起動引数・syscall | main |
| 第 3 段 E2・E3 | syscall の番号と callback | main (一括受入、2259debc) |
| 第 3 段 F・第 4 段 F | 全段の後の実測と記録 (製品コードは触らない) | 済 (2026-11-10、下の「実測」) |
| 第 4 段 A・B・C・D・E | 型の幅・配置・モジュール、PPC の型・配置の検査の入口 (tools/ppc/check_layout.py) | main |
| 第 5 段 A・D1・D2・E・F | コピーの原始操作、WC と GeForce の受入の CPU 部、SQLite の診断、boot の拒否 | main |
| 第 5 段 B1・C2・B2 | palette・IME の TVRAM・scroll を PC98 の表示 backend へ | main (一括受入、2259debc)。B2 の hardware scroll の +1 行の観測は未確認 (観測用アプリ hwscroll_probe が NP21/W で期待どおりに映らない、main でも同じ画面) |
| 第 5 段 B3・C3 | planar の初期化を PC98 の表示 provider へ、boot の IME の facade (include/ime_boot.h) | main (92cb185e、NP21/W の最小受入と B3 の 6 画面の比較は合格) |
| 第 5 段 B4・S1 | 描画 backend の選択を platform の provider へ (include/display_provider.h、platform/pc98/gfx_selector.c)、SDK の target の選択を profile へ (sdk/target_profile.py・build/sdk-target.mk) | main (a4b3b6bf、NP21/W の最小受入と B4 の 3 点は合格) |
| 第 5 段 C1・浮動小数点の保存域 | console の格子の寸法・既定の属性・表示の経路の確認を表示の backend へ (kernel/console/console_backend.h、TVRAM の番地は platform/pc98/tvram_private.h)。浮動小数点の保存域を arch の口の後ろへ (include/exec_arch.h の struct arch_fp_context と arch_fp_save・arch_fp_restore・arch_fp_init、PPC は arch/ppc/arch_exec_storage.h・arch_jmp_storage.h) | main (8a6096fc、C1 は NP21/W の最小受入と 4 点が合格、保存域は kernel.bin と全部品が一致)。**PPC の保存域の大きさは compile を通すための暫定** (USER の ABI と FPU・AltiVec の扱いは ③ の設計で確定) |
| 第 5 段 B5・B6・S2 | 起動時の SURFACE の予約と表を provider へ (platform/pc98/gfx_surfaces.c)、present のページ切替・VSYNC 待ち・表示メモリの番地を provider へ (platform/pc98/gfx_init.c、**B6 は部分完了** — 残件は下の「第 5 段で見送った物」)、SDK の利用箇所 (example の Makefile・Rust の規則・rustc の wrapper・配布物) を profile へ | 実装・独立レビュー済 (2026-11-10、枝 wt/ppc-sep5b5・wt/ppc-sep5b6・wt/ppc-sep5s2)。取り込みと NP21/W の受入待ち (B5・B6 は区分 C、S2 は区分 I) |
| 第 5 段 Z | 全段の後の実測と記録 (製品コードは触らない) | 済 (2026-11-10、下の「実測 (第 5 段 Z)」)。CONSOLE_DUAL は Z の後と本線と合意 |

### 実測 (第 3 段 F・第 4 段 F、2026-11-10)

`python3 -B tools/ppc/check_compile.py --source-profile selected --accept --json` (到達点 ② の枝の上、main 2259debc): 119 TU のうち 97 が成功、22 が失敗、受入の誤り 0。known_header_dependencies.json は 24 組。失敗は全部 missing-header で、原因は次の 2 つに分かれる。

| 原因 | TU | 解く段 |
|---|---:|---|
| `exec/appslot.h:26` の `x87.h` (x86 の FPU の保存域の型を共通のヘッダが直に include) | 13 (exec 8、gfx 3、kapi 2、kernel/kbd_inject.c) | 未割当 — 文脈の保存域を arch の口の後ろへ (第 3 段の arch_exec_storage の続き)。**1 本のヘッダで 13 TU が通る** |
| PC98・x86 の装置のヘッダ (pc98_route.h・v86_bios.h・fdc.h・pc98pt.h・serial.h・kdebug_imports.h・ime.h) | 9 | console.c は第 5 段 C1、gfx_core.c は B3〜B5、kapi_generated.c は C3。fs と serialfs と module は ③ の前の段 |

### 実測 (第 5 段 Z、2026-11-10)

同じ計測を第 5 段 B6 の先端 (main 8a6096fc + B5 + B6) で回した結果: **120 TU のうち 112 が成功、8 が失敗**、受入の誤り 0 (成功の一覧 tools/ppc/selected_success.json は 112 件、known_header_dependencies.json は 21 組)。再開の時点 (main 92cb185e) は 119 TU のうち 97。増えた 15 本の内訳は、浮動小数点の保存域で 13、C1 で kernel/console/console.c、B4 で platform/powermac/display_provider.c (新規)。

失敗の 8 本は全部 missing-header で、PC98・x86 の装置のヘッダを共通のソースが直に include している:

| TU | 止まるヘッダ | 解く所 |
|---|---|---|
| gfx/gfx_core.c | v86_bios.h (ほかに pegc.h・wab_xe10.h・pc98_route.h・splash_raster.h・kcg_module.h・geforce_module.h) | 下の「第 5 段で見送った物」。backend の名前と PEGC の BIOS ワークの読み方を provider へ |
| exec/exec.c・kapi/kapi_generated.c | kernel/ime/ime.h | IME の部品の口 (FEP) を exec・KAPI から狭い facade で引く段。③ (ユーザ実行) の前 |
| kernel/module/module.c | kdebug_imports.h | module の import の表を arch・platform の選択へ。③ の前 |
| fs/fat/fatfs/diskio.c・fs/fat/fatfs_vfs.c・fs/ext2/ext2_super.c | fdc.h・pc98pt.h | ブロック装置と区画表を platform の口へ。③ の前 (mount は ② の境界の外) |
| fs/serialfs/serialfs_session.c | serial.h | SerialFS の線を platform の口へ。LAN (Host Services) の段と同時 |

第 5 段で見送った物 (設計 PPC_SEP5_DESIGN の B5・B6 の行のうち、この段では入れていない)。B6 は部分完了で、下の 1・2 が残件:

| # | 残件 | 対象 | 後続の段 | 完了の条件 |
|---|---|---|---|---|
| 1 | 診断と raster の移設 | gfx/gfx_vram.c の gfx_diag_px (PEGC の窓を読む) と gfx_present_raster の pc98_splash_raster_table の呼出し、gfx/gfx_core.c の pc98_splash_raster_reset | PPC の FB の表示 backend を足す段の前 (② の後の FB の段) | gfx/gfx_vram.c が pegc.h・splash_raster.h を include しない。test_boot_splash_native の raster の変異 4 本が provider の実体を通って RED のまま |
| 2 | gfx の共通ヘッダの pc98.h の依存 | gfx/gfx.h:11・gfx/gfx_internal.h:5 の `#include "pc98.h"` (GVRAM_PLANES・VRAM_PLANE_*・GDC の定数の利用者が広い) | 同上 | 2 つのヘッダが pc98.h を include せず、PC98 の定数は各実体が直に include。x86 は build_compare で不変 |
| 3 | 共通の core に残る backend の名前 | gfx/gfx_core.c の pc98_route_safe・`g_backend == &gfx_backend_geforce/trident` の分岐、gfx/gfx_core_surface.inc の gfx_sf_backend、gfx/gfx_core_framebuffer.inc の backend の名前、PEGC の BIOS ワーク (v86_bios.h) | 同上 | gfx/gfx_core.c が PPC の selected で成功する (今は v86_bios.h で止まる) |

設計からの配置の変更 (残件ではない):

- B5 は「表だけ provider、手順は共通」ではなく ⑥ の手順ごと provider に置いた (順の保存のため、レビューで確認)。
- B6 の実体は platform/pc98/display.c への集約ではなく platform/pc98/gfx_init.c に置いた (display_flip_pages の隣。gfx の host の試験が既に link しているファイルで、試験の fixture を増やさないため)。
- S2 は app_sys.ld・shlib.ld の利用箇所 (resident の link、mkshlib) を profile の値に替えていない (生成物を変えない範囲)。

x86 の受入で未確認のまま残る物: B2 の hardware scroll の +1 行 (上の表)、Cirrus・GeForce・Trident の選択と予約の経路 (NP21/W の標準構成では通らない。host の試験が見ている)、v86 の往復での console (C1)。

PPC の型・配置は `python3 -B tools/ppc/check_layout.py` で確かめる (第 4 段 E)。本体の link と起動は selected とは別の入口 (票 TASK_PPC_KBOOT)。

**本体接続は案 D とし、案 B には着手しない。分離は実際に甘く、MMU 外の33件も直す。**
今の ppc-mmu は案 C を続け、`include/cpu.h` は変えず、context 操作は `arch/ppc/ppc_pmap.h` の `ppc_*` に閉じる。

|決定|内容|
|---|---|
|(a) 案 D を採用|「案Dで」(2026-11-07)。「OS32 のソフトの表の形式は x86 の2段の表と同じ」を受け入れ、既存 mm の方針・呼び手を保ち、PPC 側でハッシュ表へ変換する。実走の受入は別途行う|
|(b) 33件の修正を今から始める|「2.は今から始められるなら今」(2026-11-07)。下表(1)の独立した小枝から段階着手し、重い(2)(3)の試験枠を先に確保する|
|(c) 起動できたら LAN 経由のログを先に|「実機での実行にはシリアルが直接簡便に取り出せる経路が無いためLAN経由でのログの取得などが必要になります。ホストサービスやLANの機能が使えるようになれば問題がなくなると思いますのでブートできる段階に至った場合この点を早期に実現できるように優先してください。実機をリモート操作できるようになれば開発の障害が減ります」(2026-11-09)。決まっている分離の計画はそのまま進め、PowerPC でカーネルが起動できる段階に入ったら、画面や他の装置より先に内蔵 Ethernet の driver と Host Services の LAN 経由のログ取得・遠隔操作を置く。QEMU mac99 で先に通す|
|(d) 本線の再開の条件は QEMU の ②|os32-v3 の PM 経由のユーザー選択「QEMU で初期化が通る」(2026-11-09)。QEMU mac99 で OF から入り MMU・例外・MPIC・tick が動いてカーネルの初期化をユーザーランドの手前まで通し、起動のログが出た時点で x86 本線の実装を再開する。G4 実機は後追い。段は票 票 TASK_PPC_KBOOT|

ppc の枝（分離の直しを含む）の x86 側の受入は NP21/W だけとし、実機の回は要求しない（os32-v3 の PM 経由のユーザー決定）。

「カーネル層に分かっている不具合があるあいだは新機能より先に直す」に従い、既知の不具合修正を優先し、分離修正も新機能より先に進める。ただし33件すべてが現行 x86 の実行不具合という意味ではない。
x86 本線は止めず、小枝ごとに受入済みの変更だけを取り込む。大きな一括移設を本線の前提にしない。

|領域（SURVEY の ID）|大|中|小|代表例・実物|
|---|---:|---:|---:|---|
|実行・文脈 E1〜E7|6|1|0|`exec/exec_schedule.inc:267` の EIP-=2、`exec_tramp_init.inc:57` の x86 機械語生成|
|割込み・起動・時計 I1〜I5|3|2|0|`init/kernel_boot_cpu.inc:28` 以降の GDT/TSS/IDT/PIC/PIT 順、IRQ の16線前提|
|引数・asm A1〜A7|1|5|1|`kapi/kapi_print.c` の可変引数、`gfx/gfx_internal.h` の rep 命令|
|V86・描画・boot V1〜V5|1|4|0|共通 exec の V86 呼び手、`gfx/gfx_core.c:259` の TVRAM 直参照|
|SDK・CRT・Rust K1〜K2|2|0|0|`sdk/crt/crt0.asm:5` の BITS 32、Rust target の x86/LE 固定|
|型幅・配置 D1〜D4|2|1|1|long を u32 とする契約、gf5 の配置アサート失敗、packed の未整列アクセス|
|ビルド B1〜B2・生成器 T1|3|0|0|`build/kernel.mk:6` の固定 ASM 一覧、`tools/mkmod.py` の i386/LE 固定|
|合計|18|13|2|33件。確定31件、推定2件（D3/D4）。大は設計・共有契約、中は境界移設、小は任意経路・文言|

`arch/README.md:28-29` の「ディレクトリを 1 つ足すことで閉じる」は、MMU だけでなく、起動・実行 ABI・機種依存・生成器にも成り立っていない。
件数は原因群単位で、ファイル数ではない。PC-98 専用ドライバの配置は別集計。mm/paging と既知の `lib/kprintf.c` X1 は33件に含めない。
調査は全実行経路・全ポート・外部 apps/game・全 KAPI ABI を網羅せず、gf5 の正確な配置差、packed の実害、PC-98 専用ドライバ総数、V86 呼び手の網羅性も未確認。元調査のコンパイル結果は再実行していない。

|案|大きさ（設計概算、確定値ではない）|x86 への影響|PowerPC の利得|危険・失うもの|
|---|---|---|---|---|
|B：mm と呼び手を意味 API に分離|移設・改変・試験を合わせ約4,400〜6,000行。**根拠弱**：境界設計§5の合算で、レビュー指摘の拡大未反映|同文移設後は命令列も変わる。mm・exec・試験の広範な受入が必要|表形式を完全に分離できる|公開・失敗復旧・TLB・free 順を壊す危険。試験の抽出器にも波及|
|C：独立したソフト写像から HPT へ|製品のみ設計上約1,500〜1,800行、試験は別（400〜550行）。**根拠弱**：実装差分未計測|mm/cpu.h の変更なし、PPC 内に閉じる|今段の例外・変換試験を進められ、後で HPT 操作を再利用できる|固定16写像・単一 context。本体 mm との接続は未達|
|D：既存 PD/PT から必要時に HPT へ|製品のみ設計上約1,730〜2,200行（final §2 点4、trap の DR=0 化を含む）、試験は別（C に +150〜250行）。**根拠弱**：C に対する追加の試算|表形式のためだけの mm 改変を避ける。PPC 内だけの変更なら x86 不変を狙える|既存 prepare/commit・owner/lease 方針を保持して本体に接続|全消去・再 miss の費用、RAM BAT 撤去、trap の安全性が未検証。表形式の完全分離は失う|

C/D の規模は設計§2の比較用の値で、33件の修正・SDK・全受入の総工数ではない。B のレビューは試験対応の過小評価と二重移設を指摘しており、この表から日程は確定できない。

|実績のある手本と D|ソフトの表|ハッシュ表への供給・消去|
|---|---|---|
|OpenBSD|VP 表|enter で先に挿入し、spill は入っていない PTE の補充。pmap ごとの VSID で分離し、1枚ずつ消す|
|OS32 案 D|x86 形の2段の PD/PT|全部遅延・HPT 1個・切替と flush で全消去。**この組合せは手本に無い**|

ソフトの表から spill で詰める部分の型は同じ。ただし **x86 形の表を読むこと自体は手本に無い**。低 RAM を無条件 RW にする経路は guard を破るため、OS32 では PD/PT の保護を読む必要がある。
正典は `arch/README.md:28-29` の「ディレクトリを 1 つ足すことで閉じる」の例外として「OS32 のソフトの表の形式は x86 の2段の表と同じ」と明記し、`docs/02_memory.md` はそこを参照して形式・属性・運用を説明する。
B をやめると表形式の完全分離を失う。別 CPU・アドレス幅・大ページなどでこの形式が制約になる場合、または遅延挿入の改善でも受入性能を満たせない場合に再検討する。ページアウト導入時には A/D の扱いも再設計する。

**D は実走未検証で、確認済みなのは試作ヘッダによるコンパイルだけ。リンク・配置互換・実 HPT の動作成功を意味しない。**

|未検証の危険|QEMU で測る・確認すること|実機でしか判断できないこと|
|---|---|---|
|spill と追出し|同じ起動・アプリ切替・map/unmap 操作で spill 回数、満杯時の追出し、保護違反が通常 fault に進むこと|実際のキャッシュ・TLB と再 miss が応答時間へ与える影響|
|flush ごとの全消去|flush 頻度、消去量、全消去時間を記録。時間はエミュレータ条件を付け、機器の性能値にしない|同じ負荷で全消去・再充填の実時間、許容できる遅延か|
|RAM BAT 内で guard が効かない|接続前に RAM BAT を撤去し、固定・動的 guard、コード/stack/表/HPT の写像を確認|BAT 撤去の費用＝TLB の当たり方と実時間。QEMU の時間から推定しない|
|BAT 撤去後の例外入口|spill は DR=0 で表を読む。通常 trap も frame 構築を DR=0 で行い、入れ子 DSI を避けられるか確認|実機の例外・割込み負荷で安定するか|
|属性の意味|PCD を優先し I/G、PCD が無い PWT を W に対応。現行 x86 の CR0.WP=0 に合わせた権限、無効化後の stale 写像を試験|cache/device の実際の振る舞いと順序|

HPT のサイズは縮小可能で、全消去の費用は固定と決めつけない。まず同一負荷・同一構成で記録し、本体接続前に性能の受入条件を決める。
実機 QuickSilver が動くかは未確認であり、案 D の実機測定・受入は稼働確認に依存する。
GeForce の WC（x86 の PAT・MTRR）は PowerPC では別の口になる。D を採っても移植済みにはならない。

|順（SURVEY §1）・対象 ID|製品の変更・移設の幅と根拠|危険|x86 側の受入|
|---|---|---|---|
|(1) ツール・ソース・生成物選択：B1/B2/T1|約300〜700行、**根拠弱**。config/kernel.mk、mkmod/gen_memmap/link_guard の実物2,132行中、関連語77行を足場に選択部と decoder を見積もった|選択漏れ、ELF endian/relocation の誤処理|生成器の正常・異常入力試験を行い、build_id・日時を固定して既定 x86 の .o・ELF load section・.mod・vmkernel.lz4・R1 版を比較。全一致のときだけ追加の回は不要。不一致なら起動を含む NP21/W の受入が必要|
|(2) 起動・例外・IRQ・時計・V86：I1/I2/I3/V1、I4/I5/A4/A6/V5|約700〜1,500行、**根拠弱**。boot_cpu、IRQ/時計、V86 契約・代表呼び手、bootinfo 計2,377行・関連語500行。呼び手網羅は未完|初期化順、EOI、時刻、V86 非搭載時の扱い、例外再入|機能試験だけでは不足。NP21/W で起動、例外復帰、時計・入力、V86 往復を受入|
|(3) 文脈・起動引数・syscall：E1〜E7/A1、A2|約600〜1,400行、**根拠弱**。exec 主要部・shim・setjmp・kapi_print・gen_kapi 計3,023行・関連語183行。ABI の新設量は未設計|引数破損、誤再開、保護境界、命令同期|host の引数・再実行・異常系に加え、NP21/W で起動/終了、休止/再開、syscall、可変引数・callback を受入|
|(4) 型幅・配置・モジュール：D1〜D4|約150〜500行、**根拠弱**。types/feature5/GUI/PCI 契約計911行・関連語44行、生成器は gen_hwdev_contract.py。配置差の詳細未測定|wire と native の混同、未整列アクセス、生成契約不一致|幅・配置アサート、生成物照合、module load の機能試験。x86 ABI/配置不変なら追加の NP21/W 回は不要。変わる場合は clean 再生成後 NP21/W のロード/アプリ実行も必要|
|(5) 描画・残る asm・SDK：V2〜V4/A3/A5/A7/K1/K2|約900〜2,000行、**根拠弱**。gfx/IME/WC・受入/boot/CRT/link/Rust 代表15ファイル計2,718行・関連語195行。主に移設だが新 backend・外部アプリは未算入|表示退行、boot 選択、WC、SDK/CRT と syscall 契約の不一致|描画・SDK 機能試験に加え、x86 生成物が基点と不一致なら NP21/W で表示を確認。boot/CRT は NP21/W で受入。WC の受入は NP21/W の範囲で見られる物とし、対応 GPU 上の実際の WC の効果は見られない。診断 asm だけなら機能試験・生成物照合で可|

各幅は diff 実測ではなく、実物を `rg -n` で検索した候補の行数からの設計概算。関連語はコメントも含み、ヒット数を変更行数とは扱っていない。共有ファイルの重複があり合算不可。試験改修・生成物・PPC 新ハード実装は別枠で未見積もり。
この順の paging は D の別受入として本体接続前に置く。paging・例外・syscall・起動順を変える共有変更は重い段で、機能試験だけでは足りず **NP21/W**を要する。D の PPC 専用変更は QEMU と QuickSilver で受ける。

|ABI と完了条件|扱い|
|---|---|
|[ABI] E1・E4・E5・A1・A2・K1・K2|KAPI・SDK・CRT を一体で扱い、x86 の入口・KAPI の ABI を変える段は CRT/SDK も同じ段で変える。`sdk/kapi.json` が正。既存スロットを並べ替えず末尾追加、対応する版上げ、生成器から再生成、clean ビルド、外のリポジトリの再ビルドまで受入に含める。外部の再ビルドは H-7 の再開後とし、それまでは外部未ビルドと記録する|
|既存アプリのバイナリ互換|ユーザー方針（2026-09-24）「既存アプリケーションはすべて自作なので再ビルドで解決する問題は確認は入りますが、基本的に再ビルド方向」に従い、確認を入れ再ビルドで揃える|
|到達点|「本体が組める」「初期化して動く」「ユーザを実行できる」を分け、SDK/CRT/Rust はユーザ実行前に受け入れる|

根拠：`ppc-mmu/DESIGN.final.md` §2＋`REVIEW.r2.md`（修正を優先）、`ppc-mm-boundary/DESIGN.r1.md` §5・§6＋`REVIEW.r1.md`、`ppc-arch-survey/SURVEY.md`、読取元 `/home/hight/wt/os32-v3/ppc-env` の上記実物。今回ビルド・実走はしていない。
根拠の弱い数字は **8件の概算幅**（B/C/D の3件と順(1)〜(5)の5件）。実物の行数・検索一致行数と、資料の33件集計はこの数に含めない。
