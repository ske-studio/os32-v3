# libos32snd をゲストアプリから呼ぶ確定設計（2026-11-24）

**採用したのは、音を要求したアプリの exec で固定パスの共有像を初回ロードする案 A。** GUI の起動時ロードを保ち、音は最初の利用まで物理ページを使わない。fmgen は libos32snd の中で ring 3 実行し、アプリは SDK の sndl 入口を呼ぶ。PCM の所有・転送は既設の snd2 HAL が担う。これでアプリをリンクし直さずに同じ ABI の合成器を配備できる（反映は再起動後）。

## 1. 設計基点の経路

比較基点は origin/main（8232174f）、音側は指定作業木（1ab4ac70）。sys/kern/mm/shlib.c、exec、sdk/link/shlib.ld は両者で同じ。以下はソースから読んだ契約。

| 境界 | x86 で現在行われる処理 |
|---|---|
| 媒体 → 原本 | sys/platform/pc98/kernel.c の shlib_init が VFS 初期化後に sys/include/config.h の SYS_SHLIB_GUI（/sys/lib/libos32gui.shlib）を開く。sys/kern/mm/shlib.c は単一のページ配列とロード状態を所有。/sys/lib は配備先で、走査も名前解決もない。L5 は userland/deploy.yaml に音像の配備を登録している。 |
| ファイル → 検証 | 外側は OS32X v5、64 B、LE。sys/kern/exec/os32x_hdr.c が CPU・既知 flags、KAPI 配置・ABI 世代、メモリ世代、共通 shlib protocol、最低 KAPI 版を照合。mm は SHLIB 専用 flag、固定 load_addr、entry_offset=0、ページ境界の code_size、本文＋BSS の上限を検査する。 |
| 原本 → 呼出表 | 内側の先頭 4 KiB は SLIB。32 B の先頭部は magic、ライブラリ版、nfunc、data_vaddr、data_pages、text_pages、予約 2 語（先頭は共通 protocol）。+32 から 32 bit 絶対関数番地。各入口はヘッダの次頁から RO 部終端まで。外側の text_size は初期 data も含む。 |
| アプリ → exec | 通常アプリは sdk/link/app.ld で 0x80100000 にリンク。mkos32x.py は ELF の __os32_shlib_dependency から protocol を刻印する。非ゼロは実質 GUI が必要という意味で、常駐 shell の依存は拒否。crt0.asm が BSS を消し、_start_c が argc/argv/KAPI/aux を受け、_init、main、sys_exit へ進む。 |
| exec → アドレス空間 | exec_image_map.inc が GUI 私有頁と PT を予算化。exec_launch.inc は ring 3 の起動時に、依存印の有無によらず、ロード済み GUI を attach。master CR3 で RO+USER のヘッダ・text・rodata に同じ物理頁を張り、data/BSS は原本から AS 所有の RW+USER 頁へ複製。原本の直接写像は supervisor。 |
| 検査・終了 | access_walk と lease 監査には shlib_read_page が VA・frame・所有者の組を保証する。途中失敗は張った頁を戻す。exec_finish.inc が AS 破棄前に detach し、私有頁を返す。原本は常駐。GUI の配置世代違いと最低 KAPI 不足は PC-98 の起動診断が区別する。 |
| GUI アプリ → GUI 実装 | GUI 本体は crt0 を使わず sdk/link/shlib.ld で固定配置し、mkshlib.py が表番号と ELF の実番地を照合する。Rust の gui/stub.rs の bind は magic・共通 protocol・GUI_PROTO_VERSION・最低表長（現 124）を確認。E_SHLIB_INIT にアプリの KernelAPI ポインタを渡し、以後は番号から extern "C" 呼出し。 |

**L5 の到達点。** build/os32snd.mk が整数版 Rust staticlib を C の mixer 等と結び、同じ shlib.ld・mkshlib.py で音像を作る。GUI と音の双方のヘッダが 0x80000000 にあり、一つの AS に同居できない。原本のパスを一つ足しても解決しない。

音表 v1 の entries.inc は sndl_voice_new から sndl_voice_done まで 8 口。opna_* と sndl_mix_render 等をリンクに残す -u は内部呼出しのためで、アプリ入口を作らない。発音には AV §7 の pump（L3）、FM adapter（L2）、SOFT chip の選択（追記の L6）、それらを呼ぶ SDK stub が要る。SYNTH r3 §4・§8 の L5 は最終リンクまでを担当する。

## 2. 選択肢

行数は x86 の製品・生成規則・対象試験の追加変更の概算。生成物、予定済みの L2/L3/L6、PPC 実装は別勘定。いずれもアプリ向けの音 API は同じにできる。

