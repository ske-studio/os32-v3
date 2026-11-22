> 出所: ~/os32-tmp/run/ppc-draft-fep7a/DESIGN.final.md (astra 起草、sol 3 回、PM 確定 2026-11-21)。
> 状態: **確定 (2026-11-21)** — 段 7a の出し直し — FEP を x86 と同じ fep.mod の部品で。

# 段7a　PowerMac のシェルから fep.mod を使う

## r2→r3 の変更
- 公開判定を facade と body import で共有し、非公開の起動入口を分離。
- 設定・辞書・予約を両機種でリンクし、静的 SQLite の回収条件を指定。
- x86 の rollback 順・失敗集約・ログを維持。
- 学習先の既定値、読出しエラーの利用時報告、close 失敗を区別。
- STOP の破棄担当と範囲、remote の消費単位の迂回位置を指定。
- 読者と描画先の所有権を区別し、終了時の描画切替・行復帰順を指定。
- 段1・2に mac99 用の試験入口と CI CHECK を設け、通常 shell の観測を段4へ移動。

## 1. 実物と統合の判断

|役目|x86 の基準|PowerPC の現状と採用する経路|
|---|---|---|
|KAPI|sdk/kapi.json の target → kapi/kapi_generated.c の wrap_ime_*|sdk/kapi_target_ppc.py の段 E 後の不在16本を外し、sdk/gen_kapi.py の getkey 上書きも解除。§2 の17本を同じ target から生成する。|
|端末入力|kernel/ime/fep_module.c の public_api は未公開中も input_getkey_kapi を選ぶ。公開後は kernel/ime/ime.c の ime_getkey_kapi が変換と中断を扱う|段 E の素通しを未公開時の経路として引き継ぐ。platform/powermac/input_usb.c の platform_fep_key_local・platform_fep_key_shift で USB の由来と修飾を渡す。|
|表示と権限|kernel/gui.c の gui_ime_set_render が owner とユーザー呼出を検査。platform/pc98 の既定描画は TVRAM|platform/powermac/platform_ime_render.h は既定 NULL、ime_display.c の save/restore は画面を保存しない。FB 端末で未確定文字・候補行・消去を実装する。|
|ロード|kernel/sqlite_engine.c の services_boot → kernel/ime/fep_module.c の fep_module_load → kernel/ime/fep_body.c の fep_module_init|kernel/sqlite_static.c の sqlite_static_boot と platform/powermac/module_pair.c の platform_module_pair を使用。L1・L2 の共通 loader に PPC 用の実物を渡す。|

1. 同じ役目は本線の fep.mod にある。入力状態・辞書・学習・公開用 facade は両 CPU で一組にする。
2. CPU に依るのは呼出スタブ・再配置・命令キャッシュ同期、機種に依るのは打鍵の供給と画面。既存 L2 と固定名 platform ヘッダへ分ける。AC-INPUT-1 の空・修飾と AC-KAPI-1 の slot・不在時動作を §4 の同一ベクトルで比較する。
3. L1 → L2 → FEP → KCG の順。root の展開・pair 値の引渡し・像の構築は **lz4 の段で扱う**。共通 shell_loop と /sys/shell.bin を入口にし、常駐 CLI・旧 kboot 5 像の撤去は最後の段に残す。

## 2. 境目と呼出先

|公開名（ime_ 以下）|x86 wrap の target、PPC も同じ|
|---|---|
|getchar, getkey|ime_getchar_kapi, ime_getkey_kapi|
|trygetchar, trygetkey, feed_key|ime_trygetchar, ime_trygetkey, ime_feed_key|
|toggle, is_active, set_mode, get_mode|各公開名と同名|
|set_render|gui_ime_set_render。ユーザー指定の関数表を直接 module に渡さない。|
|switch_dict, dict_suspend|ime_switch_dict, ime_dict_suspend|
|user_list, user_delete, user_export, user_clear, dict_info|各公開名に _facade を付けた関数|

