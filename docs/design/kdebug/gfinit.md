# kdebug/gfinit.c — 設計
> 状態: **現行 (2026-10-30)** — 出所: GF_DRIVER_DESIGN (2026-10-29 確定、D0 で移した)
ソース: `kdebug/gfinit.c`

**診断 owner/lease は `kdebug/gfinit.c:70–74` のまま**（F7）。核から `int gf_production_selected(void)` 一つを export し、GFX=geforce の起動では gfinit の入口で拒否する。本番の成否や GUI 終了後にも拒否を保ち、診断と本番は設定変更＋再起動で入れ替える。S0/緊急復帰入口は止めない。
共有 parser は一つ。診断用出力/接続・lifetime/stages・owner/lease は元の場所に残す。
KDEBUG=0 fake 起動、初回 Command 書込みは 1 回・BAR 書込みは 0、0x0106→0x0102 の 16bit 確認と不一致時の停止、A2/main順・期限・各失敗出口、選択中 gfinit 拒否、再 devinit 無し

## GF6200_4A_DESIGN

- op対応: INSPECTはtrial外、SIZE_BARSは測定専用の有界trial_begin→trial_step→trial_end、PREPAREはtrial_begin(profile,plan_hash,budget)、RUN_ONEは1 opcodeごとにtrial_step、VERIFYはtrial_end(normal)、ABORT・エラー・期限超過はtrial_end(abort)。session・snapshot・arm・終端cleanupはrecoverが所有する。

- Command は descriptor の override 項目とし、snapshot の0106hは記録だけで書き戻さず、目標0102h（BM=0）を生 config の16bit読取りで verify する。SIZE_BARS の独立 trial 終端にも適用する。BAR復元失敗時は decode を止めたまま FAILED とし、目標値のために Mem を再有効化しない。

- PREPARE と各 RUN_ONE の正常帰還時は kdebug_call の外で PAUSED とし、承認待ちの期限を止める。PAUSED 中の復旧要求は `recover now <session>` だけを受け、次段の承認済み kdebug_call で RUNNING に戻す。trial の期限は kdebug_call 内の時間だけを積算し、承認待ちを数えない。

- INSPECTでROMバッファを上限128KiBまで部品所有で確保し、不足なら書く前に終了。実行中は不変とし、CRC・長さ・BIT表位置・BDF・起動世代をsessionに結び付ける。終了/ABORTで解放し、古いsessionやmainの飛越し/二重実行は拒否する。

- 書き op でも独立の `GF_ALLOW_WRITE` が無ければ DENY。shell の `--allow-write` は毎コマンド必要。rshell の同名旗だけではカーネル許可にしない。

- DIRTYはrecoverのFAILED/cold-requiredの派生表示で、別の復旧状態機械は持たない。正常なmain終了だけ次の番号へ進む。重複要求は既存結果を返す。再実行には電源再投入記録とCOLD再採取が必要で、新sessionだけでは解除しない。

## GF6200_4C_DESIGN

4a 合格を前提に、新しい同一試行の A2→main 0〜4→C を行う。旧 A4 は終端するので間に挟まない（`kdebug/gfinit.c:388–390`）。今回の VRAM 試験範囲は表示領域だけで、4a の容量試験を再実施したとはしない。

新opは**13の `gfinit modeset <sid> --allow-write` 1つ**。終了は既存abort。40B/version1・op5〜12・KAPI slotは維持（`kdebug/gfinit_wire.h:4–17`, `kdebug/kdebug_main.c:115–123`）。

flagsはprepare時にownerへ保存しplan/hash/profile_crcへ反映。再送cacheはprepareのflagsも比較する（現状はop/mainのみ、`kdebug/gfinit.c:365–375`）。途中のflag変更や旧prepareからmodesetを受け付けない。

C実施時は同sid/next_main=5/PAUSEDを確認；旧verifyはMODE_Cでは使わず、通常prepareのA4は従来どおり。

- MODE_C=2 は prepare だけが受理し owner に保存する；ALLOW_WRITE は承認の条件、MODE_C は C 節の即値と専用 profile の対象・規則・layout に反映して plan hash/profile_crc を分け、再送 cache は flags 全体の一致を要する。

- A2 と main 0〜4 は旧 builder と同じ prefix を使い、REC_RUN の書込み前の読取りは 4a の read_safe を規則に保持して判定する（C の phase 9 だけ C の read_safe を使う）；C entry (phase=9、main/pc=NONE) と全 C 節を初回に bind する；旧 A4 を含めず、MODE_C の旧 verify は拒否し、main4 後は PAUSED で C 入口を待つ（旧 prepare に C は後付けしない）。

