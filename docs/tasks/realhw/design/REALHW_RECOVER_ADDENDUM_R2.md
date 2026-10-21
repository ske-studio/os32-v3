# r1→r2 の変更

> 状態: **現行 (2026-10-21)** — 戻す処理の確定設計への追記 r2 (暫定予算・PM 決定 A3・段の割り当て)。Opus 第 2 回で Approve、本文の一部。
> 出所: `~/os32-tmp/run/realhw_recover/ADDENDUM.r2.md` の確定版を 2026-10-21 に写した。往復の版 r1〜rN は ~/os32-tmp に残る。中身は変えず、リポジトリ内の文書への参照だけリンクに直した。

- A1: UARTを1フレーム待ち・1文字1 pollとし、復帰pollの静的必要数と予約をT2で検算する。
- A2: ops.local_cleanupをcleanup()入口へ接続し、T1bの合格条件とindex選択中abort試験を追加する。
- A3: 2026-10-21のPM決定を原文どおり明記し、T5前の書込み承認・立会い・暫定値記録を明確化する。
- A4: trial_limit・opcode_limit・max_stepsの値と適用単位を表に追加し、opcodeごとのpoll根拠を修正する。
- A5: 送信余裕を単調な式に置き換え、復帰上限を10/18/33秒へ再計算する。
- A6: index選択はdataと同じtargetのSELECT substep、value=indexと確定する。
- A7: GF rule実データはkdebug/配下のrecover GF profileファイルに置き、4a S1追補で起票する。
- A8: evidence文字列をhost側へ移してCRCから外し、T5は正常10回・各失敗経路3回にする。

# recover 確定設計への追記 r2（2026-10-21）
本文 §3〜5・§7・末尾の確定を補完する。U1〜U3・N1、cold-required、0FACh 初回承認を変更しない。

## 1. 欠け 1 の決定
- `restore_limit` は RESTORING 入口から局所後始末・①②・verify・終端ログまでの独立した総経過予算。main の60秒や残余 trial 予算から借りない。
- ops に `local_cleanup` を追加し、共通 `cleanup()` 入口で `restore_tick` 設定直後、recover ①・quiesce より前に必ず呼ぶ。4a の安全点 abort から直接 cleanup() に入る場合も、残る局所 PTE/IRQ/index 後始末と窓の解放をこの順で行い、同じ復帰予算で数える。終端入口で再初期化しない。PAUSED の待機時間は従来どおり対象外。
- 以下は**実測値ではなく計算に基づく暫定打切り値**。実測済みとの表示は禁止する。
- **PM 決定 (2026-10-21)**: 「実測までは暫定値で書込み試験を許す。暫定値は超えたら FAILED に倒れる上限で、書込みの各段はユーザーの承認と立会いのもと (U1・U2)。T5 で実測して置き換え、置き換えるまでの run の記録に暫定値で走ったことを残す」。T5 前の A1/A2・script RUN にもこの決定を適用する。

| profile（T2で固定する区分） | restore_limit | 復帰ログ上限 B | 非送信実行枠 E | max_polls / restore_max_polls | trial_limit / opcode_limit / max_steps（適用単位） |
|---|---:|---:|---:|---:|---|
| S0のみ／NP21用仮想 | 10秒 | 4096 byte | 2秒 | 65536 / 16384 | 60秒 / 60秒 / 4096（session全体 / opcodeごと / session全体） |
| GF通常・4a cold-required（quiesce＋②） | 18秒 | 8192 byte | 4秒 | 65536 / 32768 | 60秒 / 60秒 / 4096（session全体 / opcodeごと / session全体） |
| GF SIZE_BARS（局所BAR復元＋①②） | 33秒 | 16384 byte | 6秒 | 65536 / 65536 | 60秒 / 60秒 / 4096（session全体 / opcodeごと / session全体） |