常駐側のロード内部に渡す依存列（案）。x86 の呼出口と依存の寿命を維持する。

```c
/* x86: fep_module_load(sqlite) の内部 */
return fep_load_with_deps(&sqlite, 1);
/* PPC: sqlite_static_boot 成功・VFS と辞書準備後 */
return fep_load_with_deps(NULL, 0);
```

- 共通内部は既存 request の dependencies/count だけを引数化。両側とも pair、ABI、import 契約、23 個の private API の text 範囲、start・quiesce、公開条件を既存コードに任せる。静的 SQLite に架空の module handle を割り振らない。
- DB は両側とも外側の os32_sqlite_* を import する。PPC は常駐 SQLite の準備成功が前提。辞書読出は /db/fep.db、学習の作成・更新は /etc/fep_user.db。メタ検査・予約・close 失敗時の所有権を services_boot と共有する。
- x86 の services_boot は UNICODE 入口条件、MODULE_REQUIRED_PATHS（SQLite 部品込み）、SQLite の内部起動・自己試験、全体 rollback を所有する。PPC は sqlite_static_boot 成功後にだけ共通の辞書準備へ進む。
- 共通部分は services_settings → settings close と残接続検査 → 辞書不在なら SQLITE_ONLY → meta probe・close → 予約 → boot config 保存 → candidate start → 最終接続数・予約検査。予約は設定優先、未指定時は meta の下限・桁あふれ検査を維持。学習先も設定値を反映し、未指定時だけ /etc/fep_user.db。 学習先の読出しエラーは boot_user_rc に保存し、学習先取得時に返す。既定値での書込みには進まない。services_settings の group 取得失敗または close 失敗は起動側に返して rollback する。
- FEP の常駐 facade に公開済みポインタを置く。初期値は stub_api。start が返した candidate は検査専用の所有物。public_api はこのポインタだけを読む。起動直列区間の状態遷移（擬似コード）は次の通り。 body が import する fep_services_public() も同じ公開済みポインタと stub_api の比較で判定する。start・接続数検査・quiesce は loader 所有の private 入口を直接呼ぶ。失敗時は公開を閉じてから回収する。

```c
if (prepare_and_start(&candidate) || final_boot_check(candidate)) goto failed;
commit_ready_state();               /* x86 はここで services_public も開く */
publish_fep(candidate);             /* 検査を通過した候補を一度だけ公開 */
return;
failed:
close_fep_publication(); invalidate_boot_config();
if (dynamic_sqlite) {
    services_public = 0;
    blocked = fep_module_abort() != 0;
    if (sqlite_module_ready() && os32_sqlite_outer_boot_check(0, 0)) blocked = 1;
    isolated = module_abort(sqlite_handle);
    restore_sqlite_stub(); report_x86_rollback(isolated, blocked);
} else { /* sqlite_static_boot 成功済み */
    blocked = fep_module_abort() != 0; /* static_api で finalize・group close */
    if (os32_sqlite_outer_boot_check(0, 0)) blocked = 1; /* 予約解除と残数検査 */
    finish_fep_cleanup(blocked); /* 失敗時は所有資源を隔離し、SQLite は公開継続 */
}






```

- x86 は元の failed_stage・rc・fault・cleanup を記録し、isolated または blocked なら MINIMAL_QUARANTINED、それ以外は MINIMAL。既存台帳から隔離ページ・予約を計数する。
- x86 の生成条件、import 順、link 順、KAPI 番号・署名を固定。共通化で機械語差が出うる fep_module_load、services_boot、表示口を移す場合の gui_ime_set_render は本線レビュー対象。各段で基準ビルドと比較し、実際に差の出た関数を列挙してから取り込む。

## 3. ファイルごとの段取り

