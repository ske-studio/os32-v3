# tools/rshell_recover.py — 設計
> 状態: **現行 (2026-10-31)** — 出所: GF6200_4C_DESIGN・GF6200_RECLOG_DESIGN・GF6200_S7_SHELL・GF6200_VPLL_PROBE・GF_STAGES_DESIGN・REALHW_RECOVER_ADDENDUM・REALHW_RECOVER_DESIGN・RECOVER_T4B_INJECT (D1 で移した)
ソース: `tools/rshell_recover.py`

monitor は C の終わりに不一致の件数・種別・番地/index・書いた値・読んだ値を表示し、
記録だけの不一致は機械項目の不合格にしない。追加読取りも既存の行数・UART/時間予算へ算入する。

## 2. ノート: 行の完全性を検証してから seq/対応表を更新する

各RECの書式は維持し、finalのみtxdを追加。全行へのlen/eorは付けない（数値の途中欠けが合法値になる破損は単独では検出不能）。

`Records.line()` は ASCII、重複/未知key、裸token、未知phase/event、数値範囲、必須欄を検証する。
共通必須: sid/seq/phase/event。u32 は0..4294967295、rcはs32、boolは0/1、enumは既知値のみ。CRLFは末尾CR一個のみ除外。
| phase / event | 共通以外の必須欄 |
|---|---|
| state / IDLE,SNAPSHOTTING,ARMED,RUNNING,PAUSED,RESTORING | version,profile_crc,plan_hash,snapshot_crc |
| snapshot,access,restore,decode,screen,route / BEGIN | step,addr |
| 同 / END | rc,elapsed |
| gf_pre / BEGIN | main,pc,opcode,exec,substep |
| gf_pre / END | 上記5欄,rc |
| gf_bus / BEGIN | pc,bar,off,width,old,new |
| gf_bus / END | pc,bar,off,width,readback,rc |
| gf_verify / END（単独結果） | saved_words,minimal,capacity_known,full_capacity,capacity_bytes,tested_bytes |
| receive / DISCARD（単独） | discarded_bytes |
| diag / TX（単独、一回） | fail_seq,pos,fn,rc,where,sent,polls,bytes |
| final / END（単独、一回、最後のREC） | result,target,verified,unknown,override,saved,actual,trial,lost_lines,discarded_bytes,txd |
virtual 専用 inject/BEGIN は step,addr の単独通知として明示登録（実機A4では出現をエラー）。
通常の BEGIN/END は LIFO スタック。親子の固定許可表は使わず、gf_bus⊃access、screen⊃screenも同じ規則で扱う。
END は phase 一致、gf_pre は5識別欄、gf_bus はpc/bar/off/width一致。通常ENDは識別欄が無いのでphaseと順序で照合。
PAUSED/final で未閉鎖、孤立END、取り違え、final後のREC、複数finalはエラー。単独結果をpopしてはいけない。
sid/seq連続性・metadata freeze は現行条件を維持。検証済み行だけ状態へ反映し、stack と sticky invalid も保存する。
初回エラーは保存状態にも固定し、後続正常行や次の起動で消さない。旧保存状態は拒否。mainの状態読込/検証もtry内でexit=1へ統一。
`exchange()` は EOT/期限時の非空行バッファを検査し `recover: truncated line before EOT/deadline`、exit=1。
不正行は `recover: malformed REC sid=S seq=Q field=F`。
対応不整合は `recover: unmatched BEGIN/END sid=S seq=Q phase=P`、seq差は現行missing RECメッセージ、全てexit=1。
行過長も sticky error にしてLFまで捨てつつ raw は保存。最初のエラーを後続エラーで上書きせず、既存期限内でEOTまで排出。
OKには全検査通過、空stack、必須diag、現行success条件に加えtrial=0、final.txd=0とdiag.fn=NONEを要求。FAIL/PARTIAL/欠落はexit=1。
PAUSEDは完全な非終端応答として従来通りexit=0/表示PAUSEDを維持するが、A4合格やresult=OKとは扱わない。