- 根拠: 4a §6 l.161 の9600bps・8N1から、フレーム待ち `F=ceil(bits/frame*10^6/baud)=1042µs`、送信枠 `S=ceil(B*F/10^6)` は5/9/18秒。余裕は `ceil(max(3秒,S*50%))` の3/5/9秒とし、S＋E＋余裕で10/18/33秒とする。E は未測定の工学的余裕で、ハード仕様上の完了保証ではない。
- B は仮の平均行長でなく、最大桁数・BEGIN/END・失敗経路・局所cleanup・最終行を含む静的最大バイト数の収容枠。T2で実列から計算し枠内を確認する。超えるprofileは開始拒否、値を黙って拡大しない。
- UART設定が異なる場合も実bits/frame・baudで再計算して表の枠内を確認する。UART は `ceil(bits/frame*10^6/baud)` µs を待ち単位とし、正常送信は1文字1 pollで計数する。TxRDY未成立で繰り返す待ちも1回ごとに加算し、初回確認を別pollとして二重計数しない。UART以外のpoll/delayは1ms単位へ分割（指定待ちを切り下げない）し、各反復で残予算を検査する。
- max_polls はopcodeごとに0へ戻す安全点上限。表の opcode_limit=60秒の1ms刻み60000＋内部安全点の静的収容枠4096=64096を65536へ切り上げた暫定値。送信待ちは独立計数する。trial_limit=60秒とmax_steps=4096はsession全体の暫定打切り値で、4aのmainごとの60秒・4096とは別に適用し、main切替でもリセットしない。複数mainの完走保証ではなく、総枠超過はFAILEDとする。内部安全点数はstepsと同一視せずT2で検算する。
- `restore_max_polls` を追加し復帰中の全poll/delay/UART待ちで累積する。静的必要数は `B＋送信以外の待ちms（切上げ）＋安全点数`。E全体をms待ちとみなす保守的な計算と安全点収容枠1024で、S0=7120、GF通常=13216、SIZE_BARS=23408。T2で実列から各区間と総枠を検算し、②の16384 polls予約を除いた局所処理・①の枠も確認する。SIZE_BARSは予約と残区間を同時に収容するため65536とする。未成立UART待ちの反復も残枠を消費し、上限到達で打ち切る。
- UARTにはtrial中も同様の独立有限回数枠をBとT_logから算出する。復帰中はUARTをrestore_max_pollsに含める。tick停止でも、戻ってくるpollの無限反復を許さない。
- 秒→tickは実HAL周波数で切上げ、checked整数演算、0禁止・半周未満。hostはfake tick周波数を明示し同じ換算を使う。host経過秒を実機根拠に転用しない。
- GFの18/33秒のうち末尾10秒・16384 pollsを独立な②のため予約する。①・局所処理は予約に入る前に打切る。S0は全枠を②に使う。ログ量も②用4096 byteを予約する。
- 枠超過・poll上限到達は最初の原因と段を保存してFAILEDに固定。危険アクセスの再試行なし。残予約内で独立に安全確認できる②だけ続行し、総枠を越えて継続しない。
- 時間制限は同期バス停止を解除しない。ログ不通時は有限待ちで送信を諦め欠落数を保持し、後始末を妨げない。FAILEDをS0成功だけで解除しない。
- T5で各profileの正常系を10回、中止・期限・注入可能な失敗経路を各3回測り、総tick、送信tick、非送信tick、poll数、B、機種/版/通信設定を保存する。①と②も分けて採る。
- 本採用値は各区間の `ceil(2*観測最大非送信tick)+2tick+送信枠`、送信枠は `ceil(B*bits/frame/baud)+max(1秒,ceil(観測最大送信時間/2))`。pollは `2*観測最大+32` と静的必要数の大きい方。全体は区間の和とし、②の予約を維持する。
- PMレビュー後にprofile版・CRCと根拠ログを更新する。未測定経路は暫定枠を残し、そのrunには暫定値使用を記録する。T5で必要な測定と置換を完了する。暫定超過なら停止して原因と枠を再レビューし、自動延長しない。
- T5を4a開始前提にはしない（本文PM決定A4）。T5以前の承認済みA1/A2でも同じ測定を収集でき、T5で不足分を閉じる。LAN/PCM等の未定義profileはこの表の流用で有効化しない。

