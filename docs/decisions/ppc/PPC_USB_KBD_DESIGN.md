# PowerMac USB キーボード → 製品 sh 詳細設計 final

> 状態: **確定 (2026-11-13)** — 出所: `/home/hight/os32-tmp/run/ppc-usbkbd-impl/DESIGN.final.md`。PM が反映した確定設計の写し。

r1→r2 の変更
- R1: 起動待ちは LAN OPEN または USB の最初のキーで解く。USB キーは消費せず、LAN の遅延起動を保つ。
- R2: 既存4本の置換 anchor を各1箇所に保ち、待ちの新文字列へ追従し置換件数を検査する。
- R3: LEFT/RIGHT/HOME/DEL を追加し、6種類の特殊キーを sc<<8 に統一する。
- R4: serial のログ混入を避け、stream の印とプロンプト境界で実行結果を判定する。
- R5: HID の修飾先行通知を実装の行番号で確定する。
- R6: is_pressed/modifiers/shift_state はこの段では0のままにする。
- R7: LAN の行喪失・OPEN による取消は USB で編集中の行にも効くと明記する。
- PM: §6を決定に変更し、カナ・IMEの後段への延期と DEFERRED_TESTS 記載行を確定する。

- 対象: main 187e02a7、PowerMac / QEMU mac99、`KBOOT_SH=1`。設計のみ。
- シングルタスク・協調切替・整数演算。共有 C に PowerPC 分岐を追加せず、x86 の生成物を変えない。
- 調査は PACK 指定の資料・コードの該当箇所のみ。GPL・QEMU ソース・archive は未参照。

## 1. 実物の確認

