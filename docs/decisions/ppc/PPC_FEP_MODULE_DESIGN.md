> 出所: ~/os32-tmp/run/docs-1122c/src/PPC_FEP_MODULE_DESIGN.md。
> 状態: **確定 (2026-11-22)** — 段 7a は共通の起動・公開処理を先に統合し、PowerMac の常駐シェルから fep.mod を使う。末尾の確定の節が本文に優先する。

# PPC_FEP_MODULE_DESIGN r4　PowerMac の常駐シェルで fep.mod を使う

## 11-21 の本線のレビューとの突き合わせ

- P1（7a）　§3 段3で load・start の各直前に静的 SQL の ready を照合し、最終検査と RUNNING を公開条件にした。失敗時の pending 失効と handle 回収の順も境界試験で確認する。
- P2（7a）　origin/main 56d7be02d の kernel/ime/fep_module.h で FEP_MODULE_OPS_VERSION は 6u。M7[3] の v5 移行は統合済み。§3 の段表と §4 で共通化を先に承認し、その直後を B-P 単独の x86 差ゼロ比較の基点とした。

## 確定版 (11-21) → r4 の変更

1. 段 E が入った。製品の入口は shell_loop_run → /sys/shell.bin（owner 1、CPL0）で、段4は既存のシェルを使う。
2. lz4 root が入った。pair・展開・CRC・rd0 の登録と mount を引き継ぎ、永続 HDD root の成立は別に判定する。
3. ATA P1 の block device を使う。HDD の有無を実際の登録結果から取る。
4. ATA P2 の生 ATA 7 口は IDE 像だけであり、hdd_geom_info はそこでも absent。hdprep の前提は別途満たす。
5. MACHCFG は root 初期化直後に /etc/machine.cfg を読んで確定する。FEP 起動時の再読込を設けない。
6. KAPI v89 の ABI 世代 2、384 口、2 頁 trampoline を基準にする。
7. KAPI v90 の slot 298–336 を含む現行表から生成し、IME は既存 slot の target だけを接続する。
8. 入力 M5〜S4・7c の in2 と現在の FEP 私有 ops v6 を引き継ぐ。B-P の v5 指定は当時の基点。
9. C04 共通の debug_probe_snapshot と依存検査が main に入った。
10. PPC の L2・kdebug 窓口は wip/ppc-c04-diag を含む wt/int-ppc-1122b の統合結果を待って使う。
11. B10 の起動診断は ESCC・LAN のログへ出し、通常像に受入用の成功表示を足さない。
12. B07 段1の旗なし IMAGE=powermac は build/out/ppc-powermac、シェルはその user/。旧名・旧旗は既存規則に従う。
13. USB in2 段 A・B の HID 差分・反復と共通 cooked 入力を使う。USB 専用 FIFO・ポインタ配送を再設置しない。

調査基点は origin/main 69d9cef40。C04 の PPC 窓口 f84f946a0、B10・B07 68687b612、USB d5d94a630 は作業枝の状態。各段はその依存が本線に統合された SHA で比較する。以下は11月21日確定版と MAINLINE_NOTE の B-P をこの基点に合わせた実装契約である。

## 共通コードの担当境界

### FEP

main の build/fep-module.mk の本体一覧、tools/prototypes/fep_imports.def の順序、kernel/ime/fep_body.c の fep_module_init、kernel/ime/ime.c の変換・ime_apply_absolute、辞書・学習処理を共用する。/boot/fep.mod は同じソース・同じ私有契約から CPU 別に作り、各 kernel と pair を合わせる。PPC 版も L2 からロードする。常駐側では kernel/ime/fep_module.c の public_api、stub_api、fep_close_input の flush・policy 無効化、focus 登録を継承する。私有表の検査は現行ヘッダ全体に従う。

直すのは fep_module_load の依存列と公開条件、kernel/sqlite_engine.c の services_boot が所有する設定・辞書準備の切出し。共通処理は kernel/fep_boot.c に一組置く。x86 は実 SQLite handle 1 個、PPC は sqlite_static_boot 成功後に依存 0 個で同じ loader request を作る。candidate は最終検査まで private。public_api と body の fep_services_public は同じ公開状態を読み、失敗時は先に閉じる。x86 は従来の SQLite 起動・自己試験、FEP abort、外側 boot_check、SQLite abort、stub 復帰の順、失敗 stage と隔離集計を維持する。切出した関数・呼出元の kernel 機械語と配置は変わり得る。x86 用 import stub・fep.mod・KAPI の生成差は予定しない。