| 案 | 利点と費用 | x86・KAPI・PowerPC・受入 |
|---|---|---|
| A　要求した exec で初回ロード（採用、450〜750 行） | 閉じた登録表の音 ID にだけ固定パスを対応させる。未使用機では原本 0 頁、2 本目から RO を再利用。初回起動に VFS 読込と検証が入り、再入・失敗時の扱いが要る。 | GUI と CUI の現行の起動経路を維持。依存表現と音枠を SDK/exec で合意し、新規 KAPI は不要。PPC は同一要求と公開 ABI に CPU 別の像・写像を実装する。受入は音アプリだけで初回ロードし §4 のゲスト発音に成功。 |
| B　起動時に固定一覧をロード（350〜600 行） | GUI・音を個別検証して原本を常駐させる。起動後の VFS 再入や初回待ちを省ける。音の無い利用者も起動 I/O と原本を負担する。既存音像では初期 data/BSS の原本込みで 33 頁。 | attach は A と同じ要求制にして 8 MiB 機の CUI に音の私有頁を課さない。壊れた音像は音だけを利用不可にする。KAPI 追加なし。PPC も VFS 後に CPU 対応像を個別登録。受入は欠品時の CUI/GUI 起動と、正常媒体での §4。 |
| C　SDK の静的 libos32snd.a（150〜300 行） | 既存の通常リンクへ本体を加え、音枠・ローダー変更を省ける。コードも利用アプリごとに配備・ロードし、修正時は各アプリの再リンクが必要。単独アプリの早期実証には小さい。 | x86 GUI、exec、KAPI は現行どおり。PPC 向けにも同じ C API の target-native archive を作る。最終アプリで命令・panic 終端を検査。§4 の発音を実証できるが、配備済み共有モジュールをロードする受入は満たさず、AV §7/§8 と SYNTH r3 の配布決定の変更を要する。 |

全案で状態は利用アプリ所有。32 bit の OPNA state は 71,544 B、alignment 4（lib/os32_opna/opna_layout.h）。ADPCM RAM 256 KiB・リズム素材・scratch は別に見積もり、必要な chip の生成時に確保する。共有で節約できるのは主にコードと定数表。既存枝の像の内ヘッダは GUI が RO 44＋私有 12 頁、音が RO 28＋私有 5 頁を示す。将来の音機能と実行時 state をこの 5 頁に含めて予算化しない。

**判断。** HAL 中心期に必要なのは、SDK から sndl を通じて実際の PCM に届く最短の共有経路。A は非利用時の RAM 費用を消し、音専用の syscall もカーネル内の合成も要しない。B は音を常時使う媒体での代案。C は静的配布を製品仕様に選び直す場合の代案。fmgen の原文 4 点・変更表示・配布条件は SYNTH r3 および配布像の /sys/licenses/fmgen/ に従い、[L1] を守る。

## 3. A の実装契約

1. **番地と共存条件。** GUI は従来どおり [0x80000000, 0x80100000) の全 1 MiB を使用できる。音だけを 0x80080000 に再リンクし、音の RO＋私有頁を上側 128 頁以内に制限する。数値は memmap.h を正としてリンク・pack・mm で照合する。exec は音要求時だけ、ロード済み GUI の検証済み text_pages＋data_pages が 128 を超えれば、音の読込・AS 確保・入口実行の前に番地衝突を返す。ちょうど 128 頁は許可する。GUI 不在なら空き、現在の 56 頁なら境界まで 72 頁（288 KiB）。GUI 単独の 129〜256 頁も従来どおり起動する。音専用でもロード済み GUI を暗黙 attach する旧動作を保つため、この衝突検査を通す。
2. **依存の符号化。** OS32X v5 の flags に SND=0x0080、WITH_GUI=0x0100 を割り当てる。後者は音アプリが GUI も必須とする修飾子。SDK は従来の GUI 依存印と新しい音 stub の ELF 印から次表を生成する。protocol は wire offset 56 の共通表形式番号（現行値 2）。表中の値は新規 2 bit のみ。

   | 新規 bits | protocol | アプリが必要とする原本 |
   |---|---:|---|
   | 0 | 0 | なし |
   | 0 | 2 | GUI（旧バイナリの意味を保存） |
   | 0x0080 | 2 | 音 |
   | 0x0180 | 2 | GUI と音 |

   WITH_GUI 単独、音要求かつ protocol=0、未知 protocol、非 ring 3・常駐 shell・SHLIB 本体への新規 bit は入口前に拒否する。SHLIB 本体は従来の protocol 検証を通す。exec_image_map.inc の「非ゼロなら GUI 必須」は表の必須集合で判定する。暗黙 attach と必須判定を区別する。
