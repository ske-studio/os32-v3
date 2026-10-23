# 0FACh：T5 の明示承認付き 1 回書込み口（追記案、2026-10-24 PM）
T5 の 1 回は、現在値が 00b でも 00h を書く経路・mask 読戻し・98 表示維持を確かめる；非98入力からの切替成功までは証明しない。

## 1 実物の確認 (file:line)
以下の参照は `/home/hight/os32-v3/` 相対。指定箇所のみ確認し、実装・実機操作は行っていない。

| 根拠 | 確認した事実 |
|---|---|
| `docs/decisions/realhw/REALHW_RECOVER_DESIGN.md:85–88,183` | 通常②は読んで00bなら無書込み；初回書込みはT5の承認1回、目視が必要。 |
| `docs/decisions/realhw/REALHW_RECOVER_ADDENDUM.md:167` | 未試験now 0はS0のみ、承認false、S1 unknownのPARTIAL。 |
| `kdebug/kdebug_main.c:25–55,94` | inpはあるがroute_writeは未接続、初期configureの承認はfalse。 |
| `kdebug/recover.c:174–194,389–400` | configureは承認を保持；now入口はsession一致、未試験ではCUI等とS0予算を検査。 |
| `kdebug/recover.c:881–894,946–970` | routeのstep 0=read／1=write；非00bで未承認ならCONTRACT、00bなら書かない。 |
| `kdebug/recover_wire.h:3–10`／`userland/shell/cmd_recover.c:28–35,56–65` | private op 4、40 byte要求/結果；nowはsessionだけを解析し、結果を表示。 |
| `kdebug/kdebug_main.c:121–149` | PAUSED制限、要求長検査、now後のdetach/release_workと結果返却がある。 |
| `docs/hw/undocumented/io_wab.md:345–366` | 0FAChはR/W、上位6bitは常時1読出し；判定は03h、書く値は00h。 |

## 2 口の形と寿命
- (b) を採用：`recover route-test 0 --approve`。承認なしの形はusage拒否、ポート値の引数は設けない。
- T5の未試験S0専用：IDLE・token.session=0・実機S0 ops接続時だけ受理する。既試験なら新たな起動で実施し、状態を強制初期化しない。
- 通常の `recover now <sid>` と自動復帰は従来どおり承認false。T5成功を恒久的な許可に変換しない。
- KAPIは不変。private NOW opの40 byte要求は維持し、専用コマンドだけ下記48 byte形式を送る（旧実装は要求長で拒否）。
```c
/* recover_wire.h にC89で共有。version/flagは名前付き定数。[C4] */
typedef struct { KdebugRecover rec; u32 version, flags; } KdebugRecoverRouteTest;
/* version=1, flags=ROUTE_TEST_APPROVE の完全一致のみ；rec.session=0 */
```
- recovery_callは40/48 byteを区別し、48 byteのversion/flags/session/IDLE/実機S0を確認；PAUSEDは専用形を拒否する。
- attach・CUI/31kHz等の既存開始検査・期限/予算検査は省かない。専用形も既存nowのS0処理と終端・資源解放を共有する。
- 承認はconfigureの永続設定を変えず、専用呼出しの範囲で `route_write_approved=true` と一回用 `route_test_once=true` を設定。
- screenの最初のread成功後、専用一回用フラグがあれば00bでもwrite分岐へ入る；通常②の条件は変更しない。
```c
/* 概念境界：screen内。callback有無・既存期限を確認した後。 */
bool once = r->route_test_once && r->route_write_approved;
if (once) { r->route_test_once = false; r->route_write_approved = false; }
/* onceなら logged_route(r, 0) を最大1回 → logged_route(r, &route) → mask検証 */
/* 専用入口の共通出口：成功・拒否・失敗とも両フラグ=false（旧値を復元しない）。 */
```
- 初回read失敗は書込み0回。消費後の期限切れ等でも再試行しない。書込み後の再入cleanupも許可を再取得しない。
- `kdebug_main.c` に `recovery_route_write(void *, u32)` を追加しrecovery_opsへ接続；値がRECOVER_ROUTE_98以外なら拒否。
- outpはこのcallbackだけに置き、`outp(RECOVER_ROUTE_PORT, RECOVER_ROUTE_98)` を1回。read-modify-writeや上位bitの再生はしない。
- [C4] ポート/98値/maskは既存RECOVER_ROUTE_*定数を再利用。03hは読戻し判定専用、FC→FCでも成功、非00bならREC_VERIFY/S0=FAIL。
- RECは既存route BEGIN/ENDで足りる：成功時step列はread(0)→write(1)→read(0)。ログ自体に値がないため照合成功は終端のS0 verifiedで確認。
- write BEGINだけでは実行済みと断定しない。ログ欠落/期限超過は成功扱いせず、追加の無期限待ちや自動再送は作らない。
- KDEBUG=0は既存NOSYSのまま、I/O 0件。shellは既存エラー表示を使い、結果はrec部分から従来形式で表示する。