### ext2

fs/ext2/ext2_super.c の ext2_mount の superblock 以降の検証、LE 読取り、ext2_sync、fs/ext2/ext2_fmt.c の ext2_format_at → ext2_format_range を共用する。変更境界は ext2_find_partition_ex が LBA 1・BIOS CHS・pc98pt_find_os32 を仮定する箇所。HDD root 担当が機種側で検証した開始 LBA と長さを共通 mount に渡す。読取り失敗・媒体外・短い区画は書込み前に拒否。x86 は従来の区画探索、旧配置の診断、format 範囲を維持し、抽出した mount 呼出とリンク配置に差が出得る。段7a 自体で ext2 を分岐させない。

### hdprep

userland/shell/cmd_hdprep.c の cmd_hdprep の事前確認・承認・書込み後の hp_verify_mount、hdprep_plan.c の hdprep_check_mounts と hdprep_parse_mb を引き継ぐ。hp_geom_from_kapi、hdprep_check_disk、hdprep_plan の PC-98 幾何・区画表と書込み内容は HDD 準備担当が機種別に選ぶ。P2 の存在だけで PC-98 の計画を G4 に適用しない。対象・容量・既存領域を検証できない場合は書込みゼロ。x86 は同じ入力で同じ拒否、区画表、書込み順となることを確認する。共通手順を抽出した場合の x86 差は shell.bin の当該関数・配置。

### install

userland/system/install.c の confirm_install、copy_file、copy_directory と VFS 経由の転送を共用する。precheck_media、fd_map_to_hdd、inst_hdd_write_boot の呼出は FD・PC-98 IPL・loader_h を前提にする。インストーラ担当が媒体検査・起動領域の配置を機種別に選択し、PPC では最終 pair の kernel と /boot/fep.mod、/db の辞書、/etc の設定を同じ HDD に配備する。現行 copy_dirs だけでは /db は配備されないので辞書の実在をコピー後に照合する。x86 の媒体一覧・IPL・配置・拒否時のディスク内容は維持し、共通化の差は install の呼出元・配置に限る。hdprep・install の公開は先行 HDD 作業であり、FEP の試験用に製品へ仮経路を足さない。

## §3 実装と引継ぎ

各段は実装者1名。本線の段は本線担当が実装し、各 PowerPC 段は対応する本線の段の統合・承認後に着手する。

| 本線の段 | 後続の PowerPC 段 |
| --- | --- |
| M7[3] の B1〜B8 と ime.c（統合済み）。現行の私有 ops v6 を継承する。 | 段1〜4 の共通基点 |
| 段1の共有生成器と import 定義の生成依存を整える。 | 段1の PPC pack・pair |
| 段3の FEP／SQLite 起動・公開・rollback を共通化し、x86 の差分と既存 rollback 試験を承認する。 | 段3の B-P 製品結線 |
| 段4で共有 KAPI 生成器・描画権限口に変更が及ぶ場合は本線で先に統合する。 | 描画権限口を使う段2、生成器を使う段4 |

