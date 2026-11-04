# HW-BASIC — ハードウェア調査の道具 (PCI の構成空間・I/O・MMIO の読み書き・待ち・スクリプト) — 確定設計

> 出所: ~/os32-tmp/run/hwbasic/DESIGN.final.md (astra 起草 r1〜r2、Opus レビュー 2 回、PM が確定)。
> 状態: **現行 (2026-11-05)** — 段の状態は票 TASK_HW_BASIC が持つ。

# HW-BASIC 詳細設計 r2 — 2026-11-04
## r1→r2 の変更
- R1: CUIシェル専用CTRL+STOP印と原子的読み消しをexec_abort.incに追加し、POINTで消費。
- R2: action別wire表、BDF配置、数値status、PCI不在の戻り値を確定。
- R3: HW_OPをREC_PAUSED判定より前で分岐。
- R4: ログ1行をkprintfで1回出力し、既存シリアルミラーに任せる。
- R5: NP21/WのTVRAM MMIO・PCI不在・終了値判定を追加。
- R6: 許可表撤去に伴う2文書の書換えとgfinit.cのinclude撤去を段表へ追加。
- R7: WAIT_SLICEを削除し、待ちはshellのsys_time_nowとsys_yieldへ移動。
- R8: run/loadの初期変数modeは省略時0、指定時は引数優先。
- R9: 実機受入はDEFERRED_TESTSで追跡し、変異試験の正常対照を明記。
作成者: Codex（実モデル ID は環境から確認できず、gpt-6-astra とは断定しない）。
対象: PC-9821、単一タスク・協調切替、整数のみ。参照パスの基点は **/home/hight/os32-v3/**。
成果物は本書のみ。実装・ビルド・配備・実機操作はしていない。以降の数値は設計値と実測を区別する。

## 1. 実物の確認
| 実物（基点からの file:line） | 確認と設計への反映 |
|---|---|
| userland/shell/cmd_sys.c:688、cmd_peek.c:188,208 | 前者は登録表、実装は後者。`-k/-p` は同じ RAM 経路、`-m` のみ MMIO。 |
| kdebug/kpeek.c:87,108,222 | RAM は既存写像を読む。MMIO は許可表照合後に一時窓、最大要求単位は既存 CHUNK。書込み口なし。 |
| kdebug/kpeek_allow.c:12,55 | 現行木には GeForce の許可行と照合が残る。HW-BASIC と peek -m では撤去する。 |
| kdebug/kdebug_window.c:46,53,66,90 | 共用窓・元 PTE 保存・PCD・必要時 RW・IF 保存復元。単回 len≤4096、最大2ページ、失敗時 cleanup。 |
| include/memmap.h:367 | 一時窓 VA は 0xFE000000。物理番地をそのままポインタへ変換しない。 |
| sdk/kapi.json:1943、kdebug/kpeek.c:4 | KAPI slot 253 `kdebug_call` → ops slot 19 `dbg_call`。公開 KAPI の追加不要。 |
| kdebug/kdebug_main.c:122,153,214 | recovery_call が振分け、未知の通常 op は kpeek_call へ。HW op を独立分岐として追加。 |
| userland/shell/cmd_pci.c:175 | lspci は構成空間 read32 で復号、BAR サイズ測定なし。pcidump は既存観測に継続使用。 |
| drivers/pci/pci.h:34–51 | config #1、CF8/CFC 対を IF 保存で保護。write16 は W1C 回避のため32bit RMWで代用不可。 |
| kdebug/gfinit_decode.c:24、gfinit_exec.c:1、kdebug_main.c:126 | NV44A ROM 専用復号・recover 所有実行器。現在 hwdev に配送されるため移植せず、幅・期限・中止点の考えだけ採用。 |
| tools/rshell_peek.py:177,189 | peek 専用の見出し・連番・CRC解析。HW ログをこの形式へ偽装しない。 |
| docs/POLICY_DEBUG.md:203 | 書込みの読戻し一致を必須にしない。明示 read/poll だけを実行。 |

- 前提資料と実物の食い違いは **3件**: ①peek 本体の場所、②診断版では外した許可照合が現行木には残る、③GeForce 実行器の配置は現在 hwdev 側。上表で補正済み。
- TASK_TRIDENT_DRIVER.md:48 の旧許可表案は今回のユーザー決定で取り消す。番地・装置・書込み値を許可表で拒否しない。
- 同票:104 の全00観測はチップ応答の証明ではない。同票:301 の恒久ドライバ復帰設計と、本書の探索用スクリプトを混同しない。

## 2. 形と行文法
- `hw <命令>` はシェルから1行実行、変数はシェルの HW セッションに保持。`hw reset` で変数だけ消す。
- `hw run /test/hw/trident.hwb mode=1` はファイルを読込み、構文とラベルを検査して実行。引数は初期変数。run/loadは毎回`mode=0`を既定の初期変数として用意し、指定引数で上書き（装置固有の解釈はscript側）。
- `hw load <path> [name=value...]` → `hw step`（1行）/ `hw continue` / `hw abort`。ファイル位置・変数を保持、abort は次の命令を実行しない。
- ファイルの配置変更だけで手順を変えられる。行頭/末尾 `#` コメント、空行可、1行1命令、CRLF可。include・文字列展開・浮動小数点なし。
- 行頭空白は無視。番地・offset・mask・timeout等の引数は単一数値/変数または括弧式、write値とletの右辺は行末までの式として区切る。
- 整数は10進/0x、u32。変数32個・名前16字、行256バイト、ファイル64KiB、ラベル64個、call深さ8を初版資源上限とする。
- `let x = expr`。式は括弧、`+ - & | ^ << >>`、通常の C の優先順位。加減の桁あふれ/負数と shift≥32 はエラー。ビット演算はu32。
- `device d = bdf 0:8.0` または `device d = id 0x1023:0x9660`。BDF各成分は10進/0x、IDの成分は16進。IDは既存PCI列挙で検索し複数なら候補を出してBDF指定へ。
- `window v = bar d 1` は BAR の参照を作る。`v + expr` は `bar d 1 + expr` の短縮。固定番地は `phys expr`。I/Oは `port expr`。
- BAR は各アクセス時にconfigを再読して解決、サイズprobe・Commandの自動変更なし。番号0..5、header typeに応じた本数、種別・64bit対を復号。
- MMIOではmemory BAR、I/OではI/O BARを要求。64bit BARの上位非0は32bit物理空間に表せない旨を返す。サイズ不明なので範囲許可判定はしない。
- base=0でも勝手に拒否しない。BDF直指定はベンダー制限なし。CS4231などPCIでない装置はport/physで同じ命令を使う。

| 命令（W=8/16/32、末尾のWは幅bit数） | 意味 |
|---|---|
| `pcireadW d offset -> x` / `pciwriteW d offset expr` | 構成空間を幅どおりアクセス。offset 0..255、自然整列・末尾越境を確認。 |
| `ioreadW port expr -> x` / `iowriteW port expr value` | 0..65535のI/O。BAR表記も可、幅による末尾越境を確認。 |
| `mmioreadW address -> x` / `mmiowriteW address expr` | phys/bar/window指定、自然整列、末尾の32bit越境を確認。 |
| `wait us expr` | 最低指定時間待つ。1回1..10,000,000us。精度はログに実際の経過を記録。 |
| `poll W address mask value timeout_us interval_us -> x` | addressは`pci d offset`/port/phys/bar/window。即時1読、以後間隔待ち、`(x & mask)==value`で成功。 |
| `print "tag" expr` / `log "text"` | 値/注釈。文字列中の制御文字はエスケープ。全操作の記録は常時有効。 |
| `label name` / `goto name` / `if expr == expr goto name` | Init等の条件分岐・探索反復用に採用。`!=`も可。毎命令中止点を通る。 |
| `call name` / `return` / `end` | スクリプト内の短い共通手順。再帰不要だが深さ上限で終了。 |

- 読んだ値を `let` で加工して書く通常のRMWだけを持つ。自動RMW/読み返しなし。幅を超える値は黙って切捨てずARG。
- pollのmask/valueは幅内、valueはmask外ビット0、timeout 1..10,000,000、interval 1..timeout。最終値・回数・経過を返す。TIMEOUTは実行停止。
- 各命令の失敗でその場停止、次命令へ進まず、再実行もしない。実行済み書込みは取り消さない。対話から次の操作を選べる。

## 3. 特権の境目と停止
```c
/* 新規 kdebug/hwb_wire.h。公開SDKを変更しない固定長u32 wire。 */
#define HW_OP 18U /* 現行1..17と非衝突。統合時に再確認 */
struct hw_req { u32 version, action, space, width, bdf, addr, value, status; };
/* action: READ=1, WRITE=2, POINT=3; space: PCI=1, IO=2, PHYS=3 */
/* call: 受理0、未実装は既存OS32_ERR_NOSYS、不正wireはOS32_ERR_INVAL。 */
```
| action | 入力（共通version=1、status=0） | 出力（call=0時のみ有効） |
|---|---|---|
| READ=1 | space、width=1/2/4、bdf、addr、value=0 | value=ゼロ拡張した読値、status。失敗時value=0 |
| WRITE=2 | READと同じ宛先、value=幅内の書値 | valueは入力のまま、status。暗黙読戻しなし |
| POINT=3 | space/width/bdf/addr/value=0 | value=読み消した中止印（0/1）、status=OK。時刻は返さない |
- PCIのbdfは`(bus<<8)|(dev<<3)|fn`（bus 0..255、dev 0..31、fn 0..7、上位16bit=0）、addrはconfig offset 0..255、自然整列かつaddr+width<=256。IO/PHYSはbdf=0、addrはport/物理番地。未使用欄は0を要求。statusは`OK=0, ARG=1, NOSYS=2, WINDOW=3, IO=4`。ARGは引数、NOSYSはPCI機構なし、WINDOWは窓確保/写像失敗、IOはバックエンドが報告できる操作失敗。PCI機構なしのREADはcall=0/status=NOSYS/value=0、機構ありで装置なしはOK/幅内全1。TIMEOUT/ABORTはshellが判定しwireには載せない。
- parser/ラベル/変数/BAR解決/ファイル/待ちはshell側。kdebugは単一アクセスとPOINTによる中止確認だけ。HW_OPはkdebug_main.c:133より前、REC_PAUSED判定(:141)を通らずhwb_accessへ直行。recoverの許可・静的計画・装置profileに接続しない。
- wireはversion=1、sizeof=32を双方で静的検査。カーネル側も幅・桁あふれ・wireサイズを検査。ポインタ、可変配列は渡さない。
- MMIO: `recover_window_hal_init → recover_window_access → cleanup`。writeでのみRW。IRQ停止は窓の保存から復元までの単一アクセス、待ち/ログ中は停止しない。
- PCI: 専用汎用アクセス関数でCF8保存→指定→CFC+laneへin/out幅どおり→CF8復元、IFを元へ。8/16書込みを32bit RMWにしない。
- I/O: 単一in/out。複数のindex/data命令間はIF=1。既存割込み処理と同じ装置を触る場合はスクリプト前提に記載し、ドライバ所有権の拒否表を追加しない。
- 待ちはshellでKAPI v59 `sys_time_now(&lo,&hi)`のµs時刻を64bit合成し、開始からの差で判定。wait/pollは最大10,000,000us。各反復でPOINT→時刻確認→`sys_yield()`、pollは読出し前にもPOINT。WAIT_SLICEは定義しない。POINTはCUIシェルの印を読んで消し、value=1ならshellはABORTで終了。毎命令（label/gotoも含む）とwait/poll反復で呼ぶ。run/load/対話実行の開始直前にPOINTを1回呼んで古い印だけ捨て、以後の要求は中止として扱う。
- `exec/exec_abort.inc:40`のring3_abort_request先頭で`!con_sink_is_enabled() && appslot_cur()==APP_ID_SHELL`なら専用volatile印を立てreturn。それ以外の既存経路は保持。新内部関数`exec_hwb_abort_take()`はIRQ保存/停止→印の読出しと0化→IRQ復元で取りこぼしを防ぐ。KAPI追加なし、module importでPOINTから呼ぶ。
- 時刻の精度は既存µs時計の契約（IRQ0を失わないこと）に従い、yield分だけ待ちは延び得る。要求usと実測elapsed_usをログへ出す。時計API失敗/逆行はIOで停止し、u32下位周回は64bit差で処理する。wait/pollは最大100万反復（poll読出しも最大100万）。時計停止でも上限でTIMEOUT。長いloopもPOINTを通り、ソフト停止応答は次のPOINTまでのaccess/yield/出力時間に依存する。
- バスアクセス自体の停止・CPU fault後の帰還は保証しない（受容済み）。CTRL+STOPは「次の命令を止める」であり映像を戻す操作ではない。
- undoの自動履歴は初版不要。write-only/read-clear/W1Cやindex付き装置を事前読出しする方が手順を変える。必要なら明示readで変数保存し、復元スクリプトを実行。
- 書込みログのoldは明示読値がある場合だけ利用者がprint。全書込みのoldを暗黙採取しない。Disableは復帰候補であって任意の以前の状態のundoではない。

## 4. 記録
```text
HW run=3 seq=12 line=8 phase=begin op=mmiowrite8 target=0:8.0/bar1+0x3C4 addr=0x204003C4 val=0x09 rc=PENDING
HW run=3 seq=12 line=8 phase=end op=mmiowrite8 addr=0x204003C4 val=0x09 rc=OK
HW run=3 seq=13 line=9 phase=end op=mmioread8 addr=0x204003C5 val=0x01 rc=OK
HW run=3 op=end rc=OK lines=10 writes=1
```
- 番地は解決した物理値、PCIはBDF+offset、I/Oはport。例の物理値は説明用であり固定しない。rcはOK/ARG/NOSYS/WINDOW/TIMEOUT/ABORT/IO。
- beginを出してから操作し、endがない行で停止位置を特定。OKはCPUがアクセスを完了した意味で、装置受理の証明ではない。
- IF復元後、整形済み1行を`kprintf("%s\n", line)`で1回だけ出す。画面とシリアルへの配送は既存ミラーに任せる。
- pollは開始と最終値/回数/経過だけを出す。ホスト採取はrun/seq/終端を検査して未完了を保存・報告、書込み再送なし。`tools/rshell_hwb.py`を別形式として追加。

## 5. 大きさ・配備の見積もり
| 部品 | 初版増分の設計予算 / 制約 |
|---|---|
| shell像 | parser+実行+記録 12〜20KiB text、常駐状態≤4KiB、load時ソース64KiB＋索引≤12KiB（heap、終了時解放） |
| kdebug.bin | 汎用アクセス/待ち/中止 3〜6KiB text、BSS≤256B、許可表削除分は相殺。GeForce部品はリンクしない |
| カーネル | 内部中止問い合わせ・PCIアクセス・import接続で≤2KiB見込み。公開ABI・ops表のslot数は不変 |
| 上限の実物 | tools/module_schema.py:51–60: image/mem各262144B、load_span524288B。既存build/out/kdebug.binは約125.7KiB（鮮度未確認）、新像の実測値ではない |
| 起動媒体 | /boot/kdebug.binはPOST_ROOT読込み。FDローダ/配置変更不要。kernel変更があれば実装時の通常のkernel再梱包・ペア再生成と媒体容量検査は必要 |
- スクリプトは/test/hwに置くデータ。初回道具導入後は変更時に像を組直さない。公開KAPI再生成・clean全ビルドを要する設計にしない。

## 6. ファイルごとの段（この表は実装計画、今回は変更しない）
| 段＝ファイル | 単一責務 | 依存 / 並列可否 |
|---|---|---|
| A kdebug/hwb_wire.h | wire/状態/限界定義 | 最初に固定 |
| B userland/shell/hwb_parse.c | 行/式/ラベル構文検査 | A後、C/Dと並列可 |
| C userland/shell/hwb_exec.c | 変数/PC/step/run/poll | A後、B/Dと並列可 |
| D kdebug/hwb_access.c | wire受付、IO/MMIO、PCI/POINTの接続 | A後並列可 |
| E drivers/pci/pci_hwb.c | 汎用幅付きconfig #1アクセス | Dと契約固定後並列可 |
| F exec/exec_abort.inc | CUIシェル中止印・ring3_abort_request分岐・exec_hwb_abort_take | A後並列可、既存GUI/app中止経路保持 |
| G userland/shell/hwb_log.c | 1行整形・kprintfを1回呼ぶ | A後並列可 |
| H userland/shell/cmd_hw.c | コマンド入口とファイル寿命 | B/C/G後 |
| I userland/shell/hwb.h | shell内契約 | B着手時に固定 |
| J userland/shell/cmd_sys.c | hw登録 | H後 |
| K userland/shell/shell.h | コマンド宣言 | H後 |
| L kdebug/kdebug_main.c | HW_OPを:133より前で振分け、REC_PAUSED判定から独立 | D後 |
| M kernel/module/kdebug_imports.def | PCI/中止内部関数のimport | E/F後、生成物は生成器で作る |
| N kdebug/kpeek.c | allow include/照合撤去 | A後独立、RAM専用の従来意味は保持、任意物理は-m |
| O kdebug/kpeek_allow.c | 削除 | Nと同時 |
| P kdebug/kpeek_allow.h | 削除、他利用の定数は必要箇所へ移す | 参照検索後N/O/Yと同時 |
| Q build/kdebug.mk | hwb追加/allowオブジェクト撤去 | D/N後 |
| R build/programs.mk | shell構成と依存 | H後 |
| S build/kernel.mk | Eをカーネルへ追加（Fは既存exec.cにinclude済み） | E後 |
| T tools/rshell_hwb.py | HWログ採取/完了検査 | A/G後並列可 |
| U tools/rshell_serial.py | hwbサブコマンド接続 | T後 |
| V tools/tests/hwb_test.py | 下表のhost/変異とCHECK登録 | A後並列可 |
| W tools/tests/kpeek_test.py（既存試験の実名は参照検索で確定） | allow撤去に当たる試験・変異アンカー更新 | N後 |
| X test/hw/trident.hwb | §8のデータ（ゲスト/test/hwへ配備） | 文法固定後独立 |
| Y kdebug/gfinit.c | kpeek_allow.h の include を外すだけ (KPEEK_NV_PROM_LEN は kdebug/recover.h:37、GF_ROM_MAX は hwdev/gf/production.h:8 のまま通る) | N/O/Pと同時 |
| AB tools/checks/run.py の CHECK の covers・tools/tests/test_kdebug_init.py:122 | kpeek_allow.{c,h} を外す: covers に残る 6 本 (test_kdebug_init・test_geforce・test_gf_display・test_gf_command・gf-runner-host・gfinit-host) と、test_kdebug_init.py:122 のコンパイルの一覧 | O/P と同時 (残すと run.py が missing input、:122 はファイルが無くて止まる) |
| Z docs/tasks/realhw/TASK_TRIDENT_DRIVER.md | §0-1の「許可表で照合」「許可リストで縛る」を削除し、任意番地と引数検査のみの方針に書換え（注記で残さない） | N後独立 |
| AA TASK_KPEEK（実装時に所在確定） | §2-5の許可表前提を削除し、peek -mの任意番地・窓・引数検査へ本文を書換え | N後独立 |

## 7. 受入 → 試験（実行は実装段で）
| 受入 | 試験・証拠 | 変異（各1行、実行時RED） |
|---|---|---|
| C無改変で新しい番地/手順 | host偽busへ未登録BDF/port/phys、2種類のファイル差替え、writeが到達 | 未登録番地拒否を挿入 |
| 幅とRMW | host PCI8/16が該当laneだけ、W1C隣接statusへwriteしない、read→式→write順 | write16をDWORD RMWに変更 |
| 引数/構文 | host overflow、zero wait、未定義変数/label、深さ超過、末尾改行なし | addr+width越境検査を除去 |
| 窓cleanup | host偽PTEでread/write後・map失敗後のPTE/IF一致、実機で連続read | cleanup呼出しを除去 |
| 中止/期限 | host時計停止・下位32bit周回・不一致poll、NP21/WのCUIシェルid=1で無限goto/10秒poll/wait中CTRL+STOP→ABORT/終了値130、後続writeゼロ・次run成功 | シェル中止印の設定を無効化 |
| 読戻し不要 | host write-onlyモデルは読出しをエラーにし、writeだけ成功 | 自動readbackを追加 |
| PC-98のI/Oで実証 | NP21/Wでport 0x60（GDC status）read8、0x68←0x0Eで消画→wait→0x68←0x0Fで復帰、シリアル記録と前後画面 | 末尾0x0Fを0x0Eへ変更 |
| NP21/WのMMIO | TVRAM先頭を`mmioread8 phys 0xA0000`で保存→`mmiowrite8 phys 0xA0000 0x41`→read8で0x41を確認→保存値をwrite8で復元、終了値0 | PHYS writeを無操作へ変更 |
| NP21/Wの限界を区別 | VGA互換port 0x3C*を使わない。PCI機構なしで`pciread32 0:0.0 0`→NOSYS/終了値3。BAR/Tridentの成功証拠にはしない | PCI不在を偽成功へ変更 |
| PAUSEDでも任意アクセス | hostでREC_PAUSED中のREAD/WRITE/POINTを検証、拒否表による拒否なし | HW_OP分岐をPAUSED判定後へ移動 |
| シリアル記録 | host欠seq/欠end/重複を検出、途中ログも保存、書込み再送ゼロ | end欠落を成功扱い |
| 実機の最初の題材 | lspci/pcidump→SR09→固定Enable→Disable、serialと映像往復を記録。00/FFだけでは識別成功にしない | Enable/DisableのFAC順序を入替え |
- `hw run`/対話/continueの終了値はOK=0、ARG=2、NOSYS=3、WINDOW=4、IO=5、TIMEOUT=124、ABORT=130。ゲスト試験は終端ログとコマンド終了値の両方を判定する。
- 実機受入は取り込みの関門にせず`docs/tasks/DEFERRED_TESTS.md`に対象・手順・未実施理由・担当・解除条件を1行で追跡し、実機成功まで完了扱いにしない。
- 変異試験は同じ入力/環境の正常対照がGREEN、その変異版が実行時REDになることを各件で確認する（ROLES §4-1）。
- 実機は探索として手順・対象・配送の根拠を記録し、TASK_TRIDENT §3-5の恒久ドライバR1〜R5を本ツールで満たしたとは扱わない。許可表を復活させない。
- 実装時は該当像ビルド、check-changed、追加/変更試験の変異、lint、NP21/W、実機の順。本書作成中は全て未実行。

## 8. 最初のスクリプト
- 出典: `/home/hight/os32-tmp/refs/trident/trident_driver-1.4.0.c:694–775`（MIT）のEnable/Disable。SR09はTASK_TRIDENT:363。VGA番号は全てBAR1相対、PC-98の3ポートだけPIO。
- 以下は **主要手順の転記**。`vgaHWProtect(TRUE/FALSE)`の本体は指定参照範囲にないため省略を明記する。完全同等版として実機投入する前にMITのvgaHWProtect本体を照合し、script内callで展開する（技術的作業、ユーザー判断事項ではない）。
- Init/ModeInitはこの例で実施しない。Enableだけで有効な画面モードが作れるとはしない。実機でSR09が応答するかも未確認。
- `mode`省略時はrun/loadの既定値0でSR09、`mode=1`でEnable主要手順、`mode=2`でDisable主要手順。索引のread副作用に配慮し、OUTWは16bitのまま、83C8は読み/書き両方の前に再選択する。
```text
# /test/hw/trident.hwb — vgaHWProtect未展開の調査用転記
 device d = bdf 0:8.0
 window v = bar d 1
 if mode == 1 goto enable
 if mode == 2 goto disable
 mmiowrite8 v + 0x3C4 0x09
 mmioread8 v + 0x3C5 -> chip
 print "SR09" chip
 end
 label enable
 iowrite8 port 0x68 0x0E
 iowrite8 port 0x6A 0x07
 iowrite8 port 0x6A 0x8F
 iowrite8 port 0x6A 0x06
 # 元ソース704: vgaHWProtect(TRUE) 展開位置
 mmiowrite8 v + 0x3D4 0x23
 mmioread8 v + 0x3D5 -> t
 mmiowrite16 v + 0x3D4 0x23 | ((t & 0xDF) << 8)
 mmiowrite8 v + 0x3D4 0x29
 mmioread8 v + 0x3D5 -> t
 mmiowrite16 v + 0x3D4 0x29 | ((t | 4) << 8)
 let mask = 0xFF
 let bits = 6
 call dac
 let bits = 8
 call dac
 mmiowrite8 v + 0x3CE 0x23
 mmioread8 v + 0x3CF -> t
 mmiowrite16 v + 0x3CE 0x23 | ((t & 0xFC) << 8)
 let bits = 1
 call dac
 mmiowrite8 v + 0x3C4 1
 mmioread8 v + 0x3C5 -> t
 mmiowrite16 v + 0x3C4 1 | ((t & 0xEF) << 8)
 # 元ソース727: vgaHWProtect(FALSE) 展開位置
 iowrite8 port 0xFAC 2
 end
 label disable
 iowrite8 port 0xFAC 0
 # 元ソース740: vgaHWProtect(TRUE) 展開位置
 mmiowrite8 v + 0x3C4 1
 mmioread8 v + 0x3C5 -> t
 mmiowrite16 v + 0x3C4 1 | ((t | 0x10) << 8)
 let bits = 0
 let mask = 0xFE
 call dac
 mmiowrite8 v + 0x3CE 0x23
 mmioread8 v + 0x3CF -> t
 mmiowrite16 v + 0x3CE 0x23 | (((t & 0xFC) | 1) << 8)
 let mask = 0xFD
 call dac
 let mask = 0xCF
 call dac
 let mask = 0xF7
 call dac
 let mask = 0xFB
 call dac
 mmiowrite8 v + 0x3D4 0x29
 mmioread8 v + 0x3D5 -> t
 mmiowrite16 v + 0x3D4 0x29 | ((t & 0xFB) << 8)
 mmiowrite8 v + 0x3D4 0x23
 mmioread8 v + 0x3D5 -> t
 mmiowrite16 v + 0x3D4 0x23 | ((t | 0x20) << 8)
 # 元ソース769: vgaHWProtect(FALSE) 展開位置
 iowrite8 port 0x6A 0x07
 iowrite8 port 0x6A 0x8E
 iowrite8 port 0x6A 0x06
 iowrite8 port 0x68 0x0F
 end
 label dac
 mmiowrite8 v + 0x83C8 4
 mmioread8 v + 0x83C6 -> t
 mmiowrite8 v + 0x83C8 4
 mmiowrite8 v + 0x83C6 (t & mask) | bits
 return