|段|主な変更先と責務|依存・並行|
|---|---|---|
|1　部品生成|tools/gen_sqlite_imports.py で同じ tools/prototypes/fep_imports.def から PPC 用関数スタブを選択。build/targets/ppc-powermac.mk から build/ppc-module.ld と L2 の pack を使い、該当 kernel と対の /boot/fep.mod（ID 3）を生成。build/fep-module.mk の共通本体一覧を基準とし本体を kernel にリンクしない。|L2 後。2 と別担当可。|
|2　入出力|PPC の import 供給表で kbd_getkey_wait・kbd_trygetkey・kbd_trygetchar を既存 input サービスへ型付きで結線。platform/powermac/input_boot.c が入力、同 ime_display.c が行の予約・保存復帰、同 platform_ime_render.h が FB 描画実装を選ぶ。caller copy・STOP・owner・診断の各 import も実 provider に解決し、未解決はリンクで失敗させる。|1 と並行、生成物で突合。|
|3　起動と辞書|kernel/ime/fep_module.c が依存列と公開判定、同 .h が private 入口を所有。kernel/sqlite_engine.c の辞書準備・公開トランザクションをモジュール SQLite 起動から分離し、PPC の root 準備後・shell 前にも利用。設定・辞書・予約・boot config の共通処理を新設 kernel/fep_boot.c に移し、build/sources/common.mk と PPC リンク一覧に登録。sqlite_engine.c の動的起動・rollback は x86 所有。失敗分岐は §2 に従う。|1・2 後。使い捨て ext2 HDD で確認。|
|4　KAPI 結線|sdk/kapi_target_ppc.py で §2 の群を provider へ移し、ime.h の宣言を有効化して正規生成。描画権限門と既存の exec 終了 hook を下記の通り選択。|3 後に実行確認。対応表の試験作成は並行可。|

### 統合順と引継ぎ

|順|取り込み基点・枝系列|担当 → 次段に渡すもの|
|---|---|---|
|先行|段 E、wip/ppc-root-lz4-3 の L1/L2、ATA 後端 P1 が入った main の SHA を固定し段7aを分岐|段 E は生成器・absent hook・shell、lz4 は ppc_kernel.c の展開・pair と ppc-powermac.mk の像規則、P1 は HDD block I/O を所有。7a は統合後の実装に追記。|
|1|x86 整合票 TASK_PPC_ALIGN_X86 の ppc-align 系列。B10 → B07|B10 が起動ログの出力先を確定し、7a はそこへ状態を記録。B07 の像規則確定後に段1を接続。|
|2|同票 B06 → B04 → 段2|B06 が端末/input の provider、B04 が remote 入力の由来・消費を確定。その版を段2の基点とする。S-B 系 wt/ppc-sbb* は別票。生成器を同時変更する際は先行変更を main に取り込んでから段4を再生成。|
|3–4|P1 → HDD インストーラ → HDD root 成功信号、段1・2 → 段3 → 段4|起動担当が mount 結果を渡し、7a が FEP 呼出を追加。段4担当が absent 実装の選択と KAPI 生成差分を照合。|

### 実行入口と依存