| 境目 | 実物と根拠（現在の行番号） |
|---|---|
| LAN bytes の取り出し | `platform/powermac/input_boot.c:15` が terminal poll、`:20` が `pm_terminal_trygetkey()`。通信側の内部は今回の読取範囲外 |
| bytes → sh 用キー | 同 `:22` が `pm_input_decode(&decoder,c,now)` を呼ぶ。`:8` の先読みを peek / try / blocking で共有 |
| Enter・BS・取消 | `platform/powermac/input_decode.c:14` は取消通知・Ctrl-C→ESC、`:41` は CRLF 統合、`:42` は LF→CR、`:43` は DEL→BS |
| 履歴 | 同 `:24` は端末の ESC ` A/B を `0x3A00/0x3D00` にする。文字は下位 byte、特殊キーは上位 byte |
| provider の登録 | `platform/powermac/input_boot.c:41` の InputOps を `:53` で登録。`:26` はキー、`:28` は下位8bitを返す |
| kernel の中継 | `kernel/input_service.c:49` が provider を選択、`:50`〜`:58` が getchar/getkey/KAPI/wait をそのまま委譲。変換器もキューも持たない |
| USER sh 側の契約 | `docs/decisions/ppc/PPC_SHELL_SC_DESIGN.md:138`〜`:146` はキー形式／文字形式を provider が供給、既存 USER sh を使う契約。syscall 内部の行は今回未調査 |
| kbd_inject の形 | `kernel/kbd_inject.c:186` は UTF-8 bytes を受ける。`:196`〜`:205` は reader / owner を照合、`:210` は宛先付き byte ring へ格納 |
| kbd_inject の読出し | 同 `:248` は reader 指定で1 byte、`:272` は現在の reader。scan・make/break・修飾の解読はしない |
| PPC の現経路 | `platform/powermac/input_boot.c:10`〜`:29` に kbd_inject の呼出しはない。LAN→decoder→InputOps→既存 sh 入力契約が接続点 |
| USB の現終点 | `platform/powermac/usb_platform.c:156` / `:158` は key / repeat をログに出すだけ。`:162` は release のログと src 割当解除 |
| scan の意味 | `include/kbd.h:10`〜`:69` は PC-98 定数。SHIFT=0x70、CAPS=0x71、KANA=0x72、GRPH=0x73、CTRL=0x74 |
| HID の通知順 | `drivers/usb/hid_kbd.c:25`〜`:29` は0x70から走査し、make/break の両方で修飾を通常キーより先に通知する |
| HID の写像 | `drivers/usb/hid_kbd_map.c:11` は A→0x1d、`:47` は Enter→0x1c、`:63` は Caps→0x71、`:107`〜`:117` は JIS と左右修飾 |
| PC-98 側の変換 | [docs/decisions/usb/USB_DESIGN.md:301](../usb/USB_DESIGN.md) と `:566` が `drivers/input/kbd.c` の deliver と修飾状態を指す。Caps・カナを含む文字化はこの系統の責務 |
| PPC での再利用 | PACK のリンク前提では `drivers/input/kbd.c` は入らない。指定範囲にその変換表本体はなく、関数名・Caps/カナの詳細動作・現在の行番号は未確認。既存変換器を呼べるとは扱わない |
| USB サービス | [docs/decisions/usb/USB_DESIGN.md:833](../usb/USB_DESIGN.md) は tick slot 0 と、EE=0 の syscall 待ち中にも動く10ms単位の terminal poll。時計は timebase |
| 今の受入 | `tools/ppc/usb_run.py:99` は LAN 接続待ちを削除、`:103` は LAN byte 入力を probe に置換。`:180` は scan ログを照合する |

実物との食い違いは次の4件（未調査箇所は件数に含めない）。
- D1: PACK の「kbd_inject を経由」は現 PowerMac provider には当てはまらない。ここへ scan を注入する案は採らない。
- D2: USB 設計 §3 の release は1引数だが、現在は `usbp_input_release(int kind,u8 src)`。1A 追記 [USB_DESIGN.md:823](../usb/USB_DESIGN.md) と実装 `usb_platform.c:162` を採用する。
- D3: USB だけで入力するには、従来の TERM_OPEN 待ち（`PPC_SHELL_SC_DESIGN.md:97`、試験の置換 `usb_run.py:99`）も製品で解消する必要がある。
- D4: 現 USB 受入の成功は sh の入力成功を意味しない。置換なしの製品像で文字列の実行結果を別に判定する。

## 2. つなぎ方

- 接続先は PowerMac の InputOps provider。USB は scan→キー値に変換して専用 ring、LAN は既存 decoder を通す。
- `kbd_inject`、共通 `input_service`、PC-98 の `kbd_merge`、USER sh は変更しない。
- 新 `platform/powermac/input_usb.c` が USB の押下状態・修飾状態・固定長キューを所有する。
- 新 `platform/powermac/input_keymap.c` は状態を持たない JIS scan→キー値の純粋変換だけを担当する。
- 新 `platform/powermac/input_usb.h` に次の境界を置く（名前は提案、関数の責務は固定）。

```c
/* caller: USB service 内、IF=0（PowerPC では EE=0） */
void pm_input_usb_key(u8 src, u8 sc, int down);
void pm_input_usb_repeat(u8 src, u8 sc);
void pm_input_usb_release(u8 src);
/* foreground: 内部で irq_save/restore、空なら -1 */
int pm_input_usb_take(void);
/* foreground: irq_save/restore、FIFO 非空を照会し消費しない */
int pm_input_usb_has_key(void);
/* pure: 文字 / 特殊キー、出力なしは -1 */
int pm_input_keymap(u8 sc, u8 shift_state);
```

```c
/* usb_platform.c: 既存のログと割当管理を保ち、下記を追加 */
void usbp_input_key(u8 src, u8 sc, int down) {
    pm_input_usb_key(src, sc, down); /* ログは既存の観測契約で残す */
}
void usbp_input_key_repeat(u8 src, u8 sc) {
    pm_input_usb_repeat(src, sc);
}
/* release: kind が keyboard のときだけ状態を消し、その後 src を返す */
```

| 状態・処理 | 決定 |
|---|---|
| 押下中 | `u8 down[4][128]`。src は1..4を添字0..3に直す。HID が左右修飾を同じ scan の押下数に統合済み（USB 設計 §4-8） |
| 修飾 | Shift/Ctrl/GRPH は4入力元の該当 down の OR。別キーボードの release で押下中の修飾を消さない |
| CapsLock | 全 USB 共通の1bit。各 src の新しい make でのみ反転。break と repeat では反転しない。抜去しても lock は保つ |
| make | 既に down なら無視、初回だけ状態を更新。修飾／lock は文字を出さず、それ以外を現在の修飾で変換して enqueue |
| break | 当該 down を消すだけ。文字を出さない。キューが満杯でも押下状態の更新は必ず行う |
| repeat | 当該 src/sc が down のときだけ再変換して enqueue。修飾・lock は出力なし。周期は class の500ms後／30ms間隔をそのまま使う |
| 抜去 | 当該 src の down 全体をゼロにする。変換済みの文字は残し、他 src と LAN の入力を消さない。src の再利用に押下を持ち越さない |
| FIFO | `u16 keys[128]` と head/tail/count、満杯なら新着キーだけを捨て drop を加算。make/repeat の順を維持、動的確保なし |
| 排他 | producer は既存 IF=0 契約。foreground の1件取出し・状態照会は irq_save/restore で元の EE を戻す。LAN poll を錠の中から呼ばない |
| 初期化 | BSS のゼロ初期値を利用。provider 登録・sh 再起動で USB の down/ring を初期化し直さない。接続済み装置の押下と先行入力を保つ |

- 同一 HID report の修飾先行は `drivers/usb/hid_kbd.c:25`〜`:29` で確認済み。`usb_run.py:182` の Shift make→A make と、修飾 break＋通常 make の結合試験で固定する。前景で遅れて修飾を読む設計にはしない。
- USB callback 内では LAN poll・sh・kbd_inject・待機を呼ばない。文字化は定数表の参照と整数判定だけで終える。

| scan→キー | 初版の変換仕様 |
|---|---|
| 英字 | HID map の26英字を明示表化。小文字を基底に Shift XOR Caps で大文字。Ctrl+英字は1..26、Ctrl-C は現 LAN と同じ ESC |
| 数字・記号 | 既決 JIS 109 配列。0x01..0x0a=`1234567890`、Shift=`!"#$%&'()` と0キーは出力なし。Caps は記号に影響しない |
| 記号の残り | 0x0b/0x0c/0x0d は `-^\`→`=~|`、0x1a/0x1b は `@[`→`` `{ ``、0x26/0x27/0x28 は `;:]`→`+*}` |
| 下段 | 0x30/0x31/0x32/0x33 は `,./\`→`<>?_`。GRPH の状態は保持するが文字選択には使わない |
| 操作 | ESC→27、RETURN→13、BS→8、TAB→9、SPACE→32、DEL/UP/LEFT/RIGHT/DOWN/HOME は sc=0x39/0x3A/0x3B/0x3C/0x3D/0x3E を sc<<8 で出力（`userland/shell/ui.c:710/724/735/748`）。break はいずれも無出力 |
| その他 | テンキーは map の0x40..0x50に対応する数字・演算子を固定表化。Fキー/変換/無変換は今回無出力 |
| カナ | §6 の決定により KANA の make/break を追跡するだけで、lock・カナ文字・IME・LED制御を追加しない |

- JIS と US の物理キー名を混同しない。QMP Shift+1→`!` を必須にし、Shift+2 は JIS の `"` を期待する。
- 未割当は -1。0（NUL）を「空」の印に使わない。sc=0 は有効な ESC である。
- `input_boot.c` の先読みは従来どおり1個。peek を繰り返しても同じ値を返し、try で1回だけ消費する。
- 先読みが空なら必ず terminal poll を1回進め、その後 USB と LAN を前回採用元と逆の順に各1回試す。
- LAN の試行は既存 decoder/pending/期限を使い、1回最大32 bytes に制限。未完の ESC 列でも USB を待たせない。LAN 読出しは1関数へ集約し、`pm_terminal_trygetkey()` の呼出しは1箇所を保つ。
- 両方に値があるときは交互に採用。USB のキー値を LAN decoder に通さず、LAN の文字を USB 修飾で変えない。
- trygetchar は現在と同じ下位8bit、getkey は16bit値。raw/GUI/診断の未対応は維持する。
- is_pressed / modifiers / shift_state はこの段では0のまま（sh の行編集は使わない）。USB 内部の修飾状態は文字化だけに使い、公開 bit 配置は追加しない。dropped_count は USB FIFO の drop 数を返す。
- 製品の `shell_boot.c` の待ちは `while (!pm_term_connected() && !pm_input_usb_has_key()) pm_terminal_poll();` にする。RAM root と exec の準備後、LAN OPEN または USB の最初の変換済みキーで sh を起動する。修飾だけでは起動せず、起動を促したキーは FIFO に残す。
- LAN のみでは OPEN 後に案内・最初のプロンプトを出す現契約を保つ（`sc_run.py:82`〜`:85`）。OPEN は出力 FIFO を捨てる（`lan_terminal.c:29`〜`:39`）ため無条件の早期起動にはしない。
- LAN の行喪失・OPEN の取消 ESC は sh の共通の1行に作用し、USB で編集中の行も取り消す。取消だけでプロンプトは再送しない。USB 先行起動後の LAN 接続では過去の案内の再送を保証せず、Enter で次のプロンプトを得る。
- terminal poll の USB サービスは GMAC 不在・端末未接続でも進める。既存10ms/timebase方式を保ち、新しい tick やスレッドを作らない。
- 出力先は既存 console_stream。USB 単独時のローカル出力を受入で確認する。画面や出力先の新設はこの入力設計に含めない。

## 3. 大きさの見積もり

| 項目 | 追加／変更量の目安 |
|---|---|
| USB 状態・FIFO・非空照会 | input_usb.c 150〜190行、header 20〜30行。BSS 約800B（down 512B、FIFO 256B、添字・lock・drop 等） |
| 純粋変換 | input_keymap.c 90〜130行、2面の128要素表なら rodata 約512B。整数のみ |
| 接続・起動・選択 | 既存3ファイルと build 合計50〜80行。常駐の別タスク・追加 DMA は不要 |
| 試験 | host 本体／runner 200〜300行、QEMU 受入の追加150〜220行。製品追加は概ね310〜430行 |

## 4. ファイルごとの変更一覧

| ファイル（新規は＋） | 1機能・変更範囲 |
|---|---|
| ＋platform/powermac/input_usb.c / .h | USB 状態と FIFO、および専用 API 宣言 |
| ＋platform/powermac/input_keymap.c | PC-98 scan→sh キー形式の純粋変換。PC-98 ドライバや GPL 表を取り込まない |
| platform/powermac/usb_platform.c | 既存 src 管理・ログを残したまま callback から接続、keyboard release を種類で分岐 |
| platform/powermac/input_boot.c | LAN/USB の選択、先読み、drop 照会。input_decode.c の LAN 変換は維持 |
| platform/powermac/shell_boot.c | 待ちを LAN OPEN または USB FIFO 非空に変更。最初の USB キーを保持 |
| build/ppc-kboot.mk | 上記2個の .c を sh 像だけに追加。対象の規則本体は実装時に確認する |
| ＋tools/tests/ppc_input_usb_host.c | 状態・変換・FIFO・provider 合流の host 試験（製品関数を実行） |
| ＋tools/checks/ver-ppc-input-usb.py | host の実行と一意な変異。CHECK に全製品入力・header・試験本体を列挙 |
| tools/ppc/usb_run.py / sc_run.py | 製品像の sh 受入を追加、既存 probe と LAN 遅延起動受入を保持。下記4本の anchor と件数検査を追従。CHECK の入力・covers を追従 |
| `docs/tasks/DEFERRED_TESTS.md` | §6のカナ・IME延期行を実装時に登録 |
| `docs/decisions/ppc/PPC_SHELL_SC_DESIGN.md` | 入力・起動待ち・受入の正本を本設計へ更新（実装時） |

- 4本の置換 anchor は各対象で必ず1箇所を保ち、`.replace` 前に `count == 1` を検査する。`sc_run.py:238` の `pm_term_connected()`、`:251` と `usb_run.py:103` の `pm_terminal_trygetkey()`、`usb_run.py:99` の待ち1行が対象。最後は§2の新しい待ち1行へ追従する。製品受入にはこれらの入力・起動置換を適用しない。
- この段で共通 USB/HID、kbd_inject、input_service、drivers/input、USER sh、SDK/KAPI の変更は予定しない。
- x86 オブジェクト選択を変えないことを build 差分と同条件の生成物比較で確認する。

## 5. 受入 → 試験と変異

| 受入 | 試験・正確な判定 | runtime で RED にする変異 |
|---|---|---|
| USB 単独で製品 sh | 7450/7455、mac99、-nic none、usb-kbd、KBOOT_SH=1。起動待ち／input の置換なし。QMP send-key で e,c,h,o,spc,h,i,ret を順送し、最初の e が消えないこと、stream の出力行 `hi` と次の `sh> ` | callback 転送削除。scan ログだけ成功しても本受入は失敗 |
| Shift・Caps | `echo Hi!` を実行、同 report の Shift+A、Caps+A、Caps+Shift+A、Caps+数字を host でも確認 | Shift XOR Caps を Shift のみに、記号面の選択を反転 |
| 繰り返し | host の仮想時刻と既存 HID 試験で500/30ms、QMP 長押しで複数字・離した後に増えないことを確認 | repeat enqueue を削除／down 判定を削除 |
| 操作キー | BS で修正した echo、Ctrl-C の行取消、上下履歴、LEFT/RIGHT/HOME/DEL による行編集後の実行結果、USB Enter の CR。peek/try/char の混用で二重消費なし | BS写像変更、特殊キーの各写像削除、Ctrl-C取消削除、peekの消費化 |
| 複数 src | host で左右Shift統合後のイベント、別srcのShift保持、一方抜去、src再利用。QEMUでも押下中に device_del→再接続 | release の down 消去削除／他srcまで消去 |
| LAN 併存 | 実 LAN 接続でUSBから echo、USB抜去後LANから echo alive→`alive`。USBなしLANのみで OPEN 前は未起動・OPEN 後の案内と最初のプロンプト、後付けUSB、USB行編集中のLAN取消も確認 | provider の USB 選択削除／LAN 選択削除／起動待ちの無条件解除／非空照会でのキー消費 |
| 公平性・溢れ | host でLAN連続bytes＋USB、ESC未完＋USB、128件満杯後break/release、drop数・修飾解除・FIFO順を確認 | 満杯で状態更新前return／常時LAN優先 |
| EE=0 の継続 | 既存1秒stallと--lanのRPC期限切れ試験を保持。復帰後USBから echo hi の実行結果、HC生存まで追加 | no-service / tick-clock の既存変異 |

- QMP は1キー100ms保持、release が済んでから次へ。コマンド echo の表示だけを `hi` の成功と誤認しない。
- serial には stream と USB KEY 等の log が混在する。`sc_run.py:254`〜`:257` の `~` 印による stream 識別を再利用し、観測用に長さ付きの chunk 境界を付けて payload だけを復元する（単なる `~` 除去や raw serial の `hi` 検索は不可）。`ppc_sh_output.py:71`〜`:75` のプロンプト境界で区切る方式を製品 sh の `sh> ` に適用し、コマンド echo を除いた出力行と次のプロンプトを照合する。Enter break ログが h/i 間に入る入力でも判定を固定する。
- 入力・起動経路は製品のまま、観測用像の console_stream serial mirror への印と chunk 境界追加だけを許可する。置換を伴う既存 stall/probe 試験は別の受入として表示する。
- 各変異は置換元が1箇所と確認し、到達印と意味上の不一致を観測する。compile失敗・単なる時間切れは RED の代用にしない。
- `usb_run.py:86` の invert-key はログ引数が anchor。転送追加後は製品の `pm_input_usb_key(src,sc,down)` 呼出しへ anchor を移し、ログだけ反転する試験を残さない。
- 実装時に変更行への既存変異を tools/tests まで検索し追従。host の --mutate、USB の --cpu both --mutate と --lan --mutate を前景で完走する。
- 実装後は所定 preflight、make all と PowerPC sh 像、変更対象の試験、`python3 -B tools/checks/run.py --lint`。check-changed／全変異は実行しない。
- AF_INET 不可の LAN 受入、QEMU/ROM 不足は理由付き SKIP。GREEN に数えず、実機 PowerMac の確認は別の残作業として報告する。
- 今回は設計だけなので preflight --fix-fonts・build・試験は未実施。PACK の読取専用指定に従い、リポジトリは変更しない。

## 6. 決定

- PM決定: 初版は ASCII・JIS記号・CapsLock・Ctrl-C・矢印・HOME・DEL・履歴まで。カナ文字・IMEは後の段とし、KANA の make/break 追跡のみ行う。
- 理由: 既存 PC-98 カナ変換器はリンクされていない。後段で UTF-8 のキュー形式・文字変換・IME の契約を設計する。
- 実装時に `docs/tasks/DEFERRED_TESTS.md` に載せる行: 「PowerMac USB カナ・IME入力 | PM決定で初版はASCII/JIS記号と行編集まで、カナ変換・IME契約は未実装 | PowerMacカナ・IME対応段の受入完了前に文字化・UTF-8キュー・IME接続・製品sh入力を検証する」。本設計では台帳自体を編集しない。
- sh の起動条件は LAN OPEN または USB の最初のキー、公開の3照会は0のまま。追加のユーザー判断は不要。

反論: 0件。

## 確定 (2026-11-13、Opus 5.5 の第 2 回の条件付き確定を PM が反映)
- N1 (P3): §5 の invert-key の変異の anchor は `pm_input_usb_key(src, sc, down);` の 1 行全体 (転送を足すと usb_platform.c に `src, sc, down);` が 2 か所できるため)。
- N2 (P3): §4 の既存の道具の置換の anchor の一覧に sc_run.py:239 の `for (;;) pm_terminal_poll();` を足し、5 本すべてに `count == 1` の検査を掛ける。
- 実装者への注: 転送を足すと usb_run.py の QMP のキー (a、Shift+a、Enter、a) が sh に `aA`+Enter として届き、`aA` がコマンドとして実行される。usb_run の照合はログの `USB KEY` の並びと停止 PC なので壊れない見込み — 実装の段で確かめる。

