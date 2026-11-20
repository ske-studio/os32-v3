> 出所: 本線の機械の ~/os32-tmp/run/ppc-module-align/DESIGN.final.md (設計ラリー 3 回、astra 起草・sol レビュー、2026-11-20 確定)。行番号つきの参照は往復の版のまま。
> 状態: **確定 (2026-11-20)** — PowerPC でも x86 と同じ部品 (module) の読み込みで FEP・KCG を使う (票 TASK_PPC_ALIGN_X86 の B08・C04)。末尾の「確定」の節は本文に優先する。

# B08・C04　PowerMac の部品読み込みを PC-98 に揃える（設計 r3）

2026-11-20。製品の基点は origin/main の 1951635dc。監査は origin/wt/docs-ppc-align の ARCH_DRIFT_AUDIT の B08・C04。以下の行番号は原則この製品版、未取り込みの出典にはコミットを付ける。

## r2→r3 の変更

- P1 容量　辞書は ATA HDD の ext2 から供給し、固定帯には loader と FEP 部品の増分を計上する。
- P1 起動　静的 SQL 成功後にメタ・予約・snapshot を揃え、FEP の公開と失敗時の解放条件を定めた。
- P1 import　静的製品にも型付き resident provider を組み、FEP の実 import 全件を製品リンクで照合する。
- P2 辞書　読取専用モードを再開・切替・I/O 再試行にも適用し、学習 API の選択を resident 側に置いた。
- P2 再配置　native candidate の命令境界と HA の桁上がりの試験値を指定した。
- P2 段順　必須は L1 → L2 → FEP → KCG。静的 SQL の置換作業は任意 B08 にまとめた。

必須の完了は両機種で FEP・KCG を同じ ID と共通 loader で起動し、PowerMac で辞書ありの変換、欠損時の打鍵・SQL・シェル継続、既知字形を確認すること。x86 の v1 と公開 ABI を保つ。

## 実物と x86 との比較

|境界|x86 の正|PowerPC の製品と変更点|
|---|---|---|
|生成|build/sqlite-module.ld:2–16 の i386 VA 0、text/rodata/data、NOLOAD bss、世代記録。build/fep-module.mk:21–36 は最終リンクに再配置を残し、対象カーネル別に pack|arch/ppc/kernel.ld:6–24 の BE ELF 内に root 自体を格納する。PPC 用 VA 0 body リンクを新設し、同じ生成器の入口を通す|
|形式|tools/module_schema.py:8–29 の KMOD v1、144 B、LE word、4 KiB page、12 B の import/export。tools/mkmod.py:59–71,149–193 は ET_EXEC i386 の REL、絶対 word だけを offset 表に残し、内部 PC 相対はリンク済み|外枠・LE metadata・部品 ID を共有。PPC32 BE の命令・データには別の fixup 表現が必要。v1 の予約語はゼロ必須（kernel/module/module_image.c:56–63）|
|契約|kernel/module/module_image.h:21–43 の u32 の policy/plan、名前 hash・版・種別。module_image.c:119–132,145–200 で KAPI、layout、module ABI、ops、pair、CRC、名前・slot・text 範囲を照合。tools/mkmod.py:256–276 は ops の関数位置が VA 0 でも再配置で存在を識別|PPC 用に生成した import provider を同じ型で検査。PPC の body は BE、表は LE。公開 KAPI の並び・版は変更しない|
|生存期間|kernel/module/module_runtime.h:23–53,67–85。module_runtime.c:33–114 の依存→予約→VFS→owner→連続頁→再配置→native import→export 候補。:117–141 で init、候補比較、prefix 頁返却の後に RUNNING。:19–23 だけが公開口|同一 C を製品リンク。機種ごとの loader や静的な代替経路は作らない|
|FEP|tools/module_schema.py:73–84 は ID 3 が SQLite（2）を要求。kernel/ime/fep_module.c:25–84,131 で stub と候補を分離。build/features/fep.mk:4–8 は x86 のみ pack|入力・描画は機種の口へ。辞書と変換、API 公開条件を共有|
|KCG|tools/module_schema.py:99–110 の任意 ID 5。drivers/video/kcg/kcg_body.c:21–55 が PC-98 の ROM ポートを操作|platform/powermac/kcg.c:4–16 は既存のファイル字形 API。build/features/kcg.mk:4–10 は PPC に ROM module を載せない。PC-98 の ROM は PowerMac に存在しない|
|診断|tools/module_schema.py:49–58,176–183 の kdebug.bin と、その ID に依存する hwdev.mod。module_runtime.h:19–33 の ref と detach 契約|build/select.mk:166–179 で両 feature を拒否、build/targets/ppc-powermac.mk:244 に製品 target の拒否がある。単に make の禁止を解除すると未実装の機種操作まで露出する|