記録属性と比較 mask は profile CRC / plan hash に入り、

## GF6200_S7_SHELL

- `kdebug/gfinit.h:5-17` は既存op5〜10と40B要求。`kdebug/gfinit.c:297-307` は版・内外size・旗・4096 steps・出力欄ゼロを検査する（r1確認時の行番号）。

- gfinit.hはwireをincludeし40B assertと関数宣言を保持。

- T5後はIDLEを前提に `gfinit abort <sid> --allow-write` でleaseを返す。この経路は装置書込みなし。同じabortでもtrial中のcleanupは書込み得るため区別する。

## GF_STAGES_DESIGN

局所index/lock/窓の保存値はCの各回で採り、初期化基準値と混ぜない。**init完了時は `gfinit_call` 末尾（`gfinit.c:463–467`）の `gf_release_rom` を飛ばす**。

同じ条件を `gfinit_recover_end`（`gfinit.c:235`）にも適用し、既存ownerのcompiled・profile・bus・leaseとその参照先ROM/decoded/flowを常駐で保持する。値領域へpointerを複製しない。

init完了の無効化時は再Cを拒否し、S0が参照するprofile/callbackを使い終えてから既存の解放手順へ戻す。再起動で全常駐値は消える。cold latchによる新initの拒否は緩めない。

入口案は `gfinit c-only --allow-write --verify record|required [--no-verify-file PATH]`。ゲストが新しい実行番号を採番し、毎回Qから実行する。途中nodeの再開ではない。

C-onlyはcold latchで拒否する `gf_inspect/gf_begin`（`gfinit.c:242,361`）を呼ばず、同じownerの専用入口から新sidを得る。初期化の入口は変更しない。

例外条件Eは「init完了・基準値valid・C-only・再C許可・直前S0成功・ID/BAR等の入口検査成功」がすべて成立した場合だけ。3か所とも同じEを使い、failedを先に消して通さない。

再実行を許すのは正常表示後・明示abort・機能CHECK不一致からS0が成功し、入口条件を再確認できた場合。バス／窓／S0失敗、ID/BAR変化、init失敗ではinit完了を無効にする。

timeout・送信失敗からの自動再Cは初版では行わず、既存のS0→再起動手順へ戻す。host解析だけの失敗でゲストのinit完了を消さない。新しいreason分類は不要。

戻しの状態は常駐1個。C-only終了後も保持中のprofile・bus・callbackでS0を呼べること、解放時は既存S0へ付け替えてから領域を返すことを試験する。都度ロードや二重ownerは不要。

> [要確認: 計画の時制?]

**restore の後も GF の owner と lease を保つ**restore の S0 も §3 の E の「直前の S0 成功」に数える。

## HWDEV_CORE_API

- owner の試行 RAM 上限は **269頁 = 1,101,824B (1076KiB)**。2048 node で decode 69 + plan 40 + ROM 32 + flow 128頁を個別に丸める。**この上限と、計画の表の大きさ (GF_PLAN_TARGETS・GF_PLAN_RULES・GF_PLAN_LAYOUTS・GF_INIT_ARGS・RECOVER_CAPTURE_MAX など) は、物理的な制約 (起動 FD に入らない・部品の像の上限・番地の配置) に当たらなければ広げてよい** — 枠に収めるために既存の利用を削らない。小さい静的 control は常駐 BSS に計上し、旧 SIZE_BARS flow は最大 flow の確保前に返す。

- ROM/decode/plan/flow は専有 trial RAM。plan は decode の node/arg を借用し、profile は compiled の表を借用。PAUSED は bundle・lease・両 handle を保持。SIZE_BARS 成功から PREPARE までの保持も参照中として数える。同期 callback が帰る前に解放しない。

- 終端順は局所 cleanup → 共通終端 → 核に結果保持 → plan/profile/expr/opaque の作業 RAM 参照解除 → release_work (I/Oなし・二重呼出し安全) → lease 解放。SIZE_BARS 成功の保持例外は解除/free を次の開始・中止まで送る。GF_STAGES T1 の初期化完了後は GF_STAGES_DESIGN §3・§10 に従い、S0 後も init・作業域・owner/lease を保持する。外部 recover、開始途中失敗、二重終端も同じ所有規則を使う。

> [要確認] 試行 RAM の頁数と表の上限は旧静的展開時の値。現行の共有 runner・C 常駐保持の構成との差を照合する必要がある。

## HWDEV_DESIGN

