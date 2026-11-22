> 出所: ~/os32-tmp/run/ppc-image-budget/DESIGN3.final.md (astra 起草、sol 2 回、PM 確定 2026-11-20)。
> 状態: **確定 (2026-11-20)** — PowerPC の RAM の root を lz4 の別の PT_LOAD にし、起動時に x86 と共通の LZ4 の展開で rd0 に。常駐は x86 と共通の上限の中。ユーザー決定 2026-11-20 (本線のセッションで「lz4 をメモリ展開するのでこの形を踏襲すれば解決しないですか？折衷案があるはず」→「この案に切り替える」、PowerPC のセッションでも確認)。

# r1→r2 の変更

- P1　G4 の像直後の 32 KiB と KHEAP_BASE の重なりを、配置実証の合否に加えた。
- P1　最大寸法の診断 ELF で、ロード前後の予約差分と両 LOAD の所有を照合する。
- P2　一時 VA、ページ表、アクセス属性、成功時と失敗時の写像除去を定めた。
- P2　製品・font normal の保存実測と、全構成で今後測る最大値を区別した。
- P1　LZ4 の本体だけを既存の翻訳単位に取り込み、x86 の生成物一致を接続条件にした。
- P2　共通 LZ4 と機種固有の LOAD・予約情報の依存境界を定めた。
- P1　L2 の低位記録、二つの PHDR と記号、pair の入力バイト順、entry の照合時点を固定した。
- P2　枠超過時の再計算と、暫定 ELF からの全再生成を規定した。
- P2　展開の戻り値と rd0 の借用開始点を定め、登録失敗と mount 失敗を区別した。
- P2　x86 の媒体別バイト比較とゲスト起動を分けて判定する。
- P2　上限・展開・pair・所有権の負例を個別の変異と対応づけた。
- P2　配置実証、共通化、L2 二段生成、起動への接続の順に分割した。

## 容量の根拠

基点 0e735249d（5c＋B11）。数値は排他終端。r1 の保存ログ ppc-image-budget3-r1/{measure.log,segments.log,product-retry.log,font.log} と同所 tmp/measure.py による。r2 は設計改訂でビルド・実機計測の再実行はしていない。

|測定値|製品|font 通常|
|---|---:|---:|
|元の常駐終端|0x366000|0x376000|
|生 ISO を算術的に除いた終端|0x2e0000|0x2e2000|
|同じ差引きによる chain 終端|0x34b000|0x34d000|
|差引き後の両上限までの余り|596 KiB|588 KiB|
|同じオブジェクトで root だけ高位に再リンクした常駐／chain 終端|0x2d6000／0x341000|0x2d6000／0x341000|
|再リンク後の両上限までの余り|636 KiB|636 KiB|
|生 ISO → LZ4 ブロック（B）|548,864 → 319,938|606,208 → 305,560|
|展開先の連続 RAM（4 KiB 頁）|134 頁、536 KiB|148 頁、592 KiB|
|G4 の展開時間の概算|5.5～22 ms|6.1～24 ms|

低位の上限は像 0x375000、chain 0x3e0000。再リンクでは後続 section の整列を ld に再計算させた。636 KiB は展開処理・検証記録を追加する前の余り。リンク時計測には生 ISO をそのまま高位に置き、圧縮長は同じ ISO に tools/mkvmkernel.py と同じ raw block、HC 12、store_size=False を適用して往復一致を確認した。VK32 ヘッダは含まない。
時間は 800 MHz、逐次コピー主体の C 展開を出力 1 B あたり 8～32 サイクルと仮定した値。OF の転送・頁確保・CRC は別計時する。実機では time base の差を tb_hz で割り、製品と font の各区間を記録する。

生 ISO 再リンクの値には新しい記録・ページ表・展開処理はまだ入っていない。高位枠の初期見積りは各 327,680 B、311,296 B（下記の丸め式）。二つの構成でも圧縮長と展開頁数の最大は別の像になる。