依頼の説明に対する実物との差は二つ。(1) SQLite のロードの所有者は outer でなく engine。(2) 製品 module target の一律拒否の所在は PPC target makefile、select は機能選択の拒否。監査がいう機能差はいずれも存在する。既存の共通 image/runtime が統合先であり、HAL の境界は命令の公開・機種入力・装置アクセス。新設部分は既存の x86 の公開順、失敗時の可用性に従う（POLICY_DEV §10）。

## pack と loader の境界

* x86 は v1 をそのまま生成・受理。PPC に同じスキーマ系列の v2 を新設。144 B の field 位置と ID/CRC の意味を維持し、予約語 0 を対象タグ（新設、値 20 = ELF の PowerPC machine）、予約語 1 はゼロ。期待版・タグはビルド対象が選ぶ。相手機種、未知の組合せは owner 確保前に拒否する。v1 で split immediate を表せないための最小拡張である。
* 新設の PPC relocation record は LE u32 三つの順で「body 内書込 offset、ELF 種別、VA 0 の完全な目標 offset」。12 B。件数から byte 数を乗算する前に上限と残り長を除算で確認。offset 昇順、書込幅で重なりを検査。HA と LO はそれぞれ完全な目標を持ち、隣接や一対一を仮定しない。
* 新設の pack 側は ELF32 BE ET_EXEC、machine 20、SHT_RELA の symbol と signed addend を読む。最終リンク値を二重加算しない。S は最終シンボル VA、A は明示 addend、P は書込番地。S の所属は保持する body 節、S+A は body 内または許可済みの末端印に限定。undefined、動的リンク、予期しない alloc 節・種類は生成失敗。世代の INFO 節は本文に含めず世代検査は行う。export の ops version と pointer は BE で読む。offset 0 の有効関数と空 slot の区別は ADDR32 の存在で行う。
* 種別の値の根拠は ELF 定義 /usr/include/elf.h:2572–2581,2597。ADDR32 (1) は整列 4 B に base+S+A の BE32。ADDR16_LO (4)、HI (5)、HA (6) は整列 2 B の即値へ下位、上位、丸め上位を BE16 で格納。HA は完全値に 0x8000 を加えた値の bit 16–31（幅を拡張して算出）。命令の上半分を保存。runtime で base 加算の wrap、書込端、import slot との重複を全件検証してから変更。
* 内部 REL24 (10) はリンク済みの分岐。S+A-P が 4 の倍数、符号付き 26 bit に収まり、対象が text 内、相対分岐命令の LI と一致することを pack で確認、移動時の書換えはゼロ。内部 REL32 (26) は 32 bit 差が本文と一致する場合だけ同様に処理。絶対シンボル、外部直接分岐、GOT/PLT/SDA 等は無言で捨てない。非 PIC、small data 無効で全 TU と assembly を組み、各段の製品 body の readelf -rW の種類・数・参照先・map を受入に残す。表外の実出力はリンク選択を直すか別の検証規則をレビューしてから通す。
* REL14 (11) は内部の相対条件分岐だけ採用する。P と S+A は text 内の 4 B 境界、opcode 16、AA=0、BD の符号拡張が S+A-P（−32768〜32764）と一致する場合だけリンク値を保持する。REL24 も両端を個別に整列検査。PPC の entry と関数 export は 4 B 整列・命令全体が text 内であることを pack と loader が検査する。未定義の REL14 派生型が出た製品は pack 失敗とする。
* tools/gen_sqlite_imports.py:55–83 の同じ定義・index・provider 型検査から PPC32 の 4 B slot と関数ごとの stub を生成する。部品内から stub までは内部 REL24。stub は slot+4×index の HA で lis r12、LO を変位とする lwz r12、mtctr r12、bctr の順。native BE の関数番地を読み、LR、SP、引数 r3–r10、stack 引数と varargs の CR を保存する。全 provider は同一 PPC32 ABI、境界を通る数値・ポインタは整数規約。L1 は型と relocation、L2 は実距離が 32 MiB 超の provider へ分岐し、整数・ポインタ・レジスタを越す可変引数・戻り値・元の呼出し元への復帰を検査する。
* FEP 実 import の os32_sqlite_snprintf は char *(int, char *, const char *, ...)（tools/prototypes/fep_imports.def:310、kernel/sqlite_outer.c:10–21）。静的構成ではこの wrapper と va_list thunk の生成が外れる（tools/gen_sqlite_imports.py:132–148）。PPC 静的 FEP 用に同じ宣言の resident provider を新設し、FEP が使う %u、%d、%s、リテラルを整数・ポインタだけで処理する（kernel/ime/ime_dict.c:636,740、ime_user_store.c:118–119,196,215）。SQLite の戻り先ポインタ・長さ制限・終端と一致させ、他の利用形式は実呼出し一覧を照合して追加する。7b の hard/soft 境界に va_list を渡さない。実 fep_imports.def 全件から生成した provider 表を静的 SQLite 込みの最終 PPC ELF に製品入力としてリンクし、pack 済み FEP の各 index・名前・型付き参照と map の resident 定義を突合。未解決・欠落・重複で失敗させ、ゲストで cache_size の数値と文字列の整形結果を確認する。
* native candidate は export 表とは別に fep_start の全 slot で検査する（kernel/ime/fep_module.c:131–144、kernel/module/module_runtime.h:56）。PPC では address が 4 B 整列、address >= text、text_size >= 4、address − text <= text_size − 4 を全て満たす時だけ candidate_valid を立てる。非整列、末尾の命令が欠ける fixture は公開前に失敗する。
* loader の既存境界は kernel/module/module_image.h:48–60 の int module_image_header / validate / relocate（引数は同箇所）。共通のヘッダ・表・CRC・名前検証を共有し、再配置の読出し/検証/適用だけ形式で選択。v1 で許されている非整列 word や順不同を狭めない。runtime の import は native u32 をコピー（module_runtime.c:91–94）、export の記録は引き続き LE decode。BSS は payload 終端から mem_size までゼロにする。
* 全ての書込みが終わり、いずれの entry にも到達できない module_runtime.c:101–109 で既存の void arch_exec_publish(void *dst, u32 bytes) に text の先頭・非ゼロ長を渡す。契約は arch/ppc/arch_exec_cache.h:6–7。arch/ppc/exec_cache.c:9–25 は全対象 line の dcbst → sync → 全 line の icbi → sync → isync（MPC7450UM §3.4.4）。arch/x86/arch_exec_cache.h:4 は no-op。端丸め・非 wrap は先の範囲検査が保証する。
* 置き場は固定 VA を新設せず、mm/pgalloc.h:225,235,238 の owner・連続物理頁を用いる。実行 VA は include/memmap.h:420 の void *P2V(u32 pa) に payload offset を足した値。4 KiB の定義は同:38。docs/ppc/cpu-mmu.md:5–10,29 は IBAT/DBAT の違いを示すが、製品は platform/powermac/kernel_boot.c:2,104,224–231 の Plan D supervisor 表。platform/powermac/platform_of_layout.h:73 の 0x10000000 は BAT の上端であり module 基点には使えない。実機種の allocator が渡した全区間で kernel root の VA→PA 恒等、通常 RAM、命令 fetch 可をゲストで照合。I/O alias、root blob、ledger、guard は割当対象外。prefix のみを物理 PFN で返し、残りは owner が保持する。