1. 段1は FEP 定義に既存の ppc-powermac スタブ生成を適用。例は型付き `int (*wait)(int) = input_getkey_wait`。各常駐 provider を宣言と照合し、slot は .data 先頭に定義順・4 byte のゼロ列。kprintf も `lis r12,slot@ha / lwz r12,slot@l(r12) / mtctr r12 / bctr` とし LR・引数・CR を保存する。L2 の PPC RELA（ADDR16_HA/LO、内部ポインタ ADDR32）と既存 cache 同期を使用。FEP ELF → mkmod の暫定 pair 0 → 暫定 root ISO → KB_PAIR_TOOL の kernel link・pair 確定・再 bind → 最終 ELF と root.iso。インストーラは検証済み最終 ISO の SYS_FEP_MOD を HDD の /boot/fep.mod にコピーし、同じ ELF と組にする。
2. 段2は mac99 試験 loader で実部品の private API を呼ぶ。USB（USB イベント注入を含む）・LAN の由来と修飾は peek 時でなく take 成功時にキーと組で渡す。body の待機・try 経路で、toggle 判定と ime_cui_process の手前で remote を分岐し、ON 中でも元の全幅キーを返す。redirect は既存の素入力、空・中断は -1。独立の注入 provider は NULL。
   - STOP は中断を先に通知し、exec_finish.inc の input_discard_stop から PPC InputOps.discard_stop を呼ぶ。中断時点で受信済みの USB 未読キー、LAN 端末受信列・lookahead・decoder の部分列と由来を各 provider で破棄する。境界以降に到着したキーは次の読者へ残す。
   - PPC の ime_display.c は端末行を最初に上書きする際にセル・属性・カーソル位置と可視状態を保存し、描画世代・所有者と組にする。常駐の gui_ime_set_render は owner 1 かつ非ユーザー呼出だけ許可し拒否を計数。登録 owner と入力読者は別に追跡する。
   - 通常・STOP・fault とも exec_finish.inc → exec_ime_owner_exit で一度回収。読者一致なら再描画を抑えて変換を取消し、描画 owner 一致なら終了者の callback を外して既定描画へ戻し、その owner の保存行を一度復帰して予約を解く。他 owner の状態は維持し、次の入力まで候補行を描かない。PPC の GUI hook は absent、x86 は既存 GUI を維持。
3. 段3は platform_boot_run から直接試す。HDD ext2 を / に mount した結果を専用フラグへ保存（RAM root の root_ready と区別）。Unicode・L1 初期化 → 静的 SQLite 成功 → L2 の有効な kernel pair → HDD root 条件下で FEP → KCG → 通常 shell。前提失敗なら依存部品を飛ばし、利用可能な root の shell へ。FEP 失敗でも独立の KCG は自身の前提で判定する。
4. 段4は通常の /sys/shell.bin の sh_getkey から試す。未公開では stub → input_getkey_kapi、公開後は body → 変換済み出力。同じ生成済み slot を両状態で呼ぶ。 sh_getkey → wrap → facade → ime_getkey_wait → ime_cui_process で USB Shift+Space 一回につき切替一回を観測。

## 4. 受入と完了

|試験|観測する結果・期待の出所|一意の変異例（実行時に赤）|
|---|---|---|
|host・生成と L2|同じ定義から得た import 名・型・順、両 ELF の target と pair を検査。PPC 命令の実行は下記の段1 CHECK が担当。別 kernel の部品では start 前に拒否。基準は build/fep-module.mk、既存 test_fep_module.py と test_module_rela_ppc.py。|pair の違う像を受理させる。|
|host・境界|全 slot の番号・target・引数を sdk/kapi.json から取得し、公開前後に呼ぶ。不在時 feed_key は入力を保存、try の空は -1、辞書照会は NOSYS。失敗時は candidate 未公開、suspend した owner の終了で再開。基準は stub_api と test_fep_boundary.py。|未公開で candidate を選ぶ、または終了時の再開を落とす。|
|両 CPU の QEMU|同じ guest 呼出列を x86 の対応 harness と mac99 で実行。非待機の空、キー上位ビット、リダイレクト、中断、モード切替、ローマ字→候補→確定を比較。GUI の不正 owner は拒否、終了後に行が復元される。期待値は x86 facade と test_fep_standard.py の辞書 fixture。PC-98 固有 TVRAM・鍵盤は NP21/W でも確認。|trygetkey を byte に切り詰める。|
|HDD 永続化|同一辞書 fixture を PC-98 の起動可能な HDD 像と mac99 の HDD ext2 root に配備。各機種で変換・学習後、その機種の同じ書込み済み像で再起動。list/export、delete/clear、辞書切替・info、suspend/reopen を guest で観測。壊れた辞書・pair 不一致では素入力で shell が動く。基準は x86 の store 試験と /db・/etc の実ファイル。|学習先を一時領域へ替え、再起動後の照会を失敗させる。|

