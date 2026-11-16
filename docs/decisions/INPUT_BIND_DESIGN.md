# 入力 HAL 追補 確定版 — BSD キーマップと標準 OS の操作

## r3 からの差分

|指摘 ID|直した節|
|---|---|
|B1|§2a:Ctrl+数字/記号の制御文字を意図した差として採用、byte利用側と台帳略号を追加。§6:M0の固定期待|
|B2|§4:初回setまたはreset成功で保存。§6:resetだけのアプリの終了復元試験|
|B3|§2・§2a:114はnopでも修飾扱いでcooked entryなし。§6:M0でentry有無を検査|
|B4|§4:kind6があればそのprofileのkind6をsetの行で全置換|
|B5|§4・§6:親§7のA0zへ合流、版上げ1回、依存M2〜M8追加、U1例外は親と同じ|
|B6|§6:両archの初期shell起動file:lineと接続ファイル、中断adapterファイルを固定|
|B7|§2a・§5・§7:N2をユーザー決定の原文で確定。案A採用時の節を削除、F3/F4の状態を明記|

- 状態: **確定 (2026-11-15)**。REVIEW.r3.mdのApprove with nitsとPM決定B1〜B7を反映。ユーザー決定 2026-11-15「文字は BSD、操作は DOS」によりN2は案Bで確定。実装/正典変更なし。F3・F4は **PM 推奨 (ユーザーに提示済み、反論なし)**。
- **追補 2 (2026-11-16)**: 表の型・共通の口・arch の backend・出口の写しは [INPUT_KEYMAP_DESIGN](INPUT_KEYMAP_DESIGN.md) が改める (PC-98 sc を共通符号とする行、USB を AT→PC-98 に揃える行、M1・M2・M3・M8・X-cooked の段)。食い違うときは追補 2 を正とする。
- 不変: 単一タスク・協調切替・整数演算。認証/許可リストを足さない。BSD形式を本体とし、OS XとDOS/Windowsの操作差を保持。Win32/DirectX互換はユーザーランド。

## 1. FreeBSD の実物と標準 OS の根拠

- 以下の `R/` は `/home/hight/os32-tmp/ref/freebsd-kbd/`。全てreleng/10.4、commit `d869f4ade283f4691dc249e435376a5883824be2`、版/取得日/hashは `R/SOURCES.md:1-28`。Q1/Q2は閉じる。

|実物|採用する意味・根拠 (file:line)|
|---|---|
|型|`R/sys/sys/kbio.h:95-121`: NUM_KEYS=256、NUM_STATES=8、ALTGR_OFFSET=128、keyent_tはu_int map[8]+u_char spcl/flgs、keymap_tはu_short n_keys+key[256]。旧u_char map版は別型 (:123-134)|
|列/ロック|`R/sys/dev/kbd/kbd.c:1244-1254`: AltGr/AltLockで添字+128、列=(shift?1:0)\|(ctrl?2:0)\|(alt?4:0)。C=1/N=2/B=3/O=0、Caps/Numの該当条件をORして列を一度だけxor 1 (`R/usr.sbin/kbdcontrol/kbdmap.5:256-271`)|
|特殊bit|列0→bit7、列7→bit0、`spcl & (0x80 >> column)`。生成も評価も同じ (`R/usr.sbin/kbdcontrol/kbdcontrol.c:334-339`、`R/sys/dev/kbd/kbd.c:1351-1359`)|
|accent|`R/sys/sys/kbio.h:99-100,201-210`: n_accsはu_short、15個まで、acccharと52組の対はu_char。`R/sys/dev/kbd/kbd.c:1192-1231`: 同じaccent二度/続く空白でaccent自身、不成立はERRKEY|
|PC98|`R/share/syscons/keymaps/jp.pc98.kbd:6,118-122` はESC=0、SHIFT=112、CAPS=113、CTRL=116。`R/sys/pc98/cbus/pckbd.c:611,681` はscancode&0x7fを直接genkbdへ。`R/sys/pc98/cbus/pckbdtables.h:33` もPC98 keymap、n_keys=0x80|
|USB/PPC|`R/sys/dev/usb/input/ukbd.c:312-345,1657-1677,1771-1772` のukbd_trtab→keycode→genkbd_keyaction、`:143,1245-1252` は同じkeymap_t/アクセント登録。`:84-90` はUKBD_DFLT_KEYMAP時の生成ukbdmap.hと共通kbdtables.hで、PowerPC独自形式ではない|
|基底/文法|PC98は上記jp.pc98、USBは `R/share/syscons/keymaps/jp.106.kbd:1-6` / `R/share/vt/keymaps/jp.kbd:1-6` / `R/share/vt/keymaps/us.kbd:1-6`。8列の.kbd文法は `R/usr.sbin/kbdcontrol/kbdmap.5:35-80,273-295`、特殊語は同lex.l/parserを正とする|

- 特殊値は次表を保持する (`R/sys/sys/kbio.h:139-199`、語と意味は `R/usr.sbin/kbdcontrol/kbdmap.5:183-253`)。ETX/EOT/SUB/ESCはspcl=0の文字であり特殊IDではない。

|値 (hex)|BSDの操作|OS32の扱い|
|---|---|---|
|00、02/03、04/05/06、07、08、09|NOP、LSH/RSH、CLK/NLK/SLK、LALT、BTAB、LCTR|提供:無文字、表による修飾/ロック、逆Tab通知、Ctrl。NOPでも旧getkeyの物理scは保持|
|0a、0b–1a、1b–7a|NEXT/nscr、scr1–16、fkey1–96|nscr/scrは受理して無視、fkey1–96はBSD_SPECIAL通知を提供 (関数番号)。byte出口は文字なし・sc保持、文字列展開は追加しない|
|80–84|RCTR/RALT/ALK/ASH/META|提供:BSDの修飾/AltLock/AltGr/Meta。CommandとMetaを同一視しない|
|85–88、98–9a、a1–a2|RBT/DBG/SUSP/SPSC、STBY/PREV/PNC、HALT/PDWN|受理して無視:全て。記録/照会可能な表の値として残すが、再起動/電源/デバッグ処理を本票で足さない|
|89–97|15種accent (90のDUML/DDIAは同値)|提供:accentmap_tで評価。拡張Unicode accent形式は追加しない|
|9b–a0、a3|LSHA/RSHA/LCTRA/RCTRA/LALTA/RALTA、PASTE|提供:修飾+AltLockのBSD動作、pasteはPASTE通知 (受け手不在なら無視)。新clipboard実装は足さない|