## カーネルと root の組を識別する

tools/mkmod.py:196–207 の x86 pair は VK32 v2 のサイズと CRC を検証して得る。PowerMac の platform/powermac/root_image.c:8–18 の CRC は ISO 全体。これを部品内に直接書けば部品が ISO の CRC を変え、自己参照になる。ゼロや定数を有効な pair として配布しない。

PPC には次の二段生成を新設する。最初に同じ長さの部品（pair 仮値）を入れた ISO を作る。arch/ppc/kernel.ld:17–26 の root section と別に固定長の検証記録（pair、ISO CRC 各 u32）を新設し、現在 C コンパイルに即値で渡す ISO CRC（build/targets/ppc-powermac.mk:174–177）をこの記録に移す。暫定 ELF の file-backed PT_LOAD を位置順に CRC32 し、root 範囲と検証記録だけをゼロ列として扱う。範囲の番地・長さ、entry、filesz/memsz と VA は固定幅 LE の前置きにして計算する。BSS は memsz で表現する。

得た pair を使って部品 CRC も再生成し、ISO と検証記録を差し替える。最終 ELF に対して同じ正規化で pair が同一、ISO 実 CRC が記録と一致、暫定と最終で section 配置・サイズが同一と確認する。arch/ppc/kernel.ld:15–55 の単一 image の開始・file_end・resident_end、root の start/end、entry と新設記録の start/end を host とゲストの同じ順で使う。filesz と memsz は開始との差。host は実 ELF の PHDR とこれらの一致も検査する。arch/ppc/kernel_entry.S:25–27 の BSS clear 後、ppc_kernel_start 前に新設 verifier を呼ぶ。CI 引数を stack に退避・復元し、計算中の保存域には file-backed data を使わない。照合結果と値を新設 module_pair.c の BSS に保存し、既存 ISO CRC 合格後だけ getter を有効にする。root 検証の後でだけ loader に検証済み pair を渡す。既存の root CRC 表示を維持するため、部品の request 組立てに専用の共通契約を新設する。

    /* 新設。成功 1 のときだけ検証済みの値を out に書く。 */
    int platform_module_pair(u32 *out);

