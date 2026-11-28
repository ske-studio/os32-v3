# INPUT_HAL_DESIGN — 入力の HAL と USB HID の共通化 (入力元ごとのイベント・共通の入力状態・文字端末 bridge)

> 状態: **現行 (2026-11-15)** — 確定 (astra 起草 r1・r2、Opus 5.5 の独立レビュー 2 回、PowerPC PM の意見 H1〜H7、PM 決定を反映)。U3 (PowerPC の CTRL+C) は追補 [INPUT_BIND_DESIGN](INPUT_BIND_DESIGN.md) (操作の割り当て表、BSD キーマップの踏襲) で確定し、段 3b・4b-map の範囲と依存は追補の §6 に従う。段の状態は票 TASK_INPUT_HAL が持つ。
> 出所: 往復の版は `~/os32-tmp/run/input-hal/` (PACK、DESIGN.r1・r2・final、REVIEW.r1、R2_REQ、FINAL_REQ)。棚卸しは `~/os32-tmp/run/hal-survey/`。確定済みの AV_HAL_DESIGN の入力の節 (XInput 基、入力元の上限 IN2_MAX_DEV) の上に立つ。

## r1→r2 の変更
- U1: 公開ABI切替の原子的な大段を承認済みとし、400行超はsol担当、準備は小段を維持。
- U2: 入力元をIN2_MAX_DEV=16へ拡張。AV正典はmainに反映済み（S1）、パッド4slotは維持。
- R1: 本体の機械式ロックとUSBの反転式を区別し、本体とUSB共通bitのOR合成・期待値を明記（S4反映）。
- R2: 抜去はsrc別in2解放と、最後の押下が消えた旧出口への解放通知を明記。
- R3: GUIは注入のみで早期復帰、try内のpark_pollを段1aのbridge責務とした。
- R4: ARCH_COMPAT T1bのINPUT分を基準とし、PPC不適合はXFAIL（main反映済み、S1）。
- R5: 段2cを内部InputEventだけで構成し、公開IN2_Event変換をA0z後へ分離。
- R6: serial/注入/parkはprovider関数ポインタを渡し、共通部品の各段でPPC像のリンクを受入。
- R7: V86中はheldと修飾を追い、旧リングとin2への投入を止めると明記。
- R8: PPC keymap統合と旧ファイル削除を計画、カナ・CTRLの差を試験化（CTRL+Cは追補で確定）。
- 軽微: serial混入の根拠からinclude行kbd.c:653を除去。
- 2026-11-14、r2を基に最終指摘を反映。U3は2026-11-15に追補で確定。
- 作業木 `ec2fa85e59deb7aed4f7bf117a064e80a0cf8fb5`。main は `89dcd9b3f648b60834b3d047d24f9cb87d06a1fa`。
- r1時点で指定コード12本と ARCH_COMPAT/ARCH_CTX/IO_TIER/AV_HAL/USB_DESIGN は `git -C /home/hight/os32-v3 show origin/main:<path>` で読み、作業木とのバイト一致を確認。以下の file:line はこの確認済みコードによる。
- シングルタスク・協調切替・整数演算。Win32/DirectInput/XInput の互換 API はユーザーランド。PC-9801互換は対象外。
- 正典: `docs/decisions/sound/AV_HAL_DESIGN.md` §5・§6・§10〜12、`docs/decisions/usb/USB_DESIGN.md` §4-8・§9〜11。PACK の realhw 配下という案内は旧所在で、現行 USB 設計は usb 配下、票は `docs/tasks/realhw/TASK_USB.md`。
## 1. 実物と確定設計の差（D1〜D7、7件）
| ID / 棚卸し | 実物（file:line） | 確定設計との差・本案の処置 |
|---|---|---|
| D1 / I1 | `sys/hal/input/input_service.c` は状態なしの転送。`sys/platform/pc98/input/kbd.c` が raw・gui_mode・押下・cooked を所有 | AV §5 の共通状態は未実装。装置から状態を移す。全入力がシェルへ直接配送という現状認識は正確でない |
| D2 / I1 | `sys/platform/powermac/input_usb.c` は独自押下表と文字キュー、`input_boot.c` は中断指定無視・pressed=0・raw=-1・GUI無処理。現行の `sys/platform/powermac/input_boot.c` にはcapture待機の期限も無い | AV in2 と AC-INPUT-1 の能力・不在区別に不足。共通状態から raw/mods/pressed を提供し、待機を共通化 |
| D3 / I2 | `sys/platform/pc98/input/mouse.c` に poll 採取・座標・8255ボタン・カーソル | AV §5-4 の独立採取でない。採取と読み出しを分け、PointerOps は互換スナップショットへ |
| D4 / I4 | `sys/dev/usb/hid_mouse.c` の `mouse_done()` → `sys/platform/powermac/usb_platform.c` の `usbp_input_mouse()` はログだけ | USB §4-8 の mouse_ext と AV in2 に未結線。PPC の入力成功はマウス配送成功を意味しない |
| D5 / I5 | `sys/platform/pc98/input/kbd.c` が注入・serial・park を選択 | AV の物理イベント入口だけでは置換不能。文字端末 bridge を共通層へ抽出 |
| D6 / I1・I4 | `sys/platform/powermac/usb_platform.c` は kind ごと4枠、`usbp_input_key()` は PPC 独自関数へ。`hid_kbd.c` は差分と repeat を別通知 | USB §4-8 の本体+USB kbd_merge 未結線。AV §5-4 の merge前・src別採取を共通の唯一の入口で実現 |
| D7 / I4 | `usb_platform.c` の `usbp_input_src_alloc()` の入力種別は鍵盤/マウスのみ、`hid_mouse.c` は boot 3B | AV §5-1/5-5 の4パッド・振動・一般HIDは未実装。既存HIDをXInputパッドとして扱わず後段へ |
- I3: PPC 常駐CLIは `sys/platform/powermac/terminal_input.c` の `pm_terminal_feed()` → `cli_boot.c` の `pm_cli_boot()`。棚卸しの事実を引継ぎ、LAN_OPERATOR P3の担当とする。AVの欠陥として重複集計しない。
- USB事実: OHCI/列挙/ハブ/boot HID は `drivers/usb/` に既に共通化。usage→PC-98 sc は `hid_kbd_map.c`、左右修飾の多対一は `hid_kbd.c` の押下数から差分化する。
- PPCはUSB IRQ未接続（`usb_platform.c`）、100Hz tickと前景10ms pollingで進行。DMAのbus窓・キャッシュ・PCI/OF写像・TB時計はPPC固有。今回実機を再試験したという意味ではない。
## 2. 層と所有者

`装置 → sc/down/src・移動/ボタン/時刻 → 共通入力状態 → InputOps / in2 / PointerOps → 利用者`

| 層 | 置くもの | 置かないもの |
|---|---|---|
| 装置 | PC-98鍵盤受信・bus mouse採取、OHCI転送、HID差分/usage写像、USB repeatタイマ、将来pad report | gui_mode・前景・文字端末選択 |
| 共通入口 | 登録、源IDの写像、時刻付与、鍵盤/相対/絶対/ボタンの正規化 | PCI・CPU命令・文字からの擬似キー生成 |
| 共通状態 | src押下、kbd_merge、mouse_ext、raw/cooked、座標/範囲、in2リング、焦点・独占・回収 | USB DMA寿命・ハード描画 |
| 互換出口 | InputOpsの既存意味、PointerOpsの既存構造体、serial/注入bridge | Win32 APIそのもの |
| 新出口 | AV確定in2（パッドはXInput形、鍵盤/マウスはRaw Input形） | キーボードをパッド報告へ変換する処理 |

- **PPCの usbp_input_key/mouse/release の切れ目を採る**。HID解釈後・文字化/合成前なのでbreak・srcを失わず両出口を作れる。文字化後の `pm_input_usb_take` をHAL入口にしない。
- `sys/hal/input/input_service.c` は引き続き公開入口の転送役。共通providerを両platformが登録する。状態の実体は `input/hal/`、文字端末のpolicyは `sys/kern/input_legacy.c`。

| arch/platformに残す | 共通化する |
|---|---|
| IRQ保存/復元、PC-98の8251/8255 I/O、V86反射/退出adapter | 押下表・合成・文字化・リング・gui_mode |
| PPC DMA bus/VA変換・cache・sync、x86 DMA割当 | OHCI/USB/HID本体、DMAのLE helper契約 |
| OF/PCI BAR→CPU写像、PC-98 MMIO予約/BIOS EHCI静止 | 入力元登録と抜去、src→dev対応 |
| TB/PITの時間源、tick登録・pollの起動条件 | 時刻単位・repeat規則・予算内の入力step |
| PC-98カーソル描画backend | PointerOps座標/範囲/表示要求。描画はFG1でbackendへ |

## 3. 内部契約（コード断片は宣言案、公開ABIではない）

```c
/* sys/hal/input/in2_int.h: 自然整列、全サイズ/offsetをSTATIC_ASSERT。 */
#define IN2_MAX_DEV 16      /* AV正典と同じ唯一の定義場所。別ヘッダに上限を複製しない。 */
struct InputLegacyProvider {
    int (*terminal_try)(void); /* x86 serial / PPC PLN1 byte stream、非待機 */
    int (*inject_try)(void);   /* GUI注入。空/不在は-1 */
    int (*terminal_peek)(void); int (*inject_peek)(void); /* 完成keyを消費しない */
    int (*try_key)(void); int (*peek_key)(void); /* local cooked key、peekは非消費 */
    int (*gui_active)(void); /* 3c前はx86 kbd_gui_modeを包む。PPCも自分のprovider */
    int (*terminal_enabled)(void); /* rshell/capture等のpolicy */
    void (*park_poll)(void); /* try、成立時戻らず再開値byte/-1 */
    void (*park_kbd)(int interruptible); /* wait、成立時戻らず再開値byte */
}; /* 常駐providerを起動時登録、NULLは不在。共通コードに装置symbolを置かない。 */
typedef struct InputEvent {
    u32 time_ms;             /* 0..3: 共通単調時計、u32 wrap */
    u16 generation;          /* 4..5: 登録世代、古い通知の排除 */
    u8 src, type;            /* 6,7: src=1..IN2_MAX_DEV、0は無効 */
    i16 x, y;               /* 8..11: REL dx/dy、ABS 0..32767 */
    u8 sc, down, buttons, flags; /* 12..15: sc<128、down=0/1 */
} InputEvent;               /* 16B。未使用欄は0、flagsは定義済みbitだけ */
int input_src_register(u8 kind, u8 origin, u16 *generation); /* dev=1..IN2_MAX_DEV/FULL */
int input_capture(const InputEvent *e); /* 有効性検査→状態更新→各出口 */
void input_src_release(u8 src, u16 generation);
void input_repeat(u8 src, u16 generation, u8 sc); /* 旧出口だけ */
int input_step(struct io_slice *slice); /* Tier2/FG1 */
enum { IL_CHAR = 0, IL_KEY = 1, IL_LOCAL_CHAR = 2 }; /* byte / cooked key / local byte */
int input_legacy_peek(int mode); /* 完成keyを消費せず、選択順を進めない */
int input_legacy_try(int mode);         /* 非待機、文字端末の選択 */
int input_legacy_wait(int mode, int interruptible); /* FG1専用 */
/* 既存 InputOps/PointerOps の関数型と公開slotは変えない。 */
/* in2_src_* は同じ状態エンジンの薄い入口。二重の押下表を作らない。 */
```