## 3 受け方と目視
- rshell_serialの通常コマンド送受信で受ける。rshell_recover.pyの新CLIは作らず、以下を送信する文字列の仕様とする。
```text
recover status
recover route-test 0 --approve
```
- ユーザーがRa266・CUI・未試験session 0を確認し、T5の1回を明示承認してから2行目を一度だけ送る。現在00b見込みは実測の代用にしない。
- 受信期待：同一session内の連続seq、route BEGIN/ENDの0→1→0が各1組、write END成功、S0検証済みの終端、shell結果 `verified=1`、`lost=0`、EOT。
- 未試験なのでS1 unknown/PARTIALは許容；全装置復元のOKとは扱わない。拒否・FAIL・欠落・EOT不着なら止めてログを保存し、再送しない。
- 読戻し完了後、ユーザーが98画面の文字と表示維持を目視して記録。表示が戻らない／応答しない場合はユーザーがリセットする。
- 実機承認・実行日時・受信全文・目視結果をT5記録へ残す。この設計案は実機書込みの承認を兼ねない。

## 4 試験・変異・段
| host試験（各1行） | 殺す変異 |
|---|---|
| 承認なし：通常nowでFCはwrite=0、FDはwrite=0かつCONTRACT；専用形の承認欠落もI/O=0。 | 既定true化／未承認write分岐。 |
| 承認あり：FC開始でも00hを厳密に1回、read→write→readの順；FD開始も1回、読戻しFCでS0成功。 | 00bで専用write省略／二重write／別値write。 |
| 読戻し不一致：FC開始→00h書込み→FD読戻しはREC_VERIFY/S0=FAIL、後続画面処理なし。 | mask検査削除／常に成功。 |
| 次呼出し：成功・入口拒否・read失敗・write失敗・期限切れの各出口で両flag=false；次の通常呼出しはwrite=0。 | 共通出口解除削除／消費削除による同一呼出し再入write。 |
- 同じhost枠で旧40 byte/新48 byteのshellとdispatch、古いsession・PAUSED拒否、KDEBUG=0のNOSYSとwrite=0も確認する。
- 段は1つ（1〜2時間）：callback＋private wire/CLI＋一回用制御、上記host/変異、KDEBUG有無のビルド、差分確認まで。今回は起草のみ。
- 実機T5はユーザーの運用ゲート後に上記手順で1回；未承認ならhost完了で止め、実機合格とは記録しない。

## 5 確定設計の書換え（差分の文）
- DESIGN §3:85へ追記：「通常②の00b時無書込みは維持。T5専用route-testは未試験S0でのみ、00bでも明示承認の00h書込みを1回行いmask読戻しする。」
- DESIGN §3:87／§7 T5へ追記：「初回はrecover route-test 0 --approve。承認は当該呼出しだけで消費・解除し、以後のnowへ残さない。98状態での成功は非98からの切替成功を証明しない。」
- ADDENDUM:167へ追記：「通常now 0の承認falseは不変。専用48 byte private要求だけを例外とし、S1 unknown/PARTIALおよび目視必須は維持する。」

## 6 未決（ユーザー判断が要るものだけ）
- 1件：実機T5の書込み実施・承認の時点。推奨はhost/変異とビルド確認後、リセット可能な立会い時に専用コマンドを1回だけ承認する。

## 確定 (2026-10-24、Opus r1 条件付き確定可、PM が所見 3 件を採った)
- C1 (P2): 一回用フラグは struct recover (recover.h:256) に足さない — hwdev_layout と hwdev_contract.h の STATIC_ASSERT がずれるため。`route_test_once` と承認は recover.c のファイル内 static とし、入口関数 (例 `recover_route_test_session`) が立て、全出口で false に戻す。struct recover・hwdev の契約・生成物は不変 (差 0 を確かめる)。§2 の「route_write_approved=true を設定」は、この static の承認を screen() が route_write_approved と OR して読む形に読み替える (configure の永続値は false のまま)。
- C2 (P3): 48B の KdebugRecoverRouteTest と version/flags は採らない。新しい private op `KDEBUG_OP_RECOVER_ROUTE_TEST` (12U、3/4/5〜10/11 と衝突しない) に既存の 40B KdebugRecover を使う。サブコマンド `recover route-test 0 --approve` が承認そのもの (`--approve` 無しは usage)。§4 の「旧40/新48 byte」の行は「op 12 の 40B の要求の検査と旧 op 4 の不変」に読み替える。
- C3 (P3): 試験に 1 行足す — gfinit の ops が接続中 (r->ops≠&recovery_ops、gfinit.c:337 の configure の後) なら route-test を拒否し I/O は 0 件。
- 未決 1 件 (実機の実施時点) は運用のゲート。推奨どおり host・変異・ビルドの確認の後、立会いの回 (S8 と同じ回) に 1 回だけ承認を取る。

## route-test の実装で設計者が決めたこと
- op 12 の session・IDLE・接続済み ops の検査は attach より前に行い、未接続時だけ既存 attach で実機 S0 ops を接続する。拒否時は detach/release_work も行わない。
- shell の専用形の session は文字列 `0` のみを受け付ける。入口関数の宣言は private wire ヘッダに置く (recover.h は宣言の追加だけでも hwdev 契約 CRC が変わるため不変とする)。
- route_write callback が無い場合は REC_CONTRACT として終了し、一回用承認は消費・解除する。
