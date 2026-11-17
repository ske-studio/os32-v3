# 入力 HAL 追補 2 確定版 — keysym の共通口と arch の backend

> 状態: **確定 (2026-11-16)** — astra 起草 r1〜r3、Opus 5.5 の独立レビュー 2 回 (r1 要修正 P1 2 件・P2 8 件、r2 要修正 P2 3 件)、PM 決定を反映。親は INPUT_BIND_DESIGN と INPUT_HAL_DESIGN。票は TASK_INPUT_HAL。PowerPC PM のレビューは後で追記する。
> ユーザー決定 (2026-11-16): 表の型は NetBSD wscons の keysym・x86 の USB 鍵盤は PowerMac と HID の表を共有・Apple JIS の英数で IME を切りかなで入れる (Mac OS X と同じ)。番号は FreeBSD ukbd.c で 0x90 = かな・0x91 = 英数 (決定時の文の番号が逆だったので意図どおりに訂正)。
> PM 決定 (2026-11-16): Apple の判定・variant は作らず、全 HID の基底表に LANG1 (0x90) = かな・LANG2 (0x91) = 英数。Clear・NumLock は keydata を積まない。exact-cell で FreeBSD の 8 列を保つ。評価器はカーネルの 1 本 (WM は評価しない)。IKM1 は v2 だけで定義し版上げは A0z で 1 回。


- 日付: 2026-11-16。REVIEW.r1 R1〜R13を維持し、REVIEW.r2全6件・PM決定を反映。今日のユーザー決定を優先し、既存の確定文書を改訂するための票。 単一タスク・協調切替・整数演算。認証/許可リストを追加しない。旧 keydata・IME・shell の契約を維持。
- 出典略記: B=`docs/decisions/INPUT_BIND_DESIGN.md`、H=`docs/decisions/INPUT_HAL_DESIGN.md`。
- R=`/home/hight/os32-tmp/ref/freebsd-kbd/`。NetBSD の行番号と型は指定の `PPC_SURVEY.md` の調査記録による（今回原本再取得なし）。 既存コードは指定箇所の行検索で確認。設計のみのため preflight 修復・build・host/実機試験は行っていない。

## 1. 実物の確認

|ID|現物・確定の行|今日の決定に対して改める点|
|---|---|---|
|D1|H:149、B:49|PC-98 sc を共通表の添字に固定 → 表は backend 固有 keycode、sc は旧出口の互換属性|
|D2|H:184、B:234（M2）|PC-98/JIS 文字化1本・FreeBSD8列本体 → keysym 評価器1本と PC98/HID の別表|
|D3|B:55-65|照合前の左右/Enter/記号の合流、AT→PC98照合 → usage を保って評価し、旧rawだけ合流|
|D4|B:61,63、`drivers/usb/hid_kbd_map.c`|0x90/91 DROP、Command欠落 → 英数/かな操作と独立Command修飾|
|D5|`input/hal/kbd_merge.c`|自前の2表と小文字だけCtrl化 → backend の keysym と共通評価|
|D6|`platform/powermac/usb_platform.c`、`input_usb.c` の `enqueue()`|sc化後の独自押下/文字ring → usage のまま共通 capture|
|D7|`platform/powermac/input_keymap.c` の `pm_input_keymap()`|Ctrl+C=27、通常文字の上位scなし → 03 と全キーの互換sc|
|D8|`platform/powermac/input_boot.c`|raw/pressed/modifiers の空実装 → 共通状態のprovider|

- 食い違いは上記8件（同じ原因の複数行は1件）。調査票の `ime.c:286` は現物では STOP 判定が289行。 x86 は `drivers/input/kbd.c` → `drivers/input/kbd_hal.c` の `kbd_hal_capture()` → `input/hal/kbd_merge.c` の `input_merge_key()`。
- PACK の `input/hal/kbd_hal.c` は存在せず、実物は `drivers/input/kbd_hal.c`。これは参照パスの訂正で件数に含めない。
- PPC の LAN先行・USB後続は `input_boot.c` の `peek()` に既にある。これを壊さず USB の読み先だけ替える。 x86 uPD720101 の入力経路は未実装（調査票）。今回その完成を前提にせず、同じ HID 表を登録できる口を先に作る。

## 2. 共通の口

- NetBSD `wsksymvar.h:39,42-46` の u16 と5セルを採る。文字コード・修飾・機能は keysym、物理番号は別型。 以下は OS32 内部宣言案。公開 SDK 型ではない。`STATIC_ASSERT(sizeof(KmEntry)==10)` を置く。

```c
typedef u16 keysym_t;
typedef struct {
    keysym_t command;
    keysym_t group1[2];
    keysym_t group2[2];
} KmEntry;
typedef struct {
    u8 src, down, repeat;
    u16 generation;             /* in2_int.h:11,49 と同じ幅 */
    u16 keycode;                 /* PC98: 0..127, HID: usage 0..255 */
} KmKey;
typedef struct {
    u16 id, count;               /* id=1 PC98, id=2 HID */
    const KmEntry *entries;
    const u8 *compat_sc;         /* 0xff means no legacy projection */
    const u8 *norepeat;          /* HID: 256 bits, generated with entries */
    const void *extensions;     /* typed descriptor: PC98 cell overrides etc. */
} KmBackend;
int km_backend_register(const KmBackend *map); /* FG0/FG1, duplicate id rejects */
int km_source_bind(u8 src, u16 generation, u16 map_id);
int input_capture_key(const KmKey *key);       /* validates generation/range */
```

- `input_capture_key` は新しい内部入口。既存 `input_capture` の KEY 処理へ共通状態更新を集約し、旧128番号の構造体に usage を押し込まない。
- 各 arch の登録関数は1個。PC98は map1+map2、PowerMacは map2。共通層で arch を条件分岐しない。 backend descriptor は静的寿命。sourceごとの map_id は登録後固定、変更は release→再bind。表交換とは区別する。
- 登録失敗時は未登録源から入力を配らずエラーを返す。capture は有限処理、割当/待機/辞書/USB転送を禁止。 keysym の数値は NetBSD 定義を採り、独自の数値再割当をしない。取り込む定義・表には出典版/hash/許諾を同梱。

|処理順|評価器の責務|
|---|---|
|1 物理追跡|`held[src][keycode]` を唯一の状態とし、make/break/repeat を区別。物理modsと互換sc集計を更新|
|2 修飾確定|新make時に選んだ修飾作用をキー単位に保存、break/releaseで同じ作用を解放。repeatでlockを反転しない|
|3 合成|全源のlogical Shift/Ctrl/Alt/Mode_switch/CommandをOR。USB共有lockと本体の機械位置をOR|
|4 group/level|Mode_switch/AltGrでgroup2、通常group1。Shiftでlevel1。group2省略はimport時にgroup1を複写|
|5 Caps/Num|通常wscons行は英字だけCaps反転、数字/記号は不変。HID源はNumLockを常時ON扱いとし、Shift併用でもテンキー数字側を選ぶ|
|6 Ctrl|Plain文字だけBSDの制御文字規則を適用。PC98 exact-cell は変換済みなので二重Ctrl化しない|
|7 操作と出口|commandは操作policyへ、文字keysymは旧出口へ。CommandをCtrl/Altへ合流しない|