- 内部KEYのbuttons欄は採取時modsとして使用し、BUTTONは現在maskをbuttons・changedをsc欄に保持する（入口の未使用欄0とは別に、リング格納時に確定）。公開変換時に最新状態から再計算しない。
- 内部構造体はCPU native endian。BE/LE間でバイト列を送らない。段2cのリングはInputEventと別のu16 seq配列のみで構成し、積むごとにseqを増加。LOST/PADBTN/WHEEL等のpayloadも内部type別にx/y等へ定義し、公開型には依存しない。A0z後の変換段がAV §5-2の16B IN2_Event（dev=src）へ写す。USB descriptorのLE変換と混同しない。
- `src`は全種類共通のIN2_MAX_DEV（16）枠。既存USBの `(kind, src=1..4)` はadapterの表で一意なdevへ写す。PPCに本体鍵盤/busはなくUSB4+4を維持、PC-98の本体/bus/seamless 3 + USB8 = 11も収まる。登録表/押下表/境界試験は全て同じ定数を使い、17源目はFULL。パッド4slotは別上限。
- 登録は起動FG0またはFG1、短い排他内で公開。releaseは待機なし、最大128キー+ボタンを解放し枠を返す。src別in2にはその源のbreakを全て出し、合成cntが0になったキーは旧rawにもbreakを出す（cookedにはbreakを積まない）。ボタンもsrc別in2に解放を出し、ORが0になったbitは旧PointerOpsの解放として見えるようにする。GUI以外では旧rawを積まない規則を維持。USBはcallback停止後にrelease、枠再利用前に古いcallbackが来ないことを保証する（世代wrapだけに寿命安全を頼らない）。
- `input_capture` / release / repeatはIRQ・FG0・FG1から可能な有限処理。割込み許可状態を保存復元し、割当・待機・前景pump・USB制御転送・描画を呼ばない。イベントのポインタを保持しない。
- 同じ入力は1回だけcapture。src状態更新とin2への採取を**merge前**に行い、その後旧出口へ分岐。リング満杯でも物理状態は更新する。

```c
/* kbd_mergeの要点（排他内）。held[IN2_MAX_DEV][128]とcnt[128]は共通が唯一所有。 */
old = held[src - 1][sc];
if (old != down) {
    held[src - 1][sc] = down;
    cnt[sc] += down ? 1 : -1;
    /* srcの新状態と修飾を確定してin2へ。 */
    emit_in2_key(src, sc, down);
    if (down || cnt[sc] == 0) emit_legacy_key(sc, down);
} else if (down) {
    emit_legacy_repeat(sc); /* in2にrepeatを入れない */
}
```

- 未押下breakは無作用。同じキーを別srcがmakeすれば旧makeは届くが、breakは最後のsrcだけ。rawにはイベント時点の合成修飾を焼く。
- Caps/Kanaは源の種類で処理する。本体の機械式はmakeで源別lock=1、breakで0（kbd.c）。USBは全USB鍵盤で共有するlock bitを各新makeで反転し、breakでは保持（現Capsはinput_usb.c:39）。Caps/Kana各1bit、repeatでは反転しない。
- 合成は各lock bitについて `lock_global = mechanical_lock | usb_lock`。USB源別のlockは持たず、USB AでON→USB Bの次makeでOFF。物理held/cntとは別に更新し、emit_legacy_keyは再反転しない。USB抜去は物理heldを解放するが共有usb_lockを反転/消去しない（初期化で0）。本体breakはmechanical_lockだけを0にする。
- `in2_state.c`はsrc押下/ボタンとin2累積、`kbd_merge.c`は合成数、`in2_ring.c`は128件/seq/drop、`legacy_ring.c`は旧raw/cooked（各128×u16）。重複したpressed表は作らず合成数から照会。
- rawはGUI時のみ、cookedはCUI時のみ。`legacy_mode.c`がgui_mode・切替時flush・STOP抑制を所有、`sys/kern/gui.c`の切替口は維持。kbd.cは装置/診断/legacy adapterのみへ縮小する。
- V86の間もheldと修飾（mechanical_lock/usb_lockを含む）は追うが、旧raw/cookedリングとin2には積まない。物理状態追跡とOSへの公開を分け、入退場で溜めたイベントをflushする。CTRL+STOPのIRQ要求とV86退出要求の実行は既存arch経路に残す。

## 4. 実行文脈・時刻・マウス

| 処理 | 文脈 / 進行 |
|---|---|
| 本体鍵盤・bus mouseの採取 | IRQで有限capture、CTRL+STOP要求も維持。共通サービス全体をIRQで回さない |
| USB既存callback | 現行のIRQ/EE=0でもcapture可能。列挙・転送寿命の進行は既存USB契約を維持し、本票だけでtick serviceを移動しない |
| input_step | AC-CTX-4に従いFG1・IRQ外・非再入、Tier2。件数/時間予算で止めてIO_MORE。通知はio_requestの印だけ |
| seamless採取 | 旧pollの位置で1回だけ採取しmouse_halの口で共通状態へ（PC-98のtick枠7つが満杯のため、tickでの採取への移動は1kHz tickの段B以降）。初回は基準だけ、以後ABSと画素差MOVE。旧pollで再採取しない |
| GUI配送・文字端末poll・振動送出 | FG1協調点。USB振動はusb_serviceの文脈。IRQ出口からTier2を呼ばない |
| wait | FG1で条件/中断→予算内pump→再検査→既存idle。IRQ/FG0/lock保持では待たず、不在を無限spinにしない |

- IO_TIERの「入力はTier2」は後続処理の契約。AV §5-4が許す有限採取・押下更新をIRQから削除する意味ではない。1kHzでも協調しないアプリへのGUI配送の期限は保証しない。
- 時刻は採取時の `in2_time_ms`。段B前は論理tickの正しいms換算、段B後は1kHz ktime。PPC TB生値や100Hz jiffyをms扱いしない。wrap差はunsigned、同時刻はseqで区別。
- `mouse_ext`はsrc別buttonsをOR、相対差は飽和加算。旧PointerOps用の差分とin2の累積は独立で、片方のreadが他方を消費しない。in2累積i32、旧差分i16、境界で飽和し符号反転させない。
- seamless選択中はUSB/bus相対差と合成しない（USB §4-8）。src物理状態/in2採取は続け、モード切替では旧差分/絶対基準をreset。seamlessのボタン読出はPC-98 adapterへ。
- `pointer_state.c`は座標・bounds・mode、描画backendは旧カーソル方式を保持。PPCは共通PointerOpsを登録しUSB相対入力でavailableとなる。描画能力の不在は入力不在と区別する。

## 5. 文字端末 bridge と互換

- I5は `sys/kern/input_legacy.c`へ。GUI注入は既存 `kbd_inject` の宛先/UTF-8 byte queueを維持し、serial/LANは文字端末として接続。sc=0のcookedを返し、raw/in2/物理pressedへ入れない。
- 最初の抽出段1aは非待機の選択helperとtry内の協調yieldを移す。GUI時はIL_LOCAL_CHARなら-1でparkせず早期復帰。それ以外は注入だけを見て早期復帰し、空ならproviderのpark_pollを1回呼び、戻った場合は-1。serial/localへフォールスルーしない。peekもGUIのlocalは-1、それ以外は注入を非消費で見るだけでparkしない。
- x86 CUIのtry/peekはserial（rshellかつcaptureでない）→local。IL_KEYはcooked key、IL_CHAR/IL_LOCAL_CHARは低位byteを返す。local-onlyはserialを見ない。local try_key/peek_keyはkbd.cのlocal cookedを包みbridgeへ再入しない。serial/注入/GUI照会/parkもprovider経由で、共通層から装置symbolを直接参照しない。
- 待機版は別段で移す。getcharはlocal優先、getkey_waitはserialを読まない（kbd.c）。GUI waitはproviderのpark_kbdを使い、park_poll/park_kbdとも通常returnはpark不成立時だけ。成立時は戻らず、try再開値はbyteまたは-1、wait再開値はbyte。中断・再開を口別fixtureで固定し、一律tryループへ置換しない。
- PPCは像別に登録する。PLN1像は terminal=NULL とし、local を「LAN (decoder と完成key先読み 1 個、platform 側) が先、空なら USB」の 1 つの口にする (2026-11-15、PowerPC P3 ③b-2。共通の wait(IL_KEY) は local だけを読むため。交互選択はやめた。PPC_P3_DESIGN 末尾)。link-SH像はmailboxのコマンドでありterminal/injectはNULL、local try_key/peek_keyとshell行頭の調停を使う。LAN交互選択をlink-SHへ持ち込まない。
- peek/has_keyは予算付きの有限採取（byte→decoder→完成key先読み）を許す。禁止は完成keyの消費と交互選択順の更新。選択順は実際のtryで消費した時だけ更新する。現input_boot.c:31-37のpoll/take/選択更新一体のpeekをそのまま移さず分離し、PLN1のwhile(!kbd_has_key()) sys_yield()も進行させる。
- capture期限は待機開始時点から数える。FREEZE中は閉じたまま、期限後は文字待ちがCR、キー待ちがESC。con_sink通知の例外を設けない。振る舞いの注: capture中の1コマンドが何度も待つ場合、それぞれが30秒まで待てる（x86現行と同じ）。コマンド全体の上限はhost側の待ちが担う。
- InputOpsの能力pressed/raw/modifier/interruptibleを独立照会する内部契約はARCH_COMPAT F6/F7と共有。不在と未押下を新規利用者が混同しない。旧値/voidのABIはAC-KAPI-1どおり保持し、公開照会追加はABI段へ。
- `kbd_getchar/getkey/try/peek/raw`・mouseのslot/型は不変。PC-98 scを共通符号として固定し、gshellのSHIFT=0x70/CTRL=0x74/STOP=0x60（`input.rs:67,68,80`）、rawのdown=bit8/mods=bit9以降を保持する。
- 共通keymapのCTRL+Cは全archで0x03（追補 [INPUT_BIND_DESIGN](INPUT_BIND_DESIGN.md)）。操作（中断・行の取り消し）の割り当ては追補の操作の割り当て表が持つ。Kanaはlock/modifierを更新し、旧CUI同様にASCIIを直接カナ化しない。SHIFT/CTRL等も両archの差分fixtureで固定し、旧結果の記録とU3後の共通仕様を区別する。
- 新独占の有効化はgshell対応と同時の受入。AV §5-3どおりSTOP/CTRL/GRPH/TABを残し、X4は配送停止だけ、X3で押下解消/ドラッグ取消。WM改修前は独占を公開しない。
- 焦点・独占の書き手はFG1のみ、IRQは参照だけ。前景交代/回収はAV §5-6のリング/累積の消去、LOST、gen増加、振動停止を保持する。