PC-98 実装は kernel/sqlite_engine.c:219–226 と同じ boot 情報を返す。PPC の ISO を改変して root CRC だけ更新しても別のカーネル用部品は pair 不一致。これは現行の版取り違え検査で、署名を追加する仕様ではない。

## SQLite、FEP、KCG と診断

1. PowerMac は int sqlite_static_boot(void) の boot check、SERVICES_SQLITE_ONLY、services_public の成立後だけ FEP を load/start する（kernel/sqlite_static.c:11–28、状態の型は kernel/sqlite_engine.h:14–20）。静的 service は kernel と同じ寿命。FEP load と start の直前に ready を確認し、部品には既存整数 facade の型付き import だけを渡す。PPC 静的構成の request は dependencies=NULL、dependency_count=0。schema の論理依存 ID 2 は製品選択時に静的 service で充足する。PC-98 の int fep_module_load(struct module_handle sqlite) と RUNNING の世代付き依存を保持する（kernel/ime/fep_module.c:162–180、module_runtime.c:25–41,122）。静的起動時の boot_check(0, 0) は FEP 予約を解除する。ATA/ext2 の /db が見えた後、一般 SQL 接続を開く前に、x86 の kernel/sqlite_engine.c:515–554 と同じ順で次を行う。
   - int fep_meta_probe(const char *, FepMeta *)（同:299）で既定 M を検査し、probe を閉じる。有効な設定値を優先し、未指定ならメタと最小 KiB の大きい方を範囲検査して bytes に換算。os32_sqlite_outer_boot_check(0, reserve) 成功後に限り resident がメタと実予約 KiB を保存して saved を立てる。int fep_boot_config_get(FepMeta *, u32 *) は未保存で OS32_ERR_AGAIN（同:252–265）。ime_dict_boot(void) はこの結果をそのまま返す（kernel/ime/ime.c:91–105）。
   - load/start 後も resident の FEP 公開フラグ（新設）は閉じておく。os32_sqlite_outer_boot_check(ime_boot_connections(), reserve) 成功後にのみ開く。SQL の公開は独立。switch は target.reserve_kb が起動予約を超えると旧辞書を閉じる前に AGAIN（kernel/ime/ime_dict.c:816–835）。切替で snapshot を更新しない。
   - メタ・予約・load・start・最終検査の失敗は FEP 公開を閉じ snapshot を無効化。取得済み handle の後始末は一度だけ行う（kernel/ime/fep_module.c:147–158,186）。probe と FEP の全接続・group の解放を確認後に boot_check(0, 0) で予約を解除し、成功時のみ予約量をゼロと記録。close・quiesce・検査失敗なら隔離して生きた資源と予約を保持し再起動まで再試行しない。SQLITE_ONLY で打鍵・一般 SQL を継続し、静的 SQL の owner と API を維持する。