- command は wscons のコマンド欄として保持し、Command修飾そのものと混同しない。未提供コマンドは記録可能だが実行しない。 Ctrlの基本規則: `@.._` と `a..z` は `&0x1f`、Space/2→NUL、3..7→ESC..US、8→DEL。それ以外は元のPlain値。
- Caps は選択した英字の大小だけ反転。Shift+Caps と Ctrl+Shift+C の fixture を独立に固定する。 HID Alt/Option（E2/E6）は文字を変えない（現行維持）。Metaの高位bit付加をしない。AltGr/Mode_switchとは区別する。Command（Meta、E3/E7）も文字不変・高位bit付加なし、操作用の独立修飾としてのみ扱う。
- Keypad group（KS_KP_*）は数字側選択後に `sym & 0x7f` でbyte化。Apple Clear/NumLock（usage53）はkeydata entry自体を積まない・lock切替なし（常時ON）。 Plain 0x80〜0xffはbyteへ出さない（全backend共通）。shell ui.cのIME UTF-8誤認を防ぐ。dead/composeの高位byte結果にも同じ出口制限を掛ける。
- PC98変換の exact-cell 拡張は§3。一般Ctrl規則と衝突する Ctrl+2=SUB 等を保存するため、必要な明示差分である。 同一scへ写るHID左右キーも別々に評価する。旧raw/pressedだけ互換scの参照数を使い、最後の解放でbreakを出す。
- sourceをまたぐShift+Aは合成modsで評価。同じscでも表の行同一性は要求しない。旧rawの情報量不足は§5の意味イベントで補う。
- 物理Ctrl/GRPH/STOP/DELの緊急判定はlogical remapから独立。in2は従来の互換物理sc/mods契約を維持する。 V86中は状態だけ追跡、OSの文字/操作/in2配送を抑止。GUIではrawと意味イベントを出し、CUI cookedは出さない。
- releaseは最大256 keycodeを走査、各作用を解放して源を返す。抜去前callback停止・generation照合は親契約を維持。

## 3. arch の backend

|表/登録|内容・生成・所有者|
|---|---|
|PC98 map1 / 本線|128キー。R `share/syscons/keymaps/jp.pc98.kbd`→keysym、B§2a案BのDOS操作差分を生成時に適用|
|HID map2 / 共通|256 usage。NetBSD `hidkbdmap.c:162-184,949` のUS基底+JP差分をhostで展開、PPC/x86で同じ生成物|
|LANG1/LANG2 / HID基底表|全HID鍵盤に90=かな/IME_ON/sc72、91=英数/IME_OFF/sc51を定義。Apple判定・variantなし（PM決定）|
|PC98登録 / 本線|本体source→map1、将来のUSB source→map2。USBモジュール不在でもmap2登録可能、装置存在とは別|
|PowerMac登録 / os32powerpc|USB source→map2。自前文字表を持たない|

- M1の `.kbd` parser/出典台帳を再利用するが、出力を8列実行表から `KmEntry` と必要な拡張へ替える。 FreeBSD8列×AltGr bankはwsconsの4文字セルに一般には収まらない。Rの002行（file:8）はCtrlでSUB、Alt+CtrlでNUL。
- jp.pc98.kbdにはAltGr bankがない。基本group1は普通/Shift、group2はgroup1を参照。exact-cellはAlt/Ctrlを含む8列を保存する（PM承認）。 exact-cellはPC98専用、既定128×1 bank（bank0）×column8。各セルは `{u16 sym; u8 kind; u8 op;}`（4B）、flgsはキーごと1B。
- 拡張bankは任意。未指定時はbank0へフォールバックし複製領域なし。ユーザー表でbank1を指定した時だけ確保、未記載セルはbank0を参照。 通常セルのkind=CHAR、symはkeysym。BSD特殊語は対応keysymへ変換し、対応なしはkind=BSD_SPECIAL、sym=KS_voidSymbol、op=元ID。
- Caps/NumのC/N/Bは列選択後に条件をORし、一度だけShift列をxor。AltGr/AltLockはbank選択。これも共通評価器のデータ指定モード。 提供/無視/拒否はB§1を保持、accent/特殊値の由来を消さない。HIDはこのPC98互換モードを使わない。
- 114/Kanaは既定で無文字・物理Kana派生、cookedなし。XFER/NFERはHenkan/Muhenkan、STOPは取消用途、旧DOS byteは出口差分。 `.kbd` 入力時の全セル→変換後の結果を照合する。左右同一化/DROPを文字表生成の前提にする旧M1規則は廃止。 NetBSD継承は `wskbdutil.c:417-462`（調査票）に従うhost展開。実行時に継承木を辿らない。
- HID 00..03は無入力/エラーとして拒否、未対応usageは明示台帳に残す。compat_sc=ffでも有効keysymは捨てない。 全HIDの90はHiragana_Katakana+IME_ON、91はMuhenkan+IME_OFFを基底表に持つ。操作IDはkeysymとは別のpolicy情報として付ける。
- 番号の根拠は R `sys/dev/usb/input/ukbd.c:308-310`（0x90 Kana、0x91 Eisu）。PACKの番号だけをPM決定に従って訂正。PM指定のLANG1/LANG2用途を全HIDへ採用し、Windows JISの88/8A/8Bとは別usageなので衝突しない。 英数/かなはmakeで絶対指定、repeat/breakで再実行しない。物理Kana lockとは別で、USB lockを反転しない。
- FG1のIME adapterは目標状態が異なるときだけ既存 `ime_toggle()` を使う（`ime.c:120-150`）。OFF時の未確定確定を保持。 ON時の辞書失敗は現行どおりOFFに留まり、成功したと偽らない。二度押しは無作用。owner/gen付き順序イベントとして処理する。

|HID usage等|互換scと注意|
|---|---|
|E0/E4、E1/E5、E2/E6|74、70、73。左右はheldでは別、旧raw breakだけ参照数集計|
|E3/E7|Commandは独立logical bit。互換scは既存使用を監査して2個予約、監査完了まで新rawを有効化しない|
|8A、8B、88|35 XFER、51 NFER、72 KANA（現 `hid_kbd_map.c` の `hid_kbd_sc()`）|
|90、91|72、51。意味イベントでIME_ON/OFFを区別、90を物理Kana lockへ誤認しない|
|48 Pause、矢印等|60 STOP、3A/3B/3C/3D等。AppleにPauseの存在は要求しない|
|写像のない有効キー|raw/in2互換投影なし、意味イベントあり。旧keydataだけが必要な文字はsc=0でbyteを返す|

## 4. 出口の写し

```c
/* GUI raw keeps break/modifier delivery; evaluation is kernel-owned. */
u8 sc = result.compat_sc, byte = 0;
if (kbd_gui_mode) {
    if (sc != 0xff && legacy_stop_filter(sc, !key.down, result.logical_mods)) return;
    if (sc != 0xff && result.raw_due) legacy_raw_push(sc, !key.down, result.logical_mods);
    policy_key_push_evaluated(&result); /* no cooked; WM never evaluates */
    return;
}
if (result.consumed || !key.down || result.is_modifier || result.no_keydata) return;
if (sc == KEY_STOP && input_key_pressed(KEY_CTRL)) return;
if (result.kind == CHAR && is_plain(result.sym) && result.sym < 0x80)
    byte = (u8)result.sym;       /* Ctrl/exact-cell already evaluated */
if (result.kind == CHAR && is_keypad(result.sym)) byte = result.sym & 0x7f;
if (result.kind == LEGACY_CONTROL) byte = result.legacy_byte;
if (sc == 0xff) sc = 0;
if (sc == 0 && byte == 0) return;
legacy_cooked_push(((u16)sc << 8) | byte);
```