## 6. 像と大きさ（実測ではなく予算）

| 部分 | 行数/メモリの見込み | 入る像・上限 |
|---|---|---|
| 共通状態/リング/旧出口/bridge | 製品約1,800〜2,400行、text増分8〜16KiB目標（旧コード削減前） | x86 vmkernel・PPC kernel。MINIMAL/起動FDも旧入力のため必要 |
| 基本BSS | held 2,048 + cnt 128 + raw/cooked 512 + 内部ring 2,048/seq 256 + 源/座標/lock管理約2KiB ≈7KiB | 動的巨大キューなし。既存PPC/PC98キュー削除後の純増をmapで測る |
| 焦点/独占/pad状態 | AV input/hal計約1,000行の内数、4slot含め約1KiB追加目安 | 常駐。初版はMINIMALでも同じ入力契約、USB本体は省く |
| USB | 確定設計§9: 段1 text約24KiB/BSS約4KiB、後段約42KiB | x86 usb.modは64KiB想定、常駐殻約3KiB。MINIMAL/起動FDにusb.modを入れない。PPC現行常駐組込みは維持 |
| kdebug | 入力製品/host fixtureを入れず増分0を目標 | `docs/08_build.md` のimage/mem各262,144B、load_span524,288Bを維持 |

- 内部ring 2KiBとseq 256B等は上段内数で二重加算しない。USBのkbd_merge/mouse_ext約3KiB枠も共通化分との二重計上を避ける。
- FDは圧縮後サイズとMINIMAL一式で判定し、空き容量を推測しない。実装の各統合段で前後map/size/FD作成結果を記録、収まらなければ構成判断へ戻す。

## 7. ファイルと段（新=新規、変=変更、行数は追加+削除の差分予算）

- 原則1段1〜3ファイル・400行以下。試験も数える。U1承認により公開ABIの原子的切替だけは例外とし、400行超をsolに出す。各段は対象を直接組むhost試験または既存試験の拡張で受入し、未結線部品は表の次段が接続先。
- 共通部品を追加/変更する各段（特に1a・3a〜3c）はx86/PPC両方の像のリンクを必須受入とし、PPCの未定義symbolを残さない。必要なbuild登録段Bをその段の受入前に完了する。
- `T_x` は `tests/support/fixtures/input_<x>_host.c`（新、80〜140行）、既存契約試験があればそれを変更。移動も追加+削除で数え、表の小移動は一回60〜100行を上限に反復する。

- 既存の基準はmainのARCH_COMPAT T1b（test_arch_compat.py (旧版 feb27053b188)）。AC-INPUT-1のPPC4件はXFAIL、AV_HALのIN2_MAX_DEV=16もmain反映済み。文書0/段0は実装対象から削除。

| 段 | ファイル（種類・概算） | 依存・出口・並列 |
|---|---|---|
| 1p | sys/hal/input/in2_int.h（新60）、T_bridge（新80） | 1a前にprovider関数ポインタ/不在契約を定義。両archで装置symbol非依存を確認 |
| 1a | sys/kern/input_legacy.c（新80）、kbd.c（変100）、T_bridge（変120） | 既存T1b/1p→非待機helperとtry内park_pollをbridge所有へ。GUIは注入のみ早期復帰、両archリンク |
| 1b | input_legacy.c（変80）、kbd.c（変100）、T_bridge（変100） | 1a→peek/local/captureの退行固定、同じファイルなので直列 |
| 2a | sys/hal/input/in2_int.h（変100）、in2_dev.c（新130）、T_source（新120） | 内部宣言のみ。公開AV0を先取りしない |
| 2b | in2_state.c（新130）、kbd_merge.c（新100）、T_merge（新140） | 2a→本体/USB接続先を固定。AV N4とUSB 1Dの状態を統合 |
| 2c | in2_ring.c（新190）、in2_time.c（新30）、T_ring（新140） | 2a後、内部InputEvent/seqのみ、AV0非依存。AV N2/N6の内部処理、公開変換はA0y。2bと並列可 |
| 3a[n] | legacy_ring.c（新/変80）、kbd.c（変100）、T_legacy（新/変120） | raw/cookedを小分け移動、合計3〜4段。削除分込み≤400 |
| 3b[n] | legacy_keymap.c（新/変80）、kbd.c（変100）、T_keymap（新/変120） | 両arch共通のJIS/Caps/Kana/CTRL文字化1本、2〜3段。PPC現行との差をT_keymapで固定。3aとkbd.c競合のため直列 |
| 3c[n] | legacy_mode.c（新/変80）、kbd.c（変100）、T_mode（新/変120） | gui_mode/STOP/flush、2段。V86 adapterは既存kbd側 |
| 3d | sys/platform/pc98/input/kbd_hal.c（新100）、kbd.c（変60）、T_merge（変100） | 本体src登録/capture、旧採取を除去して二重採取ゼロ |
| 4a | sys/hal/input/usb_input.c（新140）、PPC usb_platform.c（変60）、T_usb（新130） | 2b/3→usbpのkind/src写像・repeat・release。PPC文字入力維持 |
| 4b | PPC input_usb.c（変160）、input_boot.c（変100）、test_arch_compat.py (旧版 feb27053b188)（変100） | 4a/3b/4c後、PowerPC P3③bがあればその後（input_boot.cは4c→P3③b→4bの直列）。共通状態/出口へ。③bのlocalを共通cooked ringに結び替え、③bで足したUSBの非消費peekを消す。U3までは旧keymap policy維持。raw/pressed/modsのbit 2/4/8をPASS化しPPC期待14→0、XFAIL表示/変異期待を更新、CHECK pins=[input-hal/4b]を外す |
| 4b-map | platform/powermac/input_keymap.c（削除約70）、ppc_input_usb_host.c (旧版 d58b71db6a29)（変100）、ver-ppc-input-usb.py (旧版 b7cf31327e4f)（変80） | **追補 INPUT_BIND_DESIGN §6 の段の後**。4b後、build登録削除は直前B段。旧pm_input_keymap直呼びと27期待をU3準拠の共通口試験へ変更、CHECK pins=[input-hal/4b-map,U3]とbasisを更新 |
| 4c-pre | sys/kern/input_wait.c（新130）、T_wait（新130） | 1b後、待機契約を両archで受入、4cへ結線 |
| 4c | sys/kern/input_wait.c（変100）、input_boot.c（変60）、test_arch_compat.py (旧版 feb27053b188)（変180） | **1b/4c-pre後**（4bに依らない）。PPCは既存input_boot.cの先読み付きtry_key/peek（LANとUSBの交互選択）をproviderのlocalとして登録し（terminalはNULL）、InputOpsの読み口（getkey_wait・getchar・peekkey・has_key）を同じ経路で共通の待ちへ委譲する。input_usb.cは変えず、独自状態の除去はP3③b。共通input_wait.cへ期限規則を先に置く。完了条件: PPC providerで待機開始起点（x86 kbd.cと同じ）・各待機30秒・期限後の文字CR/キーESC・FREEZE中の閉鎖・中断の要求を受けたら抜ける（要求はhostの偽物、USBのSTOPの実物はU3/4b-map）を試験で満たす。bit1をPASS化しPPC期待15→14、XFAILの列挙からbit1を外し、ppc-cancel-xpassを新しい中断経路を壊すRED（観測15）に替え、raw/pressed/modifiersのxpassの観測値を12/10/6に直す。CHECK pins=[input-hal/4b]（残るXFAIL 2/4/8） |
| 4d[n] | input_wait.c（変80）、kbd.c（変100）、T_wait（変100） | 1b/4c後、x86 wait/parkを4cで先に置いた同じ共通期限規則へ口ごとに2〜3段で載せる。4bとファイルが重ならず並列可 |
| 5a | sys/hal/input/mouse_ext.c（新100）、pointer_state.c（新140）、T_pointer（新130） | 2a後、鍵盤移動とは並列可 |
| 5b | sys/platform/pc98/input/mouse_hal.c（新130）、mouse_bus.c（変30）、T_pointer（変100） | IRQ採取を一度に限定 |
| 5c[n] | mouse.c（変100）、pointer_state.c（変80）、T_pointer（変100） | 座標/bounds/差分を2段移動。描画backendはmouse.cに残す |
| 5d | mouse_hal.c（変90）、mouse.c（変60）、T_pointer（変120） | seamlessを旧pollの位置で1回採取して共通の口へ・旧poll消費除去（tickでの採取は1kHz tickの段B以降） |
| 5e | usb_input.c（変60）、PPC input_boot.c（変40）、T_usb（変130） | USB mouseを共通口→PointerOps。4b/5a後 |
| 6a | sys/hal/input/input_service.c（変100）、sys/platform/pc98/d2_bind.c（変50）、T_contract（変130） | 共通InputOps/PointerOps登録、旧slotは保持 |
| 6b | input/hal/input_work.c（新120）、IO登録側1ファイル（変30）、T_context（新130） | Tier2・再入/予算。実依頼時に登録側の現行pathを固定 |
| A0a〜 | AV公開宣言/C89 layout fixtureを各2〜3ファイル・≤350行で準備 | HAL v2 A0後。AV0約750行を1段としない。切替は承認済みU1 |
| A0z | kapi.json/生成/対象別不在provider/版の原子的更新 | U1承認済み。400行超のsol担当段で原子的切替、clean→all。既存AV0のsnd/MMLと二重版上げしない |
| A0y | sys/hal/input/in2_export.c（新100）、T_api（新120） | A0z/2c後に内部InputEvent→公開IN2_Eventを変換。全typeのpayload/seq/mods/flagsを検査、両archリンク |
| 7a | sys/kern/av_focus.c（新110）、exec接続側1ファイル（変20）、T_focus（新130） | AV0後、AV F1 |
| 7b | in2_excl.c（新150）、in2_reclaim.c（新100）、T_focus（変130） | 7a/2c後、AV N3/N7。IRQ書込み禁止 |
| 7c | sys/kapi/kapi_in2.c（新170）、sys/kern/exec/exec_finish.inc（変10）、T_api（変140） | A0y後、ABI公開の実体結線、AV K2/E1 |
| 7d | gshell/src/excl.rs（新180）、input.rs（変60）、multiapp.rs（変40） | AV G1、公開独占の有効化まで直列。既存WM試験で受入 |
| B[n] | build登録/manifest/試験CHECKを各回1〜3ファイル・≤150行 | 各結線前に必要分だけ直列、両archをビルドしてから次へ |