2. 7b の整数境界・kernel owner の pool/guard/別 stack と FP の保存復元を保持する（kernel/sqlite_outer.c:23–27、kernel/sqlite_engine.c:38,50–74,136–172）。任意 B08 を実施する時だけ r1 の module owner へ移し、hard TU・生成 VFS・restore を body に集約、FP/stack/SQL と欠損時 stub を再検証する。
3. 段 8 の ATA の口 → PowerPC の ATA 後端 → 6c の D3 を終え、PowerMac の ATA HDD の ext2 から /db/fep_s.db、/db/fep.db、/db/fep_l.db を x86 と同じ名前で供給して 7a を公開する（kernel/ime/ime_store.h:25–29、ime.h:18）。/db の RAMFS と /dbu は使わない。学習のパスは /etc/fep_user.db。辞書の可読性と学習媒体の書込可否を個別に判定する。
   - system-only のときはストアの寿命中そのモードを保持する。int fep_store_open(FepStore *, const char *, const char *) のメタ・再開時予約検査を保ち、system 接続は READONLY の store_system_open で開く。boot・resume、switch の成功／旧辞書復旧、I/O 再試行の全てで同じモード判定を使い、user open・再接続・自動学習・設定保存を抑止する（kernel/ime/ime_dict.c:724–750,766–804,807–854,896–905、ime_suspend.c:34）。system の完全一致・前方一致で候補を返し、辞書欠損時は打鍵を通す。
   - candidate の学習関数も本文内の実入口として検査する。公開後に resident dispatcher が学習不可の登録・削除・export・clear 等を stub_api へ振る（kernel/ime/fep_module.c:67–86,131–144）。system-only の全経路で辞書関連の VFS 書込要求と user open 回数がゼロであることを検査する。
4. KCG の ROM I/O は PC-98 のみ。共通の字形利用者とファイルフォントは維持し、PowerMac で module 化するときは ID 5 の同じ initialize と typed API を持つファイル provider を新設。本文から呼ぶ resident のファイル字形取得と公開 dispatcher は別の入口にし、再帰を避ける。欠損時も既存ファイル字形を使えること。PC-98 の文字 ROM の移植や同梱は不要。
5. C04 の loader の成功と診断装置の移植を独立に判定する。まず欠損・世代違い・隔離を PPC の起動ログから ID、結果、owner 頁で読めること。kdebug/hwdev は別段で既存の ops 順、任意 slot の欠番、HWDEV→KDEBUG の依存を保ち、共通の情報取得を部品へ、機種固有 probe は提供機種のみ登録。旧 PC-98 の I/O を PPC で呼ぶ stub 成功を作らない。PPC で型付き provider が揃った部分だけ選択し、欠けた操作は既存非対応結果。PPC 実装がない診断の同等動作は C04 完了として数えない。

### 7a の枝を取り込む単位

|出所|採用する部分|破棄・差し替え|
|---|---|---|
|M1 a3ca17b69、include/platform_fep_input.h:7–8|int platform_fep_key_local(void)、u8 platform_fep_key_shift(void) と fep_input.h の入力契約|OS32_FEP_STATIC、静的な呼分けとその試験|
|M3 9cf5ed62a|fep_boot_config.c の設定所有権と既存 x86 の保存・公開タイミング|静的起動を前提にした選択|
|M4 46e6398a3|fep_suspend_owner.c の PID 所有、終了時の一度の再開と CUI 入力回復|静的 backend 用呼出し|
|M5 5e71993d1|辞書失敗表を製品 module の受入データとして利用|fep_static.c の起動・直接呼出しを丸ごと除外|
|M6 7f2d71453、S3 5c23d92e0、選択 ec74fc4e7|IME 終了 hook と生成器の feature 対応、FEP→SQLite→EXEC の前提|静的像を合格条件とする fixture。module 供給前に有効化しない|
|入力 1c9ed35c5（origin/main に存在）|USB の取得時修飾、remote 区別、root の媒体能力判定、表示 backend|再取り込みは不要。input_usb.h の仮宣言を M1 の共通口へ|

## 分担と着手順

各セル内の矢印は依存、各パスが一つの実装段。生成物は生成元の段で更新。単独では選択しない部品を先に入れ、最後の選択段で製品を切り替える。行数は手書き差分の概算。