- 提供/受理して無視/拒否の区分は上表で全既知特殊値を網羅。拒否は未定義01・7b–7f・a4以降等のspcl値と範囲外fkey/scr/accent。通常文字の同じ数値は別。これは実装能力の分類であり認証・許可リストではない。案Aでも未提供機能を追加せず、表をそのまま受理して無視する。
- BSDの許諾は `R/sys/dev/kbd/kbdtables.h:2-22` 等とSOURCESに従い転載時に保持。kbio.h/.kbdに個別条項がない点も台帳に残す。GPLのコード/構造は使わない。
- OS操作の根拠: PACKのユーザー決定、[Apple Terminal](https://support.apple.com/guide/terminal/keyboard-shortcuts-trmlshtcts/mac) (Ctrl-U/K、Cmd-C/V/K/.)、[Apple終了操作](https://support.apple.com/en-bn/guide/terminal/apdb66b5242-0d18-49fc-9c47-a2498b7c91d5/mac) (Ctrl-C)、[Microsoft操作](https://support.microsoft.com/en-us/accessibility/windows/keyboard-shortcuts-in-windows) (Ctrl-X/C/V/Z等)。旧版の確認はQ3〜Q6として残す。

## 2. 添字・修飾・合流

- 実行時の物理scは従来どおりPC98の0..127、BSD表は256行で後半128行のAltGr bankも保持。AT keycode→PC98はhost変換器だけで行い、E0/E1バイトと正規化keycodeを混同しない。
- **F4 PM 推奨 (ユーザーに提示済み、反論なし)**: 修飾/ロックのspclを受理し、makeで表の操作を確定・保持、breakでは保持した操作を解放 (BSD `R/sys/dev/kbd/kbd.c:1256-1258,1354-1359`)。Caps→Ctrl等がcooked/旧raw modsの両方に効く。
- 物理held/src/cntを唯一の押下状態とし、表から導くlogical mods/lockは派生状態。新makeでのみUSB共有lockを反転、本体機械式lockはmake/set・break/clear、両者OR。PC98 Kanaは既存IME用派生bitとして維持し、jp.pc98の114=laltは案Bの配布差分で8列ともnopへ変更し、Kanaは表外の派生bitとする。114はnopだが修飾扱いでcooked entryを作らない (`kbd.c:360-383`のis_modと同じ)。案Aでは114=laltを保持しKana機能を外す。
- 表交換は押下中BUSYとして古いmake/新しいbreakの混在を防ぐ。機械式lockの安定ONは妨げず、交換時に物理位置から再導出、USB lockは意味の変わったbitをreset。通常repeatでlockを反転しない。
- 緊急CTRL+STOP/CTRL+GRPH+DELだけは物理CTRL/GRPH/STOP/DELで判定、表から独立。Caps→Ctrlでは緊急停止を作らず、物理Ctrlを他キーへ変更しても救済を失わない。F4代案:修飾/lockの変更をUNSUPPORTEDとする (BSD踏襲度が下がるため非推奨)。
- in2はmerge前の物理キー/物理modsを保持しbindingを掛けない。旧rawはsc/downを保持しmodsのみlogical値へ。独占でWMに残すSTOP/CTRL/GRPH/TABは物理scで選別する。
- 合流は8列の値・spcl・flgs・AltGr側まで同一の行だけ許可。左右Ctrl/Shift/AltのBSD値が違う場合、自動同一視しない。配布表は差分表で左右を同じlctrl/lshift/laltへ明示調整後に合流、左右別設定を要求するユーザー表は衝突エラー。

|明示の写像/捨てる表|扱いと試験|
|---|---|
|E0/E4→74、E1/E5→70、E2/E6→73|共通scへの合流。両方同時押下/片側解放で保持する現行HID集約を維持|
|31/32→28、28/58→1c|同一行に調整した時だけ合流。Enter/Keypad Enterや記号の意味を黙って捨てない|
|35、47、53、64–66、68–84、86、8c–df、e8–ff、00–03|現行hid_kbd_mapが0xffのusageを明示DROPとする (未割当・ScrollLock/NumLock等)。90/91も含む。配布変換台帳に各usageの理由を展開、既知外を暗黙DROPにしない|
|67→4d、85→4f|BSD ukbd_trtabではNN、OS32はKeypad =/commaを提供。usage付き明示OVERRIDEが必要、AT番号だけでは表せない|
|E3/E7|A0z前は現行どおりDROP、Command段後は監査済み未使用sc二つへEXT。GRPHへ潰さず、旧SHIFT=70/CTRL=74/STOP=60を変えない|

- F5受入: 全256usageで `AT_to_PC98(ukbd_trtab[u]) == hid_kbd_sc(u)` をまず比較。不一致は上記OVERRIDE/EXTのusage単位だけを期待値にできる。NNはキー0(ESC)と区別、DROPは双方ff、差分台帳にない不一致は失敗。母表と製品表を別々に読み、製品から期待値を生成しない。
- `.kbd`の文字列文法は改変しない。host側で表・差分・DROP/OVERRIDE台帳を合わせて生成する。初期JIS配列を保ち、ユーザー指定US等の表も同じ経路。操作profileと配列を独立にする。

## 2a. jp.pc98.kbd と今の OS32 の差分台帳 (N2、案Bで確定)

- 番号は10進/16進、BSD欄と現行欄はbase/shift/ctrl/shift+ctrl/alt/alt+shift/alt+ctrl/alt+shift+ctrlの8列、BSD末尾[]はflgs。`R/share/syscons/keymaps/jp.pc98.kbd`は番号nの行がfile:(n+6)。現行値はhex、`drivers/input/kbd.c:164-215,459-475`の2表+Caps/Ctrl処理 (Altは無作用) を評価した値。
- 表はCaps=Num=OFF。Caps=ONでは現行の英字大小反転後に小文字だけCtrl化、BSDはC/N条件で列xor1なので英字Ctrl列にも差がある (同じ英字行に含む)。Numによる追加差は057のNだがbase/shiftは同値、alt+ctrlは両方boot。Kana差は114。raw scの値を比較対象から消さない。
- ユーザー決定 2026-11-15「文字は BSD、操作は DOS」によりBを採用。比較欄のA=jp.pc98全セル/特殊操作を無変更で評価 (ただし§1の未提供特殊操作は無視)。B=文字キーはBSD、操作キー/旧ナビゲーションbyteは以下の明示差分と出口規則。Bの「LEGACY」は§3のLEGACY_CONTROL、ETXとの区別を維持。BSDの通常文字が新たな制御文字になる差も隠さず採用結果を示す。
- 今の使い手の略号 (全てrepository file:line): P=`userland/shell/ui.c:773-775`(印字判定/非印字無作用)、E=同`:770`(ESC取消)、T=同`:757`、R=同`:660`、D=同`:748`、U=同`:686`+`userland/cmds/less.c:405`、L=ui`:710`、V=ui`:724`、W=ui`:694`+less`:402`、H=ui`:735`+less`:408`、J=less`:41,411`、I=`kernel/ime/ime.c:289-291`、K=`drivers/input/kbd.c:239-245,366`、G=`drivers/input/kbd.c:476-481`(sc付き旧出口)+`userland/gshell/src/input.rs:108-110,129-130`(同値変換)。Gの行はこの読取範囲で専用CUI操作の使い手が確認できず、推測しない。 F=`userland/shell/cmd_filer.c:44-50,476-508` (byteのESC/上下左右)、LS=`userland/cmds/less.c:38-41,220-225,390,402-411` (ESC/7F/上下)、IN=`userland/system/install.c:255` (ESC)、HD=`userland/system/inst_hdd.c:236` (ESC)、RS=`userland/shell/rshell.c:294,471` (ESC)。台帳には新しく出る制御byteを同じ値として受け取る使い手も含める。uiの矢印/HOME/DELはsc判定で、Ctrl+記号をそのscに合成しない。

|番号|キー|BSDの値 (8列)|今のOS32の値 (8列)|今の使い手|案A「BSDのまま」の結果|案B「文字BSD・操作/制御コード維持」の結果|
|---|---|---|---|---|---|---|
|000 / 00|ESC|esc esc esc esc esc esc debug esc [O]|1b 1b 1b 1b 1b 1b 1b 1b|E/LS/F/IN/HD/RS|alt+ctrlはdebug無作用|8列ESC、従来取消維持|
|001 / 01|1|'1' '!' '!' '!' '1' '!' '!' '!' [O]|31 21 31 21 31 21 31 21|P|BSD8列を採用|BSD8列 (文字変更)|
|002 / 02|2|'2' '"' sub sub '2' '@' nul nul [O]|32 22 32 22 32 22 32 22|P|BSD8列を採用|BSD8列 (文字変更)|
|003 / 03|3|'3' '#' esc esc '3' '#' esc esc [O]|33 23 33 23 33 23 33 23|P/E/LS/F/IN/HD/RS|BSD8列を採用|BSD8列 (文字変更)|
|004 / 04|4|'4' '$' fs fs '4' '$' fs fs [O]|34 24 34 24 34 24 34 24|P/F|BSD8列を採用|BSD8列 (文字変更)|
|005 / 05|5|'5' '%' gs gs '5' '%' gs gs [O]|35 25 35 25 35 25 35 25|P/F|BSD8列を採用|BSD8列 (文字変更)|
|006 / 06|6|'6' '&' rs rs '6' '^' rs rs [O]|36 26 36 26 36 26 36 26|P/F/LS|BSD8列を採用|BSD8列 (文字変更)|
|007 / 07|7|'7' ''' us us '7' '&' '&' '&' [O]|37 27 37 27 37 27 37 27|P/F/LS|BSD8列を採用|BSD8列 (文字変更)|
|008 / 08|8|'8' '(' del del '8' '*' bs bs [O]|38 28 38 28 38 28 38 28|P/LS|BSD8列を採用|BSD8列 (文字変更)|
|009 / 09|9|'9' ')' '9' '9' '9' '(' '(' '(' [O]|39 29 39 29 39 29 39 29|P|BSD8列を採用|BSD8列 (文字変更)|
|010 / 0a|0|'0' nop '0' '0' '0' ')' ')' ')' [O]|30 00 30 00 30 00 30 00|P|BSD8列を採用|BSD8列 (文字変更)|
|011 / 0b|-|'-' '=' '-' '-' '-' '_' us us [O]|2d 3d 2d 3d 2d 3d 2d 3d|P/F/LS|BSD8列を採用|BSD8列 (文字変更)|
|012 / 0c|^|'^' '`' rs rs '=' '+' '+' '+' [O]|5e 60 5e 60 5e 60 5e 60|P/F/LS|BSD8列を採用|BSD8列 (文字変更)|
|013 / 0d|\|'\' '&#124;' fs fs '\' '&#124;' fs fs [O]|5c 7c 5c 7c 5c 7c 5c 7c|P/F|BSD8列を採用|BSD8列 (文字変更)|
|015 / 0f|TAB|ht btab ht btab ht btab ht btab [O]|09 09 09 09 09 09 09 09|T|ShiftはBTAB通知、byteなし|8列LEGACY 09、Tab維持|
|016 / 10|q|'q' 'Q' dc1 dc1 'q' 'Q' dc1 dc1 [C]|71 51 11 51 71 51 11 51|P|Ctrl全列が制御文字|BSD8列、Shift+Ctrlも制御文字|
|017 / 11|w|'w' 'W' etb etb 'w' 'W' etb etb [C]|77 57 17 57 77 57 17 57|P|Ctrl全列が制御文字|BSD8列、Shift+Ctrlも制御文字|
|018 / 12|e|'e' 'E' enq enq 'e' 'E' enq enq [C]|65 45 05 45 65 45 05 45|P|Ctrl全列が制御文字|BSD8列、Shift+Ctrlも制御文字|
|019 / 13|r|'r' 'R' dc2 dc2 'r' 'R' dc2 dc2 [C]|72 52 12 52 72 52 12 52|P|Ctrl全列が制御文字|BSD8列、Shift+Ctrlも制御文字|
|020 / 14|t|'t' 'T' dc4 dc4 't' 'T' dc4 dc4 [C]|74 54 14 54 74 54 14 54|P|Ctrl全列が制御文字|BSD8列、Shift+Ctrlも制御文字|
|021 / 15|y|'y' 'Y' em em 'y' 'Y' em em [C]|79 59 19 59 79 59 19 59|P|Ctrl全列が制御文字|BSD8列、Shift+Ctrlも制御文字|
|022 / 16|u|'u' 'U' nak nak 'u' 'U' nak nak [C]|75 55 15 55 75 55 15 55|P|Ctrl全列が制御文字|BSD8列、Shift+Ctrlも制御文字|
|023 / 17|i|'i' 'I' ht ht 'i' 'I' ht ht [C]|69 49 09 49 69 49 09 49|P|Ctrl全列が制御文字|BSD8列、Shift+Ctrlも制御文字|
|024 / 18|o|'o' 'O' si si 'o' 'O' si si [C]|6f 4f 0f 4f 6f 4f 0f 4f|P|Ctrl全列が制御文字|BSD8列、Shift+Ctrlも制御文字|
|025 / 19|p|'p' 'P' dle dle 'p' 'P' dle dle [C]|70 50 10 50 70 50 10 50|P|Ctrl全列が制御文字|BSD8列、Shift+Ctrlも制御文字|
|026 / 1a|@|'@' '~' nul nul '[' '{' esc esc [O]|40 7e 40 7e 40 7e 40 7e|P/E/LS/F/IN/HD/RS|BSD8列を採用|BSD8列 (文字変更)|
|027 / 1b|[|'[' '{' esc esc ']' '}' gs gs [O]|5b 7b 5b 7b 5b 7b 5b 7b|P/E/LS/F/IN/HD/RS|BSD8列を採用|BSD8列 (文字変更)|
|028 / 1c|ENTER|cr cr nl nl cr cr nl nl [O]|0d 0d 0d 0d 0d 0d 0d 0d|R|Ctrl列NL、旧shellのCR判定と相違|8列LEGACY 0d、Enter維持|
|029 / 1d|a|'a' 'A' soh soh 'a' 'A' soh soh [C]|61 41 01 41 61 41 01 41|P|Ctrl全列が制御文字|BSD8列、Shift+Ctrlも制御文字|
|030 / 1e|s|'s' 'S' dc3 dc3 's' 'S' dc3 dc3 [C]|73 53 13 53 73 53 13 53|P|Ctrl全列が制御文字|BSD8列、Shift+Ctrlも制御文字|
|031 / 1f|d|'d' 'D' eot eot 'd' 'D' eot eot [C]|64 44 04 44 64 44 04 44|P|Ctrl全列が制御文字|BSD8列、Shift+Ctrlも制御文字|
|032 / 20|f|'f' 'F' ack ack 'f' 'F' ack ack [C]|66 46 06 46 66 46 06 46|P|Ctrl全列が制御文字|BSD8列、Shift+Ctrlも制御文字|
|033 / 21|g|'g' 'G' bel bel 'g' 'G' bel bel [C]|67 47 07 47 67 47 07 47|P|Ctrl全列が制御文字|BSD8列、Shift+Ctrlも制御文字|
|034 / 22|h|'h' 'H' bs bs 'h' 'H' bs bs [C]|68 48 08 48 68 48 08 48|P|Ctrl全列が制御文字|BSD8列、Shift+Ctrlも制御文字|
|035 / 23|j|'j' 'J' nl nl 'j' 'J' nl nl [C]|6a 4a 0a 4a 6a 4a 0a 4a|P|Ctrl全列が制御文字|BSD8列、Shift+Ctrlも制御文字|
|036 / 24|k|'k' 'K' vt vt 'k' 'K' vt vt [C]|6b 4b 0b 4b 6b 4b 0b 4b|P|Ctrl全列が制御文字|BSD8列、Shift+Ctrlも制御文字|
|037 / 25|l|'l' 'L' ff ff 'l' 'L' ff ff [C]|6c 4c 0c 4c 6c 4c 0c 4c|P|Ctrl全列が制御文字|BSD8列、Shift+Ctrlも制御文字|
|038 / 26|;|';' '+' ';' ';' ';' ':' ';' ';' [O]|3b 2b 3b 2b 3b 2b 3b 2b|P|BSD8列を採用|BSD8列 (文字変更)|
|039 / 27|:|':' '*' ':' ':' ''' '"' ''' ''' [O]|3a 2a 3a 2a 3a 2a 3a 2a|P|BSD8列を採用|BSD8列 (文字変更)|
|040 / 28|]|']' '}' gs gs '`' '~' '~' '~' [O]|5d 7d 5d 7d 5d 7d 5d 7d|P/F|BSD8列を採用|BSD8列 (文字変更)|
|041 / 29|z|'z' 'Z' sub sub 'z' 'Z' sub sub [C]|7a 5a 1a 5a 7a 5a 1a 5a|P|Ctrl全列が制御文字|BSD8列、Shift+Ctrlも制御文字|
|042 / 2a|x|'x' 'X' can can 'x' 'X' can can [C]|78 58 18 58 78 58 18 58|P|Ctrl全列が制御文字|BSD8列、Shift+Ctrlも制御文字|
|043 / 2b|c|'c' 'C' etx etx 'c' 'C' etx etx [C]|63 43 03 43 63 43 03 43|P|Ctrl全列が制御文字|BSD8列、Shift+Ctrlも制御文字・ETX中断/取消|
|044 / 2c|v|'v' 'V' syn syn 'v' 'V' syn syn [C]|76 56 16 56 76 56 16 56|P|Ctrl全列が制御文字|BSD8列、Shift+Ctrlも制御文字|
|045 / 2d|b|'b' 'B' stx stx 'b' 'B' stx stx [C]|62 42 02 42 62 42 02 42|P|Ctrl全列が制御文字|BSD8列、Shift+Ctrlも制御文字|
|046 / 2e|n|'n' 'N' so so 'n' 'N' so so [C]|6e 4e 0e 4e 6e 4e 0e 4e|P|Ctrl全列が制御文字|BSD8列、Shift+Ctrlも制御文字|
|047 / 2f|m|'m' 'M' cr cr 'm' 'M' cr cr [C]|6d 4d 0d 4d 6d 4d 0d 4d|P|Ctrl全列が制御文字|BSD8列、Shift+Ctrlも制御文字|
|048 / 30|,|',' '<' '<' '<' ',' '<' '<' '<' [O]|2c 3c 2c 3c 2c 3c 2c 3c|P|BSD8列を採用|BSD8列 (文字変更)|
|049 / 31|.|'.' '>' '>' '>' '.' '>' '>' '>' [O]|2e 3e 2e 3e 2e 3e 2e 3e|P|BSD8列を採用|BSD8列 (文字変更)|
|050 / 32|/|'/' '?' del del '/' '?' del del [O]|2f 3f 2f 3f 2f 3f 2f 3f|P/LS|BSD8列を採用|BSD8列 (文字変更)|
|051 / 33|ろ/_|nop '_' us us '\' '&#124;' fs fs [O]|00 5f 00 5f 00 5f 00 5f|P/F/LS|BSD8列を採用|BSD8列 (文字変更)|
|052 / 34|SPACE|' ' ' ' nul nul ' ' ' ' nul nul [O]|20 20 20 20 20 20 20 20|P|BSD8列を採用|BSD8列 (文字変更)|
|053 / 35|XFER|esc esc esc esc esc esc esc esc [O]|00 00 00 00 00 00 00 00|E/P/LS/F/IN/HD/RS|8列ESCで行取消へ変化|8列LEGACY 00、XFERで行を消さない|
|054 / 36|RLUP|fkey59 fkey59 fkey59 fkey59 fkey59 fkey59 fkey59 fkey59 [O]|12 12 12 12 12 12 12 12|G|fkey通知、旧byteなし|8列LEGACY 12、旧操作維持|
|055 / 37|RLDN|fkey51 fkey51 fkey51 fkey51 fkey51 fkey51 fkey51 fkey51 [O]|03 03 03 03 03 03 03 03|G|fkey通知、旧byteなし|8列LEGACY 03、旧操作維持・非VINTR|
|056 / 38|INS|fkey60 fkey60 fkey60 fkey60 fkey60 fkey60 fkey60 fkey60 [O]|16 16 16 16 16 16 16 16|G|fkey通知、旧byteなし|8列LEGACY 16、旧操作維持|
|057 / 39|DEL|del del del del del del boot boot [N]|7f 7f 7f 7f 7f 7f 7f 7f|D/LS|alt+ctrl boot無作用、他del|8列LEGACY 7f、旧操作維持|
|058 / 3a|↑|fkey50 fkey50 fkey50 fkey50 fkey50 fkey50 fkey50 fkey50 [O]|1e 1e 1e 1e 1e 1e 1e 1e|U/F/LS|fkey通知、旧byteなし|8列LEGACY 1e、旧操作維持|
|059 / 3b|←|fkey53 fkey53 fkey53 fkey53 fkey53 fkey53 fkey53 fkey53 [O]|1d 1d 1d 1d 1d 1d 1d 1d|L/F|fkey通知、旧byteなし|8列LEGACY 1d、旧操作維持|
|060 / 3c|→|fkey55 fkey55 fkey55 fkey55 fkey55 fkey55 fkey55 fkey55 [O]|1c 1c 1c 1c 1c 1c 1c 1c|V/F|fkey通知、旧byteなし|8列LEGACY 1c、旧操作維持|
|061 / 3d|↓|fkey58 fkey58 fkey58 fkey58 fkey58 fkey58 fkey58 fkey58 [O]|1f 1f 1f 1f 1f 1f 1f 1f|W/F/LS|fkey通知、旧byteなし|8列LEGACY 1f、旧操作維持|
|062 / 3e|HOME|fkey49 fkey49 fkey49 fkey49 fkey49 fkey49 fkey49 fkey49 [O]|01 01 01 01 01 01 01 01|H/LS|fkey通知、旧byteなし|8列LEGACY 01、旧操作維持|
|063 / 3f|HELP|fkey57 fkey57 fkey57 fkey57 fkey57 fkey57 fkey57 fkey57 [O]|05 05 05 05 05 05 05 05|J/LS|fkey通知、旧byteなし|8列LEGACY 05、旧操作維持|
|081 / 51|NFER|meta meta meta meta meta meta meta meta [O]|00 00 00 00 00 00 00 00|P|Meta修飾へ変化|8列LEGACY 00、Metaにしない|
|082 / 52|vf1|fkey11 fkey23 fkey35 fkey47 scr11 scr11 scr11 scr11 [O]|00 00 00 00 00 00 00 00|G|fkey通知/scr無視|8列LEGACY 00、sc維持|
|083 / 53|vf2|fkey12 fkey24 fkey36 fkey48 scr12 scr12 scr12 scr12 [O]|00 00 00 00 00 00 00 00|G|fkey通知/scr無視|8列LEGACY 00、sc維持|
|084 / 54|vf3|slock slock slock slock slock slock slock slock [O]|00 00 00 00 00 00 00 00|G|ScrollLock状態へ変化|8列LEGACY 00、sc維持|
|096 / 60|STOP|slock saver slock saver susp nop susp nop [O]|00 00 00 00 00 00 00 00|I|ScrollLock/無視、裸STOP中断なし|セルnop+STOP操作、IME優先/通常中断|
|097 / 61|COPY|nscr nscr debug debug nop nop nop nop [O]|00 00 00 00 00 00 00 00|G|nscr/debug等無視|8列LEGACY 00、sc維持|
|098 / 62|F1|fkey01 fkey13 fkey25 fkey37 scr01 scr01 scr01 scr01 [O]|00 00 00 00 00 00 00 00|G|fkey通知/scr無視|8列LEGACY 00、sc維持|
|099 / 63|F2|fkey02 fkey14 fkey26 fkey38 scr02 scr02 scr02 scr02 [O]|00 00 00 00 00 00 00 00|G|fkey通知/scr無視|8列LEGACY 00、sc維持|
|100 / 64|F3|fkey03 fkey15 fkey27 fkey39 scr03 scr03 scr03 scr03 [O]|00 00 00 00 00 00 00 00|G|fkey通知/scr無視|8列LEGACY 00、sc維持|
|101 / 65|F4|fkey04 fkey16 fkey28 fkey40 scr04 scr04 scr04 scr04 [O]|00 00 00 00 00 00 00 00|G|fkey通知/scr無視|8列LEGACY 00、sc維持|
|102 / 66|F5|fkey05 fkey17 fkey29 fkey41 scr05 scr05 scr05 scr05 [O]|00 00 00 00 00 00 00 00|G|fkey通知/scr無視|8列LEGACY 00、sc維持|
|103 / 67|F6|fkey06 fkey18 fkey30 fkey42 scr06 scr06 scr06 scr06 [O]|00 00 00 00 00 00 00 00|G|fkey通知/scr無視|8列LEGACY 00、sc維持|
|104 / 68|F7|fkey07 fkey19 fkey31 fkey43 scr07 scr07 scr07 scr07 [O]|00 00 00 00 00 00 00 00|G|fkey通知/scr無視|8列LEGACY 00、sc維持|
|105 / 69|F8|fkey08 fkey20 fkey32 fkey44 scr08 scr08 scr08 scr08 [O]|00 00 00 00 00 00 00 00|G|fkey通知/scr無視|8列LEGACY 00、sc維持|
|106 / 6a|F9|fkey09 fkey21 fkey33 fkey45 scr09 scr09 scr09 scr09 [O]|00 00 00 00 00 00 00 00|G|fkey通知/scr無視|8列LEGACY 00、sc維持|
|107 / 6b|F10|fkey10 fkey22 fkey34 fkey46 scr10 scr10 scr10 scr10 [O]|00 00 00 00 00 00 00 00|G|fkey通知/scr無視|8列LEGACY 00、sc維持|
|114 / 72|Kana|lalt lalt lalt lalt lalt lalt lalt lalt [O]|文字なし、Kana派生 (表のbyteは全00)|K|Alt修飾、Kana派生なし|8列nop+表外Kana派生、修飾扱いでcooked entryなし|