```

## 9. 未決（ユーザー判断のみ）
- **0件**。許可表なし・任意番地・実行器の配置・undoなしは上記で決定。Protectの出典照合、STOPの実接続、像の実測は実装前/実装時の技術確認として残り、ユーザーへ再承認を求める項目にしない。
反論: 0件。

## 確定 (2026-11-05、Opus 第 2 回「N1 の段を §6 に 1 行足せば確定してよい」、PM が反映)
- N1 (P2): §6 に AB を足した (kpeek_allow を参照する CHECK の covers 6 本と test_kdebug_init.py:122 を O/P と同時に外す)。
- N2 (P3): Y を「include を外すだけ」に書き換えた。
- N3 (P3、実装の段で直す): §7 の NP21/W の行は `device d = bdf 0:0.0` を置いてから `pciread32 d 0 -> x` と書く。
- N4 (P3、実装の段で直す): F の段で exec/exec_abort.inc:46-47・appslot.c:408 の「CUI 中は 1 バイトも変えない (K2)」のコメントを新しい印に合わせて書き換え、CUI で印以外は変わらないことを host 試験で確かめる。

## PM 決定 (2026-11-05、HW-BASIC の機能 PM、実装の前)
- P1 (段の分け方): §6 の 1 ファイル = 1 段を、実装者の単位で次の 10 段にまとめる。a0 = A・I と段をまたぐ宣言 (drivers/pci/pci_hwb.h・exec/exec_hwb.h) と host 試験の土台 (tools/tests/hwb_fake.h・tools/tests/hwb_host.py)。parse = B、exec = C、log = G、rshell = T・U、kd = D・L、pci = E、abort = F、allow = N・O・P・Y・AB・W・Z・AA と build/kdebug.mk の kpeek_allow.o の撤去、trident = X、wire = H・J・K・R・M・Q (hwb_access.o の追加)・S と追従の試験・本番の組み立ての試験。
- P2 (試験の置き方): §6 の V (tools/tests/hwb_test.py 1 本) は、段ごとの `tools/tests/test_hwb_<部品>.py` (各自の CHECK と MUTATIONS) に分ける。共有の登録の衝突を避けるため (run.py の lint は 0 件に当たる covers の glob を missing input にする)。共通の組み立て・実行・変異は a0 の tools/tests/hwb_host.py。
- P3 (N4 の appslot.c:408): 書き換えない。新しい印は exec_abort.inc の中だけにあり、appslot の状態は CUI で今までどおり 1 バイトも変わらないので、あのコメントは正しいまま。exec_abort.inc のコメントだけを書き換え、CUI で印以外が変わらないことは abort の host 試験で確かめる (wt/p6-close の exec/appslot との重なりも避ける)。
- P4 (スクリプトの置き場): リポジトリの `userland/tests/hw/*.hwb` に置き、配備マニフェスト (userland/deploy.yaml、tags: [test]) でゲストの `/test/hw/` へ。§7 の NP21/W の試験のスクリプトも同じ所に置く。
- P5 (vgaHWProtect の展開): 出典は X.Org xserver hw/xfree86/vgahw/vgaHW.c の vgaHWProtect (MIT、xorg-server-21.1.13、PM の手元 ~/os32-tmp/refs/xorg-vgahw/vgaHW.c:574)。trident の段で script 内の call に展開する。
- P6 (検査): このブランチはメインストリームでない (PARALLEL_BRANCHES §3)。段の実装者は make all (check_slot.sh 経由) と自分の試験の直接の実行・--mutate・--lint だけ。check-changed は回さず、取り込みの検査は wt/hw-basic の GitHub の CI の緑で代える。
- P7 (§4 の seq の初期値、rshell の段の欠け): seq は run ごとに 1 から始め、命令ごとに 1 ずつ増やす (begin と end は同じ seq)。ホストの採取は最初の seq が 1 でなければ先頭の欠けとして扱う。