|順|本線担当（各ファイル）|PPC 担当（各ファイル）|合流の条件|
|---|---|---|---|
|L1|tools/gen_sqlite_imports.py（型共有・PPC stub +20）、tools/module_schema.py（+25）→ tools/mkmod.py（形式選択 +50）、kernel/module/module_image.c（検証の分離 +50）|新設 tools/ppc/module_rela.py（+150）、新設 build/ppc-module.ld（+30）、新設 kernel/module/module_fixup_ppc.inc（+90、wire 演算のみ）|VA 0 合成 ELF の両形式、誤 CPU と split 境界|
|L2|kernel/module/module_runtime.c（公開直前 +3）、新設 include/platform_module_pair.h（+8）、PC-98 の新設 platform/pc98/module_pair.c（+10）|arch/ppc/kernel_entry.S（照合呼出し +8）、arch/ppc/kernel.ld（検証記録 +12）→ 新設 tools/ppc/module_pair.py（+100）→ 新設 platform/powermac/module_pair.c（+50）→ build/targets/ppc-powermac.mk（組合せ生成 +50）|各 request は getter の成否と値を policy に渡す（FEP fep_module.c:166、KCG kcg_module.c:66、診断 kdebug_module.c:48・hwdev_module.c:118、任意 DB sqlite_engine.c:219）。allocator・VFS・cache を含むゲストで二つの異なる配置から関数を呼ぶ|
|FEP|M1/M3/M4 の各所有ファイルを個別段にし、kernel/ime/fep_module.c、build/features/fep.mk、build/sources/hooks.mk の順に接続|新設 build/ppc-fep-module.mk（+45）、platform/powermac/input_usb.h の宣言統合、ATA/ext2 の辞書配置、静的 SQL 後の起動接続、resident provider と公開 dispatcher|実入力・辞書・owner 終了・SQL 独立|
|KCG|drivers/video/kcg/kcg_module.c（契約共有 約25）、build/features/kcg.mk（製品選択）|新設 platform/powermac/kcg_file_module.c（+45）、platform/powermac/kcg.c（既存 backend 接続）、新設 build/ppc-kcg-module.mk（+35）|既知字形と fallback|
|診断|kernel/module/debug_probe.c（共通結果抽出）、hwdev/hwdev_module.c（依存確認）|新設 platform/powermac/module_diag.c（診断 provider 約50）、PPC target の対応済み目標だけ開放|kdebug 欠損で hwdev 未実行、対象外 probe に I/O が出ない|
|任意 B08|kernel/sqlite_engine.c（起動・診断分離 約40）、kernel/sqlite_outer.c（静的分岐削除）、build/features/sqlite.mk（選択）、kernel/sqlite_static.c（削除）|新設 build/ppc-sqlite-module.mk（+65）、build/features/sqlite-static.mk（hard TU 一覧を前段へ移し削除）、platform/powermac/ppc_kernel.c（起動を置換）、PPC target makefile の旧 static group を削除|旧静的経路を製品と同時に外し、SQL 単独・欠損両像|

## 失敗と後始末

|原因|観測・後始末|
|---|---|
|依存なし・記録満杯|module_runtime.c:40–51、fd・頁を取らず返す。既存 record 不変、新規頁 0。静的 SQL が ready でない時も FEP 未開始|
|欠損・短読・誤 ID/世代/形式/pair|同:60–81,111–114、開いた fd を一度閉じる。header 段なら頁なし、確保後なら owner 回収|
|CRC・fixup 範囲・import 契約不一致|module_image.c:145–222,227–244 の全検証後に書く規則を継承。init、cache 公開、exports は未到達|
|pool/stack/guard 確保、init、candidate、prefix 返却の失敗|module_runtime.c:126–139,158–185。未公開、開始済みなら quiesce/detach/stop DMA。guard 復元が確認できた owner のみ返す。正常回収後は ABSENT・残留頁 0。FEP 失敗時は既存 SQL と打鍵を観測|
|依存あり、停止失敗、ref 残り|module_runtime.c:164–176。公開解除後 QUARANTINED。回収前の全 owner 頁を保持（prefix 返却済みなら残りだけ）。|
|reclaim 失敗|同:177–180。QUARANTINED。ledger に実際に残った owner 頁を記録し再利用しない。|
|retire 失敗|同:180–185。QUARANTINED。reclaim 成功後なので backing/pages/prefix_pages と残留頁は 0、owner の記録が残る。|
|辞書/学習領域の欠損・KCG 任意部品なし|読み込み失敗と辞書初期化失敗を区別して記録。学習不可なら書込禁止、変換不能なら素通し。SQL と shell 継続。KCG は既知のファイルフォントを確認|

## 大きさ・受入と変異

手元の既生成 x86 ファイルは SQLite 390716 B、FEP 35688 B、KCG 4672 B、kdebug.bin 145412 B、hwdev.mod 159144 B（build/out、今回再ビルドなし）。PPC 製品の実測は実装段。tools/module_schema.py:67–68,79–81,105–107 の現行上限（ファイル／body メモリ／prefix 含む割当、KiB）は SQLite 512/512/532、FEP 64/64/80、KCG 16/80/84。PPC の fixup 表増分は同数なら 8 B/件、各上限件数では +32/+16/+2 KiB。命令長と split 件数も変わるため x86 値に足しただけでは確定できない。各 body を実際に pack して file、payload、BSS、表件数、round 後の peak と prefix 返却後を記録し、超過なら先にコード生成と重複を調べる。必要な上限変更は PPC の行だけを実測から導く。