- 全128行中、差分80行を上記に列挙。残り48行は 014,064,065,066,067,068,069,070,071,072,073,074,075,076,077,078,079,080,085,086,087,088,089,090,091,092,093,094,095,108,109,110,111,112,113,115,116,117,118,119,120,121,122,123,124,125,126,127。112/113/115/116は修飾/lockとして現行と同じ意味、その他の非修飾nop行は「文字なし・sc保持」の出口契約により現行byte00と等価 (文字NULとNOPは内部では別。非修飾NOPも旧口ではsc付きentryを残し旧getcharはその低位00を返すが、新操作口では文字を発生させない)。
- B欄のLEGACY行は8セルを旧byte値・spcl=0・flgs=Oへ置換してから評価し、COMPAT行でLEGACY_CONTROLを付ける (元のmeta/slock/fkeyを発火してからbyteだけ上書きしない)。000は8列esc/O、096は8列nop/O+STOP操作、114は8列nop/O+Kana派生、修飾扱いでcooked entryを作らない。その他の文字行はBSD値/flagsをそのまま採る。
- 案Bの基底文字変更でCtrl+Shift+CもETXになる。Ctrl+数字・記号から出る1B/1C/1D/1E/1F/7Fは、ユーザー決定「文字は BSD」の意図した差として受け入れる。旧byteの出口ではキー由来を区別しないため、filerでCtrl+5は左移動、shellでCtrl+3は行取消になる。N3のETX中断とRLDNの識別は内部評価時に維持する。固定期待は本台帳をレビューして独立fixtureへ転記し、テスト時に旧/新製品のどちらからも再生成しない。