- is_modifier の定義 (PM 決定 2026-11-17): effect!=0、または CHAR で 0xf101〜0xf111、または互換 sc 70〜74。日本語キー 0xf112〜0xf116 は修飾ではない。
- no_keydataはHID usage53のClear/NumLockで真、再配置後もkeydataを積まない（PM決定）。IME_ON/OFFの操作配送はこの文字抑止より先に行う。
- compat_sc=ffは出口直前に0へ置換。scもbyteも0なら積まない（F13 usage68等）。非修飾nop/NULは有効な非0 scがあればentryを残す。 raw_dueは互換scのmake/最後のbreak/repeatの既存集計規則。GUIのSTOP filterをraw/意味イベントの前に適用し、二重配送しない。
- `drivers/input/kbd.c` の救済判定も `kbd_shift_state & SHIFT_CTRL` から `input_key_pressed(KEY_CTRL)` へ変更。物理Ctrl+STOPはcookedへ積まない。
- 日本語/機能keysymの基本出口はbyte0+互換sc。Plain>=80は無文字（非byte確定列は既存IME/UTF-8配送経路）。 B§2aで確定済みの矢印等DOS byteはLEGACY_CONTROL例外として維持。RLDN=03をCHAR_ETXとして再解釈しない。
- x86/PPCともSTOP=6000、XFER=3500、NFER=5100。STOPは `ime.c:289`、XFERは調査票の `ime_convert.inc` と一致。
- UPは上位3A、LEFTは3B、HOMEは3E、DELは39。`userland/shell/ui.c` の比較はPPCでも成立する。 Ctrl+Cは互換sc2B/byte03。英数/かなはFG1で消費する操作であり、XFERへ化けさせてIME切替を代用しない。
- 操作policy→IME優先→旧出口の順とし、PC98変換中のSTOP/ETX取消を通常中断へ二重投入しない（B§3）。 GUIはカーネル評価済みの意味イベントをWMが一度配送。従来rawのscから別表を再評価して文字を作る経路を残さない。 serial/LAN/GUI文字注入は従来のsc0経路で、物理source/usageを捏造しない。peekでは操作を実行しない。

## 5. IKM1・KAPI・profile

- IKM1は未実装・未公開（レビュー確認）。本票でmagic `IKM1`・LE16B header/row・version=2だけを定義する。v1互換処理/変換器は作らない。 row外枠はB§4どおり。mapをprofileへ混ぜず、kind1〜4のcontext欄をmap_idに再定義（v2のみ）。profileは0。

|kind|v2の行内容（未使用/予約は0）|
|---|---|
|1 META|context=map_id、index=0はキー数、1はaccent数。selector/aux=0|
|2 KEYSYM|context=map_id、index=keycode、selector=0 command/1,2 group1/3,4 group2、value=u16 keysym、aux=0|
|3 EXACT|context=1、index=keycode、selector=bank*8+column（bank=0/任意1）、value=keysym、aux=kind低8bit+op次8bit|
|4 ACCENT|context=1、B§4のbyte accent行。selector=ffffはaccchar|
|5 TERM|旧定義維持、profile=1/2 PC98、3/4 Mac、context=端末文脈|
|6 EXT/COMPAT|旧定義維持、操作profileと互換scで指定。LEGACY_CONTROLはselector=ffff|
|7 FLAGS|context=1、index=keycode、value=C/N/B/Oの0..3。exact-cell選択用|
|8 KEY_ACTION|profile=各操作profile、context=文脈、index=keycode、selector=map_id、value=IME_ON/OFF等、aux=修飾完全一致mask|

- 欠落行はOS既定から補完、直前表へのpatchにしない。kind6のprofile単位全置換、owner保存/reset/終了復元/BUSYはB§4を維持。
- map1はPC98だけ、map2は両archが受理。他arch profileは全体拒否。x86 USBでも操作profileはPC98の1/2である。 setは押下中BUSY、安定した機械lock位置だけは許可。snapshot検証後短い排他でactive交換、全体失敗は旧表/保存表を不変。
- 既定exact1024行+2表5セル1920行=2944行。任意bank付き最大3968行にaccent/操作を加えるためcount上限8192、最大wire131088B。
- wireを丸ごと常駐コピーせず1行ずつ退避編成。重複/範囲/長さ積算/予約/BE-LEを検査し、native struct cast禁止。 旧getkey/raw/in2のslot・型は不変。内部keysym化だけならABI変更なし。新policy APIと下記意味イベント公開は親A0zで末尾追加・版上げ1回。
- GUI用 `kbd_policy_key_read(out,max)` を追加。評価器/lock状態はカーネル1本、RustのWMとuserlandラッパーに評価器を作らない。
- 評価済みイベントはu32×12（48B）: src_generation、map_id、keycode、sc_down_repeat、physical_mods、logical_mods、time_ms、map_generation、keysym、kind、action、value。
- src_generationはsrc低8bit/generation次16bit、sc_down_repeatはcompat_sc低8bit/down bit8/repeat bit9/no_keydata bit10/is_modifier bit11（WMもkeydata抑止に使用）、map_idは低16bit（上位16bit予約0、variant欄なし）。keysymは低16bit、kind/actionは固定ID、valueはlegacy byte/BSD op等のkind別値。余剰bitは0。
- WMは採取時の評価済みkeysym/kind/compat_scを配送し、焦点文脈の操作を一度処理。raw再評価と二重配送を禁止。表交換/焦点交代でpendingをflush。
- 48B layout・u16世代・全offsetをC89/BEで検査、A0zで両arch providerとSDKラッパーを生成。公開ABI版はA0zで1回だけ上げる。 公開宣言/指紋/版の手編集は禁止。B§6のA0z完了条件とclean→all、親H§7の原子的切替をそのまま適用。

## 6. 部品の大きさ（上限案、実測ではない）

|部分|計算/上限|像|
|---|---|---|
|基本keysym表|PC98 128×10=1280B、HID 256×10=2560B|x86両方、PPCはHIDのみ|
|互換sc/metadata|384B+norepeat32B+送出順2bit×256=64B、計480B（補助込み≤512B）|HID分は両像で同一、LANG1/2は基底表の内数|
|PC98 exact/flags|既定128×8×4+128=4224B、任意bank追加4096B（最大8320B）|x86のみ。既定はr1比4KiB削減、PPCへリンクしない|
|編成表1面|基本表+拡張+accent約1.6KiB+操作等、既定目標20KiB/任意bank込み上限24KiB|省略bankは未確保、最大容量契約は共通|
|active/保存/検証|既定3面≤60KiB、任意bank込み≤72KiB、初期const表は共有|Bの48KiB枠を更新|
|状態追加|held16×256bit=512B、保存作用16×256×2=8192B、互換cnt128×u16=256B、意味ring32×48=1536B|旧held等の削減前で約10.5KiB|
|その他/総増分枠|源/世代/操作等込み既定目標76KiB/最大追加BSS≤88KiB、text目標≤20KiB|Bの56KiB/16KiB枠を置換、親約7KiBと重複控除|

- stateの16源は鍵盤だけに別枠を増やさず親の共通source枠。全同時heldでもu16互換cntは溢れない。 MINIMAL/起動FDも互換口を持つ。USB controller本体は従来の像選択、PPCはHID常駐。kdebug増分0を維持。 各統合段でmap/size/FD圧縮前後を採取。上限超過なら公開前に疎なexact表・保存差分へ再分割、収まったとは推測しない。

## 7. ファイルと段（新規パスは提案）

- 1行=製品1機能1ファイルの1段・1実装者。宣言H、試験T、build Bも別段。ただしS2uは既存host試験1ファイルの期待更新を同じ段に含める（レビュー指定）。各差分≤400行、超過は同ファイルを直列n分割。 本線は共通口/評価器/PC98/出口/1E、os32powerpcはHID表（S1h/S1ht）・S2h/S2u/S2a・PowerMac結線。今回はagent/実装を起動しない。