既定 M 辞書 5,826,560 B（レビュー実測）は固定帯 0x100000–0x3DFFFF の 2,944 KiB（include/memmap.h:169、arch/ppc/kernel.ld:57）を単体で超える。HDD に配置し像への加算は 0 B。7a の像増分は常駐 loader・接続コードと ISO 内 FEP module（ISO の丸めを含む）。常駐 loader の増分見積は PPC text 8–16 KiB と記録配列。SQLite pool は 512 KiB（lib/sqlite3/os32_sqlite_config.h:64）、別 stack 32 KiB＋guard 1 頁は kernel/sqlite_engine.c:140–149 と 7b 確定設計:171。静的 SQLite 込みの link-sh の実測余り 61 KiB（sh 188、shell 464 KiB、PPC_NOTES）を起点にする。61 KiB から loader だけを引いた概算は 45–53 KiB。ここに記録・接続コード・FEP・ISO の丸めが収まるかは最終リンクで判定する。静的 body の削減は計上せず、埋込 ISO を含む各段の resident/chain 差分、最大連続割当頁・prefix 返却後の owner 頁を測り、配布 ELF と実行時複製の両方を計上する。arch/ppc/kernel.ld:54–60 の resident・chain 制約、include/memmap.h:197 の image 予算を最終 map で確認。x86 の MINIMAL・起動 FD・kdebug.bin は既存生成と比較し、媒体空きと規定上限を維持する。

|受入|既存検査と固定の追従|落とす変異|
|---|---|---|
|x86 v1 互換と PPC v2 の既知値|tools/tests/test_mkmod.py、test_module_image.py、module_image_host.c、test_module_policy.py。新設 fixture は BE body/LE 表を独立生成、HA 境界の直前・直後、末端・重複・誤った機種を含む|HA 丸め削除、body のみ LE 書き、reloc/import 重なりを許す。異常入力時に元の配列不変を比較|
|実行と rollback|tools/tests/test_module_runtime.py、module_runtime_host.c の VFS・owner stub と covers に公開口を追加。新設 PPC ゲストで同じ頁に別命令を上書きして分岐、prefix 頁の再利用後も動作|prefix 解放を抜く、cache 公開を移動。キャッシュ非再現のエミュレータでは実ゲスト結果と命令列・呼出し順の検査を併用|
|pair と媒体|tools/ppc/kboot_memory_host.c、root_image.c の CRC 期待、PPC ELF audit。異なるカーネル・同じサイズの root、部品欠損、誤版、改変を各々起動|root CRC を pair に渡す、pair 照合を除く。起動ログと init 未到達を比較|
|任意 B08 のみ|tools/checks/sqlite-static-host.py、ppc-sqlite-static-guest.py と static probe は製品 module 検査に移行。tools/checks/ppc-db-guest.py、ppc-fp-guest.py、ppc-sqlite-stack-guest.py、sqlite-engine-host.py と tools/tests/test_sqlite_module_proto.py を維持。両像で同じ VFS の DB を作成・再読し REAL、終了処理、欠損時 stub を確認|FP end を抜く、import slot を入れ替える、boot check 前に公開。FPR/FPSCR・MSR・SP と戻り値で判定|
|必須 FEP|tools/tests/test_fep_module.py、fep_module_host.c、tools/checks/fep-budget-host.py、fep-worst-load-host.py、ver-ppc-input-usb.py。USB と remote、修飾、候補確定、辞書切替、owner 終了、辞書なしで入力継続|SQLite handle を除く、remote を local 扱い、終了時再開を外す。実際のキー列と次の読出しで判定|
|KCG・診断|tools/tests/test_kcg_module.py、kcg_module_host.c、tools/checks/ppc-kcg-guest.py、hwdev-module-host.py。module 有無で既知 ANK/JIS bitmap を比較し probe 実行回数を観測|字形行を落とす、依存順を逆転、未対応 probe を登録|
|選択と大きさ|tools/tests/test_build_profile.py、fixtures/build_profile_ppc.json と x86 版。製品 kboot-query から再生成。各段で同名ソースを読む tests/checks と host の製品 include、既存 mutation の置換箇所、CHECK の covers を追従。新規検査は tools/checks/weights.json へ測定値を反映。層違反の許容はゼロ|static と module の二重リンク、無効像への混入を map/provider 数で検出。生成器の世代固定、層 lint、両機種ビルド、変更検査の全変異を CI で確認|

### 段ごとの検査入力と変異アンカー