## 第1段　両 OF による配置の実証

低位 PT_LOAD は 0x100000 起点、従来の PT_NOTE、BSS、ledger、起動 stack を含む。高位は専用 section の R 属性 PT_LOAD 一つ。候補は PA＝VA＝0x04800000。各 LOAD の整列合同、PA＝VA、非重複、ファイルに VA 差の穴がないことを readelf で確認する。像と chain の上限は include/memmap.h から生成される既存定数をそのまま使う。§9 への追加は不要。

1. 製品 kboot-link-sh と tools/accept/ppc_{boot_info,console_cells,db,display,display_lifecycle,font,fp,gfx2,kcg,loop,mouse,newlib,path,pci,port,rtc,sfs,shm,sqlite_stack,sqlite_static,surface,sysinfo,ver}.py の各 case（font は normal・range-end・font-absent）を列挙する。build/targets と tools/ppc の全受入入口から実際の ELF 生成引数を併合し、共用像を重複排除する。各像の resident_end、chain_end、高位枠、生 ISO 頁を表にし、列別の最大値と像名を保存する。低位 memsz と高位枠の各最大を同時に持つ診断像を作る。最終寸法が包絡を越えたら第1段を再実施する。
2. PPC_BOOT_DESIGN §§2,6–7 の OF boot 経路を使う。mac99/OpenBIOS と QuickSilver OF 4.x（USB 鍵盤と screen を撮影）で entry、load-base/real-base、chosen mmu、RAM reg と available をロード前と最後の OF 問合せで採る。OF-PPC Binding 2.1 §8、OpenBSD macppc/stand/ofwmagic.S と macppc/ofw_machdep.c が NOTE・reg/available の根拠。二つの離れた LOAD の予約はファームウェアごとに実証する。
3. 前に空いていた各 PA 頁が後に予約され、各 VA 頁の translate が期待 PA を返し、両端を含む固有パターンが全 file-backed 域で一致することを確認する。OF の一時ファイルコピー、既存予約、デバイス、隙間を覆う一括 claim を前後の地図・転送範囲と照合する。既占有で由来を区別できない候補は棄却。LOAD の再 claim はしない。候補の必要 RAM 終端は 0x04800000＋最大枠（製品見積りで 0x04850000）。連続 RAM を仮定せず reg で包含を検査。既存 mac99 受入の最小試験構成は 128 MiB。対応機の最小 RAM で同じ判定を通すまで採用番地は未確定。
4. kboot_reserve.c の image_end＝window は最初の available まで最大 64 KiB 進むが、include/memmap.h の KHEAP_BASE は __kernel_resident_end から始まる。間の 32 KiB を allocator から除外しても heap が上書きする。両 LOAD の位置・寸法を振り、低位終端直後から chain の末尾までと、その先の既存 15 MiB 窓が連続 available、16～32 MiB に整列済み 128 KiB HPT を取れる組を両 OF で選ぶ。予約のある番地への試験書込みは禁止。実寸で配置が決まるまで接続へ進まない。
5. 配置で解けない場合の再レビュー案は、低位 LOAD の物理終端を別記号にし、その外の 64 KiB を機種の予約として課金した先に論理 __kernel_resident_end を置く形。heap と全 chain をそこから導出し、二つの共通 ASSERT は予約込みで適用する。OF が実際にそこまでに収まり、以後の全窓が空くことを再実証する。低位 LOAD の memsz を伸ばすだけでは予約も後ろへ動く。

## 第2段　x86 の同一性付きの共通化

lib/crc32_core.inc と同じ配置で lib に共通 include を置く。boot/lz4_mini.c の boot_lz4_decode と lib/lz4.c の lz4_decode は従来の宣言・引数型・翻訳単位のまま、関数の中に同一ループ本体を include する。初期化順の差が命令列に出る箇所は各入口に従来どおり残す。定数は共通の数値定義に対応づける。本体の機種・装置ヘッダ依存はゼロ。HDD -Os、PPC -O0、x86 kernel/userland の現行個別フラグを保持。LE offset、重複 match の順コピー、負長・切断・不正 offset、出力超過 −2 のベクトルを HDD C、lib C、FD の pm_lz4_decode の対応入口へ通す。