> [要確認] GF_STAGES_DESIGN の後続契約では C のレジスタ二重比較を廃止し、ゲスト OK/NG を転記する。後掲の monitor 契約を優先する。

ノートはその値で `--cleanup-timeout` が足りるかを確かめる (足りなければ警告)。

ノートはこの欄をmetadataの4欄から分離して検証・保存し、`--cleanup-timeout < cleanup_seconds + 余裕` を警告する。freeze条件は変更しない。
待ちの既定値は容量最大で求めた秒数＋余裕を上回る値にし、--timeoutも整合させる。

PARTIAL/overrideは現状どおり失敗終了・証拠保存、OK条件や終端表示は変更しない。

hostは既存metadataのversion（O/kdebug/recover.c:448–449、O/tools/rshell_recover.py:41,122–123）=56504C03hで診断版を識別し、対象5規則だけverify=0でも実測として扱い、それ以外の版でこの5規則のreadback≠NONE（FFFFFFFFh）が出たら誤りとする（他のverify非零規則は従来どおり）。

**止めずに記録し、ホスト (rshell_recover.py) で読み戻しを見て判定する**。

## 6. monitor と引数なしの戻し

判定はseq、BEGIN/END、EOT、欠落とゲストOK/NGの写しだけ。
必須CHECKの実施と集約結果はゲストが保証する。
recordは「観測完了」、required成功は「機械項目合格・映像保留」。S0のPARTIALをmonitorが独自にNGへ変えない。元のOK/NG行を改変せず報告に残す。

待機中（shell受付可能時）は新しい `recover restore`（追加引数なし）を `--restore` から送る。常駐ownerが対象を選び、sid/state/image CRCは不要。既存 `recover now <sid>` は互換入口として残せる。
`main():550` のstate読込みより前にrestoreを分岐し、ログファイルを開けないことも送信の門にしない。返答解析が失敗しても戻し送信を取り消さない。
RUN中はshellを再入せず、道具が現ストリームからsidを補った**既存abort frameだけ**を送り、安全点でS0へ進む。sidを利用者に要求せず、固定S0 frameは追加しない。sidを取得できない通信断・停止は既存の期限／物理復帰手順の範囲とする。

monitor の `--restore` は、EOT を読み終えた待機中のシェルが EOT を送り直さないので、**30 秒の無通信** (RESTORE_IDLE_SECONDS) を待機中とみなして `recover restore` を送り、その間に REC が来れば sid を得て abort。RUN 中に REC が 30 秒以上途切れる (C の VRAM の全域転送の間は REC を出さない) と取り違えるが、RUN 中のゲストは届いた文字列を捨てるので害は無く、戻しが期限切れになるだけ — **運用の制約**として受け入れる。monitor は RUN 中の REC の受信間隔の最大と直前の phase を終わりに 1 行出す (判定には使わない)

hostはexclusive serialとflockで単一所有しraw受信とsession/seq/CRC/最後のBEGINを保存、初回verの起動image CRC/buildを照合；PAUSEDの継続は保存済み起動照合を使い通常コマンドを送らず、以後のsession/seq/CRC不一致を拒否する。host timeoutはabort後に終端が届いても成功へ変更しない。
hostは終端RECと対応command echo/EOTを揃えて判定し、欠落・lost・FAIL/PARTIAL・shell拒否は非成功；CPU/バス停止や物理再起動は自動操作しない。

> [要確認] flock による所有は後続の REALHW_RECOVER_ADDENDUM の serial exclusive=True のみという契約で差し替えられている。

- hostの--statusはsession/state/image-crc不要の状態表示専用とし、echo・本文・EOTを照合する；復旧成功とは扱わずver・abort・session更新を行わない。aidebug:は既存AidebugPortを使い、serialはexclusive=Trueだけで所有する。

/dev/ttyUSB0 を単一所有し session/seq/CRC を保存、期限監視・abort 要求・起動検証。LAN は大量ログと配備用、制御の必須依存にしない

--command は一命令、PAUSED は EOT で帰還。state/log を継続し、初回だけ ver。欠落・timeout は非成功