- AVのN5/P0〜P5（4pad/GIP→XUSB→一般HID→map）はUSB段1受入後、1実装ファイル+試験を基本に別段化。ドライバ拡張はHALへ接続する装置の続きであり、この回は実装しない。
- PC-98 USB残り1E: (a)platform殻+試験≤350行、(b)PCI MMIO予約+resource結線+試験≤350、(c)早期EHCI静止+接続+試験≤300、(d)resident+試験≤300、(e)module+試験≤400、(f)tick登録+定義+試験≤250、(g)build/manifest各≤150、(h)usb_stat_get ABIをU1同様に別扱い。各1〜3ファイル、殻は2回に分けてもよい。
- PC-98のDMA/PCI/tickはPPCのOF/TB実装を流用しない。BIOS EHCI引継ぎ/CF=0はOHCI段1から必要。新ハード設計をこの文書で追加せず、USB正典のRenesas S16265EJ5V0DS・FreeBSD ukbd.c:1102/1344・ums.c:218/592等の参照を継承する。

修飾なしの単独キーに割り当てた SWITCH_APP は最初の make で切替を確定する。同じ押下の repeat と break は消費し、窓には配送しない。

操作 ID の窓への通知は G の後の段で扱い、通知の形は未決とする。

## 8. 受入と変異（本回は計画のみ）

| 試験 | 合格条件 / 必須の実行時RED変異 |
|---|---|
| host契約 | 空=-1、能力なし≠未押下、非待機・中断・FG0/IRQで待機なし / 能力を全有効・中断無視 |
| src/merge/HID | 同じ修飾を本体+USBで押し片方を離して保持、左右Shift、rollover、16源成功/17源FULL、PPC8・PC-98計11源 / cnt無視break、kind別src衝突、上限8への戻し |
| release | USB Shift押下中に抜去→最後のsrcなら旧raw/in2ともbreak、他src保持中はsrc別in2だけbreakで旧Shift保持。ボタンも同様 / releaseでbreakを省く |
| lock/keymap | 本体make→ON/repeat→ON/break→OFF、USB A make→ON/break→ON、USB B make→OFF。本体ONならUSB OFFでも合成ON、本体OFFかつUSB ONならON、USB抜去でも共有bit保持。CTRL+Cは追補の表で検査 / 本体make反転、USB源別bit化、ORをXOR化、repeat反転 |
| V86 | V86中held/修飾は更新、旧raw/cooked/in2は投入ゼロ、退出後状態一致 / V86中in2投入 |
| ring | 128境界、LOST count/seq wrap、隣接同dev MOVEのみ合算、満杯STOP保持、repeat排除 / merge後採取、repeat混入、満杯状態更新省略 |
| bridge | GUI空でtryはpark1回、GUI localは-1/park0、peekは完成key非消費/選択順不変（有限採取可）、PLN1 has_key進行、link-SH mailbox非消費、getkey_waitのserial非読取、captureのCR/ESCとpark再開値 / GUIからserialへ落下、peek消費/交互順更新、waitでserial読取 |
| mouse | src別ボタンOR、独立read、i16/i32飽和、seamless初回/モード境界、USB signed dx / 差分二重採取、readで両累積を消去 |
| focus/WM | 排他境界gen/LOST、回収/STOP、X4で配送停止・X3で取消、振動0 / focusで累積を残す、X4でUI遷移 |
| context/portability | fake IRQ/FG0/FG1・再入・予算打切り、u32時計wrap、x86/PPC layout一致 / IRQからpump、msをjiffy扱い |
| NP21/W | CUI/gshell/端末注入・serial・STOP・V86・bus/seamless・bounds切替。NHDに検査SHAを配備して起動 / USB PCI実機の代替にはしない |
| QEMU mac99 7450/7455 | 同じ試験SHAでUSB鍵盤→sh、raw押下/解放、USB mouse→PointerOps、LAN並行、中断待機、抜去。GUIは起動可能な構成で別記、状態probeだけでGUI合格にしない |
| 実機 | Ra266本体+uPD720101、G4内蔵OHCI/カードを区別して同時押下・抜去・mouse・LAN併用。HighPoint交換はユーザーの物理操作。4MiB転送中p95≤50ms/max≤100ms（IO_TIER） |
- 実装時は各CHECKに入力を宣言。移動/削除行に付く既存変異を `rg -n` で探して移設、anchor一意・コンパイル失敗でなく実行時RED。触った像（kernel、PPC、gshell、MINIMAL/FD）を組む。ABI段はclean→all。
- 各段の触った試験を `--mutate`、最後に 旧手順（778201b36787 の記録）。check-changed/全変異は手元で回さずCI/full-checkへ。ログは最終変更後のものだけ。
- 本回は読むだけの作業木と設計書のみ。preflight --fix-fonts・build・lint・host/変異・エミュレータ・実機は実行していない（PACKの実装なし/書込先限定に従う）。調査と設計の整合/行数だけ検査した。
## 9. ユーザー決定（U1・U2・U3決定済み）
| ID | 決定と反映 |
|---|---|
| U1 | 「認める」。kapi.json・版・生成物・両arch providerの公開ABI切替は原子的な1段。400行超はsol担当、準備は小段化。§7 A0zに反映 |
| U2 | IN2_MAX_DEV=16へ拡張。8は物理制約ではなく、devのu8には収まる。PPC USB4+4とPC-98本体等3+USB8を保持。AV正典の範囲/表/試験条件はmain改訂済み。XInputパッド4slotは不変 |
| U3 | **確定 (2026-11-15)**: 追補 [INPUT_BIND_DESIGN](INPUT_BIND_DESIGN.md)。ユーザー決定「操作は各 arch の標準 OS が基準」(PowerMac は Mac OS X、PC-98 は CUI が MS-DOS・GUI が Windows) と「文字は BSD、操作は DOS」。文字の流れは全 arch で CTRL+C=0x03、操作 (中断・行の取り消し) は platform ごとの操作の割り当て表で決める |
## 反論
- なし（0件）。
## 確定 (2026-11-15、Opus 第 2 回・PowerPC PM の意見・PM 決定を反映)
- S1: 段0/文書0を削除、mainのT1bとAVの16源契約を既存基準として参照。
- S2: 4b/4cにtest_arch_compat.pyを割当、pinsとXFAIL→PASS（15→14→0、4c→4bの順）の更新を明記。
- S3(b): 4b-mapで旧keymap直呼び試験/実行器を改訂し、pinsとU3に従って期待を更新。
- S3(a): CTRL+CはU3で確定（追補）。4b-mapは追補の段の後。
- S4: USB全体の共通反転bitと本体の機械式bitをOR、複数USB・抜去の期待を修正。
- S5: IN2_MAX_DEVの唯一の定義場所をsys/hal/input/in2_int.hに統一。
- H1: local try/peek・mode値・bridge peek・GUI照会をprovider契約に追加。
- H2: PLN1 byte端末とlink-SH mailbox/行頭調停を像別に分離。
- H3: peekの有限採取を許し、完成key消費と交互順更新を禁止。
- H4: park_kbdとpark_pollの成立時非復帰・再開値・不成立時returnを明記。
- H6: GUI localの-1/parkなしとgetkey_waitのserial非読取をfixture化。
- H5: 指摘取り下げ。capture期限は待機開始基準、FREEZE維持、期限後CR/ESC、通知例外なし。
- 依存: 4cは1b・4c-pre後（4bに依らない）、PowerPC P3③bは4c後・4b前（input_boot.cは4c→③b→4bの直列）。
- H7: 4cで共通待機のcapture期限規則を先に置きPPCの期限・CR/ESC・FREEZE・STOPを完了条件化。現PPCの期限なしを§1に記載し、4dはx86を同じ規則へ接続、各待機30秒/全体host上限を明記。

## 10. A0z 追補案（2026-11-28、操作・端末文脈の公開）

基点は a77e100f1。AV0 の公開は v90、前景決定 F1・独占 G1 と PowerMac の USB Pointer 接続は main にある。ここで版を使うのは操作 ABI の残分だけとする。[操作設計](INPUT_BIND_DESIGN.md) §3・§4 の端末契約、[KEYMAP 設計](INPUT_KEYMAP_DESIGN.md) §5 の wire・意味イベント、[独占設計](INPUT_7D_EXCL_DESIGN.md) §5 の境界を合わせて適用する。AV の口と世代は [v90 追補](sound/AV0_V90_ADDENDUM.md) に従う。

