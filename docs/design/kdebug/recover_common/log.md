# kdebug/recover_common/log.inc — 設計
> 状態: **現行 (2026-10-31)** — 出所: GF6200_4A_DESIGN.md・GF6200_4C_DESIGN.md・GF6200_RECLOG_DESIGN.md・GF6200_VPLL_PROBE.md・HWDEV_DESIGN.md・REALHW_RECOVER_ADDENDUM.md・REALHW_RECOVER_DESIGN.md (D1 で移した)
ソース: `kdebug/recover_common/log.inc`

## GF6200_4A_DESIGN

## 6. 停止・復旧・観測

- ログ形式は recover r3 §5 のRECのみ。各命令前の`phase=gf_pre event=BEGIN main=... pc=... opcode=... exec=...`、各書込前の`phase=gf_bus event=BEGIN pc=... bar=... off=... width=... old=... new=...`を送信完了してから処理し、ENDにreadback/rcを残す。sid/seqはrecover一元採番。最終result・試験結果・復帰結果も同契約に従う。

## GF6200_4C_DESIGN

記録だけでも読取りを行い、不一致なら `gf_bus END` の `readback` に続けて

`mismatch=1 space=<recover_space> port=<port> index=<index>` を付け、`rc=0` で進む。

書いた値は対応する BEGIN の `new`。一致時と既定の行形式は変わらず、バスエラーは失敗する。

## GF6200_RECLOG_DESIGN

`transport_state()` の terminal 分岐で DISCARD 後・final 前に `transport_diag(r)` を必ず一回呼ぶ。

例: `REC sid=7 seq=16000 phase=diag event=TX fail_seq=15980 pos=72 fn=PUT rc=-22 where=BYTE sent=0 polls=93 bytes=72`

diag は成功時も必須（fail_seq/pos/rc/sent/polls/bytes=0、fn=NONE、where=NONE）。sid は失敗と同一。

attempted を送信前に立てる。diag 失敗でも再帰出力・再試行せず、その後に最新のFAIL/lost_linesでfinalを構築、txd=0/1（最初の送信失敗なし/あり）を付す。

fn/where は表記を全て大文字、正常はNONE。recover status は既存表示とは別の1行 `txd sid=... valid=... seq=... pos=... fn=... rc=... where=... sent=... polls=... bytes=...` をRAMから表示。

診断が出ない final、final 自体の欠落はノートで非成功。final 自身の送信失敗は RAM には残るが同runでの回収保証はない。

row_text/row_number の黙った切詰めを廃止: rec_row にoverflowを設け、既存384 B枠とLF/CR分の境界を検査。

overflow は送信せず ROW/-35、lost_lines++、REC_LOG。最大長計算・改行込みの境界試験を必須にする。

REC: `transport_state()` の `phase=state event=RESTORING` 行だけに必須u32欄 `cleanup_seconds=cb.seconds` を1つ追加し、当該行の最大長もPに含める。

## GF6200_VPLL_PROBE

現行verify=0のEND.readback=FFFFFFFFhは未採取であり実測ではない（kdebug/recover.c:2015–2032）。

## HWDEV_DESIGN

| 関数・データ単位の配置 | 判断 |
| --- | --- |
| recover.c:294–340 recover_gf_pre / recover_gf_result / gf_bus_record | 核に残す。REC 送信・課金・失敗伝播を共有し、前 2 関数を slot 公開 |

## REALHW_RECOVER_ADDENDUM

## T3 の実装で設計者が決めたこと (2026-10-21)

- abort は10 byte（A5 5A、sessionのLE32、先頭6 byteのIEEE CRC32のLE32）、1安全点64 byteまで；不正候補は1 byteずつずらして再同期し、session不一致・通常行・不正CRCは実行せず破棄累計へ入れる。

- 部分フレームは同一kdebug_call中だけ保持し、開始・PAUSED・終端で破棄；各境界では既存4096 byte RXリング分までを有界に排出し、上限到達はFAILED。hostはabortを一度だけ送り、EOTまで次コマンドを送らない（遅延tailを次セッションへ再利用しない）。

- RECはrecoverがsession内seqを1から採番し、状態行にversion/profile_crc/plan_hash/snapshot_crc、各accessのBEGIN/END、最終行にtrial/lost_lines/discarded_bytesを加える；DISCARDは終端直前に累計1行へ集約し、T2の固定Bは拡大せず最大桁数の静的計算へ追加算入する。

- REC送信中は受信による再入cleanupを遅延し1行終了後の安全点で処理、期限超過は送信を打切って呼出元がcleanupする；trialのBEGIN送信失敗では対象accessを行わず、restoreは残る独立S0予約を使う。局所資源の解放をUARTより先に行う既定順を維持し、その内部accessだけを記録する。

- RECの途中送信失敗後は次行前にchecked改行1byteで境界を復帰し、その送信も当該予算へ課金、静的Bには各行1byteを追加算入する；gf_preは展開node/substepごと、gf_busは必要readback後にENDを出し、verify無しのreadbackはNONE（未確認）とする。

## REALHW_RECOVER_DESIGN

- 各安全点で固定長の abort 要求だけを受信し session を照合、復帰へ移る。受信量にも上限。試験中は有効な当該 session の abort フレーム以外（通常行・不正/古いフレーム）を捨て、破棄したバイト数を session ごとに累計して `REC ... phase=receive event=DISCARD discarded_bytes=...` と最終行へ記録する。通常のコマンド解釈器を呼ばず、二重 restore は状態で抑止。

- ログ送信も有界にし、送信詰まりで cleanup を妨げない。失われた行数を保持。ホストは連番欠落や最終 verify 不着を成功と扱わない。

- 設定書込みの直前に `REC sid=... seq=... phase=restore event=BEGIN step=... addr=...`、後に `... event=END rc=... elapsed=...` を各 1 行。採取・停止・窓・画面・verify・reboot も同形式。4a の `GF PRE` は `REC ... phase=gf_pre event=BEGIN main=... pc=... opcode=... exec=...`、`GF BUS` は `REC ... phase=gf_bus event=BEGIN pc=... bar=... off=... width=... old=... new=...` に統一し、後者の END に readback/rc を残す。sid/seq は recover が一元採番し、独立した GF 行を出さない。命令前・書込前の行は期限付き送信完了を必須とし、失敗時は LOG_FAIL で試験を止め共通 cleanup へ進む。

- 最終行は `result=OK|PARTIAL|FAIL target=S0 verified=... unknown=... override=...`。試験結果と復帰結果を別フィールドで残す。host 側に版・plan/profile hash・snapshot CRC・最後の開始行を保存。