## 2. 欠け 2 の決定
- GNU11のkdebug内部契約を版上げする。32件のsnapshot表とW規則表を分離するが、許可の所有者は共にrecover。同じ番地の複数phase/値規則を表現する。
- `key` は下記targets配列のID（0始まり、ポインタや詰込み番地でない）。全read/write accessorでこの共有表を解決する。snapshotのregsとquiesceも同じIDを参照する。
```c
struct recover_target { /* canonical address tuple */
    u32 space, bar, offset, head, port, index, width, access, read_safe;
};
struct recover_value {
    u32 mode, mask, bits, min, max, expr_id;
    const u32 *allowed; u32 allowed_count;
};
struct recover_guard { u32 key, mask, expected, when; };
struct recover_rule {
    u32 id, key, phase, main_id, pc, substep, write_mask, verify_mask;
    struct recover_value value;
    const u32 *requires; u32 requires_count; /* rule IDs */
    const struct recover_guard *guards; u32 guard_count;
    u32 flags; /* evidence (file:line) はhost側のrule ID対応表/コメントに置く */
};
/* recover_profile に追加。既存のregs/countはsnapshot/復元順専用。 */
/* const struct recover_target *targets; u32 target_count;
   const struct recover_rule *rules; u32 rule_count;
   u32 restore_max_polls, s0_reserve_ticks, s0_reserve_polls; */
/* recover_reg に u32 restore_rule_id を追加。
   recover_write に u32 rule_id を追加。key/width/valueは規則と照合。 */
```
- table上限はtargets=1024、rules=2048、各requires/guards=8。S1の619副作用候補は許可済み件数ではない。展開後不足なら開始拒否しレビューで上限を更新する。実行回数は既存の動的予算で制限する。
- spaceはPCI_CFG、BAR_MMIO、BAR_VRAM、PC98_IO、VGA_DIRECT、VGA_INDEXED。不要欄は0、非indexedのindexはNONE（0xffffffff）、非scriptのmain/pcはNONEとし曖昧なwildcardにしない。
- VGAのportは論理3xx番号、headは0/1。3C2/3CC・3C3・3C4/5・3CE/FはPRMVIO=`BAR0+0x0c0000+head*0x2000+port`、3D4/5はPRMCIO=`BAR0+0x601000+head*0x2000+port`（4a §5）。幅は1。
- INDEXEDはportを3C4/3CE/3D4のindex側で正規化しindex=0..255、data側はport+1。index選択もWとし、条件読取も承認済み書きop内限定。直接data側や同一別名へのraw MMIOによる迂回を拒否する。
- index 選択はdata書込みと同じtargetを使い、`substep=SELECT`、valueはtargetのindexと一致させる（fixtureのc03c4/c03c5も同じtargetの別substepへ対応付ける）。
- CR44はhead0で操作し値3で以後head1、値0でhead0。共有accessorがhead状態を管理し、不明値を推測しない。3C9・3DA/3BA等は従来どおり拒否する。
- accessorは元indexと分類済みlock/dataを保存し、CR1F/CR11の必要な解錠→data→lock→元indexの順を実装する。内部書込みもsubstep別ruleで許可し、失敗後にCOLD値を再生しない。
- phaseはSIZE_BARS、PREPARE、RUN、VRAM_TEST、QUIESCE、RESTORE、ROUTE、SCREEN、VERIFY。既存ログphaseと区別した権限用enumを追加し、実行contextはrecoverが保持する。
- main_id/pc/substepを含め完全一致で引く。SUB/CALLは呼出元main＋実pc、PLL/macroは各内部substepで照合する。INSPECTはread_safeな読取だけで、index選択書込みを行わない。
- value.modeはLITERAL、SET、RANGE、EXPR、SAVED。共通条件は`(new & mask)==bits`、幅内、RMWでは`((old^new)&~write_mask)==0`。SETは列挙値一致、RANGEはmin..maxを追加で満たす。
- EXPRは任意callbackでなく登録済み整数式ID（RMW/COPY/PLL係数/VRAM pattern等）と検証済みplanの引数を使う。規則側でも結果・範囲を検査し、未解決式は拒否する。旧値の読取りが安全でないRMWは禁止。
- SAVEDは同sessionのvalid snapshotに結び付いた安全なRWのRESTORE、または分類済み局所復元だけ。COLD/trigger/W1Cは許可しない。CommandはSAVEDでなくoverride=0102h、BAR復元失敗時のMem再開禁止も規則にする。
- requiresは先行ruleの成功・必要readback完了を要求する。自己参照・循環・存在しないIDをprofile_okで拒否。展開plan上の全経路で先行成立を静的検査し、runtimeでも成立記録を照合する。
- 記録はsession/phase/呼出し・反復世代に束縛する。世代を跨ぐ依存はplanで先行実行を明記する（例:PREPAREの停止確認）。skipされた書込みや前反復の成功で依存を満たさない。
- N1専用ruleはRUN/main3/pc=E81E/SR01 bit5解除のみ。headは展開planで確定し、他bitは原命令のmaskと値制約を満たす。別pc/mainの表示有効化は拒否する。
- N1 guardは生PCI cfg50/54とBAR0 PBUS1850/1854を各32bit、mask=0xffffffff、expected=0として直前/直後に確認する。どれか不一致・読取失敗なら後続scriptを止めFAILED→共通cleanup。
- PREPAREで0を確立した後は全phaseで非零書込みを拒否し、各RUN_ONEの入口/出口・終端でもguardを確認する。N1直後確認を遅延しない。cfg/MMIO同値視だけで片側確認を省かない。
- N1 guard前にも生Command.Mem/BAR0を検査。不成立ならMMIOを読まずFAILED。0FAChはrecoverだけが扱い、54h=1での迂回や未承認ROUTE書込みは認めない。
- profile_okを拡張し、key範囲、幅/整列/BAR範囲、mask/値/式、read安全性、phase、依存・guard、重複矛盾を検査する。根拠文字列はhost側のrule ID対応表またはコメントで検査し、カーネルに載せずCRCから外す。CRCはポインタ/構造体paddingでなく実行規則の全表の正規化値・参照先列を含める。
- 619候補の分類を含むGF rule表の実データは `kdebug/` 配下のrecoverのGF profileファイルに置き、4a S1追補で起票する。許可表の所有者はrecoverとし、4a側へ重複配置しない。
- 4aはdecode/整数計算・展開write-setを作りrecoverの照合APIを引くだけ。静的検査と実アクセス直前に同じ検査器を使う。ROM候補表・fixture・S5 busに別の許可表を置かない。
- T1bは表現形式を確定する段。個々のW行の資料根拠・安全なreadback mask・PLL式が未分類ならDENYのまま。形式追加を全619候補の一括許可にしない。