|段|担当ファイル|責務・依存|
|---|---|---|
|H0|`input/hal/keymap.h` 新|型/登録/結果/拡張の内部契約を先に固定|
|H-usb|`include/usb_platform.h` 変|os32powerpc、新 `usbp_input_usage(u8 src,u8 usage,int down)` / `usbp_input_usage_repeat(u8 src,u8 usage)` を宣言、旧sc口はS2mまで残す|
|H-hid|`drivers/usb/hid_boot.h` 変|os32powerpc、hid_kbd_sc宣言を維持し `hid_kbd_norepeat(u8 usage)` / `hid_kbd_order(u8 usage)` の生成表参照口を追加|
|S0a|`input/hal/keymap_registry.c` 新|H0→map登録/source bind|
|S0b|`input/hal/keymap_eval.c` 新|H0→keysym/Ctrl/PC98 exact評価、arch依存なし|
|S0c|`input/hal/keymap_state.c` 新|H0→make作用/lock/source release|
|S0d|`input/hal/kbd_merge.c` 変|S0a〜c→usage状態と互換sc投影、旧文字化は移行中維持|
|S1p|`tools/kbd_import.py` 変/新|H0→PC98変換、出典/差分は別台帳段|
|S1h|`tools/hid_keymap_import.py` 新|H0→NetBSD継承展開、LANG1/2を基底表へ、usage別norepeatビットと送出順metadataを同じ生成物に出力|
|S1pt/S1ht|`input/keymaps/pc98_jp.c` / `input/keymaps/hid_jp.c` 各新|各々独立段、生成表。S1p/S1h後、同一ファイルを共同編集しない|
|S1x|`platform/pc98/input_keymap.c` 新|本線、S1pt/S1ht→map1/2登録|
|S1m|`platform/powermac/input_keymap_backend.c` 新|PPC、S1ht→map2登録、S1xと並列|
|S2h|`drivers/usb/hid_kbd_map.c` 変|os32powerpc、S1ht/H-hid後→手書き表を撤去、compat_sc/norepeat/orderは生成表への参照だけ|
|H-usbp|`platform/powermac/usb_platform.c` 変|os32powerpc、H-usb/S2h後→新usage口の暫定実体を追加、旧sc口へ明示変換（下記）。S2mと直列|
|S2u[n]|`drivers/usb/hid_kbd.c`＋`tools/tests/usb_hid_host.c` 変|os32powerpc、H-usbp/H-hid/S2h後→usage送出とhost callback/期待を同段更新、norepeat/order参照|
|S2a|`input/hal/usb_input.c` 変/新|os32powerpc、S0d/H-usb→usageからcapture、src/repeat/release|
|S2x|`drivers/input/kbd_hal.c` 変|S1x/S0d→PC98 keycode入口。本体scのbreak分離|
|S2m|`platform/powermac/usb_platform.c` 変|os32powerpc、H-usbp/S1m/S2a/S2u後→新usage口をcaptureへ結線、暫定変換と旧sc口を除去|
|H-usb-clean|`include/usb_platform.h` 変|os32powerpc、S2m有効化後→旧sc口の宣言を除去、H-usbと直列|
|S3|`input/hal/legacy_keymap.c` 新|S0b→由来付きkeysymから互換keydata|
|S3d|`input/hal/kbd_merge.c` 変|S0d/S3→共通出口へ切替、旧文字表撤去。S0dと直列|
|S3x|`drivers/input/kbd.c` 変|S3d/S2x/親3a,3c完了→X-cooked接続と物理救済|
|S3m|`platform/powermac/input_usb.c` 変|S3d/S2m→独自ring/held/Caps撤去、共通出口wrapper|
|S3b|`platform/powermac/input_boot.c` 変|S3m→provider raw/pressed/mods、LAN優先保持|
|S4|`input/hal/input_ime_gate.c` 変|M7全小段完了・S3後に直列→IME_ON/OFF順序配送|
|S4i|`kernel/ime/ime.c` 変|M7完了・S4後に直列→FG1の絶対ON/OFF adapter|
|W[n]|`input/hal/input_policy.c` 変/新|v2 wire検証/交換/復元、旧M4の後継|
|G[n]|`userland/lib/input/kbd_policy.c`、`userland/gshell/src/input.rs` 各別段|A0z後→評価済み意味イベント一回配送（WM評価器なし）、旧U1/U3gを更新|
|D|`platform/powermac/input_keymap.c` 削除|全結線・policy受入後、旧4b-map。build除去は先行B|

- T段は製品段ごとの `tools/tests/input_<段>_host.c` とCHECK実行器をそれぞれ1ファイルで準備、製品段受入時に実行する。 B段はorder.mk/commonまたはarch一覧/fixture/PPC一覧を各1ファイルずつ。対象製品段の受入前に全登録を完了する。
- S2uの現物は `hid_kbd.c`。E0〜E7のmake/break→lock類（39/47/53/88等）→残り、各群内はusage昇順。Shift+AだけでなくCaps+Aも同一報告で大文字にする。生成orderの3群を使う。
- S1hのnorepeatはE0〜E7・lock/Clear・IME_ON/OFF等で1、S2hは `(bits[usage>>3]>>(usage&7))&1` を返すだけ。S2uは候補設定/発火の両方で参照し、A保持中Capsを押してもrepeat候補はAのまま。表交換後のlogical修飾/操作repeatはHALでも抑止する。
- H-usbpの暫定adapterはusage別heldから互換sc参照数を作り、旧 `usbp_input_key` へscを渡す（04→1D）。最後のalias解放だけbreak、repeatも明示sc変換。未写像usageは旧出口に渡さず、releaseで暫定状態も消す。
- S2uでは `usb_hid_host.c` を新usage口の記録とE1/04等の期待へ同時更新。S2u単独の像も旧PPCのaを維持し、S2m/S3m以後に共通cookedへ移す。 S2mの新経路有効化はS3m/S3b受入と同時。宣言整理は後続H-usb-clean（include/usb_platform.h、旧口削除）を独立段とし、新旧両方へ同じ入力を送らない。
- prev/nowはusageごとの存在で比較、00は無入力、01〜03のrolloverは現行どおり報告全体を保留。repeat候補はu16/無効=0xffffとしusage ffと衝突させない。 互換scの唯一の実体はS1h生成のKmBackend.compat_sc（LANG1/2込み）。S2hに数値表を重複保持しない。本線1Eが後で同じS2aへx86 USBを接続。

|旧段|置換先|
|---|---|
|M0|既存128×8×Caps/Num/Kana固定期待を維持しS1p/S0b/S3へ適用。任意bank不在/追加は別fixtureで補う|
|M1|S1p/S1h/各生成表・台帳。AT→PC98同一性試験をusage→keysym/互換scの独立試験へ|
|M2|S0b+S3。8列本体からwscons基本表+PC98 exact拡張へ|
|M3|S0c/S0d+S2x/S3x。物理/論理の分離と256キーrelease|
|M4|W[n]へ置換。未公開v2の検証/交換/復元を実装、v1移行段なし|
|M8|S2h/S2u/S2a、CommandとLANG1/2はS1h。sc監査は独立台帳段|
|X-cooked|S3x、PPC側はS3m/S3b。新既定の有効化条件は旧B§6 F10を縮めない|
|親4a/4b|S2m→S3m→S3b。旧policy削除DはM5〜M7/U2/E-x/E-p/L/Gも完了後|

## 8. 受入 → 試験と変異