同じ基点・ツール・入力で tools/build_compare.py --all を実施。BOOT HDD/FD/FD144、kernel、VK32、MINIMAL、kdebug、userland、媒体まで対象一覧とバイト一致を記録する。ツール対象外の媒体は個別に cmp。差異があれば翻訳単位の初期化・命令列を調べこの段で直す。check-layers、common-compile、VK32 正負例も通す。x86 QEMU で対応する起動経路を走らせ、PC-98 専用媒体は NP21/W の成否を別記録する。未実行は runner・媒体名を残す。

## 第3段　L2 の二段生成と pair

基点と本線作業木の docs/decisions を検索した範囲では L2 本文を特定できなかった。確定稿 ppc-module-l1/DESIGN.final.md「カーネルと root の組」（52–59 行）の生成器・entry verifier・platform_module_pair を L2 と同じ変更で取り込む。単一 LOAD 版を先に媒体へ出さず、次の wire を host/guest 同時に採用する。

- 低位 L は (__kernel_image_start, __kernel_file_end, __kernel_load_mem_end)、高位 H は (__root_image_start, __root_file_end, __root_load_end) で各 (VA, file 終端, mem 終端) を表す。L の mem 終端は実 PHDR 末尾、通常 __kernel_resident_end。H の filesz＝memsz＝固定枠長。低位の file-backed RO section に __root_record_start/end を設け、16 B の記録を LE u32 の pair、最終生 ISO CRC、実圧縮長、生長の順で置く。ISO CRC の C 即値は除く。
- pair の前置きは全て LE u32、詰め物なし。順は entry、L.va、L.filesz、L.memsz、H.va、H.filesz、H.memsz、root 枠 VA、枠長、記録 VA、記録長。サイズは各終端から VA を引く。続けて L、H の順に各 filesz バイトを番地昇順に取り込む。記録と root 枠の全バイトを同長ゼロに置換。LOAD 内の file padding は含み、BSS・ファイル間隙・NOTE の二重取り込みはない。CRC は crc32_core.inc の反転 0xedb88320、初期値と最終 XOR とも 0xffffffff、全列で一度だけ確定する。
- host は e_entry＝ppc_kernel_entry、二つの実 PHDR と記号、記録の包含を検査。guest は同じ記号から列を組む。固定した小型 ELF の wire のバイト列と独立計算した既知 CRC を fixture にし、host と PPC BE verifier を別々に照合する。順序交換、記録のゼロ化漏れ、memsz 改変で検出する。
- 同サイズの仮 pair 部品入り ISO を raw LZ4 HC12/store_size=False で圧縮し、初期枠を align_up(圧縮長,4096)＋4096 とする。暫定 ELF の pair で部品と部品 CRC を作り直し、最終 ISO を再圧縮。超過時は同式を超過した実長に適用、別作業先で暫定 ELF から全依存物を再生成。実証済み最大枠を越えれば停止。入力順・日時・圧縮器を固定し、残りをゼロ詰めする。最終 ELF の section 配置・サイズ・pair が暫定と一致し、再展開した生 ISO の長さ・CRC・内容一致後にだけ最終像を rename で公開する。
- entry は OF 引数を stack に退避し BSS clear 直後、ppc_kernel_start と file-backed data 更新の前に照合。計算中の状態は stack、結果は BSS。高位枠はゼロ列として供給する。OF 引数を復元。生 ISO CRC も合格した時だけ getter を有効にし、別 kernel の部品は loader の pair 検査で拒否する。

## 第4段　予約・写像・rd0 と退行検査

