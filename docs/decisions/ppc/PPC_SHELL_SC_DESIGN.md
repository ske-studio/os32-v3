# PPC_SHELL_SC_DESIGN — PowerPC の到達点 S-C (既存の OS32 のシェルを USER の sh.bin として起動し、LAN の端末で使う、段 W7) の詳細設計

> 状態: **確定 (2026-11-12)・実装済み (2026-11-12、QEMU の受入に到達)** — 製品の経路 (第 3 の像 KBOOT_SH=1) で RAM root の mount の後に kernel が /bin/sh.bin を USER として直接起動、fd1 を端末へ、LAN の端末の InputOps、USER の入れ子の先行確認、共通の sh のソースは選択の口だけ (x86 不変)。段の状態は票 TASK_PPC_SHELL が持つ。

> 出所: ~/os32-tmp/run/ppc-shellc/DESIGN.final.md (gpt-6-astra 起草 r1→r2、Opus 5.5 の第 1 回 Request changes・第 2 回 条件付き確定、PM が未決・#1・#5・#10・#12 と N2〜N6 を反映)。前提の設計は [PPC_SHELL_DESIGN](PPC_SHELL_DESIGN.md) (S-A と段の計画) と [PPC_SHELL_EXEC_DESIGN](PPC_SHELL_EXEC_DESIGN.md) (S-B)。

- 基点：`origin/main 9ba00a59`、実コードは `/home/hight/wt/os32-v3/ppc-shellc`。
- r1 の設計を引き継ぎ、レビューに関係する接続・分割・検証だけを修正する。
- 本文は設計であり、ビルド・実行による確認結果ではない。
- シングルタスク、整数のみ、FP・AltiVec なし、module は静的リンク。
- 共有ファイルに `#ifdef __powerpc__` を追加しない。x86 の既定展開と生成物の不変を基本とする。
- GPL・QEMU のソース・`docs/archive/` は参照しない。新しいハードウェア制御は追加しない。
- 製品経路：RAM root mount → kernel が `/bin/sh.bin` を USER として直接起動。
- W7 では製品経路を第3像に実装し、既定の診断像への移行は範囲外とする。

**1. 実物との対応**

|番号|実物の制約|r2 の扱い|
|---|---|---|
|D1|root・exec 閉包は `KBOOT_SHELL` 条件内|診断像を保ち、CLI または第3像のときだけ閉包を組む|
|D2|exec の深さ0は常駐・特権シェル扱い|kernel 親の深さ1を維持し、sh を USER として起動|
|D3|`SHELL_AS_APP` は launch request・yield・poll を使う|拒否処理を残し、起動の核だけ同期 exec に選択|
|D4|InputOps の初期値は空 provider|LAN 端末用の完全な InputOps を起動前に登録|
|D5|端末 byte と sh のキー形式が異なる|platform で変換。ESC の処理箇所は `ui.c:770`|
|D6|`SHELL_AS_APP` だけでは PC-98 コマンドが残る|登録列と base 表を profile で選択|
|D7|USER SDK だけでは stdio・可変引数出力が足りない|sh 専用 USER runtime と出力 adapter を組む|
|D8|旧 SYS_LS callback は PPC USER で使えない|既存 `userland/lib/rt/ls.c` の window 列挙を流用|

- `exec_launch.inc` の常駐専用 aux・shell_service 公開は変更しない。
- sh は通常 USER の app ID を使い、`APP_ID_SHELL` を付け替えない。aux は0を保つ。
- font・WM を使わないため、常駐 shell_service の特権口は不要。
- 既存 `SHELL_AS_APP` の入力、`sh> `、文字列再描画、history、exit、USER pipe buffer を使う。
- 不在 slot の棚卸しはレビューの集計方式で **38種類・103箇所**とする。
- 条件付きコンパイル前の参考値であり、実行時に必要な slot 数ではない。
- 最終的な到達範囲は選択後の前処理結果・link map・未解決 symbol で確定する。

**2. S-C の範囲と受入**

- parser、展開、行編集、history、script、内蔵コマンド間の pipe・redirect を流用する。
- コマンド群は base の選択部分、file、dir、env、script。
- base は `help ? tick uptime time exit`、`clear/cls` は USER から端末制御列を出す。
- `ver/uname/date/beep/np2`、mnt、sys、pci、hdprep、filer、rshell は登録しない。
- PATH=`/bin`、HOME=`/tmp`、SHELL=`/bin/sh.bin`、起動時 cwd=`/`。
- `/etc/profile` は存在すれば実行し、history は `/tmp/.sh_history` に保存する。
- 外部コマンドを含む pipe・redirect は既存の拒否を維持する。
- ASCII、Enter、BS/DEL、Tab、取消、上下履歴を受入対象とする。UTF-8 は byte 転送を保つ。
- 左右・Home・Delete の変換、IME、画面寸法、ローカル入力装置は追加しない。

|操作|合格となる観測|
|---|---|
|第3像を起動し、遅れて端末接続|接続後に `OS32 External Shell Started` と `sh> `|
|`help`、`pwd`、`ls /bin`|選択した一覧、`/`、`sh.bin` と `hello.bin` が端末に出る|
|`echo sc-ok > /tmp/sc`、`cat /tmp/sc`|redirect 中は漏れず、cat で端末に `sc-ok`|
|`cat /nofile > /tmp/e`|エラーは端末へ出て、`/tmp/e` にエラー文字列が入らない|
|`/bin/hello.bin ok`、`echo $?`|`hello ok`、`7`、再びプロンプト|
|同じ hello を再実行|親の変数・cwd・history を保ち、同じ結果|
|`echo again` の後に上矢印・Enter|前の行を再実行し `again`|
|script を `/tmp` に作り source|`script-ok` とプロンプト|
|`echo pipe-ok \| cat`|`pipe-ok`。外部 pipe・redirect は既存の拒否|
|端末を再接続して Enter|入力中の行を取消し、新しいプロンプト|
|起動媒体を利用不能にして反復|RAM root と `/tmp` だけで成功|
|`exit 3`|終了コード3を記録し、sh を再起動してプロンプト|
|sh のロード失敗|失敗を表示し、再試行ループに入らず LAN poll を継続|

- kernel の起動ログはログ経路で観測し、端末に同じ表示が出ることを要求しない。
- CLI 不経由は map と実行観測で判定する。
- 第3像に `resident_cli_run`・`pm_cli_boot` がなく、最初の USER は `/bin/sh.bin`、PR=1。
- CLI 像は従来の `os32> ` と S-B の hello → `exit=7` を維持する。

**3. 三つの像と起動順**

|像|make の選択|既定の出力先|挙動|
|---|---|---|---|
|既存 LAN 診断像|追加指定なし|`build/out/ppc-kboot/`|現行の診断とマイルストーン|
|既存 CLI 像|`KBOOT_SHELL=1`|`build/out/ppc-kboot/shell/`|常駐 CLI と S-B|
|第3像|`KBOOT_SH=1`|`build/out/ppc-kboot/sh/`|kernel から USER sh を直接起動|

- 新しい利用者向け make 変数は `KBOOT_SH ?= 0` の1つとする。
- `KBOOT_SH=1` と `KBOOT_SHELL=1` の同時指定は make エラーにする。
- 第3像と `NEGATIVE!=0` の組合せも受け付けず、既存 NEGATIVE 像の意味を保つ。
- 既存の出力先は既に分かれている。r2 では第3像の枝だけを追加する。
- 第3像の object、生成 KAPI、root.iso、blob、ELF、map を専用ディレクトリへ置く。
- `KB_OUT` を上書きする利用者も、像ごとに異なるディレクトリを指定する。
- root・exec・USER KAPI の閉包は `KBOOT_SHELL=1` または `KBOOT_SH=1` で選択する。
- `kboot_unavailable.c` の除外もその2像に限定し、診断像では現在どおり残す。
- CLI のソースは `KBOOT_SHELL=1`、sh boot と InputOps は `KBOOT_SH=1` だけに入れる。
- 第3像の root に sh を加える。既存 CLI root の hello・fault と内容は変えない。
- `kboot_accept.py` の KERNEL_MUTATIONS・マイルストーンの期待値は変更しない。
- `kboot_run.py`、`kboot_audit.py`、`kboot_mutations.py` の既定像の指定も変更しない。
- `test_lan_p*.py`、NEGATIVE 像、実機 LAN 診断の消費者を移さない。
- 第3像の監査・受入は明示した第3像を対象とし、既存試験の期待を緩めない。

```text
診断像：現行の初期化 → 現行の診断／LAN poll
CLI像 ：RAM root → pm_exec_boot → pm_cli_boot
第3像 ：RAM root → pm_input_boot → pm_exec_boot
       → LAN OPEN または USB の最初のキーを待つ → pm_shell_boot
```

- `pm_exec_boot()` は kernel 起動文脈を親とし、深さ1を設定する。
- sh は深さ2、sh の外部コマンドは深さ3の既存 exec 経路を通る。
- `pm_shell_boot()` は `exec_run` 直後に `exec_last_result` を読み、種別とコードを保持する。
- 正常終了ならコードを記録して再起動する。非0の exit code も正常終了に含める。
- ロード失敗は表示後に LAN poll で待つ。CLI へ切り替えない。
- fault 等の正常終了以外も、結果を表示して待機する。
- 接続確認には既存 `pm_term_state()` の成立を使い、LAN protocol に新しい状態を足さない。
- TERM_OPEN が出力 FIFO を捨てるため、LAN のみでは初回 OPEN を待つ。USB は変換済み FIFO の非空で起動し、最初のキーを残す。
- 初回 OPEN の取消通知は起動前の入力初期化で消費し、その後の入力は捨てない。
- 再接続では既存の行取消を使う。ESC 自体にプロンプト再発行を追加せず、Enter で復帰する。

**4. USER 入れ子の先行確認と launch**

- **N01 を U/S 系の前に実施し、成功するまで sh の結線へ進まない。**
- `userland/ppc/nest.c` は USER から `exec_run("/bin/hello.bin")` を呼ぶ。
- 直後に結果種別・コードを読み、stream に表示し、同じ子をもう一度起動する。
- 親の局所値・USER の復帰位置が保たれ、2回とも子の結果が得られることを確認する。
- `arch/ppc/exec_context.c:14–31` と `exec_launch.inc:324–336` の既存退避・復元を使う。
- arch の仕組みを新設する段ではなく、USER→USER の実動作を確かめる段とする。
- 試験専用 root に nest を加え、既存 CLI 像の配布用 root は変更しない。
- QEMU 受入の1行は「CLI → nest → hello → nest → hello → nest → CLI、2回とも EXITED/7」。
- SC frame、kernel SP、AS、owner、FD、結果記録の復元を実動作と既存試験で確認する。
- 子 AS へ切り替えた後に親 USER のコマンド文字列を読み続けない既存契約を保つ。
- 不成立なら該当 arch/provider の修正を先に受け入れ、共通 exec の無条件変更で通さない。

- sh の同期実体を `platform/powermac/sh_launch.inc` へ100行コピーする案は取り下げる。
- CPU 名を含まない `SH_LAUNCH_SYNC` 選択を設け、既定は現行の要求表経路とする。
- `sh_launch.inc` の pipeline depth・redirect active と拒否表示を両経路で共有する。
- 拒否を通過した後の核だけ、launch_req/poll または exec_run に分ける。
- 同期経路の結果取得・分類は既存 `sh_exec.inc:89–113` の処理を共有する。
- `exec_last_result` は exec_run 直後に読み、途中に別の KAPI 呼出しを挟まない。
- 常駐版・要求表版・同期 USER 版で関数の二重定義が起きない選択にする。
- WM、scheduler、要求表の新設は行わない。

**5. 入出力と USER runtime**

|入力の範囲|PowerMac provider の契約|
|---|---|
|getkey/getchar・KAPI版・wait|terminal poll を進め、LAN と USB を交互に試し、キー形式／文字形式に合わせて返す|
|try・peek・has_key|先読み1要素を共有し、二重消費しない|
|Enter・BS・取消|LF→CR、DEL→BS、Ctrl-C・行喪失→ESC、CRLF は1回|
|上下履歴|`ESC [ A/B` → `0x3A00/0x3D00`|
|単独 ESC|decoder の期限で取消として確定|
|raw・押下・修飾・GUI・診断|既存契約の空値・無操作・未対応値|

- USB の文字化・FIFO・起動待ち・受入の正本は [PPC_USB_KBD_DESIGN](PPC_USB_KBD_DESIGN.md)。USB の修飾は文字化の内部だけに使い、公開の押下・修飾・shift_state は0、drop 照会は USB FIFO の値を返す。
- LAN の行喪失・OPEN の取消は USB で編集中の行にも作用する。USB 先行起動後の LAN OPEN で過去の案内の再送は保証せず、Enter で次のプロンプトを得る。
- InputOps は19本すべてを定義する。1024B ring と LAN protocol は変更しない。
- scan 値の変換は platform 内に置き、sh に PowerPC 分岐を加えない。

|出力元|第3像での経路|
|---|---|
|USER printf・通常の fd1 出力|sys_write(1) → redirect／pipe → 既定時だけ console_write → console_stream|
|USER kprintf adapter|USER 整数書式化 → shell_putchar → console_stream|
|USER shell_print_utf8 adapter|shell_putchar → console_stream。色は無視|
|hello・clear/cls|既存 shell_putchar → console_stream|
|kernel 自身の kprintf|既存の書式化 → ログ専用 provider → pm_log_write|

- **P03 で fd1 の既定出力を接続する。console_write を stream とログへ二重投入しない。**
- `kboot_console.c:18–22` の変更だけでは kernel kprintf も stream へ移るため、分離が必要。
- 第3像だけ `lib/kprintf.c` の TU 内の console_write 参照をログ専用関数へ結び替える。
- その結び替えは make の当該 object 専用定義とし、共通 C ソースは変更しない。
- kboot console の shell_print 系はログを維持し、kprintf の文字列・数値部分もログにそろえる。
- 通常の console_write は第3像だけ console_stream_write を呼び、他の2像は現行どおり。
- P03 の試験は書式付き kernel kprintf と fd1 を同時に出し、両 sink の内容を照合する。
- console_write を pm_log_write へ戻す1行変異で、端末の fd1 マーカー欠落を RED にする。
- USER kprintf を fd1 に流す変異は `cat /nofile > /tmp/e` の内容照合で RED にする。

- runtime は `userland/ppc/sh/` に置き、汎用 SDK の libc として拡張しない。
- `sh_runtime_init(api)` は USER 内の KernelAPI 写しを作り、出力 slot を差し替える。
- `g_api` と CRT 大域 `kapi` の両方をその写しへ向け、RO 公開表は変更しない。
- `sdk/crt/ppc/start.c` の終了処理は元の固定引数 sys_exit を使う現行契約を保つ。
- `sdk/crt/help.c` をそのまま組み、内部の `kapi->kprintf` も adapter に到達させる。
- `userland/lib/rt/ls.c` をそのまま組み、sys_ls_window と USER callback を使う。
- USER の通常列挙が legacy SYS_LS へ落ちないことを試験する。ls/help の写しは作らない。
- formatter は `%s %u %d %02u %03u %08x` と選択閉包で必要な文字・`%%` に絞る。
- 可変引数は USER 内で処理し、SC には固定引数だけを渡す。
- printf は短い write の残りを送る。FP 書式・hard-float libgcc・i386 newlib はリンクしない。
- 専用 stdio.h は printf、必要な書式化関数、setvbuf、fflush、stdout、_IOLBF、BUFSIZ を宣言する。
- stdout は専用の最小表現とし、即時出力なので setvbuf・fflush は無操作で成功を返す。
- string.h と実装には選択閉包の文字列関数に加え、構造体代入等に必要な memcpy・memset を含める。
- 仮の成功値で未解決 symbol を埋めず、最終リンクで必要な宣言・実装を照合する。
- 公開 KAPI の slot・型・世代は変更せず、不在群から戻す必須 slot は0本とする。
- 共通 sh の登録列・runtime・clear・環境値は CPU 非依存の選択口にする。
- 環境値は `SH_DEFAULT_PATH/HOME/SHELL` を使い、既定値を既存 `SYS_DEFAULT_*` にする。
- `include/config.h:68–70` の無条件定義は再定義しない。PPC profile は SH 側だけ指定する。

**6. 大きさの予算**

|対象|計画値・比較基準|
|---|---|
|sh.bin 格納量|96〜224KiB。OS32X header＋text/rodata/data、BSS 除外|
|sh BSS|192〜320KiB。pipe pool 128KiB、一覧・history・env を分離|
|sh stack／初期 heap|64KiB／128KiB。script・展開・子起動で確認|
|root 増分|sh 格納量＋概ね4〜16KiB。第3像の追加分だけ|
|kernel の W7 追加|S-B の exec 閉包あり像に対し +8〜32KiB 程度、CLI 除去は別記|
|既定診断像との比較|exec/root 等の既存閉包追加も含み、約716KiB級の差を別枠で計上|
|実行時 RAM 増分|概ね0.5〜0.9MiB＋子。root 内 sh と実行用ページを含む|

- 716KiB は S-B 文書の resident 値であり、基点間の正確な差分測定値ではない。
- 診断像との差、S-B CLI 像との差、root blob の差を map で別々に報告する。
- S-B 文書の予約残り1800KiBに対し、W7 追加見込みは概ね108〜272KiB、CLI 除去前。
- この概算では収まるが、最終 ELF の resident・BSS・alignment を含めて予約内収容を判定する。
- root blob は二重計上しない。16MiB 管理内の sh＋hello 同時生存と heap 使用量も確認する。

**7. 実装段と追従する試験**

- 行数は変更・追加の目安。各製品段は400行以下・1〜3ファイルを基本とする。
- 試験の追従は別の試験段にまとめ、対応完了まで製品段を受入済みにしない。
- r1 の S03 は launch 段へ吸収し、登録・runtime・環境の3ファイルを S01 にまとめる。
- 「共」は x86 と共有する場所。「★」は x86 生成物変更予定。現時点では全段なし。
- 差が出れば★へ変更し、理由と x86 の受入を追加する。既定値変更で比較を回避しない。
- 表の新規 check 名は提案。L は `python3 -B tools/checks/run.py --lint`。
- R は runtime、I は入力、X は同期 launch、O は出力分離、SC は第3像の専用試験。

|段|ファイル|行数|依存|共通の場所か|★か|追従する試験|回す check|
|---|---|---:|---|---|---|---|---|
|N01|`userland/ppc/nest.c`、`tools/ppc/sc_run.py`、`build/ppc-user.mk`、`build/ppc-kboot.mk` (root に足す bin の変数と試験用の出力先)|≤300|最初|PPC専用|なし|試験用root・nest反復・既存hello/fault|nest QEMU、USER build、L|
|P01|`userland/ppc/sh/profile.h`|≤80|なし|専用|なし|S01/S02 の既定展開・選択契約|X、L|
|P02|`input_decode.c`、`input_boot.c`、`input_boot.h`（platform/powermac）|≤365|なし|専用|なし|I、既存LAN入力試験|I、L|
|P03|`platform/powermac/kboot_console.c`、`build/ppc-kboot.mk`|≤120|N01 (同じ mk、直列)|PPC専用|なし|O、既存kboot出力・変異|O、既存対象試験、L|
|P04|`exec_boot.c`、`shell_boot.c`、`shell_boot.h`（platform/powermac）|≤130|P02/P03|専用|なし|接続待ち・exit再起動・ロード失敗|SC、L|
|U01|`runtime.c`、`format.c`、`runtime.h`（userland/ppc/sh）|≤300|P01|専用|なし|R、g_api/kapi一致、help色出力|R、L|
|U02|`stdio.c`、`include/stdio.h`（同上）|≤220|U01|専用|なし|R、短いwrite・redirect・無操作stdio|R、L|
|U03|`string.c`、`include/string.h`（同上）|≤220|なし|専用|なし|R、memory・文字列・リンク閉包|R、L|
|S01|`main.c`、`cmd_base.c`、`cmd_env.c`（userland/shell）|約120|P01/U01|共|なし|ver/hwb/about/mem、sh_shell、ver-shell-host|対象host試験、x86 programs、L|
|S02|`userland/shell/sh_exec.inc`、`sh_launch.inc`|≤120|N01/P01|共|なし|sh_launch/status/truncation、X|対象host試験、x86 programs、L|
|B01|`build/ppc-user.mk`|≤120|U01–03/S01–02|PPC専用|なし|R、既存ls/help、hello/fault|sh＋hello/fault build、R、L|
|B02|`build/ppc-kboot.mk`、`init/ppc_kernel.c`|≤180|P02–04/B01|PPC専用|なし|三像・NEGATIVE・既存消費者|三像build、対象kboot/LAN試験、L|
|T01|`tools/tests/ppc_sh_runtime.py`、`ppc_sh_input.py`|≤400|U/P|試験|なし|R/I の実ソース試験と変異|R/I --mutate、L|
|T02|`test_ver_shell.py`、`test_hwb_shell.py`、`test_about_info.py`|≤200|S01|試験|なし|旧anchor・include・固定hash・dep-dir|各変更試験 --mutate、L|
|T03|`test_mem_stat.py`、`sh_shell_host.c`、`tools/checks/ver-shell-host.py`|≤200|S01|試験|なし|同上、CHECK の入力追従|mem/sh-shell/ver-shell-host、L|
|T04|`test_sh_launch.py`、`sh_launch_host.c`、`sh_status_host.c`|≤300|S02|試験|なし|要求表の既存期待＋同期結果|sh-launch/sh-status --mutate、L|
|T05|`sh_truncation_host.c`、`tools/tests/ppc_sh_launch.py`|≤300|S02|試験|なし|拒否の共有・PATH走査停止|sh-truncation/X --mutate、L|
|T06|`tools/tests/ppc_sh_output.py`、`tools/ppc/sc_run.py`|≤400|B02|試験|なし|O、三像・S-C・変異|O/SC --mutate、L|
|D01|`docs/09_exec.md`|約25|SC完了|共|なし|実装契約との照合|文書対象check、L|
|D02|`TASK_PPC_SHELL.md`、`docs/tasks/DEFERRED_TESTS.md`|≤60|SC結果|文書|なし|S-C記録・実機延期1行|文書対象check、L|

- T02〜T05 の既存ファイルは anchor 等が変わる場合だけ編集し、不要な書換えはしない。
- ラッパー `test_sh_shell.py`・`test_sh_status.py`・`test_sh_truncation.py` も対応段で実行する。
- 新規試験は自身の CHECK に入力を宣言し、追加の登録ファイルが必要なら試験段側に含める。
- N01→P03→B02 (build/ppc-kboot.mk)、N01→B01 (build/ppc-user.mk)、N01→T06 (sc_run.py) は同一ファイルを触るため直列にする。N01 (入れ子の実動作) を前提にするのは結線の段 S02・B01・B02 だけで、P02・P03 (mk 以外)・U01〜U03 は N01 を待たずに並列で出してよい。
- ls/help の流用元は変更不要。共通 exec・shell_service・InputOps 宣言も現時点では変更不要。

**8. 変異・検査・延期**

|受入項目|1行変異|RED の観測|
|---|---|---|
|fd1 の端末出力|console_write をログへ戻す|TERM_OUT の出力欠落、ログへの誤流入|
|kernel ログ分離|ログ専用結線を通常 console_write へ戻す|kernel マーカーが端末に混入|
|USER 診断出力|kprintf adapter を fd1 へ変更|`/tmp/e` にエラーが入る|
|CRT の写し|kapi の差替えを削除|既存 help の kprintf 経路が欠落|
|接続待ち|待機条件を除去|遅延接続で起動表示・プロンプトが欠落|
|直接起動|起動先を hello へ変更|sh の入口・プロンプト観測が不一致|
|入力|取消変換削除／UPをDOWNへ変更|行取消・履歴再実行が不一致|
|列挙|既存 ls の skip 更新を削除|複数windowの一覧が不一致|
|同期 launch|exec_run を即時成功へ変更|hello の出力・反復結果が欠落|
|結果・再起動|codeを0へ／再起動を停止|終了コード3・次のプロンプトが不一致|
|pipe・redirect|保存長を0へ／printfをstream直結|pipe出力・ファイル内容が不一致|
|x86 不変|既定登録列を変更|shell.bin または sh.bin の基点比較が不一致|

- 各 anchor は一意にし、実行時 RED を確認する。文字列検査だけで成立としない。
- 変更・削除する製品行の anchor を `tools/tests` で検索し、対応する T 段で移す。
- x86 は `make programs` で常駐 shell.bin と USER sh.bin の両方をビルドし、基点とバイト比較する。
- 対応 ELF・生成 KAPI も比較する。共通 kernel 変更が必要になれば `make kernel` を追加する。
- 診断像・CLI 像は同条件の基点生成物と比較し、既存試験の期待値を保つ。
- 実装時は指定環境を export し、`tools/preflight.sh --base 9ba00a59 --fix-fonts` の NG を解消する。
- 触った像をビルドし、追加・変更試験の `--mutate` と L を実行する。
- 全 covers の一律実行は要求せず、各段の実ソースを直接組む対象試験を回す。
- QEMU は mac99 の7450・7455。N01、S-B 回帰、S-C、fault復帰、媒体切離しを確認する。
- 検査は前面で完走させ、最後のソース変更後のログだけを合格に数える。
- `make check-changed`・全変異は回さない。取り込みは CI、全変異は full-check workflow。
- D02 は実機 S-C を `DEFERRED_TESTS.md` に1行追加し、対象像・未実施項目・再開条件を記す。
- QEMU 合格と実機合格を区別し、実機延期を W7 の試験済みとして扱わない。
- 本回答は読み取りのみ。preflight・build・host・QEMU・lint・バイト比較は未実行。

反論：なし。
r1→r2 の変更の数 13 / 反論の数 0


## 確定 (2026-11-12、Opus 5.5 の第 2 回の条件付き確定を PM が反映)
- N2 (P2): N01 のファイルに build/ppc-kboot.mk (root に足す bin の変数と試験用の出力先) を加え、同じ mk を触る P03・B02 と直列にした (§7 の表)。
- N3 (P3、PM 決定): fd1 の既定の出力を端末へ向ける結び替えは **fs/vfs_fd.c:528 の 1 か所 (fd1 の既定の出力) だけ**にする。console_write と kernel の kprintf はログのまま。PPC の第 3 像の build で fs/vfs_fd.o にだけ置き換えの名前を渡すか、platform の provider を介すかは P03 の実装で決める (kboot_accept の変異 build (ppc-kboot.mk:101-104) でも同じ形で組まれること)。共通の C のソースは変えない。
- N4 (P3): N01 を前提にするのは S02・B01・B02 だけ。P01・P02・U01〜U03 は依存なしで並列に出せる (§7 の表を直した)。
- N5 (P3): SH_LAUNCH_SYNC の選択は sh_exec.inc:89 の `#ifndef SHELL_AS_APP` と sh_launch.inc 末尾の sh_exec_result の 2 か所。既定値は userland/ppc/sh/profile.h (P01) が与え、shell.h には置かない (x86 は未定義 = 今の展開)。
- N6 (P3): 端末の接続の判定は、platform/powermac の中に「接続しているか」を返す小さな関数を置く (pm_term_state は STATE の message を組み立てる関数なので使わない)。P04 に含める。


## 確定の後の追記 1 (実装と受入で決めたこと)

- N01 で見つかった USER の入れ子の不具合を `arch/ppc/exec_context.c` で修正。
- R01 で ramfs の FD の口を用意。
- P03 の fd1 の結び替えは `fs/vfs_fd.o` の 1 か所 (`KB_FD1_STREAM`)。
- 受入アプリは 8.3 名 `outprobe` を使う。
- serial 版の試験は停止の前に EE=0 とする。
- K01 の時計の検査は TB の読み 2 回で挟み、周期は変えない。
- S01 の clear は、SH_CLEAR_TERMINAL を廃して tvram_clear の KAPI の口 (PowerPC では pm_console_clear: 画面は共通の消去、ESC[2J ESC[H は stream だけ) に変えた (2026-11-13、FB 端末 段 H)。