3. **互換性と実機配備。** sys/kern/exec/os32x_hdr.c の os32x_layout_check は known を GFX/RING3/SHLIB/CUI_ONLY/LAUNCHER/PPC32 の論理和とし、flags & ~known なら OS32X_LAYOUT_OLD を返す。旧カーネルは新しい音アプリを入口前で拒否し、新カーネルは旧アプリ・GUI の番地、上限、依存印、SLIB 表形式を維持する。よって memory_layout_generation=4、shlib_protocol=2、OS32X v5、KAPI ABI=2 を据え置く。音表 v1 の既存 8 口を固定し、同 major の入口は末尾に追加する。実機は通常の boot 先行同期で新カーネルを先に入れ、音像・音アプリを同期して再起動する。旧 boot に戻しても旧 GUI/CUI が使える。
4. **初回読込。** exec_launch.inc の static resolved と static hdrbuf を呼出し単位の起動コンテキストへ移す。パス探索から同じファイルの本文読了まで保持し、待機中に別 exec が双方を書き換えられないようにする。音 ID ごとに短い排他区間で未読→読込中を取得した一人だけが、固定パスを開き、一時 descriptor に全頁を取得し、内外ヘッダ・番地・音表・入口を検査する。VFS I/O・頁確保中は排他を解く。後着は同じ試行に待ち登録し、状態検査と睡眠登録を不可分にして CPU を譲る。成功時は排他下で完成 descriptor を一度だけ公開して全待機者を起こす。失敗時は fd・仮頁を全回収して未読に戻す。待機者には参加した試行の結果を保持して返し、次の独立した exec が再試行する。待機者の取消は他の要求を取り消さず、読込所有者の中断も同じ失敗後始末を通す。
   LD3 では通常アプリのスロット取得を読込・待機の完了後へ移す。現行の appslot_start_admit() は空き ID を返すだけで予約せず、後段の予算検査で返す ID も使われない。起動前半は呼出し元 ID と起動計画だけを保持する。公開後、§3.5 の予算検査を通った呼出しが、全起動経路に共通の短い排他区間で空き ID を選び、同時に予約済みにする。空きがなければ OS32_ERR_FULL。予約した呼出しだけがその ID の ctx を初期化し、起動成功で実行中に移す。失敗・取消時はその予約に属する資源を回収してから ID を空きに戻す。常駐 shell は従来の専用 ID を使う。
5. **予算と回収。** 公開後に「ロード済み GUI＋要求した音」の同一集合で私有頁と重複しない PT を含む必要総頁数を算定する。LD3 は ring 3 の実測空き頁が必要頁数を下回れば、スロット予約・AS 確保の前に EXEC_ERR_NOMEM を返す。公開直後の空き 0 頁もこの比較に含める。appslot_start_admit() の free_pages == 0 は予算検査を省く合図なので、実測値との比較は独立して行う。予算通過後は予約した ID の AS に master 上で attach する。AS ごとに進捗を記録し、途中失敗・終了・kill は逆順で写像、私有頁、不要 PT を回収する。read_page と監査は検証済み VA/frame を照合する。公開した音原本は起動予算不足でも再起動まで常駐する。L5 計測値は 33 頁（132 KiB）、音利用 AS の追加私有分は 5 頁。初回失敗が公開前か公開後かで回復すべき空き頁の基準が異なる。
6. **呼出し。** SDK の C89 ヘッダと型付き C stub（Rust は同じ C ABI）を entries.inc と同一の番号定義に結ぶ。初回 bind で表を照合し、新設の init 入口へアプリの KernelAPI を渡す。失敗時は後続入口を呼ばない。chip_open/write/close、pump、stream の open/close 等の公開分だけを追加し、OPNA のレジスタ実装・Rust 状態はライブラリ内部に保つ。
7. **PPC と後続設計。** POLICY_DEV §10 に従い公開関数・失敗条件・所有・PCM の s16le を x86 に揃える。PPC は現在 sys/kern/exec/absent/exec_shlib.c で未提供。音依存は入口前に拒否する。対応時は PPC32 フラグ付き像、PPC 用リンク・呼出規約・ページ保護・コードキャッシュ同期と、外包み LE／内表の byte order の明示的な encode/decode を用意する。共通部へ x86 CR3 操作を持ち込まない。
8. **P7 への引継ぎ。** LIBC_SHLIB_DESIGN §3–4 の 2 MiB 帯、shlib.idx、(id, major) の依存表は後続の一括切替。上記の音番地は P7 の libc.2 予定枠と重なる。P7 の同一世代一括切替で音を枠 3 の 0x80180000 に移し、枠 1 を libc.2 に渡す。P7 の配置規則で音 data を 0x801F0000 起点、終端を 0x801FF000 以下（60 KiB）として時計頁を除外する。現行私有 20 KiB は収まる。P7 の表・依存 ID に移して SDK・アプリ・全像を再リンクし、リンク時・読込時に両端を検査する。既に使われている OS32X v5 を P7 の新形式番号に流用しない。今回の IL は libc 共有化を前提にしない。

## 4. 実装段と合格条件