|項目/段|host固定期待・guest|固有anchorでruntime REDにする変異|
|---|---|---|
|型/登録 S0a|sizeof10、source世代00ff→0100/範囲/未登録、両archリンク|世代比較を除去|
|PC98変換 S1p|R原本とB台帳の128×8×Caps/Num/Kanaと任意bank有無、CTRL+2=1A、Alt+Ctrl+2=00|bank選択/列xorを破壊|
|HID継承 S1h|独立fixtureのJP記号/左右/Enter/全256usage、同生成hashを両像で確認|JP差分1行欠落|
|共通評価 S0b|Shift/Caps/Ctrl、Ctrl+Shift+C=03、NULとnop、非byte拒切詰め|Ctrl二重適用/英字限定除去|
|HID文字/出口|KP1=31・Num/Clearは空queue・Shift併用、Alt/Command+A=a、Plain80/ff抑止、F13で空queue。host/PPC QEMU|KP byte化を除去/Meta bit付加/0000投入|
|USB順序 S2u|同一報告Shift+A/Caps+A、A保持中CapsでもA repeat継続、Clear/90/91非repeat。host/PPC QEMU|lock後回し/norepeatビット反転|
|合成 S0c/d|別源Shift+A、左右片側break、抜去、repeat lock不変、V86抑止|最後の解放条件/lock新make条件を反転|
|互換出口 S3|裸STOP=6000/XFER=3500/UP上位3A/RLDN由来、物理Ctrl+STOP cookedなし、GUI filter/raw。NP21/WとPPC QEMU|上位scを落とす/LEGACYをETX化/GUI filterまたはCtrl+STOP抑止を除去|
|LANG1/2 S4|Apple判定なしで全HIDの90 ON/91 OFFを二度、repeat、未確定、辞書失敗、owner交代。PPC QEMU usage注入|絶対指定をtoggleに替える|
|PPC結線 S2u/S2m/S3b|暫定usage04→旧a/移行後usage04→共通a、alias解放、callback→capture→read、peek非消費、raw/mods/pressed、LAN優先。QEMU打鍵/抜去|usageをscに無変換で渡す/共通capture迂回/peek消費|
|wire W/A0z|BE/LE全offset、8192上限、重複、他profile、v2/予約値検証、短get、reset復元/BUSY|version検査/保存契機を除去|
|GUI G|左右別設定、同sc別usage、Command、英数/かな、焦点世代、評価済み値/lock一回更新。EXCL規則|rawから二重文字生成/世代検査除去|
|本体 S3x|NP21/W Caps/Kana/CTRL+STOP/IME取消、Ctrl+数字の旧byte利用側|救済判定をlogical Ctrlに変更|
|x86 USB 1E接続時|本体Shift+USB Aと逆、HID共通表、抜去。NP21/Wは装置再現可能時のみ|USBをPC98 mapへ誤bind|

- 順序: H/T準備→S0・S1並列→S2→S3→M7完了後S4（Wは独立）→A0z/G→D。共有ファイル段は直列、未結線の完成を全体完成と呼ばない。 各製品変更後に対象build、共通変更は両archリンク、触った試験--mutate、CHECK inputs/pins/basisとlint。変異anchor移設も検査。
- A0zはclean→all。全check/全変異はCI。NP21/W配備前は停止、PPC toolchain/QEMU不足は理由付きSKIP、必須モードでは失敗。 実機Appleの型番/descriptorとOS X詳細挙動は旧Q6/Q4の確認作業。決定済みのON/OFFを再び未決へ戻さない。

## 9. 未決

- 新たにユーザー判断を要する未決は0件。8列の保存はexact拡張、wireはv2、操作profileは従来値とする。variantなしのLANG1/2基底組込み、Clear/NumLockのkeydata抑止はPM決定。 Command互換scの空き監査、NetBSD版/hash/許諾固定、像サイズ実測は実装開始条件でありユーザー選択ではない。 正典更新時はH§5/§7とB§2/§4/§6を本票へ整合させる。今回は指定されたこの設計書だけを出力し、リポジトリは変更しない。

## r2→r3 の対応

|ID|反映箇所・結果|
|---|---|
|P2-1|§7/§8: H-usb/H-hid/H-usbpで新usage口と暫定明示変換を先行、S2uでusb_hid_host期待も更新、S2mまで旧sc口維持。|
|P2-2|§2/§6/§7/§8: S1hがnorepeatを生成、S2hは参照のみ、S2uで候補/発火を抑止。A保持中Capsのrepeat維持を検査。|
|P2-3|§2/§3/§5/§7/§8: variant/Apple判定を撤去、全HID基底へ90=ON/sc72・91=OFF/sc51を組込み（PM決定）。|
|P3-4|§7/§8: E0〜E7→lock類→残りの3群送出、Caps+Aの同時報告を固定期待化。|
|P3-5|§2/§4/§8: is_modifier=KS_GROUP_Modまたはsc70〜74、Clear/NumLockはno_keydataでentry自体を抑止（PM決定）。|
|P3-6|§2/§8: Command/Meta E3/E7も文字不変・高位bit付加なし、Altと併せて試験。|

## PowerPC PM のレビューと PM 決定 (2026-11-16)

os32powerpc のレビュー (PPC の Opus が PPC の実物と照合、K1・K3・K5 は PPC の PM が file:line で確認) の判定は「条件付きで可」。下の K1〜K10 を段の表と完了条件に加える。骨格は変えない。

|ID|重さ・段|PM 決定 (段の完了条件に入れる)|
|---|---|---|
|K1|P1・S2u|試験の追従を 3 ファイルにする。usb_hid_host.c、tools/tests/usb_stack_host.c (同ファイル内 の usbp_input_key だけの用意と `topology()` の sc 0x70・0x1d の期待)、tools/tests/test_usb_hid.py の変異の目印 `i = (order + 0x70) & 127;` の移設|
|K2|P2・S2h|test_usb_hid.py の目印 `case 0x47: return 0xff;` の移設、test_usb_hid.py と test_usb_stack.py の SOURCES・covers に生成表を足す。E3・E7 の互換 sc は監査が済むまで 0xff (usb_hid_host.c の期待を保つ)|
|K3|P2・S2m|新しい usage の口も、serial に旧い口と同じ形の行 `USB KEY src=.. sc=.. down=` と `USB REPEAT` を出す (usage を添える)。tools/ppc/usb_run.py と test_ppc_usb_link.py の目印を保つ|
|K4|P2・S2m・S3m・S3b|3 段は同じ統合の枝で 1 回に取り込む (S2m だけでは provider が input_usb.c の ring を読み続け、USB のキーが失われる)|
|K5|P2・S2a・S2m|S2m で usbp_input_src_alloc → input_src_register と km_source_bind、release → input_src_release を加え、usbp の src から (in2 の src, 世代) への写しを持つ|
|K6|P2・S3m・S3b|pm_input_usb_take・peek・has_key・dropped の名前は、共通の出口を包む薄い関数として保つ。dropped は共通の出口の取りこぼしの数を返す。ppc_input_usb_host.c は包みの試験に置き換える。使う所 (arch_compat_vectors.c、test_ppc_link_sh.py、ppc_sh_input.py、ppc_sh_boot.py、test_arch_compat.py) は名前を保つので変えない。ROLES §4-3 の追従表に写す|
|K7|P2・§6|PPC の常駐像 (MEM_KERNEL_IMAGE_MAX 0x275000) の行を §6 に足し、S1m と S2m で map を採って確かめる|
|K8|P3・S4|PPC の kboot には kernel/ime が無い。S4 の PPC の受入は「操作として消費し、cooked を積まない」まで。IME の受入は x86 だけ|
|K9|P3|0x90・0x91 の出典は freebsd-usb の ukbd.c:268-269 (注釈)・:290 (表)・:2271-2272。:308-310 は ukbd_boot_desc (値は同じ)|
|K10|P3・S2u|「E0〜E7 → lock → 残り」の lock の群は FreeBSD に無い OS32 の差 (同じ報告の Caps+A を 'A' にするため)。0x90・0x91 も lock の群に入れる。usage の番号は 0x を付けて書く (NetBSD の KC() は 10 進)。is_keypad は 0xf220〜0xf27f に限る (KS_KP_Home 0xf295 を & 0x7f すると 0x15 になる)|