各行に掲げた製品入力、生成元、fixture、host stub を当該 CHECK の covers に加える。既存アンカーを含む関数を移す場合は移動先で一意にして実行時 RED を確認する。

|段|fixture・リンク入力／stub|検出する変異|
|---|---|---|
|L1|test_mkmod の BE RELA ELF と module_image_host の独立 wire fixture。生成 stub と provider_types を同じ型定義で cross compile。REL14 境界、非整列 entry/export/分岐、HA の独立 wire 試験（S+A=0x7fff、合成 base=1 で完全値 0x8000、期待 HA=1・LO=0x8000。頁整列 base のゲスト試験とは別）、負 A（S=0x104,A=−4 で 0x100）の各 fixture を分離|HA 丸め除去、A の無視、REL14 の範囲・整列検査除去を各 fixture で検出|
|L2|module_runtime_host の VFS/owner に cache spy を足し、呼出し時の text 内容・import slot・先頭・長さ・回数・init 前を照合。ゲストは異なる割当、同頁再利用、遠距離 stub、pair 取り違えを実行。pair の host 入力に linker script・entry・記録・root verifier を登録|cache 省略、relocate 前へ移動、最終 line 脱落。既存 prefix-failure-success、early-publication、dependency-free を維持（test_module_runtime.py:82–95）|
|FEP|fep_module_host に静的 ready/unready と PC-98 handle の二組。静的 SQL と実 import 表をリンクし、ATA/ext2 の辞書あり・なし・ISO の FEP 部品なしを別々に起動し、ありは既知候補、なしは素通し、部品なしでも SQL の作成・再読を確認。sqlite-static-host、ppc-sqlite-static-guest、ppc-db-guest、ppc-fp-guest、ppc-sqlite-stack-guest を静的製品のまま維持|boot-relay-zero（test_fep_module.py:48）、予約・snapshot 設定を飛ばす、隔離中の予約解除、再開・切替・I/O 再試行で user open を呼ぶ、非整列 candidate を通す、実 provider を欠落させる。SQL/FP/stack は既存の期待を維持|
|KCG|kcg_module_host の型付き provider と ppc-kcg-guest に既知字形の module 有無を追加。両像で ANK/JIS 行が一致し、ありでは provider を実際に通ったことも数える|wrong-export、adapter-double-init（test_kcg_module.py:249–255）、provider の一行欠落|
|診断|hwdev-module-host の依存 stub と製品 PPC の対応操作一覧を照合。C04 は L2 の loader 合格と、各操作の結果・非対応経路の I/O ゼロを別記録|依存順逆転、非対応操作登録|
|任意 B08|上表の切替用 fixture と module owner を使う SQL host/guest はこの段で更新|import slot 交換・boot check 前公開。必須段の静的 SQL を切替対象に含めない|

## 未決

1 件。辞書の供給も ATA の後になり、7a の公開が ATA の後にずれることを承認するか。PM はこの順を推奨する（「学習は書ける媒体まで待つ」を辞書供給にも適用する解釈）。

反論　0 件。

## 確定 (2026-11-20 夜、sol 第 3 回「条件付きで可」、ユーザー決定と PM の反映)
以下は本文に優先する。
- **ユーザー決定 (2026-11-20)**: 7a の辞書も学習も x86 と同じパス、7a の公開は ATA の後。RAM の読み取り専用の root はインストールの媒体 — **製品の PowerPC も x86 と同じく HDD にインストールして HDD (ext2) を root に起動する**。よって辞書は `/db/fep.db`、学習は `/etc/fep_user.db` (kernel/ime/ime_store.h の x86 の値のまま) が HDD の root の上にある。PowerPC の FEP の公開の前提は「ATA の口 → PowerPC の ATA の後端 → HDD へのインストール (x86 の hdprep・install に寄せる) → HDD の root での起動」。RAM の像の上の FEP の道は作らない。
- **P1 (§1) 学習のパスの設定**: load の前に M3 の `fep_boot_user_path_set()` と `fep_boot_user_rc_set()` で学習のパスと取得の結果を設定し、失敗の時は snapshot と同時に消す。
- **P1 (§3) 辞書の供給**: 上のユーザー決定で決まり (HDD の root の上の x86 と同じパス)。受入は HDD の root での固定のパスでの変換・学習の読み直しと、読み取り専用の時の変換。
- **P2 大きさ**: ATA・ext2・HDD の root を含む最終の構成で、FEP の追加の前後の resident/chain と ISO の丸め込みの差を測り、両方のリンカの制約を受入の条件にする。