## 3. 文脈・出口・中断

- N1/S6: `exec/exec_abort.inc:73-77`の「GUIの宛先を知るのはWMだけ」を上位規則とする。con_sink有効中、IRQは通常入力の文脈を引かず物理状態/in2/旧raw配送のみ。実行中の裏slotのRUNNING宣言を見てCtrl+Cを中断にしない。既存暴走時の物理CTRL+STOP救済例外だけ維持し、通常ETXには拡張しない。
- CUIは前景実行単位のowner/exec_gen/command_gen/contextをIRQ内でsnapshotし、BSD評価→IME優先→端末意味→cooked/操作。GUIはgshellが焦点の実行単位を確定しrawを一度評価、GUI編集か端末意味かを選択して既存WM停止口へ渡す。焦点世代が変われば破棄、GUI注入byteをbridgeで再解釈しない。IRQのlogical mods更新は状態の導出だけで文脈操作を発火しない。
- N3: 評価結果にCHAR_ETX (spcl=0の表セルがETX)/CHAR/BSD_SPECIAL/LEGACY_CONTROLの種別を持つ。台帳のRLDN等の旧制御コードはLEGACY_CONTROLで、低位byte=03でもVINTR判定に使わない。物理中断は表の通常文字評価が設定VINTRに一致するmake (既定03ではCHAR_ETX、または明示Cmd+.操作)、文字端末serial/LANだけ受信byteで判定。種別は旧ABIへ落とす前に利用し、旧getkeyはscを維持、shellは低位byteだけで再判定せず共通処理を使う。
- KAPI案 `kbd_policy_context_set(context,flags)` は呼出し実行単位の文脈をFG1で登録する口 (宣言を登録するownerと入力の宛先選択は別)。contextはLINE_EDIT/RUNNING/BYTE_INPUT/GUI、flagsはISIG/ICANON相当のみ。一般アプリの起動/execの宣言なし既定はRUNNING+ISIG。最初の常駐shellはkernelの起動経路が、ユーザーコードへ入る前にLINE_EDIT+ISIG/ICANONを設定する (x86/PPC共通、名前照合による特権ではない)。復帰時は親の文脈へ戻す。
- shell自身 (PPC sh.binとx86 SHELL_AS_APPを含む) がプロンプトに入る前にLINE_EDITを宣言し、poll/has_key/sys_yield待ちでも有効。組込コマンド開始直前に `kbd_policy_command_begin()` (新command_gen発行、RUNNINGへ)、終了に `kbd_policy_command_end()`、外部execは同じ切替をexec入口/出口が行う。
- `kbd_policy_context_set`単独ではcommand_genを進めない。BYTE_INPUTはアプリが明示宣言、通常byteをそのまま読む。GUIはgshell/焦点切替adapterが配送先として選択し、GUI内端末の実行単位はCUI文脈を登録する。どちらも「今CPUを使うslot」を宛先にしない。
- A0z前は内部contextエンジン/host fixtureのみを用意し、ユーザーshellには新KAPIを呼ばせない。新ETX/STOP中断を無効にして既存像policyを維持する。A0z後、shell宣言→exec結線→有効化の順に統合し、未対応shellにRUNNING既定の新中断を先行適用しない。