### 本線と os32powerpc の待ち合わせ

|待つ側|待つ段|待たれる段 (担当)|
|---|---|---|
|os32powerpc の S2a|共通の口と usage の状態|H0・S0d (本線)|
|os32powerpc の S3m|共通の出口|S3・S3d (本線)|
|本線の S1x|生成表|S1h・S1ht (os32powerpc の生成器を本線と共有、担当は §7 のとおり)|
|本線の 1E (x86 の USB 鍵盤)|usage からの capture|S2a (os32powerpc)|
|両方|K4 の 3 段の同時の取り込み|S2m・S3m・S3b (os32powerpc)|

## S1h の実装で生成器の側が決めたこと (os32powerpc、2026-11-16、PM 承認)

- `KmBackend.extensions` の HID 用の descriptor は `KmHidExtensions`。持つものは次の 4 つ。
  - packed order へのポインタ
  - `const u8 (*actions)[2]`
  - `u8 action_count`
  - no_keydata の 256 bit の bitmap へのポインタ
- actions は {usage, action} の疎な表 (今は 2 件)。action の値は 0=なし・1=IME_ON・2=IME_OFF。keysym は NetBSD の数値のままにし、command のセルに独自の番号は入れない。
- no_keydata は 0x53 (NumLock・Apple の Clear) だけが 1。元の keysym は残し、出口で抑える。
- order は usage ごとに 2 bit (下位の usage を下位の bit に入れる)。
  - 群 0: 0xe0〜0xe7
  - 群 1: 0x39・0x47・0x53・0x88・0x90・0x91
  - 群 2: 残り
  - norepeat は群 0 と群 1 の全部で 1。
- KS の群に独自の番号は足さない (NetBSD の値だけ)。is_keypad は 0xf220〜0xf27f で固定。
- 大きさ (32bit の nm で実測): entries 2560B、compat 256B、norepeat 32B、order 64B、actions 4B、no_keydata 32B、descriptor 16B、backend 20B。補助と descriptor の合計は 424B (os32powerpc の報告の値。compat 256B を含む)。
- 生成器は tools/hid_keymap_import.py。原本は NetBSD 8a12c4d95f03 で、取得は tools/keymap/fetch_netbsd_wscons.sh、sha256 は netbsd_wscons_sources.json。生成物 input/keymaps/hid_jp.c は commit する。CI は自己照合だけを行い、原本から作り直して照らすのは原本がある機械だけ。

## H0b: 本線の S0・S1p の内部契約 (設計者が決めたこと)

2026-11-17。H0 の不足を補う内部契約。本文・ユーザー決定・K1〜K10 と
KmHidExtensions は変更しない。以下は `input/hal/keymap.h` の宣言と一対一で対応する。
公開48Bイベントや IKM1 を native struct として扱う宣言ではない。

### 表と評価結果 (S1p → S0b)

```c
enum { KM_MAP_PC98 = 1, KM_MAP_HID = 2 };
enum { KM_CHAR = 0, KM_BSD_SPECIAL = 1, KM_LEGACY_CONTROL = 2,
       KM_CELL_INHERIT = 255 };
typedef struct { keysym_t sym; u8 kind, op; } KmExactCell; /* 4B */
typedef struct { u8 accchar, count; u8 pairs[52][2]; } KmAccent;
typedef struct {
    const KmExactCell (*bank0)[8], (*bank1)[8];
    const u8 *flags;
    const KmAccent *accents;
    u8 accent_count;
} KmPc98Extensions;
typedef u16 KmEffect;
typedef struct { u16 mods, locks; } KmState;
typedef struct {
    keysym_t sym, command;
    KmEffect effect;
    u8 kind, op, compat_sc, is_modifier, no_keydata, action;
} KmEvalResult;
extern const KmBackend pc98_jp_backend; /* S1pt */
extern const KmBackend hid_jp_backend;  /* S1ht、既存名 */
int km_eval(const KmBackend *map, u16 keycode, const KmState *state,
            KmEvalResult *out);
```

- `extensions` は map id 1 なら `const KmPc98Extensions *`、id 2 なら既定の
  `const KmHidExtensions *`。タグを先頭に追加しない。HID の型は下記の配置を保ち
  keymap.h に移す。生成器はこのヘッダをincludeし、typedefを生成しない。
  S0a/S0bも同じ型を参照する。PC98 の count は128、HIDは256。
  全ポインタは読み取り専用、登録から利用終了まで有効。既定生成物は静的寿命。
- bank0 は128×8セル必須、flags は128バイト必須 (O=0/C=1/N=2/B=3)。
  bank1 はNULLなら全体がbank0を参照、存在するなら128×8セル。
  bank1 の未指定セルだけ `{0, KM_CELL_INHERIT, 0}` にする。
  bank0 の INHERIT は拒否。CHAR の NUL と BSD_SPECIAL の NOP は明示セルであり継承しない。
  wire kind3 の aux に255を受理しない。W は欠落行を編成時にこの表現へ変換する。
- CHAR は keysym、op=0。対応する NetBSD keysym があるBSD特殊語はそのkeysymへ写す。
  対応しない語は sym=KS_voidSymbol、kind=BSD_SPECIAL、op=元のBSD ID。
  LEGACY_CONTROL は評価結果/出口用で、EXACT の入力セルには受理しない。
  §3のDOS差分はCHARとして生成、S3のCOMPAT処理がLEGACY_CONTROLとbyteを付ける。
  accent_count は0..15、0ならaccents=NULL、それ以外は同数のKmAccent。
  count は0..52、pairs は入力byte/出力byteの対、未使用対は0。accentの元IDを失わない。
- bank は `!!(mods & KM_MOD_MODE) ^ !!(locks & KM_LOCK_ALT)` (両方ONなら0)。
  column はShift=1/Ctrl=2/Alt=4。flagsとCaps/Numの該当をORして一度だけxor 1。
  そのbank/columnを選んだ後にINHERITだけbank0の同じcolumnへ戻す。
  exactのCHARへ一般Ctrl/Caps処理を重ねない。HIDのNum常時ONは評価器がmap2に適用し、
  stateの共有Num bit自体は変えない。group2の省略はimport時に展開済みとする。
- km_eval は状態を変えない。成功0、NULL引数・範囲外・不正descriptorはOS32_ERR_INVAL、
  失敗時out不変。成功時全フィールドを初期化、commandはcommandセル、compat_scは表の値。
  kind/opは由来を保持、effectは選択された修飾作用、actionはHIDの0/1/2。
  no_keydata/is_modifierは§4の条件。repeat/down/消費/raw_dueと物理modsはS0d/出口の責務。
  accent/deadの保留合成はWとS3まで延期する (PM決定、REVIEW 3)。
  それまでは登録/交換の検証で、command/group1/group2/両exact bankの全セルについて
  NetBSD Dead群 (0x0300..0x036f) とBSD_SPECIAL op=0x89..0x97を拒否し、
  accent_count非0もOS32_ERR_INVALにする。active/保存表を変えず全体失敗とする。
  既定jp.pc98.kbdにaccentはなく、この制限が届くのは利用者の表だけ。
  KmAccentは将来のデータ形式だけを予約し、S0cに合成待ち状態を暗黙に持たせない。
  W/S3で待ち状態の持ち手・source/owner世代・flush条件・試験を確定してから拒否を解除する。
  KS_Multi_keyはdead群とは別。既定HID表の値を保ち、当面effect=0・合成開始なしとする。