## 3. 段の割り当てと各段の合格条件・変異の追加
| 段 | 追加成果・合格条件 |
|---|---|
| T1b（推奨・T1の補完小段） | 共有型・CRC・profile_ok・静的/runtime検査器・fake accessor・ops.local_cleanup。cleanup()でrestore_tick設定直後に局所後始末が呼ばれ、①・quiesce前にPTE/IRQ/indexと窓を解放すること。RUN_ONEのindex選択中abortを注入し、quiesceが窓占有によるBUSYにならないこと。4a S1対応表の全欠けに型と検査先があり、未分類は拒否。T2と4a S1追補の先行条件とする |
| T2 | 表の暫定予算・区間予約・poll計数・全callbackの安全点を接続。B＋非送信ms＋安全点の静的必要数と②予約/残区間の検算、正常UARTで1文字1 poll・各profileの時間枠内完了の確認、全失敗注入で有限終了・FAILED固定。既存窓/98/Command受入も維持 |
| 4a S1追補→S2〜S5 | S1追補で619候補の分類とGF rule実データをkdebug/配下のrecover GF profileファイルに起票し、展開W行を単一表へ照合、別名/index/値/依存/N1の正常traceと拒否trace。未知行はGPU書込み0、実行途中guard失敗は後続0＋cleanup。S5はaccessor実装のみ |
| T3/T4 | 送信詰まり・tick停止時のpoll上限・host終端不着を確認。NP21の結果を実機計測扱いしない |
| T5（先行A1/A2測定を併合可） | 各profileの測定記録・暫定/実測区分・固定上限のレビュー。0FACh初回書込み承認を維持 |
| T6/T7 | 失敗経路の測定を補い上限の根拠を更新。U1/U2の実施承認は従来どおり |

- 追加変異: local_cleanup呼出し削除／quiesce後へ移動、UART待ちを1msへ戻す／1文字を二重計数、restore予算をtrial残量で代用／入口で再延長、②予約削除、UART/poll計数除去、tick wrap比較反転、超過後VERIFIED化。
- 追加変異: head stride除去、PRMVIO/PRMCIO混同、index/data逆転、index選択W検査省略、phase/pc照合省略、値mask/SET/RANGE/式検査省略、依存順逆転・前反復記録流用。
- 追加変異: N1前/後guard片側削除、cfgのみ又はMMIOのみ検査、非零保持許可、CRCから新表除外、COLDをSAVEDで復元。各変異を対応試験が検出すること。
- solは関連試験をMUTATE=0で実行、変異実行は本文どおりPMレビュー後。T1bをT2へ混在させず先にレビューし、4a開始条件は「補完済みT1＋T2」のままとする。

## 4. 未決（ユーザー判断が要るものだけ、推奨付き）
- 0件。上限実測・個別W行の分類は実装/PMレビュー条件。既存の実施承認や4b前のモニタ決定、PG解除条件の追加判断を本追記で消費・変更しない。

## 反論
- なし（0件）。