### 10.1 公開表と実装の境界

v91 の末尾は slot 339。次表の順で v92 の関数を追加する案とする。全口の接頭辞は `kbd_policy_`、戻り型は int。表中の u32 は SDK の符号なし32ビット型を表す。set 系は成功時 0、read は非同期で取得件数（空なら 0）、失敗は負の OS32 エラー。owner と exec_gen は syscall の呼出元から確定する。ユーザーが他の実行単位や常駐設定用 owner 0 を指定する引数は設けない。

|slot|口と引数|観測できる結果|
|---|---|---|
|340|set(const void *wire, u32 length)|FG1 で ikm_policy_set。表全体の検証に成功してから世代を更新|
|341|get(void *wire, u32 capacity, u32 *needed)|ikm_policy_get の正規化済み差分表。容量不足は FULL、needed のみ更新。NULL,0 で必要量取得|
|342|reset(void)|ikm_policy_reset。最初の set/reset 成功時だけ復帰先を保存|
|343|context_set(u32 context, u32 flags)|input_context_owner_set。宣言するだけで前景と command_gen は変化しない|
|344–345|command_begin(void)、command_end(void)|input_context_owner_begin/end。旧 pending を破棄し、u32 の command_gen を通常加算して RUNNING+ISIG に入る。コマンドの有無は command_active で判定する。end は保存した文脈・flags に戻す。二重 begin / 対のない end は失敗|
|346|delivery_set(u32 mode)|owner 単位の BYTE=0（既定）/ACTION=1。切替時は compose・未配送入力を消し、表示復帰の後始末を完遂|
|347|action_read(Action *out, u32 max)|ACTION の端末通知。u32 順に action, command_gen, time_ms, value の16 B。同じ消費入力を byte にも積まない|
|348|key_read(Key *out, u32 max)|前景を仲介する WM が読む評価済み make/break/repeat。割当操作と文字を採取時の世代で一度だけ処理|

slot 347/348 の SDK 型名は KbdPolicyAction / KbdPolicyKey。context・flags・delivery の ID は本節、操作 ID・TERM.index は KEYMAP 末尾の「操作ID (1列挙)」と TERM 列挙、keysym・kind・map・profile と IKM1 の配置は KEYMAP §2–5 の列挙と v2 を正とし、BIND の旧 v1 wire 表に優先する。C と Rust に同じ定数を出す。

生成器の argument_guard_c の順序を採る。① invalid_length で引数の形を検査、② out 全域の書込権・in 全域の読取権を検査、③ target で provider、登録世代・呼出権限、独占、配送モードをこの順に判定する。①はポインタを参照せず、非空範囲の最終 byte が u32 に収まるかを長さの上限確認後に調べる。get の重複は両範囲の最終 byte を用いて判定する。

|口と範囲|① CPL0・CPL3 共通の INVAL|② CPL0 直呼び|② CPL3 syscall|③ 許可後の target|
|---|---|---|---|---|
|両 read の out（max × 16 / 48 B）|max > 128、正数時の NULL・4 B 非整列・末尾折返し。出力は一つ|ページ検査を省く|正数時に書込不可なら既存 fault kill|0 件指定は out を無視して 0。正数時だけ dequeue|
|get の needed（4 B）と wire（capacity B）|needed の NULL・4 B 非整列・末尾折返し、capacity > 16400、正数時の wire の NULL・末尾折返し・needed との部分または全重複。wire は byte 整列|ページ検査を省く|needed と非空 wire のいずれかが書込不可なら既存 fault kill|容量 0 は wire を無視。容量不足は needed だけ書いて FULL、十分なら両方を書いて 0|
|set の wire（length B）|16..16400 の範囲外、ヘッダを除く長さが 16 の倍数でない、NULL・末尾折返し。byte 整列、入力は一つ|ページ検査を省く|全域を読めなければ既存 fault kill|ヘッダ行数と length の一致を含む IKM1 の内容を検証してから交換|

input_policy_abi は次の競合を各々独立に観測する。CPL0 は形が正しければ呼出元がアクセス可能な実領域を渡す。失敗した呼出し自体による出力・表交換・dequeue はゼロとし、fault 終了に伴う owner 回収は §10.2 に従う。

|同時に成立する条件|CPL0 の期待|CPL3 の期待|
|---|---|---|
|①違反と provider 無効・非権限・ページ違反のいずれか|INVAL|INVAL、target 未到達|
|①適合、provider 無効または背景・非 WM・独占、②で要求するページ権限がない|ページ権限は判定対象外。③の拒否理由|fault kill、target 未到達|
|①②通過、provider 無効と他の拒否が併存（read の max=0 を含む）|九口とも NOSYS|九口とも NOSYS|
|provider 有効、登録済み非 WM の key_read と独占|INVAL|INVAL|
|provider 有効、前景 ACTION の action_read と独占、max=0|BUSY|BUSY|
|provider 有効、許可された read、max=0、out=NULL|0|0（ACTION）。Key の WM 権限は CPL0 限定|

Key は native u32 の src_generation, map_id, keycode, sc_down_repeat, physical_mods, logical_mods, time_ms, map_generation, keysym, kind, action, value の順。全 offset は 0 から 4 刻み、48 B。src は下位8 bit、続く16 bit が source 世代。sc は下位8 bit、bit 8–11 は down/repeat/no_keydata/is_modifier。map_id・keysym は下位16 bit、予約 bit は 0。raw と in2 の物理入力、端末の Action、WM の Key は別形式。独占中は in2 に物理入力を渡し、WM の通常配送を閉じる。境界で compose・押下・保留配送を清算し、消費した make の break を新窓へ漏らさない。

互換 scan が 0xff のキー（F13・F14 など）は、割り当てた操作と文字を WM で処理し、窓への物理 Key イベントは省く。

Key リングが満杯なら、新しい repeat は捨てる。make または break が届いた時は、未読の最も古い repeat を一件取り除き、残りの順序を保って末尾に新しいイベントを積む。未読がすべて make・break なら既存の入力を保持し、新しい make・break の投入失敗は従来の LOST の扱いに従う。
旧 raw が優先していた修飾なしの Tab も、WM が make・break を128件読み残した場合は LOST を許す。

IKM1 はヘッダ16 B、version=2、行16 B、最大1024行。多バイト値は LE、最大16400 B。全行を一度ずつ読み、欠落値は OS 既定から補う。kind 6 の置換単位は profile。重複、予約 bit、異機種 profile、保留中の ACCENT と非零 accent_count は INVAL。押下中または他 owner が保存中なら BUSY。失敗時は active・保存先・世代を維持する。終了・異常・STOP は同じ owner/exec_gen の ikm_policy_owner_exit で復元する。kind 5/6 は合計16行まで。編成結果が一面の容量を超えれば INVAL、交換も初回保存もしない。既定表を const で共有し、各面には最大1024件の明示差分だけを置く。差分列3113 B（3 × 1024 + 31 + 10）、64件ごとの索引16個 × 8 B、管理9 B と整列を含めて一面3256 B、active・編成中・復帰先の三面で9768 B を上限とし、圧縮像・BSS の増分を両機種で実測する。
IKM は OS 既定からの差分 profile。2026-11-28 のユーザー決定で行の上限を下げ、PM は1機種の全128キーを8通りの修飾・文脈で置換できる1024行（16 + 1024 × 16 = 16400 B）を基準とした。

pc98_input_keymap_register と pm_input_keymap_boot は必要な全 map 登録後、入力採取前に ikm_policy_init を一度呼ぶ。PC-98 は本体 map 1 と USB HID map 2、PowerMac は HID map 2 を渡す。押下照会は PC-98 本体と USB の合算、PPC は全 HID source の合算。初期化失敗は起動診断に記録し①②を通った九口は target で NOSYS を返し、旧入力で起動する。操作 profile は入力装置によらず PC-98 の CUI/GUI が 1/2（MS-DOS/Windows）、PowerMac が 3/4（Mac OS X）。共通 HAL が logical mods と表で評価し、WM に第二の評価器を置かない。例は PC-98 CUI の ESC による行取消、SUB (0x1a)+Enter による canonical の EOF 候補（物理 Ctrl+Z の値は表による）、Mac CUI の Ctrl+U・Ctrl+D、GUI コピーの Ctrl+C / Command+C。文脈は LINE_EDIT=1、RUNNING=2、BYTE_INPUT=3、GUI=4、flags は ISIG=1 と ICANON=2。ISIG 時の真正な VINTR は編集時に取消、実行時に中断。BYTE_INPUT と非 canonical の EOF は byte、空行の EOF 判定は編集側。IME 変換中の PC-98 STOP/ETX は取消を一度配送する。

### 10.2 前提と初期 shell

|前提|現行の製品入口と成立範囲|残る担当|
|---|---|---|
|M2/M3|本体・USB の make/break → km_eval・合算 release → legacy keydata。S0b/c/d、S2x/S3x、PPC S2m/S3m/S3b の旧出口は接続済み|A0 が評価済み世代を保持、A0z が read、G が WM 消費を接続|
|M4|W の ikm_policy_set/get/reset/owner_exit は内部単体成立、syscall と終了回収は未結線|A0 準備、A0z 両 provider で子 BUSY→親復元を確認|
|M5|input_context_owner_register/select/release・begin/end は内部成立、exec_launch/finish/reclaim が未結線|A0 準備で全 owner を接続、A0z で公開、x0/p0 は CUI 初期値|
|M6/M7|input_interrupt の世代一致・表示復帰、input_ime_gate と S4/S4i の FG1 取消・ON/OFF 順序は準備済み。通常中断の exec_abort 接続は未開放|U2 後 E-x/E-p で同世代中断を有効化|
|M8|HID usage → keymap、LANG1/2・Command・release は S1h/S2h/S2u/S2a。PPC USB の in2 独占 D は接続済み|R で両機種 acquire、BIND §6 F10 の 4b-map 新既定は別の開放判定|

初期 CUI の指定者は sys/kern/init/shell_loop.c の shell_loop_run。各周回で next_shell を取り込み、prepare の fallback を反映した最終 is_gui が 0 のときだけ、内部 exec 起動引数に初期 CUI の意図を渡す。理由 1 の --kcg-unavailable 付き起動、GUI ロード失敗と連続 fault 後の次周回にも同じ規則を適用する。一般 exec は既定 RUNNING+ISIG。ファイル名による exec 内の特権付与や次回起動まで残る大域フラグは用いない。