| 実物・根拠 (file:line) | 確認したこと |
| --- | --- |
| `kdebug/gfinit.c:25`、`:46`、`:213` | GF owner、ページ確保、開始と bind。ROM/decode/plan/flow は試行寿命、A1 成功時だけ次の準備まで一部保持 |

- PAUSED も lease と部品参照を保持。解放は局所 cleanup → 共通終端 → 結果保持 → plan/profile 参照解除 → 作業領域 free → lease 解放。A1 成功の ROM/decode 保持は既存例外として参照を残す。

- lease は常駐 GF control と同じ boot body に置き、単調増加する claim 世代・所有世代・session を作業領域の世代と照合する。

## REALHW_RECOVER_ADDENDUM

- ownerのcallback・状態だけを小さい静的controlへ残し、ROM・decode・plan・flowは既存`pgalloc_alloc_phys(LEDGER_OWNER_KERNEL)`と対のfreeで試行中だけ所有する；flowはbind対象の実node数×64 u32、decodeは作業2048step/引数6144、planはtarget768/rule1280/layout1056/expr16のGF固有上限（超過は開始拒否）とし、32MB機の一時領域を1MiB以内でhost検査する。

- SIZE_BARS成功だけは次のPREPARE用にROM/decodeを保持し、終端・中止・開始失敗では全ページを返しlast_opを無効化する；外部recover後の同session再送も成功キャッシュを返さない。

> [要確認] 一時領域 1MiB・旧表件数・終端で全ページ解放の原文は、後段の上限拡張と GF_STAGES の init 保持例外と異なる。

## REALHW_RECOVER_DESIGN

4a op と recover の対応（4a r3 と共通の契約）:

| 4a の op | recover 側の契約 |
| --- | --- |
| INSPECT | PROM 取得・静的検査のみ。trial を開始せず、許可された読取りだけを共通 descriptor で検査 |
| SIZE_BARS | 測定専用の有界 trial_begin→trial_step→trial_end。下記の BAR 保存・復元を局所 cleanup として行い、終端は recover が所有 |
| PREPARE | trial_begin(profile, plan_hash, budget)。session・snapshot・arm は recover の単一状態機械が所有 |
| RUN_ONE | 1 opcode ごとに trial_step（安全点）。内部 poll/delay も安全点を持ち、main 正常終了だけ次の番号へ進む |
| VERIFY | trial_end(normal)。recover が唯一の終端 cleanup と verify を実行 |
| ABORT | trial_end(abort)。エラー・期限超過も同じ入口へ集約し、重複要求は既存結果を返す |

- 4a の DIRTY は recover の FAILED または cold-required による再実行禁止を示す派生表示とし、独立した復旧状態機械を持たない。4a は実行位置などの解釈器状態だけを持つ。局所 PTE/IRQ/index の後始末を終えてから recover の終端 cleanup を一度だけ呼び、その後に 4a が GPU を再設定しない。cold-required は新 session だけで解除せず、電源再投入記録と COLD 再採取を必要とする。

- 4a 側は実行前に分岐先を含む write-set、BAR sizing、PLL/PFB、VRAM test の範囲を確定。全 write を descriptor へ照合し、未知 opcode/動的番地は書く前に拒否する。

## SLIM_HWDEV_DESIGN

- gfinit.c:228：inspectでclaim後にg->compiledをgf_alloc(sizeof(*g->compiled))、展開後にidentity設定・bus_init・probe・ROM/decodeへ進む。

- 確保後のprobe/ROM/decode/展開の失敗は全てgf_release_romへ集約し、追加した160 KiBとleaseを残さない。inspect済み表示は成功時だけ。

- gfinit.c:308：gf_beginではcompiled存在を確認して再展開し、identityを再設定。SIZE_BARSにも必ず展開済みprofileを渡す。

- RUNのgfinit.c:317の遅延確保は削除。

- gf_read（gfinit.c:94）はcompiledの存在だけで通知せず、`r->plan == &g->compiled->plan`も条件にする。先行確保しただけのSIZE_BARSではplan_readを呼ばない。

- gf_release_rom（gfinit.c:194–209）は既存のdetach成功を確認→全backing解放→compiled=NULL→expand(&g->profile,NULL)の順。失敗したdetachを飛ばして解放しない。

- 再展開はinspectまたは既存gf_beginのIDLE/VERIFIED門内だけ。bind後からcleanup完了まで配列の番地・内容・参照先の寿命を保つ。

- gf_release_rom は detach 拒否を戻り値 -1 で伝え、再 inspect はそこで中止する；そのとき backing・profile・lease を保持し、再確保や再展開で上書きしない。