- 各段で対象ビルド、変更した試験とその変異、check lint。最終段で両 CPU の出力・再起動結果・x86 成果物差分を記録する。QEMU で実行できない機種固有経路は未検証として区別する。
- 開発中の合格は使い捨て HDD 像まで。利用者への公開条件は ATA → HDD へのインストール → その HDD の ext2 root から起動 → 通常シェルで変換と再起動後の学習を確認、の順とする。

- 段1・2は make ppc-ci-image に加え、各専用 CHECK を tools/checks に登録し CI で mac99 試験 kernel を起動。段1の check-ppc-fep-import は L2 loader・定義順の型付き試験 import 表・引数記録用 kprintf をリンクし、最終 ISO の実部品で init・実スタブ経由の可変引数と戻り先を検証。HDD コピーとも byte 比較する。
- 段2の check-ppc-fep-io は試験 loader、実入力・FB provider、試験用 USB/LAN キューと辞書 fixture をリンク。SQLite だけ公開した時点で facade は stub、private 経由の body toggle/getkey も未公開動作であることを観測し、試験公開後は同じ入口で切替・変換を確認。ON 中の LAN、STOP 前後のキー、終了後のセル・カーソル・callback 解除を観測する。
- 段3は同ビルドと test_sqlite_module_proto.py の rollback 系を拡張し、最終検査だけ失敗させ、PPC の SQL 成功・FEP stub・残予約／隔離台帳を確認。段4は make all と make ppc-ci-image、生成・guest 試験で PPC target を段 E の input_getkey_kapi に戻す変異が候補確定の不一致で赤になることを確認。
- x86 比較基準は設計基点 58770721e4053348b69bccb96a273aaa8215c657。実装時は先行段統合 SHA でも同条件で対照ビルド。生成 wrap・import スタブ・fep.mod、kernel の map と関数別逆アセンブルを比較。配置移動を正規化し、§2 の列挙関数以外の差は個別レビュー。SQLite/FEP の既存 rollback・close fault 試験の stage、公開値、隔離ページ・予約と復旧後の DB 操作を比較する。
- PM の完了記録に SHA、kernel pair、辞書・像の識別値、QEMU 両 CPU の呼出結果、NP21/W の入力・画面と再起動後の export を残す。G4 実機は HDD root の mount 元、公開ログ、USB 変換画面、電源再投入後の学習結果を採取して公開判定する。

## 5. 未決

ユーザー判断は0件。

## 確定 (2026-11-21、sol 第 3 回、PM が反映)
以下は本文に優先する。sol 第 3 回 (/home/hight/os32-tmp/run/ppc-draft-fep7a/REVIEW.r3.out.md) の「上記を反映すれば可」の残りを、そのまま実装の取り決めとする:
    - **§2・§4** PPC は quiesce・close が済み、接続と group の解放を確認した場合にだけ `boot_check(0, 0)` で予約を解除する。失敗時は予約を保持して隔離し、静的 SQL を継続する。x86 の呼出順は現行どおり。PPC の試験に quiesce/close 失敗時の予約保持も加える。
    - **§2・§3** B-P を段3の接続として明記し、HDD root 後に `fep_boot_user_path_set()`・`fep_boot_user_rc_set()` で値と取得結果を load 前に渡す。失敗時は snapshot とともに無効化する。
    - **§3・§4** 製品選択を `build/sources/hooks.mk`・`build/ppc-fep-module.mk`・静的 SQL 後の起動接続に割り当てる。私有 `ops_version = 5` に常駐側と部品を揃え、B-P の受入は静的 SQL 構成の FEP 境界試験で、load 前の設定引渡しと失敗時の無効化まで観測する。
- 本線の INPUT_BIND_M7_3_DESIGN (wip/kfill-designs d53261df1) の PowerPC の担当 B-P の段は、この設計の段 3 の接続として入る (MAINLINE_NOTE.md)。