|BSD termios名|OS32の対応 (termios/TTY全体を実装する意味ではない)|
|---|---|
|VINTR / ISIG|CHAR_ETX (既定Ctrl+C=03)、ISIG有効のRUNNINGはINTERRUPT、LINE_EDITはCANCEL_LINE。無効ならbyte。MacのCmd+.も同じ中断意味へ|
|VKILL / ICANON|PC98のESC、MacのCtrl+Uを行編集側で解釈。Mac Ctrl+Uはカーソル前を削除し、bashの編集動作をTTY全行killと同一視しない|
|VEOF / ICANON|Mac EOT(04)、PC98 SUB(1a)+Enterを利用側でEOF相当へ。非canonical/BYTE_INPUTはbyte。入力内容と空行条件は編集側が判定|
|LINE_EDIT / RUNNING / BYTE_INPUT|既定は順にISIG=1,ICANON=1 / ISIG=1,ICANON=0 / ISIG=0,ICANON=0。RUNNINGのcanonical入力はアプリが明示指定。シェル編集と実行中断の配送先を追加区別する|

- **F3 PM 推奨 (ユーザーに提示済み、反論なし)**: PC98 RUNNINGはCtrl+C/裸STOPで実行中アプリを止める。ただしIME変換中は両方ともIMEへ先に渡し、取消の一打を中断へ再投入しない。MacはOS X TerminalのCtrl+C/Cmd+.をSIGINT相当へ。F3代案:裸STOPをIME専用の現行挙動に残す (DOS標準から外れるため非推奨)。
- IME本体が変換開始/終了時に内部 `kbd_policy_ime_state(active,owner,command_gen)` を公開し、IRQはそのsnapshotを読むだけ。PC98 profileでactive時のSTOP/ETXは取消pendingを専用保持し、FG1のIMEが消費。MacのVINTRは通常の中断経路を使い、旧OS XのIME条件の実証はQ4で記録する。辞書/学習処理をIRQから呼ばず、満杯でも取消を失わない。`kernel/ime/ime.c:289-291`のSTOP取消から接続する。
- 中断pendingは `{owner,exec_gen,command_gen,reason}`。CUIはIRQ採取時、GUIはWMが焦点宛先を決定した時に固定し、ring3/PPC協調口/待機解除で消費直前に一致確認。要求した文字は次のシェルへ残さず、一度だけ消費、repeatから再要求しない。
- N4は2種の印を分ける: (1) hwb/実行中断pendingはowner/exec_gen/command_gen不一致で捨て、command_beginで旧印破棄→新世代公開、end/exec終了で消去する。(2) display_restore_pendingは中断世代とは別の冪等な後始末。表示ownerと資源の世代を保持し、FG1のcommand_end/exec終了/STOP回収で `appslot_gfx_owner_exit` 相当を実行してから消す (`exec/exec_abort.inc:8-16`)。
- 表示復帰はcommand_gen不一致だけで捨てない。表示ownerの資源が既に回収済みなら完了扱い、新ownerの表示を解放しない。IRQでは実行せずpendingを保持し、次コマンド開始前の安全点で後始末完了を保証する。ls中断を次のhwbへ持ち越さず、描画中の組込STOPではテキスト画面へ戻す。
- 物理CTRL+STOPの既存救済とGUI STOP抑制は維持。V86中はOS側表の操作を配送せず状態追跡のみ、`drivers/input/kbd.c:389-394`の退出経路を保持。PPC非協調コードに中断期限は約束しない。
- 操作IDはNONE/INTERRUPT/CANCEL_LINE/EOF_INPUT/KILL_TO_BOL/KILL_TO_EOL/REDRAW/CLEAR_VIEW/COPY/CUT/PASTE/UNDO/REDO/SELECT_ALL/SWITCH_APP/CLOSE/BSD_SPECIAL。BSD特殊IDとは別名前空間、未実装受け手にはUNSUPPORTED、新機能を実装して埋めない。
- 旧getchar/getkey/peek slot/型は維持、ETXは全archで03。`kbd_policy_delivery_set(BYTE|ACTION)` と `kbd_policy_action_read(out,max)` はowner単位の選択式出口、通知 `{u32 action,command_gen,time_ms,value}`、BYTEとACTIONへ同じ入力を二重配送しない。切替は入力pending/composeをflushするがdisplay_restore_pendingは消さず後始末する。
- 既存 `userland/lib/input/libos32input.h:29-31,61-98,131-162` のinput_bind/INPUT_CTXはゲームのアクション/保存スロットで維持。本票のAPIは `kbd_policy_*`、共有ユーザー処理は同ディレクトリの別 `kbd_policy.c/.h`、INPUT_CTXを端末文脈に転用しない。
- gshellはrawから共有kbd_policy処理を一度呼び、操作が消費したmakeのbreakも通常配送へ漏らさない。焦点交代で押下解消/compose/保留操作を消去。EXCL_KBDではAV規則どおりWMへのCommand操作を止め、アプリが生入力を扱う。
- F9(a): UTF-8文字の全byte分を確認して全部または無しでリング投入 (欠落数を記録)。途中byteを制御文字へ再解釈しない。peekでは文脈操作を実行しない。
- F9(b): Mac-CUIのESCは0x1bを保持、行取消にしない。既存のescape列decoderに渡し、未認識の単独ESCは編集側で無作用。PC98のESC取消は保持。
- F9(c): 最初の常駐shellはEOT/SUBで終了しない。空行EOFはプロンプト再表示、非空行はその編集規則。EOF入力を要求したアプリのread終端とは区別する。
- F9(d): serial/LANのETXはCUI bridgeで採取した時の前景owner/gen/contextによりRUNNING+ISIGなら同じ中断pendingへ。GUI内端末のbyteは焦点宛先を持つ端末配送側で一度だけ解釈する。sc/in2を合成せず、協調poll以前の停止は保証しない。WMからのGUI注入は解釈済み配送として再判定を禁止。