### 修飾作用と合成状態 (S0b → S0c → S0d)

```c
enum {
    KM_MOD_SHIFT=0x0001, KM_MOD_CTRL=0x0002, KM_MOD_ALT=0x0004,
    KM_MOD_MODE=0x0008, KM_MOD_COMMAND=0x0010, KM_MOD_META=0x0020,
    KM_LOCK_CAPS=0x0100, KM_LOCK_NUM=0x0200, KM_LOCK_KANA=0x0400,
    KM_LOCK_SCROLL=0x0800, KM_LOCK_ALT=0x1000, KM_EFFECT_SHIFTA=0x2000
};
int km_state_apply(const KmKey *key, u8 old_down, u8 origin, KmEffect effect);
void km_state_snapshot(KmState *out);
void km_state_release(u8 src, u16 generation);
void km_state_cancel_tap(u8 src, u16 generation);
```

- effectはu16の上記bit集合 (0は無作用)。左右は別keycodeに保存し、合成時だけOR。
  BSDのLSHA等は該当MODとKM_EFFECT_SHIFTAの組合せで、LOCK_ALTとは合成しない。
  下表でmap/keysym/BSD IDから作用を一意に選ぶ。いずれも文字の上位bitを足さない。
  内部bitと旧SHIFT_*は別の名前空間。SHIFTAタグはsnapshot.mods/locksに含めない。
- S0d が唯一の held[src][keycode] を所有する。S0cは同じ配列を複製せず、作用u16とsource世代を保存。
  applyへ渡すold_downは更新前held (0/1)、originはINPUT_ORIGIN_NATIVE/USB。
  新makeだけeffectを保存し、momentaryを加え、USB lockを反転する。
  native lockはmake/set・break/clear。Kana物理派生はS0dがnative keycode 0x72の
  作用へLOCK_KANAをORする (再配置で選んだ別作用も保持)。USBの0x90はこれを行わない。
  repeat/二重makeではeffectを無視して保存作用を維持、lockを反転しない (SHIFTA候補は下記)。
  breakは渡されたeffectを無視し保存した作用だけ解放、未押下breakは無作用。
- applyは成功0、不正src/世代/未bind/keycode/down/repeat/old_down/origin/effect予約bitは
  OS32_ERR_INVALで状態不変。repeatはdown=1かつold_down=1だけ許可する。
  snapshotは非NULL outに全源のmods ORとUSB共有lock/native位置ORを返す (void)。
  releaseは一致世代の全保存作用/native位置を消す。USB共有lockは抜去で反転も消去もしない。
  stale/二度releaseは無作用、最大256キー。state APIは配送をしない。

```c
typedef struct {
    const u8 *order;
    const u8 (*actions)[2];
    u8 action_count;
    const u8 *no_keydata;
} KmHidExtensions; /* 32bit: offsets 0/4/8/12, sizeof 16 */
```

作用表の出典は固定NetBSD `wsksymdef.h` のGroup 1と、B§1のFreeBSD `kbio.h`。
KS名は選択文字セルに対して照合する。commandセルのKS_Cmd1/2を物理修飾と解釈しない。
表のKM_接頭辞は省略しない。BSD_SPECIALの値をCHARの同値byteとして照合しない。

|選択したkeysym / BSD_SPECIAL op|map1 のeffect|map2 のeffect|
|---|---|---|
|KS_Shift_L/R (0xf101/02)、LSH/RSH (0x02/03)|KM_MOD_SHIFT|KM_MOD_SHIFT|
|KS_Control_L/R (0xf103/04)、LCTR/RCTR (0x09/80)|KM_MOD_CTRL|KM_MOD_CTRL|
|KS_Alt_L/R (0xf107/08)、LALT/RALT (0x07/81)|KM_MOD_ALT|KM_MOD_ALT|
|KS_Meta_L/R (0xf110/11)|KM_MOD_META|KM_MOD_COMMAND|
|BSD META (0x84)|KM_MOD_META|KM_MOD_META (HID拡張にBSD opセルなし)|
|KS_Mode_switch (0xf10a)、ASH (0x83)|KM_MOD_MODE|KM_MOD_MODE|
|KS_Caps_Lock (0xf105)、CLK (0x04)|KM_LOCK_CAPS|KM_LOCK_CAPS|
|KS_Num_Lock (0xf10b)、NLK (0x05)|KM_LOCK_NUM|0 (Num常時ON)|
|KS_Hold_Screen (0xf10c)、SLK (0x06)|KM_LOCK_SCROLL|KM_LOCK_SCROLL|
|Mode_Lock相当のALK (0x82)|KM_LOCK_ALT|KM_LOCK_ALT (HID拡張にBSD opセルなし)|
|LSHA/RSHA (0x9b/9c)|KM_MOD_SHIFT \| KM_EFFECT_SHIFTA|同左 (HID拡張にBSD opセルなし)|
|LCTRA/RCTRA (0x9d/9e)|KM_MOD_CTRL \| KM_EFFECT_SHIFTA|同左 (HID拡張にBSD opセルなし)|
|LALTA/RALTA (0x9f/a0)|KM_MOD_ALT \| KM_EFFECT_SHIFTA|同左 (HID拡張にBSD opセルなし)|
|その他の受理済みセル (NOP、文字、操作等)|0|0|

- 固定版NetBSDにKS_Mode_Lockはない。新しいkeysym値を作らず、PC98のALKは
  BSD_SPECIAL/op=0x82で保持する。map2のMetaはusage番号でなく選択されたkeysymで
  COMMANDへ写すため、再配置後も同じ。BSD METAをmap2のMeta keysymと取り違えない。
- SHIFTAON相当はS0cに全源で一つの候補 `(src, generation, keycode, armed)` を持つ。
  新makeの保存effectがSHIFTAなら該当momentaryを押し、候補を立てる。makeではAltLockを反転しない。
  他キーの有効なmake/break/repeatは候補を消す (別源も含む)。別のSHIFTA makeなら旧候補を消して
  新候補に置換する。同じキーのrepeat/二重makeは保存作用を使い、候補を再武装しない。
  対象キーの実breakで候補が一致しarmedなら共有AltLockを一度だけ反転してから
  momentaryと候補を解放する。候補が消えていればmomentaryの解放だけ。
  タグはSHIFT/CTRL/ALTのどれか一つとの組合せだけ有効とし、LOCK_*との組合せは拒否。
  抜去の合成breakはtap扱いにしない。km_release_keysは掃除の前にkm_state_cancel_tapを呼び
  当該source/世代の候補だけを消す (bindingと保存作用はまだ有効)。戻り値void、
  不一致は無作用。後段のstate_releaseも候補を消す冪等な掃除。
  stale releaseは候補を変えない。通常breakとsource抜去を同じ操作と見なさない。
  S0cの受入: 単独make→breakで反転、他キー介在で不変、repeatで再武装なし、
  別源の介在・別SHIFTAへの候補交代・抜去でも意図しない反転なし。
  FreeBSD `kbd.c:1245-1247,1258-1374,1390-1438` のXOR/SHIFTAONを基に、
  本票の保存作用・repeat抑止・共通source契約へ接続する。

### 登録、採取、解放の順 (S0a/S0c/S0d/S1x)

```c
typedef struct { const KmBackend *map; u32 map_generation; } KmSource;
int km_source_lookup(u8 src, u16 generation, KmSource *out);
void km_source_unbind(u8 src, u16 generation);
void km_release_keys(u8 src, u16 generation);
/* H0bだけ: 現行の像に実行コードを追加しない予約 */
static inline void km_source_release(u8 src, u16 generation)
{ (void)src; (void)generation; }
```