1. **生成**　build/ppc-fep-module.mk が共通本体一覧と import 定義を使い、PPC ELF を L2 用に pack する（ID 3）。既存 root 像の pair 確定・再 bind に載せる。check-ppc-fep-import は最終像の部品を mac99 の試験 loader で呼び、型・順序、可変引数、戻り先、pair 不一致の start 前拒否、HDD コピーの byte 一致を観測する。C04 の台帳・cache 同期・再配置を再利用し、診断像は既存 kdebug_call を通す。
2. **入力と表示**　USB 枝の platform/powermac/input_usb.c の pm_input_usb_take が legacy_cooked_take_mods で消費したキーと修飾を結ぶ。LAN の消費時は pm_input_usb_note_remote。peek は由来を変えない。local の切替は既存 policy と FEP action の経路で一回だけ適用し、remote は変換中も全幅の元キーを返す。redirect・空・中断は現行契約。in2 の独占取得は開かない。STOP 通知後、既存終了 hook から境界以前の未読入力・LAN の部分列を破棄し、後着分を残す。ime_display.c は初描画時にセル・属性・カーソル位置と可視状態を owner・世代付きで保存。gui_ime_set_render は owner 1 の非ユーザー呼出だけ許可。exec_ime_owner_exit は通常・STOP・fault 共通で読者の変換を取消し、終了 owner の callback を外し、その保存行を一度復帰する。check-ppc-fep-io は実部品と USB/LAN 入力で未公開・公開、remote、終了時の復帰を観測する。
3. **静的 SQL 後の起動（B-P）**　製品選択は build/sources/hooks.mk と build/ppc-fep-module.mk、呼出は静的 SQL 後・shell 前に置く。pm_root_boot の rd0 登録や RAM の /db・/dbu は HDD 成功信号に使わず、HDD ext2 が実際に / に mount された結果を受ける。Unicode・L1、静的 SQL、L2 pair と HDD root が成立したら settings を読み close と残接続を検査。辞書不在は SQL のみ。meta の版・下限・桁あふれを検査して予約し、snapshot と fep_boot_user_path_set・fep_boot_user_rc_set で学習先と取得結果を load 前に渡す。未指定だけ /etc/fep_user.db とし読出しエラーは保存する。load と start の各直前に静的 SQL の ready（SERVICES_SQLITE_ONLY と services_public）を再照合し、失敗した時点で起動を打ち切る。候補 start 後に os32_sqlite_outer_boot_check(ime_boot_connections(), reserve) が成功して初めて fep_services_public を開き、facade はこれと RUNNING の両方を確認する。失敗時は常駐側で公開を閉じ、inactive を通知し、全フィールド 0 の snapshot で take して pending を失効させ、取得済み handle を一度だけ回収する。設定・snapshot を無効化し、quiesce・close 成功かつ接続と group 解放確認後だけ boot_check(0, 0) で予約解除。回収失敗なら予約保持・隔離、静的 SQL は継続する。独立の KCG は自身の前提で判定する。静的 SQL の境界試験で各直前の ready 喪失を個別に注入し、対応する load／start が呼ばれないこと、公開条件が片方だけなら stub を返すこと、上記の失効順と回収回数を観測する。併せて load 時の値、最終検査失敗、close/quiesce 失敗時の stub・残予約・SQL 操作を確認する。
4. **公開と製品受入**　sdk/kapi_target_ppc.py と sdk/gen_kapi.py の PPC target 選択を修正し正規生成する。ime_getchar/getkey、try 系、feed_key、toggle、active、mode、set_render、辞書切替・suspend、user_list/delete/export/clear、dict_info は sdk/kapi.json の x86 target と同じものを使う。userland/shell/ui.c の常駐 sh_getkey → ime_getkey をそのまま通し、同じ slot が未公開では input_getkey_kapi、公開後は変換済み入力を返すことを確認する。端末の子 sh.bin の入力経路は維持する。

## §4 検証と公開

段1・2は make ppc-ci-image と専用 CHECK、段3は同ビルドと静的 SQL の境界・既存 x86 rollback 試験、段4は make all と make ppc-ci-image。各段で変更試験の変異と check lint。pair 誤受理、remote の切詰め、失敗時の予約解除、素入力への target 差戻しを各段の観測で赤にする。公開 KAPI の番号・署名・版を維持する。B-P 単独では承認済み共通化の直後と B-P 統合直後を同一条件でビルドし、x86 成果物のバイト差ゼロを受入条件とする。各統合基点で x86 の生成表・部品・map・関数別逆アセンブルを比較し、上記で予想した差を実測で特定する。

使い捨て HDD へ install し、その HDD を / として起動。PC-98 と mac99 の通常シェルで Shift+Space、候補、確定、学習、再起動後の list/export、delete/clear、切替・info・suspend/reopen を同一辞書で比較する。壊れた辞書・pair 不一致は素入力で操作可能。PM は SHA、pair、辞書・媒体識別値、mount 元を記録し、PC-98 固有の入力・画面は NP21/W、G4 公開判定は実機の USB 変換と電源再投入後の学習で確認する。

## 未決

ユーザー判断は0件。先行 HDD 実装と作業枝の統合・CI は実装開始条件として追跡する。

## 確定 (2026-11-22、本線の sol「本線の目で可」、r4 を確定)
- 前回の本線の P1 (load・start の直前の ready、RUNNING の公開、閉じてから inactive → ゼロ snapshot → 一度だけ回収) と P2 (私有 ops、共通化の x86 の差を先に承認してから B-P 単独の x86 一致を基点) は解消。
- 順: 本線の段 (段 1 の共有生成器、段 3 の FEP／SQLite の起動・公開・巻き戻し、必要なら段 4 の共有 KAPI 生成器・描画権限口) を先に main に入れ、PowerPC の段をその後に接続する。