## 4. 搬送表・API・メモリ

- BSD表の意味/値を保持し、IO_TIER §4にそろえたLE/16B header+16B rowの搬送外枠だけ追加。native structをcastしない。全offsetとBE/LE往復を検査。

|部分|byte offset:幅=値|
|---|---|
|header|0:4=`IKM1`, 4:2=version1, 6:2=row_size16, 8:4=count≤4096, 12:4=flags0|
|row|0:1=kind, 1:1=profile, 2:1=context, 3:1=flags0, 4:2=index, 6:2=selector, 8:4=value, 12:4=aux|

- profile=0は配列共通、1/2はPC98-CUI/GUI、3/4はMac-CUI/GUI。他platformのprofile行はset全体をINVAL、getも現在platformだけ。context=0共通/1 LINE_EDIT/2 RUNNING/3 BYTE_INPUT/4 GUI。
- kind1 META: index=0ならvalue=n_keys≤256、index=1ならvalue=n_accs≤15、両方selector/aux=0。kind2 CELL: index<256,selector=列0..7,value=u32 BSD値,aux=spclの当該列0/1。kind3 LOCK: index<256,value=flgs0..3,selector/aux=0。
- kind4 ACCENT: index<15,selector<52,value/aux=入力/出力byte (≤255)。selector=ffffはaccchar,value≤255,aux=0。kind1〜4はprofile/context=0。
- kind5 TERM: index=VINTR/VKILL/VEOFまたは既存編集制御ID、selector=0,value=byte,aux=0。文脈flagsはruntime宣言に属し、表は端末文字と意味の対応だけ。複数取消(PC98 ETX/ESC)は別編集IDで明示。
- kind6 EXT/COMPAT: index=sc<128,selector=SHIFT/CTRL/ALT/COMMANDの完全一致mask,value=操作ID,aux=0。Command/GUI/STOPと台帳の旧制御コード出口用。context=0,selector=ffffは全列共通のLEGACY_CONTROL指定 (value=旧byte,aux=0)、操作IDのEXTとは別指定で重複を拒否。台帳のセル変更はCELL行で表す。Caps/Num/Kanaはmaskから除く。TERM+EXT/COMPAT計≤128行。
- 不在行はOS既定で補完 (直前表へのpatchではない)。ただしsetにkind6が1行でもあれば、そのprofileのkind6は全部setの物で置き換え、未記載のkind6は補完しない。他profileおよび他kindの補完規則は変えない。既知BSD特殊値を保持、値域/重複(kind,profile,context,index,selector)/長さ/予約/overflow/不正Unicodeは全体拒否、旧有効表と保存表に無変更。BYTE accentの拡張はしない。
- `kbd_policy_set/get/reset`はFG1限定。setは入力を1行ずつ一度読み退避済み編成表を検証、短い排他で交換。getはneededを返し不足時コピーなし。resetはOS既定へ戻し、初回成功なら直前表を保存、既に保存済みなら終了時復元先を変更しない。
- F8予算: 編成表1個≤16KiB (key256×36=9216B、accent約1.6KiB、補助≤2KiB等)、active/保存/検証の3面≤48KiB、派生状態/操作ring≤8KiB、追加BSS≤56KiB・text目標≤16KiB。wire最大65552Bの丸ごと常駐コピーは作らない。
- 同じownerの最初のsetかresetの成功で直前表を保存 (保存契機はIO_TIER §4と同じ)、同ownerの以後のset/resetは保存を増やさず、終了/異常/STOPで復元。IO_TIER §4の入れ子保存とは意図的に異なり、本票では保存中の別ownerによるset/resetはBUSY (容量制約、認証ではない)。子は親から継承した有効表をそのまま使い、独自表必須なら起動処理でエラーを返す。子の失敗/終了で親の表や保存先を変更しない。子set/reset BUSY→子終了→親get不変→親終了で基底復元を試験する。常駐設定は起動時にロード。親設計§6の約7KiBに追加分を計上、両arch map/FD圧縮像で予算を超えたら公開前に再分割する。
- 本票のA0zは親INPUT_HAL_DESIGN §7のA0zに合流する (版上げは1回、親A0zの依存にM2〜M8を追加、U1の400行例外の範囲は親と同じ)。公開はこのA0zでkapi.json末尾追加/版上げ/生成/C89検査/両arch provider/clean→all。旧raw modsの既存bitはlogical、Commandは追加scから導出し未定義bitを無断流用しない。in2は物理値の契約を維持。