- 最終 available で第1段の予約条件を再検査して detach。LOAD 範囲、予約値、機器との照合は PowerMac 内に閉じ、共通コードの機種選択には include/platform_module_pair.h を使う。低位 pool の採用端は 16 MiB。
- 一時 VA は第1段の高位 VA と同値の専用頁枠。kernel_boot.c の ppc_boot_load_root で root 切替前に、必要 PDE 数ぶんの頁整列した専用 PT を低位 BSS に確保済みとし、容量集計にも含める。既存 PDE を上書きせず、VA/PA 両範囲について shell、固定帯、HPT、低位 pool、ESCC/MPIC/FB/clock/VIA/GMAC と交差、加算 wrap、既設 alias を拒否。supervisor RO・通常 RAM の WB で各頁を設定し、walk で PA と属性、書込・user 拒否を確認する。装置の RW/PCD 用 install_alias は使わない。
- 機種側の unmap 入口が成功・失敗とも設置済み PTE を消し、該当 HPTE と TLB を無効化・同期して再 walk の不在を確認し、専用 PDE を外す。管理外の高位 PFN は free せず OF detach 後の release もしない。専用 BSS は常駐予算に残す。
- ppc_kernel.c は allocator ONLINE 後に int pm_root_image(struct boot_root_image *out) を呼び、0 の時だけ pm_root_boot に進む。長さの正値、int と頁丸めの上限、圧縮長の枠内包含を確認して pgalloc_alloc_n_owner で LEDGER_OWNER_BOOT の低位恒等写像済み連続頁を得る。実圧縮長だけ入力し、生長まで展開。戻り値＝生長、全生バイトの CRC と entry の pair 合格後に boot_common と out を公開する。
- 登録前失敗は出力を空にし同 owner に取得済み全頁を返す。boot_root_mount 内で ram_ro_register の成否を呼出側へ伝える（登録済みフラグを返す内部入口を追加し既存入口を維持）。登録失敗なら返却、登録成功後は rd0 が借用するため / または /tmp の mount 失敗でも保持。root_ready は全成功時のみ。rd0 は RAM インストール媒体で、HDD root 上の既定 FEP の探索・起動経路を維持する。
- host の独立 fixture で resident と chain の各上限ちょうど／各 1 頁超過を他の制約から切り離して検査。ASSERT 除去、root を低位へ戻す変更、枠超過検査の除去はそれぞれ link 結果・section 所属・未公開で検出する。
- 確保失敗、切断、不正 offset、出力超過、正常だが短い出力、同長 ISO 改変、別 kernel pair を個別に注入。戻り長・CRC・pair の各照合を外す変異は対応する失敗段と公開状態で RED。登録前後の失敗を分け owner 頁数、rd0 登録の有無、root_ready、写像不在を検査。unmap を省く変異は再 walk と既に参照した頁の実アクセスで検出する。
- mac99 7450/7455 の双方で全 PPC 受入・既存変異を実行。負例の停止理由と rd0 未公開、成功時の rd0 の内容・書込拒否、最大 font、shell、module 公開順、guard、確保前の最大連続空き頁数と起動後収支を採る。各段で CHECK.covers と入力を更新し既存 mutation のアンカーを追従、一意な変更で対象の観測が実行時 RED になることを確認。両 CPU 系の起動退行を最終像でも再確認する。
- QuickSilver では OF 転送は loader 側の前後、割当・展開・CRC は各呼出前後の time base 差と tb_hz で別計時する。製品・font の値を r1 の仮定値と並べる。

後続 DESIGN.r1.md 案 a は RAM root の取得元を ELF の外へ移し、生 ISO の検証と rd0 の借用契約を再利用する。
未決（ユーザー）は QuickSilver 実機の物理操作の承認 1 件。両 OF の配置・最小構成・実時間は PM の計測事項。共通上限または対応機の変更が必要になった時点で別途判断を求める。
反論 0 件。