**exec の予約と回収（A0z 公開の前提）**。exec の slot 管理が予約ごとに u32 の世代を発行し、解放で消さない slot 別カウンタと予約中／実行中 slot の exec_gen に保持する。予約中／実行中の状態で有効性を判定し、同じ slot の旧世代は exec_gen の不一致で拒否する。shell lease は別寿命。exec_launch の FP 保存・初期化より前に、固定長の入力 owner 表で同じ slot を予約し、親の現行世代と初期文脈を検証する。予約は snapshot・syscall・前景選択から不可視。失敗は予約だけを取り消して既存の commit 前 exec_launch_abort でページ・起動元・slot を戻す。lease 発行・親 syscall suspend・AV 焦点変更はまだ行わない。

slot commit 後は既存順で shell lease publish／親 syscall suspend、AV focus、nest depth を確定し、割当済み入力 entry を IRQ 排他内で有効化してから arch_entry_enter。ここでは再登録・割当・失敗可能な検証をしない。A0z では CUI shell の公開と同じ IRQ 排他内で、first_shell と同じく APP_ID_SHELL の現行 exec_gen を前景に選ぶ。初回 entry から RUNNING+ISIG で前景限定口を使える。x0/p0 は予約時の初期文脈だけを LINE_EDIT+ISIG/ICANON に変える。一般子は RUNNING+ISIG、CUI の同期前景子だけを exec が選択する。GUI shell も初回 entry 前に GUI として登録・選択し、WM 初回通知までは shell に配送する。通知成功時にアプリ窓なら窓 owner の現行世代、机なら GUI shell の現行世代を選ぶ。通知失敗時には前景を維持する。

正常・fault・同期 STOP は finish、park 後の STOP は ABORTED reclaim で、slot 解放前に owner/exec_gen を確保する。同世代の表を復元し、compose・配送・中断 pending と世代付き表示資源を清算してから input_context_owner_release で前景を公開する。前景移行まで入力を旧世代へ再投入させない。子は解放 entry の親と親世代へ接ぎ替え、前景終了なら有効な親、非 shell 終了なら生存 shell、最後に前景なしの順。背景終了では現前景を保つ。次の予約には新世代を発行する。

**配送の権限**。自分の登録世代だけが context_set できる。begin/end・delivery_set は同世代の前景だけが実行でき、背景は BUSY を discard_pending より先に返す。登録世代不一致は STALE。action_read は前景・ACTION を要求し、採取時の owner/exec_gen と command_gen を照合する。背景・独占は BUSY、モード不一致は INVAL。key_read は常駐 GUI shell の現行 owner/世代かつ既存 WM の権限（APP_ID_SHELL、CPL0、GUI session）に限定し、アプリ窓が前景でも仲介できる。非権限は INVAL。KBD 独占中の通常 Key は BUSY とし、STOP・CTRL・GRPH・TAB の WM 制御経路と in2 を維持する。拒否はいずれも出力とキューを動かさない。con_sink 中の IRQ は背景 RUNNING を参照せず、文脈操作は WM が確定した宛先で一度処理する。

登録済み WM 自身の key_read（OP_WAIT(0) の handler 内を含む）と X4 の callback（wm_depth > 0 かつ g_gui_pump_busy）は現行 shell slot の owner・exec_gen で読み、その他のアプリによる直接呼出しは INVAL とする。

6b 合流時は §11.2 の input_work_invalidate_locked を旧 pending の清算に含める。成功条件を確定し、IRQ 排他を取ったまま旧配送先の反復枠と既存 pending を清算し、最後に新しい状態を公開する。適用点は表交換、前景の登録・選択・解放、配送に影響する context_set、command_begin/end、delivery_set、WM 焦点通知と G の消費口切替。前景から外れる時点で全反復枠を消す。背景だけの回収では現前景の反復枠を保つ。同 owner の別窓への成功通知も §11.2 の WM 経路で受ける。予約・拒否では印を維持する。

表交換・GUI 焦点交代（同 owner の別窓を含む）では旧 pending と compose を破棄し、旧窓への成功済み押下だけを清算する。消費済み make の break は同じ source の解放まで抑止。G は provider の初期化成功を確認した場合だけ WM pump を切り替える。失敗時は旧 raw・退避を清算せず旧 pump の通常 GUI 配送を続ける。成功時は切替境界で旧 raw・退避を清算してから通常配送の消費口を Key に一本化する。独占制御用 raw は通常配送へ戻さない。

アプリ自身が方針・文脈・配送を変更した時は、その成功を入力の区切りとする。set/reset、配送に影響する context_set、command_begin/end、delivery_set などの変更前に採取した未配送 Key は、旧 raw なら同じ窓に届いていた場合も破棄する。WM またはユーザーの操作による焦点交代も、同 owner の別窓への切替を含めて区切りとする。旧 raw が新しい窓に渡していた修飾なしの Tab も、旧窓向けの未配送 Key として破棄する。拒否された呼出しでは未配送 Key を保持する。アプリの文脈変更では key_read の成功を配送済みとし、X4 が X3 に退避した分は保持する。焦点交代では WM 自身が旧窓向けの退避分も破棄する。

### 10.3 登録検査を先行させる

現行の hal_contracts は HAL 登録、kapi_abi / kapi_sync は表順・生成同期を測る。独占時の配送件数と順序は追加登録で観測する。実装の最初の候補を下表の R とし、各 ID に tests/registry.toml の spec・sources・expect・limits・mutations と host 本体を対で置く。正典は本節と 7d §5。歴史的な PPC NOSYS の期待は現在の両機種 acquire 成功へ更新する。

|R の登録案|製品入口から観測する正常対照|先に RED にする変異|
|---|---|---|
|input_focus（C host）|両機種設定で acquire、KBD→MOUSE、unacquire、焦点交代、ABORTED reclaim。保持 A は反復せず、二台の最後の break 後の make は通る。in2 の入力は続き、交代の先頭 LOST{FOCUS}、回収後 LOST{EXCL}、STOP 一回|解除経路ごとの保持記録を省く、最初の break で解除、in2 に WM の門を適用、LOST 順逆転、各機種の公開を閉じる|
|gshell_excl（Rust host）|二窓で make と Pointer を記録、満杯→一部成功→再試行で旧窓へ release が各一回。消滅・再利用した窓と新窓への漏れゼロ。raw/退避/配送を個別に満杯にし、追加打鍵なしで STOP と切替確定。ドラッグ枠取消・静止解除時のカーソル・非独占の通常配送も確認|失敗 release を消す、成功分を再送、slot だけ照合、満杯で制御キーを読まない、確定 raw を新窓へ配送、取消を通常 up に変える|

7d §5 の両表を R の変異台帳とし、各スラッシュで並ぶ変異も一つずつ独立に実行する。追加の割付は次の通り。

|登録案|正常時に採る観測|個別の変異|
|---|---|---|
|input_focus|legacy・keymap 各々で最後の break を GUI 外で受け、戻って再押下。raw IRQ の保持記録書込ゼロ、解除時は IRQ 禁止|各 release 世代加算を削る／一台目で加算／raw で valid 消去／restore 後に記録|
|input_focus|独占中の in2 全入力、WM の STOP・CTRL・GRPH・TAB、通常窓への配送ゼロ。非独占 owner 交代でも保持採取|in2 に門を掛ける／各制御を止める／通常配送を開く／owner 交代の採取省略|
|gshell_excl|物理・マウスキーの両方で独占中ゼロ、右保持解除後は新規左だけ。枠取消、CLOSE 一件、同周期再合成、静止解除で現在位置に表示|物理直渡し／合成門を外す／通常 up／CLOSE 再送／取得を再合成後へ移動／show 省略|
|gshell_excl|焦点通知失敗から再試行、X4 途中・清算中の新窓配送ゼロ。gen 単独変更、同 owner 別窓、失敗 make、Pointer 単独、旧窓起床と再送を各入口で観測|失敗を通知済みにする／X4 再開／各入口同期・両デバイス終了伝播・同 owner 掃除を個別に外す／excl のみ比較／失敗 append を記録／Pointer 記録省略／注入後に門／起床停止／再送表消去|
|input_focus（raw）・gshell_excl（退避・配送）|各キューの保護順位を取り出し順で確認。二つの GRPH 区間が後続打鍵なしに二回順に確定。配送満杯でも制御は進む|各隣接順位の逆転／raw 置換・確定枠予約を外す／別区間を重複扱い／退避満杯で早期 return／space_ok 先行|
|input_focus・gshell_excl|ABORTED 回収の STOP 一回と LOST 順、非独占アプリ・机・KBD_NAV 対照。両機種の実 include 選択で acquire 成功と gen 更新|解除後 clear／STOP 掃除省略／FOCUS 順逆転／常時配送停止／機種設定を個別に反転|

新規登録案 **input_policy_abi** は実関数表・CPL3 wrap から九口を呼び、公開前は旧版の要求版照合と関数表末尾で slot 未追加を記録する（x86 CPL3 の範囲外 dispatch は kill）。別の対照には候補と同じ要求版・型・九 slot・生成 wrap を揃え、provider だけを未結線にする。各口に形・ページとも正当な引数を渡し、CPL3 の版照合通過、wrap 到達、target の NOSYS を個別に記録して RED とする。結線後は同じ呼出元で各口の結果を照合する（非 WM の Key は INVAL）。SDK・生成側・wrap・内部検証で同じ上限を用い、1024行の有効表の受理、1025行の拒否と旧表保持を確認する。短い get・重複・整列・境界・max=0、背景 begin/end、非 WM の Key、古い世代の Action と拒否後の正当な読出しを検査する。PPC BE は Action/Key の全 offset と native 数値、IKM1 だけは LE の各 byte を x86 と照合する。