## 5. 既定の操作と今との差

|profile|基準/既定|今との差|
|---|---|---|
|PC98 CUI (DOS)|jp.pc98基底+§2a案Bの全差分。RUNNINGのCtrl+C/STOPは中断、IME変換中はIME取消優先。編集中ETX/ESC/STOPは行取消。SUB+EnterはEOF、画面消去はCLS|裸STOPは従来cookedのIME取消キー (`ime.c:289`) から、非変換RUNNINGでは中断へ。Ctrl+Cも実行停止/行取消へ変わる。CTRL+STOPは維持|
|Mac CUI (OS X)|USB JIS/US基底、Ctrl+C=03/Cmd+.はSIGINT相当、編集中Ctrl+Cは取消、ESC取消なし。Ctrl+DはEOF、Ctrl+U/Kは編集、Ctrl+L再描画、Cmd+K表示消去|`platform/powermac/input_keymap.c:33`のCtrl+C→27を廃止。Apple鍵盤に専用Break/Pauseを要求しない|
|PC98 GUI (Windows)|Ctrl+X/C/V/Z/Y/A、GRPH+Tab/F4は対応操作通知|Ctrl+Cはコピーであって通常中断ではない。既存救済は残す。未提供操作の実装追加はしない|
|Mac GUI (OS X)|Cmd+X/C/V/Z/A、Shift+Cmd+Z、Cmd+Tab/Wは対応操作通知|CommandをControl/GRPHと区別、独占中のWMショートカットは配送しない|

- N2はユーザー決定 2026-11-15「文字は BSD、操作は DOS」として案Bで確定。F3・F4は **PM 推奨 (ユーザーに提示済み、反論なし)**。Ctrl+数字/記号が旧byte利用側で取消/移動等として働く差も「文字は BSD」の意図した差として受け入れる。U3の文字値=03は決定済み、操作のplatform差を共通化で消さない。


## 6. 段・依存・受入

- 各小段は製品1機能1ファイル+宣言/結線1ファイル+試験1ファイル、追加/削除合計≤400行。nは同一機能を小分けした直列段。T名はtools/tests内の専用host試験、登録Bは直前の独立段。A0zだけ既承認U1例外。

|ID|担当ファイル (新規は案) / 差分上限|前提と出口|
|---|---|---|
|M0[n]|tools/tests/input_pc98_compat_host.c、固定期待fixture、CHECK実行器 /380|台帳全128行×8列×Caps/Num/Kana。旧表版を固定した独立literal期待と照合し、製品表から期待を生成しない。意図差は台帳のB欄とB1のbyte利用側の動作を固定fixtureに載せる (Ctrl+3のESC取消、Ctrl+4/5/6/7のfiler右/左/上/下、Ctrl+8・/の7F等)。114はcooked entryなし、その他の非修飾nopはentryありも期待に含める|
|M1[n]|tools/kbd_import.py、出典/差分台帳1個、T_import /380|実物→.kbd変換、合流/DROP/全usage照合、基底表は1表ずつ別小段|
|M2[n]|input/hal/legacy_keymap.c、内部kbd_policy.h、T_keymap /380|M0/M1、BSD8列/AltGr/特殊値、由来種別と台帳の出口。親3b評価器部分を置換|
|M3[n]|input/hal/keymap_state.c、kbd_merge.c、T_mods /380|M2/親2b、修飾/lock派生、物理緊急判定のkbd.c接続は別M3小段|
|X-cooked[n]|drivers/input/kbd.c、T_keymap、必要な共通宣言1個 /380|親3a全段/3c全段・M2/M3/M0後、x86旧scancode_to_ascii使用箇所を新評価器へ置換。試験は現kbd_deliver→旧cooked readまで通す。統合途中は旧policyを選び、確定台帳の反映/全結線後に新既定へ切替|
|M4[n]|input/hal/input_policy.c、内部宣言、T_policy /380|M2、wire検証/交換/復元。未結線で準備|
|M5[n]|input/hal/input_context.c、内部宣言、T_context /350|文脈/command_gen、ABI前は内部試験のみ|
|M6[n]|input/hal/input_interrupt.c、exec/exec_abort.inc、T_interrupt /380|M5、hwb世代照合と独立display後始末。exec境界の接続は1ファイルずつ別M6小段|
|M7[n]|input/hal/input_ime_gate.c、kernel/ime/ime.c、T_ime /350|M5/M6、IME snapshot/取消pending|
|M8[n]|drivers/usb/hid_kbd_map.c、内部sc宣言、T_usb /300|M1/M3、Command sc監査/追加、全usage照合の期待を同時更新|
|A0準備/A0z|公開宣言/layout試験を各≤350、親§7のA0zへ合流。原子的kapi.json/生成/版/provider切替の400行例外は親U1と同じ|親A0zの既存依存にM2〜M8を追加、版上げは1回。新中断の有効化はまだしない|
|U1[n]|userland/lib/input/kbd_policy.c/.h、T_user /380|A0z、既存input_bindと別の操作/文脈ラッパー|
|U2-x0 / U2-p0|x86: init/kernel.c / PPC: platform/powermac/shell_boot.c、各T_shell /≤350|A0z/U1/M5後、各arch別小段で初期shellのLINE_EDIT接続を先行。起動箇所は下記file:lineで固定|
|U2[n]|userland/shell/ui.c、T_shell、内部宣言1個 /350|U2-x0/U2-p0後、両archのLINE_EDIT/command_begin/end、EOF/ESC|
|E-x[n]/E-p[n]|x86: arch/x86/input_interrupt.c (新) / PPC: arch/ppc/input_interrupt.c (新)、共通exec/exec_abort.inc、各T_exec /≤380|A0z/U2/M6/M7、archごとのadapterはこの1ファイルに固定し別段。shell宣言済みで通常中断を有効化|
|U3g[n]|userland/gshell/src/input.rs、T_gui、ユーザー側結線1個 /380|U1/M3/M8/A0z、GUI操作・独占・make/break消費|
|L[n]|kernel/input_legacy.c、T_bridge、像別provider1個 /350|M5/M6/E-x/E-p、serial/LAN/注入の一回だけの端末解釈|
|B[n]|§6完了条件のbuild/fixtureを1〜3ファイルずつ /≤350|order/selection/fixture/PPC一覧を全て追従してから当該部品を受入。B単独一段に4ファイルを詰めない|
|4b-map[n]|platform/powermac/input_keymap.c、ppc_input_usb_host.c、ver-ppc-input-usb.py /350|下記依存全完了、27期待廃止/旧ファイル削除、build除去は直前B|