- register/bind/lookupは成功0、失敗OS32_ERR_INVAL。重複idは同じpointerでも拒否。
  bindは生存する鍵盤source/一致世代/登録mapだけ、二重bindは同じmapでも拒否。
  idは1/2のみ、表サイズ/必須pointer/拡張値を登録時に検証する。
  lookupは生存世代とbindingの両方を検査し、成功時だけoutを書き、未bind/stale/NULLは失敗。
  active表の世代はu32、初期1、Wの成功した交換/復元で増分 (0を飛ばす)。sourceのu16世代と混同しない。
  S0aの初期実装は静的既定表と世代1を返す。Wはlookupの実体をactiveへ接続する。
  out.mapは短い排他区間中だけ借用し、queueにpointerを残さない。
- S1xはPC98/HID backendを順に登録して戻り値を確認、本体sourceをregister→bind(map1)。
  bind失敗ならsourceをreleaseして入力を有効化しない。既登録mapは取り消さない。
  USBはK5どおり別sourceをregister→bind(map2)、非搭載でもmap2登録は行う。
- S0dのcaptureはirq_save→lookup→keycode/down/repeat検査→old_down取得→
  snapshot→km_eval (新makeの作用を選ぶ)→km_state_apply→held/物理/互換cnt更新→
  snapshot→km_eval (合成後の文字)→配送→irq_restore。
  break/repeatの作用には必ず保存値を使う。評価は二度でも作用更新は一度だけ。
  新makeのis_modifierは最初の評価もORし、自己作用でセルが変わっても文字を漏らさない。
  評価/状態更新に必要な検証は変更前に済ませる。captureは成功0/不正入力OS32_ERR_INVAL。
  V86でも状態は更新、配送だけ抑止。raw_due等とtime_ms/source/map_generationはS0dが添える。
- input_src_releaseは既存のsrc/世代検査内、IRQ停止中に次の順で呼ぶ。

```c
km_source_release(src, generation); /* 今回ここだけ予約を挿入 */
input_state_release(src, generation); /* 旧キーとpointerの掃除を維持 */
sources[src - 1].used = 0;
/* S0a: inlineを外部宣言へ置換し、keymap_registry.cに実体を置く。
 * 初めはkm_source_unbindだけ。S0c/S0dの結線完了時の実体は: */
/* km_release_keys(src, generation);  S0d: mapが有効な間にheld/旧投影を解放
 * km_state_release(src, generation); S0c: 残る作用/native位置を掃除
 * km_source_unbind(src, generation); S0a: 最後にbindingを失効 */
```

- unbindは一致世代のみ失効、他は無作用。通常の呼び手はreleaseだけ。
  新経路有効化時に旧heldへ新キーを重複保存しない。release_keysは最大256キーのbreak処理、
  操作/cookedのmakeを作らず、最後の互換aliasのbreakだけ出す。
  sourceが生存中に掃除するので既存のinput_src_info検査を通せる。
- 空inlineを選ぶ理由はH0bにリンク依存や機能実装を持ち込まず、最適化後の両像を保持するため。
  weak stubは使わない。S0aがinlineを外部実体へ替える際、既存のin2_dev.c直接リンク試験にも
  stubまたはregistryを足す。`tools/tests/test_input_keymap_h0b.py` もS0aの追従ファイルとし、
  空inlineを置換するanchor/assertを外部実体の試験へ同時に切り替える。
  S0c/S0dのrelease結線前は新captureを有効化しない。

|渡す段|受ける段|確定する受け渡し|
|---|---|---|
|S1p|S0b・S1pt|KmExactCell/PC98 descriptor、bank/INHERIT/flags/accentの上記表現|
|S1pt・S1ht|S1x|静的KmBackend (1/128、2/256)、extensionsはidで型を判定|
|S1x|S0a|backend_register→source register→source_bind、失敗を確認|
|S0a|S0d|source_lookupのmapとu32表世代、release時のbinding失効|
|S0b|S0c・S0d|km_evalのKmEvalResult、選択したu16 effectと文字/操作の由来|
|S0c|S0b・S0d|km_state_snapshotのmods/locks、apply/releaseの保存作用管理|
|S0d|S0c・S0a|S0aとS0cの取り込み後に直列。唯一のheldからold_down、release_keys→state_release→unbindの順 (release_keys冒頭で候補を無効化)|

H0bの試験は宣言の型/配置・数値とrelease予約の呼出位置を検査する。
評価/登録/状態の実装の正否はS0a〜dの試験で検査し、宣言試験の合格を代用にしない。

PM決定 (2026-11-17): S0aは`km_source_release`の空inlineを維持し、別名`km_registry_source_released`に失効処理を実装してhostで試験する。inlineからの呼出しと像への結線はS0dで行う。

## S1pt・S1x・S0d の取り込みで S2x へ送る項目 (2026-11-17、Opus レビュー)

- PM決定 (2026-11-17): pc98_input_keymap_register の backend登録・source登録・bind のどこかで失敗したら、bindしない別sourceを登録して本体の全打鍵を旧 input_capture(sc) へ送り、bind済みsourceと入口を混ぜず、起動時のklogに失敗を1行残す。
- S1x・S0d の段では、x86 の打鍵はまだ新しい経路につながっていない。pc98_input_keymap_register と input_capture_key は S2x で呼ぶ。起動時の登録の失敗で入力が無くなる危険も、S2x で扱う。
- S2x の試験では、128 キー × 32 修飾の比較を CUI の make の文字だけでなく、GUI の raw とリピートでも旧経路と並べて行う。
- bind 済みの源は input_capture_key だけを通す。sc の入口 (input_capture) と usage の入口を同じ源で混ぜると、sc の入口の break で held だけが消え、保存した作用が残って修飾が押されたままになる。S2x の依頼にこの約束を書く。

## S2x の取り込みで後の段へ送る項目 (2026-11-17、Opus レビュー、どれも P3)

- platform/pc98/input_keymap.c (pc98_input_keymap_register()): HID の backend の登録が失敗しても、先に登録した PC98 の backend は残す (解除の口は作らない、使う物も無い)。意図どおり。
- 登録の失敗のログは /var/log/boot.log の 1 行だけで、画面には出さない。設計の「klog」はこの意味とする (PM 決定 2026-11-17)。
- 試験: V86 の中で CAPS・KANA を押してから抜けた後の文字を、旧経路と比べる 1 件を、S3 の段の試験に足す。
- kbd_hal_init を 2 回呼ぶと旧経路になり bindings[] が残る。呼ぶのは boot.c:53 の 1 回だけなので、今は起きない。2 回呼ぶ道を作るときに直す。

- PM決定 (2026-11-17、S3/S3d): 登録失敗時の未bind sourceの旧sc経路も旧文字表を使わず、静的なpc98_jp_backendと経路内の局所KmStateでkm_evalを呼び、通常経路と同じBSD byteを返す。S2xの登録失敗試験も通常経路と同じkeydataを期待する。

## S3・S3d の取り込みで後の段へ送る項目 (2026-11-17、Opus レビュー、どれも P3)

- tools/tests/keymap_s3_oracle.py: 意図した差の一覧の new と M0 の fixture の oracle を、読み込みのときに直接 assert する (今は製品を介した推移だけ)。
- no_keydata の判定と、kbd_merge.c の capture_state の boot_locks の上乗せを消す変異を 1 本ずつ足す。
- legacy_keymap.c: 静的な表が不正 (km_eval の INVAL) なら予備の経路も全キーを捨てる。今の表では起きない。S3x の段で、起動時に km_eval を 1 回試して boot.log に残すかを決める。
- userland/gshell/src/input.rs の旧い表の写しは G 段で更新する。