新規登録案 **input_exec_entry** は両機種 shell_loop → exec_launch → 最初の arch entry の snapshot を観測する。A0z 前の RED は shell の登録世代なし。A0z で登録世代と前景 snapshot の owner/exec_gen を別々に読み、CUI 初回 entry で delivery_set(BYTE) が 0 になることを確認する。登録済み・前景一致・同口成功でも文脈が RUNNING+ISIG なら x0/p0 用の独立した RED。各機種の初回・fallback・再起動で LINE_EDIT+ISIG/ICANON を観測して GREEN にする。GUI は初回通知前・アプリ窓・机の順に前景世代を読み、各時点の前景から delivery_set(BYTE) の成功を確認する。登録予約失敗を注入し entry 未到達、FP・lease・AV・親の次回 syscall と前景不変を確認。正常・fault・同期 STOP・park 後 reclaim、親先行終了、同 slot 再起動を通し、子の旧入力ゼロと次の前景世代を読む。

R は各 ≤350 行の観測単位に分割する。既済み 7d は正常対照 GREEN と契約固有の変異 RED を先に取る。新 ABI・初期 shell は同じ候補系列で未結線の出口・最初の user entry の snapshot を期待して RED を記録し、製品接続で GREEN にする。恒常的に失敗する登録を main に載せない。登録 lint、各 ID の --mutate、最終ソースでの正常対照を受入とする。host は NP21/W・G4 実機の証跡を代替しない。

### 10.4 実装順と受渡し

|段・差分上限（試験込み）|次へ渡せる条件|
|---|---|
|R → A0 準備、各 ≤350 行|上記の赤を確保。C89・Rust・PPC BE で Action/Key の全 offset と v2 LE の同一バイト列を確認。リングと provider を非公開で準備|
|A0z、sol の原子的切替 ≤400 行例外|sdk/kapi.json 末尾追加、version と shared header の KAPI_VERSION を同期。C と Rust の生成器を実行し、両機種の実 provider、関数表、要求版、KAPI_SPEC・README・INDEX を同時更新。kapi_abi・kapi_sync・c_dialect、make clean → make all、PPC 像リンク。v91 から先行版が入った場合は末尾と次版を再採番。外部 apps/game は H-7 に従う|
|共通ラッパー ≤380 行|userland/lib/input に文脈・操作ラッパーを置き、既存ゲームの input_bind とは独立に SDK の結果・失敗を伝える|
|U2-x0 / U2-p0 各 ≤350 行|x0 が共通 shell_loop・exec 引数と PC-98 入口、p0 が pm_shell_loop_ops の機種入口と検証を持つ。PC-98 と PPC の実入口で初回・fallback・再起動の最初の poll まで LINE_EDIT+ISIG/ICANON。GUI と通常子は対象外。x0 後に INPUT-S4-CANCEL を実機で確認|
|U2 各 ≤350 行|**最初に ACTION の配送の owner の実打鍵を Action のリングへ積む結線** (input_delivery_action、同じ消費入力を byte に積まない — A0z の後、それまで action_read は 0 件、PM の決定 2026-11-28)。両 shell の prompt・poll・組込 begin/end・外部子の親復帰・EOF/ESC を通す|
|E-x / E-p 各 ≤380 行|U2 と M6/M7 後、arch adapter を exec_abort に接続。RUNNING の真正な中断だけを同世代へ配送、IME 取消と表示復帰、PC-98 CTRL+STOP と PPC Ctrl+Pause の救済を確認|
|L 各 ≤350 行|両 E 後、input_legacy の serial/LAN/注入を sc=0 で一度だけ解釈し、peek 無副作用|
|G[n] 各 ≤380 行|A0z・共通ラッパー後に CUI と並行。**最初に実打鍵の採取 (input_capture_key) から Key のリングへ積む結線** (input_delivery_key、採取時の評価済み世代を持つ — A0z は口の公開だけで、それまで key_read は 0 件、PM の決定 2026-11-28)。WM が評価済み Key を一度配送、独占・焦点・消費 make/break を R と同時に通す|

os32powerpc へは公開 layout・HID provider 初期化契約・共通 exec 引数・R の期待を渡す。担当は PPC provider の BE 照合、p0、E-p、7450/7455 の USB/LAN CUI と GUI・独占の退行確認。QEMU の記録と G4 実機の未確認項目を分けて報告する。


版切替では KAPI_SPEC §3・ROLES §4-3 に従い、shared header の版数と JSON を照合する。生成物は手編集せず、HWDEV・FEP の該当する指紋・要求版を生成器／関連検査の出力で基点と比較し、変化の理由を記録する。POLICY_DEV §10 に従い kernel・SDK・userland・像を一式入れ替え、既存の早期版照合で混在を検出する。今回は既存 slot と意味を維持する末尾追加で互換層は不要。既存口を壊す将来変更には [ABI] の個別承認が要る。

PC-98 と PowerMac の各初期化入口で ikm_policy_init の失敗を注入し、機種ごとに三つの結果を別々に記録する。①②を通る九口の NOSYS、旧 pump を使う GUI の通常窓への一打鍵一配送、旧 CUI での文字入力と Enter によるコマンド実行。成功対照では G が Key を一度だけ配送する。

受入記録は機種別に採る。PC-98 は NP21/W で初回・fallback・再起動、IME 取消、二窓の保持・独占解除・CTRL+STOP を確認する。PPC QEMU は ppc_common_compile・ppc_media_build と起動、BE の九口、初回 entry・親復帰・slot 再利用を確認する。G4 7450 と 7455 はそれぞれ USB 打鍵・LAN の CUI、GUI の Command 操作、独占・Pointer・Ctrl+Pause 救済を記録する。実機未確認は機種・項目ごとに残す。

### シングルタスクの棚卸し (2026-11-28)

- IKM1 は一機種の128キー × 8通りを収める1024行・16400 B に統一し、受入検査に上限とその直後を加える。
- 編成表は既定値を共有するため一面3256 B に縮める。交換失敗時の現行表保持と終了時の復元に要る三面を確保する。
- command_gen の0飛ばしを削る。同じ owner で毎秒10回 begin しても一周は約13.6年後で、実行中かは command_active で分かる。
- exec_gen の0飛ばしを削る。同じ slot を毎秒1回予約しても一周は約136年後で、通常の再利用には状態と世代一致の照合で足りる。

## 11. 6b 入力の後続処理を FG1 で進める（設計追補案）

6b で移す仕事は、両機種の **USB 鍵盤の期限到来済み repeat を旧入力出口に一度配送する処理**。USB 側が時刻と反復対象を決め、常駐 HAL が配送する。GUI の協調点で進めるのはこの配送まで。既存の WM が配送結果を読む。§10 の新しい Key/Action ABI、1 kHz tick、パッド実装を前提にしない。

### 11.1 現行入口と分担

|入口（現行ソース）|6b 後にその場で完了する仕事|FG1 へ渡す仕事|
|---|---|---|
|PC-98 IRQ1、pc98/input/kbd.c の kbd_irq_handler|8251 の状態を1回判定、データは最大1 byte。エラー処置、V86 分岐、救済 STOP、物理押下・lock・raw/cooked/in2 の有限更新は入力時点で確定|なし。本体の再送 scancode も現在の順序で扱う|
|PC-98 IRQ13、mouse_bus.c と mouse_hal.c|1ラッチ・4 nibble、MOVE 最大1と BUTTON 1。seamless は mouse.c の snapshot から従来どおり1回だけ採取（§7 の段表 5d）|なし。旧差分・in2 差分は各読者が独立に消費|
|PC-98 USB の OHCI 割込み、usb_resident.c の usb_tick / usb_idle|割込みは要因の採取。tick と idle の scan_pending 調停、モジュール呼出し、ログ排出を既存の所有者に残す|鍵盤 tick の期限到来通知だけを共通 HAL へ渡す|
|PowerMac の pm_usb_boot / pm_usb_poll、usb_boot.c の service|OHCI の MIE は無効。100 Hz の常駐サービスと10 ms 間隔の補助 poll が同じ HC を進める。USB の排他中は EE=0|同じ期限到来通知。DEC 出口で dispatch しない|
|両機種の usb_service.c、hid_kbd.c、hid_mouse.c|HC 当たり scan(32)、完了・制御・探索・再 submit は USB 所有。鍵盤 boot report は8 byte、修飾8 bit＋6 usage の差分を既存の3順×256候補で採取。マウスは1報告の相対移動とボタン。切断は callback 停止後、源別解放は HID map の256 usage を走査し、押下済み usage ごとに break を処理。互換 sc の集計は128欄|kbd_tick は従来の初回500 ms、以後30 ms の整数期限を維持。1 iface 当たり1回の通知まで。協調が遅れた回数を後で連射しない|
|gshell/src/pump.rs の gshell_gui_pump、input::capture、X3 の待ち|X4 が上限付きの raw・pointer 取込みと窓用リング追記、X3 が焦点・ドラッグ・閉じる操作を担当。カーソル描画は既存 backend。通常の syscall / wait の FG1 巡で repeat を進める|HAL へ WM callback を登録しない。HAL 側は旧出口までとし、同一打鍵を別の GUI キューへ複製しない|
|kern/input_legacy.c と機種別 InputLegacyProvider|PC-98 の serial 優先・GUI 注入・IME、PowerMac の LAN decoder/lookahead と USB の優先順、peek と try の消費差を保つ。wait は既存 idle_wait_step を使う|端末を先読みして消費する仕事は追加しない。将来の振動出力は USB サービス所有。現行 in2_pad_vibrate は接続 pad 不在を返す|

USB サービスの IRQ マスク区間には HC・機器・iface の走査、転送 callback、探索が入る（usb_service.c）。scan(32) は HC ごとの一呼出の上限。6b の追加走査は保留4枠と io_request の登録表最大16枠。期限では従来の状態評価を1回行い、配送値を1枠に保存する。通知に割当・待機・時計予算換算を含めない。採時・集約は kdebug 有効時に限る。サービス全体の IRQ マスク時間と、FG1 が1枠の検証から旧出口の完了まで再度 IRQ を止める最長時間を分けて測る。

### 11.2 登録と通知の寿命

新設の常駐 sys/hal/input/input_work.c に int input_work_register(void) と本書で予定した input_step を置く。io_desc.step は state 引数を受ける薄い adapter。PC-98 は pc98_boot_input（sys/platform/pc98/boot.c）の kbd_init 前、PowerMac は pm_usb_boot（sys/platform/powermac/input_keymap_backend.c）の pm_usb_scan 前に一度登録する。後者は pm_input_boot より早く HID が動き得る。callback/state はカーネル常駐。USB モジュールは既存 import の usb_input_usage(handle,usage,down,repeat) を通し、その repeat 分岐を常駐側で切り替える。登録失敗は起動診断に返し、旧同期 repeat を使う。成功時にだけ移行し、同じ通知に二つの配送経路を開かない。