- B6の現行起動経路 (int-1115k、main a822e11c): x86は `init/kernel.c:407-417` でGUI/CUIを決め、`:450-451`のexec_runでshellを起動するため、U2-x0の接続ファイルはinit/kernel.c。PowerPCは `platform/powermac/shell_boot.c:29-36` のpm_shell_bootからexec_run("/bin/sh.bin")を呼ぶため、U2-p0はplatform/powermac/shell_boot.c。各々、CUI初期shellのユーザーコードへ入る前にLINE_EDITを引き渡す。E-x/E-pは上表の新規adapterから共通exec/exec_abort.incの要求/待機解除へ結線する。

|親の段|書き換える依存/責務|
|---|---|
|3a[n]|旧ring抽出を維持、全段完了→X-cooked。文字変換の重複を置かない|
|3b[n]|M0/M1→M2→M3→X-cookedへ置換。評価器準備とx86結線の完了を区別|
|3c[n]|mode/STOP/flush抽出を3a後に先行し、旧変換で受入可能にする。M2/X-cookedへの逆依存を置かず、X-cookedは3c全段後|
|4b|従来「4a/3b後」を「4a/M2/M3後」へ変更、PPC旧policyは維持。4b-mapでX-cookedを含む全結線条件を満たして有効化|

- B完了条件 (ROLES §4-3): 新ソースは `build/sources/order.mk` (順)、`build/sources/common.mk`またはarch/platform一覧 (選択)、`tools/tests/fixtures/build_profile_x86.json`のC_KERNEL、PPC像に入る場合 `build/ppc-kboot.mk`一覧へ追従。`check-build-selection-host`・`make all`で確認。
- A0z完了条件 (同§4-3): 版2か所 (kapi.json・生成os32_kapi_shared.h)、README/INDEX/KAPI_SPEC、レビュー済指紋 `tools/tests/fixtures/dep_dir_a/l01.json` kapi_sha256・l04.json api_sha256・l05a.json public_sha256、`tools/tests/hwdev_contract_fixture.py`のHWDEV layout CRC/inventory、`tools/tests/test_ime_display_boundary.py`のFEP_MODULE_ABI_GENを追従。`check-dep-dir-a-l01`〜`l06`・`check-hwdev-image-host`・`check-ime-display-boundary-host`・KAPI audit/ranges/version、`make clean && make all`。
- 共通箇所の完了条件 (同§4-3): PPC像リンク、手元確認は `PATH=/home/hight/opt/cross-ppc/bin:$PATH make -f build/ppc-kboot.mk KBOOT_SH=1`。新しい製品関数呼出しは兄弟stubを持つ全試験土台 (#include型/python切出し型) をrgで確認して追従。
- 試験の完了条件 (同§4-3): PPC toolchain/QEMU/socket不在は理由付きSKIP・rc=0、OS32_REQUIRE_PPC=1では失敗、`PPC_CROSS_DIR=/nonexistent make check-<名前>`で確認。CHECK covers/basisまたはkind/env_limit/pinsと変異anchorを追従、`tools/checks/run.py --lint`と `python3 ~/os32-tmp/bin/stale_anchor.py <基点>`。一覧/指紋/生成値は手計算せず生成器/試験出力を基点と比較し、意図した変更だけか確認。
- F10: 4b-mapは親4b・M0〜M8・X-cooked・A0z・U1・U2・E-x・E-p・U3g・Lと対応B完了後。CHECK pinsは `[input-hal/4b-map,input-bind/U3-03,input-bind/F3,input-bind/F4,input-bind/N2-B]`、basisは本票§2/§2a/§3/§5の確定版。旧単独`U3`を残さず、N2のユーザー決定とF3/F4のPM推奨 (ユーザーに提示済み、反論なし) を反映して着手する。
- host/変異: 列の順/spcl逆向き/lock OR→XOR/AltGr欠落、Caps→Ctrlのcooked/raw整合と物理救済、左右合流/usage差分欠落、UTF-8部分投入、peek副作用を各固有anchorでruntime RED。同じ入力の正常対照は最後の製品変更後に必ずPASS、変異だけREDでも受入しない。
- F1/F2/F3/N1/N3/N4/N11試験: 両arch初期shellのLINE_EDIT宣言前とpoll待ちCtrl+Cでshell生存、GUI前景editorで裏BがRUNNING中もCtrl+Cはコピーのみ・B非中断、RLDNの03は中断せず真のCtrl+Cは中断、ls中断→終了→hwb開始で保留ゼロ、描画中組込STOP→command_endで表示owner解放・テキスト復帰・二度cleanup冪等、同owner新command_gen/owner再利用/IRQ境界/二度take、IME中STOP/ETX取消のみ→次の打鍵は中断、満杯pending/押しっぱなしを検証。世代照合/開始時clear/IME優先を外す変異はRED。
- wire/出口試験: 他platform拒否と旧表保持、短いget、owner終了復元/BUSY、resetだけのアプリの終了で常駐表に戻る、kind6のprofile単位全置換で旧COMPATが残らない、BE/LE、root shell EOF、Mac ESC、serial/LAN ETX、in2不変/GUIコピー非中断。新CHECKはinputs/pins/basisを宣言。
- NP21/Wは本体/USB/IME/CTRL+STOP/V86、QEMU PPCはpoll shell/実行中断/Command/抜去、各段は対象buildと両archリンク・触った試験--mutate・lint。ROLES §4-3のmake all/clean→allはPM統合/A0zの完了条件、NP21/Wはmain取り込み前の結線節目でまとめて行う。全体/全変異はCI。設計のみの今回はpreflight修復/build/実機試験を実行しない。

## 7. 残る確認と担当

- N2はユーザー決定 2026-11-15「文字は BSD、操作は DOS」により確定。F3・F4は **PM 推奨 (ユーザーに提示済み、反論なし)**。新規のユーザー判断事項はなし。標準OSの実証は次の4件、Q1/Q2を再び未決へ戻さない。
- Q3: PC98担当PMが対象NEC版MS-DOSの版/COMMAND.COM・メーカー説明書と実機/NP21/WのFEP有無でSTOP/Ctrl+C/ESC/EOF/CLSを確認し、IME優先の条件を記録。
- Q4: PowerPC担当PMが対象OS Xの版/Terminal設定/bashを固定し、プロンプトと実行中、IME有無、Ctrl+C/Cmd+./D/L/U/K/ESCを実機で照合 (現行Apple資料だけで旧版を断定しない)。
- Q5: PC98担当PMが対象Windows 9x/2000の版・標準アプリとメーカーHelpでCtrl-X/C/V/Z/Y/A、GRPH+Tab/F4を確認。アプリ差は表の差分として残す。
- Q6: PowerPC担当PMが対象Apple USB鍵盤の型番/メーカー配置図/HID descriptorとusageログでCommand左右・Break/Pause不在を確認。専用Break非依存の方針は維持。

## M5 の小段の割り方 (実装者の案)

|小段|責務と内部受入|
|---|---|
|M5[1]|単一実行単位の文脈値の初期化・明示設定。既定 RUNNING+ISIG、設定は command_gen を変えない。未結線|
|M5[2]|command_begin/end の世代発行と文脈復帰、旧中断印の破棄順序を M6 と接続|
|M5[3]|owner/exec_gen ごとの登録・親への復帰と、前景宛先の IRQ snapshot。GUI 宛先は WM が決定|

各小段は内部試験のみ。実像への登録 B と A0z 後の各呼出し元の結線は別段とする。世代の幅・周回と owner 登録容量は後段の設計で確定する。