## 確定 (2026-11-20 夜、sol 第 2 回、PM が反映)
以下は本文に優先する。
- **P1 (第 4 段、一時写像の書き込み保護)**: 圧縮の区画の一時写像に**ハードウェアの書き込み保護を求めない** (PM 決定)。理由: 現行の PowerPC の walk は supervisor の store で RW を見ず supervisor の頁を PP=0 にするので、RO の PTE は書き込みを止められない (arch/ppc/pd_walk.c の ppc_pd_lookup_read、arch/ppc/kernel_cpu.c の ppc_cpu_load_segments・SEG)。x86 も vmkernel.lz4 の置き場を書き込みから守っていない。シングルタスク・カーネルだけが触る区画なので、守りは**展開の後の CRC の照合** (生の root の CRC、本文の pair の契約) で取る。RO の属性・書き込みの実アクセスの受入・それの変異は本文から外す。写像の除去 (展開の後に一時写像を外す) と、除去の後の読みが失敗することの受入は残す。
- **P2 (第 3〜4 段、root の無い像)**: RAM の root を持つのは KB_EXEC の像だけ (build/targets/ppc-powermac.mk の KB_EXEC・KB_INC_EXTRA・KB_EXTRA)。root の無い像は単一の LOAD と従来の道のまま — 二段の生成・高い LOAD の検証・予約の検査・一時写像はしない。選択は build の側の 1 か所 (KB_EXEC) で決め、ppc_kernel.c の起動の道は root の区画の有無を kboot の情報から読む (機種のマクロを足さない)。root の無い像も受入の像の一覧に入れ、単一の LOAD であることを host の検査で照らす。
- **P2 (第 4 段、登録の前の失敗)**: pm_root_boot (platform/powermac/root_boot.c の pm_root_boot) は boot_root_fs_init の失敗でも登録の前に戻る。展開の入口から boot_root_mount (init/boot_root.c の boot_root_mount・boot_root_start) まで「rd0 に登録したか」の旗を戻り値で最上位まで返し、初期値は偽。旗が偽なら展開先の頁を返し、真なら rd0 が借りたまま (解放しない)。両方の道を host の試験で照らす。
- 両 OF の配置と最小の RAM は第 1 段の採用の条件 (第 1 段で解けなければ再レビューへ戻す)。G4 (QuickSilver) の実機の物理の操作はユーザーの承認を得てから (実機ノートへの依頼、[D2])。

## 本線のレビュー (2026-11-20 夜、本線 PM、可 — 第 1 段から)
- 可: 第 2 段の build_compare --all のバイト一致 (boot/lz4_mini.c は PC-98 の起動の LDR なので、一致しない時は直すまで出さない)、LOAD の範囲・予約・機器との照合を PowerMac に閉じる、共通の LZ4 の本体は機種・装置のヘッダに依存ゼロ、L2 の pair は本線の口 `int platform_module_pair(u32 *out)` を変えずに PowerPC の中の wire を 2 つの LOAD に広げる (単一 LOAD の版を先に出さない)。
- 記録 (1): L2 の確定設計は docs/decisions/ppc/PPC_MODULE_ALIGN_X86_DESIGN.md (wip/kfill-designs、末尾の確定の節に L2 の口の寄せの範囲・L1 の接続の PM 決定)、本線の側は main 87741a727 — 第 3 段の参照はこれ。
- 記録 (2): 第 1 段の 5 の再レビュー案 (低位の物理の終端を別の記号にし __kernel_resident_end を予約込みに、共通の ASSERT を予約込みで当てる) を取る時は、共通の memmap の意味に触るので本線のレビューに戻す。
- 記録 (3): 第 4 段の「共通コードの機種選択には include/platform_module_pair.h を使う」は取り消す。root の区画の有無は確定の P2 のとおり build の 1 か所 (KB_EXEC) と kboot の情報で決め、pair のヘッダには入れない。
- PM の段の分け方: 第 1 段を 1a (QEMU の OpenBIOS — 像の一覧と最大、診断の像、配置の採取の台本) と 1b (G4 の OF 4.x — ユーザーの承認の後、実機ノートへの依頼、診断の像は自分で起こし直す作り) に分ける。