入力サービスの内部登録値は Tier2・120 µs 固定。io_work.c の登録時と policy 更新時に入力 ID には同じ固定値を供給し、既存の dispatcher と slice に接続する。IOT1 は4行・80 byte のまま。USB repeat に利用者別の優先度・時間設定を必要とする用途はない。

保留は USB 鍵盤 handle 当たり1件、最大4件。src/generation、usage と、採取済みの KmKey・old・KmEvalResult・raw_due・修飾値を値で保持する。USB iface やモジュールのポインタは保存しない。各期限で既存 capture の検証・km_state_apply・評価を IRQ 排他中に一度行う。V86 中はこの時点で配送を抑止する。他は同じ枠を上書きして通知する。FG1 は評価済み値による旧出口だけを実行する。期限→ShiftA make→FG1→break は tap 成立、ShiftA make→別キー期限→ShiftA break→FG1 は期限時に tap 解除済みで ALT は反転しない。印の破棄は採取済み状態を巻き戻さない。

HID と常駐 make は sys/hal/input/keymap.h の共通 inline 判定を用いる。HID map の norepeat bit が0、compat_sc が128未満、4文字面のどれかが void 以外の場合だけ対象を更新する。hid_kbd_repeatable はこの判定に委譲する。kbd_done は usage 通知後に対象を更新するので、常駐 usb_input_usage はその make 中に同じ判定で旧対象の枠を消す。非反復 make は HID の対象と次の正規期限を保つ。PC-98 本体と全 USB 鍵盤で、後発の通常 make が同期で旧出口へ進む直前に、同じ IRQ 排他内で全源の旧印を捨てる。A の期限後に B を打てばその巡は B だけを出し、次の正規期限から反復を再開する。対象 break・detach は源別に解放前に消す。修飾・lock の遷移は PC-98 本体、全 USB 源、切断 release と boot lock 解放を含め、状態更新と同じ IRQ 排他内で全旧印を消す。

共通内部口 input_work_invalidate_locked は配送境界で全4枠を消す。鍵盤の解放では該当源の枠を消す。6b 単体では exec_enter/leave の前景変更、in2_excl の transition・reclaim による配送変更、kbd_set_gui_mode、PC-98 v86.c の active 設定・解除で、旧印の清算から状態公開まで IRQ 排他を保つ。

WM は sync_av_focus で最後に通知が成功した front_id と owner を現在値と比較する。X3 で最前面の窓を変更したら、次の FG1 巡・入力取込みより先に sync_av_focus を呼ぶ。同 owner でも窓 ID が変われば既存 av_focus_set を呼ぶ。カーネルは認可後、select_owner の同値 return より前に全枠を清算する。成功時だけ WM の通知済み窓・owner を更新し、excl の既存 purge_boundary を経て新窓の配送を開く。拒否時は新窓への配送を閉じたまま境界通知を再試行する。A0z 合流後も同じ WM 通知を §10.2 の権限検証・清算・公開へ通す。FG1 は枠の選択から旧出口への配送完了まで同じ IRQ 排他を保つ。

### 11.3 予算と再予約

1単位は1枠の検証・消費（無効化済みの取り出しも最大1単位）。1巡最大4件、通常120 µs、max_unit_us 初期見積り30 µs。数値は整数の初期調整値。両機種で最長1単位を測り、公平枠で30 µs を超えれば値または単位を見直す。1件の途中で切らないため、時限は次を開始する上限である。

1. 製品入口は AC-CTX 適合の idle_work_run / 明示 pump 経由の io_dispatch に限る。自動入口は idle_work_run の IF/EE・IRQ 深さ検査を使う。明示入口 kapi_io_pump にも dispatch 前に同じ検査を置き、不適合時は仕事量0。呼出元はロック外の FG1 に限る。FG0・排他中は採取・清算・io_request まで。adapter を呼ぶのは io_dispatch だけで、入れ子の dispatch は既存 running により仕事量0。
2. dispatcher の slice を引き継ぐ。io_slice_limit(slice,120,4) で残時間・公平枠1件を広げず、各単位の直前に io_slice_take を行う。定常時は換算済み表を引く。表 miss 時の整数換算は FG1 内に限る。120 µs は開始締切であり、4件消化や締切までの完了は保証しない。換算失敗、時計世代交代、時限・件数到達では未着手の枠を保つ。u32 raw count の差と source/generation は既存 slice API に任せる。
3. IRQ 排他内で固定4枠から有効な印を一つ選び、鍵盤 source の同一性と押下を確認し、印の消費と評価済み値の input_capture_delivery / input_emit_keymap による配送1件を不可分にする。割込み状態を戻してから次の単位へ。無効な印を捨てても後続枠は残す。in2 への repeat、押下数の加算、lock 反転はゼロ。
4. 印が残れば IO_MORE、空なら IO_DONE。io_work.c が callback 前に pending を下げ、IO_MORE で再予約する。走行中の新しい io_request は次巡まで残る。HAL は dispatcher の pending を消さず、終了判定の直後の到着も失わない。未到来の期限には IO_MORE を返さず USB の次回通知を待つ。協調点のない区間には配送期限を約束しない。

### 11.4 RED を先に取る段と受入

|候補（試験・登録込みの差分目安）|製品入口の観測と独立の変異|
|---|---|
|R、各 ≤280 行。登録案 input_work_context と input_work_hid|前者は実 io_policy・io_work・idle_work と整数 fake clock、後者は実 HID 報告・tick と両 adapter・旧読出口をつなぐ。現行では ID 未登録、期限到来直後で FG1 前に旧出口へ1件出る。この二つを別々の RED として保存。欠けた新関数のリンクエラーを RED の代用にしない|
|P、≤280 行。常駐登録、ownership/build と両起動入口|両起動順、policy 更新後も固定予算、重複・満杯失敗時の旧経路を観測。PPC 登録を scan 後へ移す変異と登録失敗で無配送になる変異を個別に検出|
|W、≤280 行。保留4枠・消費と HID 結線|4台保持、499/500/530 ms と ms wrap、期限後 pump 前0・後1、二重 tick でも1、break/repress・抜去同枠再利用・焦点往復後0。PC-98 本体＋USB と PPC USB 二台で最後の break、in2 の repeat 0、旧 GUI/CUI に各1。変異は同期配送、期限印の重複、鍵盤 source 照合・境界清算の個別省略、in2 への追加|
|T、各 ≤280 行。文脈・競合・機種別の退行|IRQ/FG0・IF/EE=0 で印だけ、FG1 入れ子の二重消費0。4枠＋消費中の再通知、公平1件、時限ちょうど、u32 wrap、clock token 交代で次巡再開。take を消す、件数を広げる、残件で DONE、最後に pending を消すを各々 RED にする。追加到着のない4枠は1件予算の4巡で消費する|

R は候補上で先に実行し、実装で同じ観測を GREEN にしてから取り込む。tests/registry.toml に spec・sources・expect・limits・mutations を宣言し各登録の --mutate と lint、src_owner・build_sources・include_layers・link_layers・ppc_common_compile を最終差分で確認。PC-98 像と PPC 像で text/BSS と最長単位を記録。NP21/W は本体打鍵・STOP・seamless・GUI/CUI、USB 搭載 PC-98 は USB 保持と抜去、PPC QEMU と G4 7450/7455 は USB 保持・Ctrl+Pause・LAN 入力・GUI 切替を機種別に記録する。実機未観測の時間値は推定のまま扱う。

**段別の追従と独立対照**。各変異は一つずつ入れ、同じ入口の正常対照が通る状態から指定した観測だけを赤にする。

|段|入口と正常対照|変異と判定|
|---|---|---|
|P|入力登録と既存 policy の変更を通し、4行・80 byte の get と入力の固定予算を確認|入力を公開行に加える変異を検出。sfs_stale_peer も再実行|
|P/W|input_work.c は common HAL 所有、両像の常駐 source set・ownership.mk・PC-98 の link order に登録。HID は PC-98 モジュールと PPC 像で同じ header 判定をビルド|既存 usb_input_usage import を通して PC-98 旧出口で1件。PPC の USB REPEAT trace は kdebug 有効時の期限通知、旧読出口の pump 前0・後1で配送を判定。import ABI・trace 形式は追加引数不要のため変更対象外|
|W|USB A の期限後、FG1 前に本体または別 USB 鍵盤から B を実打鍵し、CUI で B だけ、次の A の正規期限で A を読む。非反復・非修飾 make でも同期出力時に旧印0、元の対象・次期限は維持。同じ鍵盤の新反復対象 make は対象を更新する。本体または別 USB 源の修飾往復・lock 往復・修飾源抜去後は0、次の期限で1|判定の各条件を単独除去し対象と期限を読む。後発打鍵時の全枠清算を外せば同じ巡で BA。源内だけ清算、lock 清算省略、release 清算省略を各々検出|
|W|両 ShiftA 順序で ALT 状態を同期経路と比較。V86 突入直前の印、ゲスト中だけの期限は復帰後0、復帰後の新期限は1|採取時 apply 省略と配送時 apply 再実行を別々に注入。突入清算だけを外す対照は突入前に印を作り、active 公開直後・ゲストの初期限前に保留0を読む。通知抑止だけを外す対照は空枠で突入し、ゲスト内の期限直後・復帰前に保留0を読む|
|T・合流|各成功境界の前に期限を置き、同 owner の別窓、同 slot 再 exec、command_begin/end、delivery_set 往復を WM・exec・公開 KAPI から通して旧出口0、新期限1。拒否なら旧印1|各境界の清算だけを外して旧印の残存を読む。実 WM で同 owner の別窓を選び、次の FG1 後に切替前の印から新窓への配送0を両構成で確認。owner だけの同値 return を戻す変異で通知漏れを検出|
|T|idle_work_run と明示 pump の双方で IF/EE 禁止・IRQ 深さ非零を別々に注入し旧出口0、適合時1。|各入口の文脈検査を個別に外す。公平枠1件で2枠を用意し件数制限を外す変異は同巡2件で赤。時計は両件が開始可能な値に固定|