AV_HAL_DESIGN §12 の段に対応する。担当は主変更点で分け、L6/L7 の既存番号を保つ。

| 段 | 主変更点 | 依存 | 保持する状態 | 行目安 | 統合 | 着手前提 |
|---|---|---|---|---:|---|---|
| LD1 | SDK の音枠・依存印・リンク/pack・旧世代互換と表の検算 | L5 | 枠契約 | 100〜160 | IL | メモリ契約の決定 |
| LD2 | sys/kern/mm/shlib.c の枠別原本、初回読込、attach/detach と RO 判定 | LD1 | 原本・AS の付着集合 | 120〜200 | IL | LD1 |
| LD3 | exec の要求検証・初回取得・公開後の予算検査（空き 0 を含む）・待機後の ID 予約と巻戻し | LD2 | 起動計画 | 70〜120 | IL | LD2 |
| LD4 | SDK の sndl stub、音表と init、ゲスト sndl_test・対象 host 試験 | LD1〜3・L2/L3/L6 | アプリの bind 状態 | 160〜270 | IL | ソフト音源と PCM の接続 |

§13-1 の IL 行は既存の前提・DOS ゲーム試験を残し、前提に **LD1〜LD4、SOFT 選択の L6、PCM の IS** を追記する。§13-2/3 の追加受入は次のとおり。

- **NP21/W 発音。** Pentium 以上・PCM 対応構成で通常 SDK で作った sndl_test を起動。PREFER_SOFT で SOFT を確認し、決めた音色・音程で key-on/off、pump、drain、close。PCM の取り込みで発音区間の非無音・期待音程・消音後の静音を確認し、played frame の進行を記録する。実 FM の出力や host の render 単体を合格の根拠にしない。
- **ゲストでの切り分け。** 音像を外すと音要求だけが入口前に欠品を報告し、従来 GUI/CUI は起動。GUI を外した音専用アプリでも動く。GUI＋音のアプリでは両方を呼ぶ。2 AS の mixer 状態が独立し、終了後の再起動で前回の voice が残らないことを確認する。
- **host と変異。** 4 種の依存、別 CPU・旧世代・誤った load_addr・枠越え・表不足を検査。初回失敗からの再試行、2 AS の RO frame 同一／RW frame 別、写像途中失敗で私有頁と PT が回収されることを試す。依存判定の脱落、音の RW 原本直接写像、途中解放の欠落を各 1 箇所変えてそれぞれ RED を得る。

- **同時初回。** 異なるパス・ヘッダの音アプリ 2 本を VFS 待ちで交差させ、両者が別 ID で同時に生存し、各自の入口と起動引数、原本読込・公開が各 1 回、共有 RO と独立 RW を確認する。一方を終了させ、他方が元の ID・状態で発音を続け、独立して終了できることを確かめる。初回に短い読込を注入し、両者に同じ失敗、仮頁残数 0、修復後の次回だけ再読込成功を照合する。待機者にも公開させる変異を公開回数で RED にする。
- **LD4 の空き 0 頁境界。** 初回の音原本公開で実測空きがちょうど 0 頁になる条件を作る。正の必要頁数に対して EXEC_ERR_NOMEM、AS 確保の呼出し 0 回、スロット予約 0 件、原本は公開済みのままと照合する。空き 0 を検査対象から外す変異は AS 確保前の拒否を観測して RED にする。
- **境界と互換。** GUI の RO＋私有を 127・128・129・256 頁とした適正像で、音要求は前二者のみ成功、GUI 単独は全て成功。境界検査の削除は「音原本取得前に衝突」の観測で、等号の逆転は 128 頁の成功で RED にする。旧カーネル＋新音アプリでは未知 flags、新カーネル＋旧 GUI アプリでは既存入口までを照合する。
- **8 MiB ゲスト。** 同一媒体・設定で小さい CUI の起動中と終了後の空き頁を記録する。音アプリ初回の原本公開直後の空き頁、起動計画の必要頁、実際の成否・理由を対に記録する。予算を満たせば入口と発音、足りなければ入口前の予算エラーを期待する。公開前の読込失敗、公開後の予算不足、attach 途中失敗、成功後の kill を個別に再現し、同じ CUI を再起動・終了して同じ時点同士を比較する。公開前失敗は元の値、公開後は原本 33 頁だけ少ない値（更新像では実測原本頁数）に戻ること。8 MiB で通常起動できない構成の kill 試験は、同じ媒体でメモリを増やして補い、その条件を記録する。

## 確定 (2026-11-24)
sol 第 1 回 直しが要る → 第 2 回 世代の据え置きは妥当・P1/P2 → 第 3 回の直し (起動の ID は待った後に選んで予約、空き 0 頁は AS の確保の前に EXEC_ERR_NOMEM) を PM が照合して確定 (往復 3 回)。
