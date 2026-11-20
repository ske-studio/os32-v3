# PPC_P5_REST_DESIGN — PowerPC P5 の残り: 台本 5 本の link 化と、既定を link に切り替える段

> 2026-11-20 の見直しで x86 の形に改めた ([DESIGN_X86_AUDIT](DESIGN_X86_AUDIT.md))。

> 状態: **現行 (2026-11-16)** — 確定設計の写し。sol 起草 r1・r2、Opus 5.5 の独立レビュー 2 回、PowerPC PM が第 2 回の N1〜N4 を反映。ユーザー決定 2 件は §6。出所は ~/os32-tmp/run/ppc-p5-rest/DESIGN.final.md。段の状態は票 TASK_PPC_LAN が持つ。上位の確定設計は [LAN_OPERATOR_DESIGN](../network/LAN_OPERATOR_DESIGN.md) §5 (P5 の行)。


## 1. 共通 transport

P5-t・P5-sd/fs では共通 link transport から host agent に接続し、LOG・RSH・SFS CALL を使う。

## 2. 台本の入口

K は起動時の LOG と診断を採取し、R は実機の HELLO/ALIVE と連続した LOG を記録する。C は製品シェルの子の終了値と再起動、O は fd1 と LOG の分離、U は SFS の期限切れ後も USB 打鍵が届くことを確認する。

| 台本 | 観測する結果 |
|---|---|
| K (kboot_run) | 起動前に LOG を購読し、OF・ELF entry と段階印を照合する。5 診断の応答を ELF/map と比較する。 |
| R (realhw_run) | 配備した ELF の SHA、OF/PVR、pcap と LOG を保存する。端末の完了結果は同じ世代・要求 ID で照合する。 |
| C (sc_run) | nest の子の終了値 7 を 2 回、親に戻った直後の echo $? で 5 を確認する。シェルの終了後は次の READY を待つ。 |
| O (ppc_sh_output) | outprobe の FD1 PROBE は OUT に 1 回、LOG に 0 回。kernel READY は LOG で採取する。 |
| U (usb_run) | mouse 列挙後に keyboard を追加する。SFS CALL 応答だけを止め、ゲストの期限後にも FB で打鍵を確認し、応答を戻して /host を読む。 |

## 3. 製品経路の選択

PPC_SHELL_ALIGN_X86_DESIGN の起動入口から R・C・O・U と製品一巡 D を開始する。K の 5 診断は PDIAG を備えた診断像で別に実行する。旧像を選ぶ KBOOT_SH・KBOOT_LINK の指定を呼び出し元へ追加する準備段は取り消す。常駐 CLI と旧 kboot の 5 像は、x86 に寄せる作業の最後に外す。

## 4. 統合の受入

D には K の診断回帰と R（host fixture）・C・O・U の個別受入後に進む。製品像の同じ起動で LOG、製品シェルの RSH、/host の読み書きを観測する。再起動後は新しい boot の READY を待ち、保存した内容を読む。両 CPU（7450・7455）でこの一巡、ゲストの PLN1 送信 0 件、像・map・heap の照合が揃ってから製品経路を既定とし、G4 の受入へ進む。

## 5. 受入項目 → 試験と変異
|受入・期待の出所|試験|変異 (正常判定が対象理由で落ち、到達・後続生存も確認)|
|---|---|---|
|OF/READY継続、既存kboot観測ABI|KのQMP TB/EE/tickとserial milestone|記録fixtureのREADY欠落/順序違反・tick停止を拒否|
|初期LOG・drop、§2 K/R|KとRのLOG生記録/欠落判定|drop報告消去、末尾欠落fixtureでRED|
|5診断、§2 K/R / 旧lan_judgeの値根拠|KのOF/map/entryとの値照合|PEEK応答1byte破損、clock不一致fixtureでRED|
|USER入れ子・旧CLI契約、sc EXPECTED|製品入口の C で二重子結果/親復帰|既存parent-result、第二結果のみ破壊|
|USER fd1、outprobe/既存sh-output|OのOUT/LOGの正負両判定|fd1-logを本物のlink像で実行|
|CLI redirect/pipe/再起動、check_shell/P3|旧 link_sh_run の判定を製品入口の C に移す|既存fd1-log/no-restart、P3 END/重複ID変異|
|HostFS永続/RO/期限/世代、§4 保存|旧 sd_run --link の判定と変異を製品入口の D に移す|既存tmp-target/fixed-nonce等、永続値・STALEの実観測|
|RPC停止後USB、usb_run --lan/PPC_NOTES §3|UのCALL到達/ゲスト期限/打鍵/復帰|no-service/syscall-ee、打鍵invert-key/no-forwardも維持|
|打鍵がゲスト時計基準、PPC_NOTES §3|Uのpress/repeat/releaseとFB|release待ち省略の試験刺激で余分な文字を検出|
|実機記録の完結性、realhw run_rh|R host fixture + G4 pcap/LOG/RSH|世代途中変更/未完了OUT/欠落記録を拒否|
|linkのみ全一巡、§4 D|Dの同一runでLOG→RSH→SFS|各イベント欠落fixtureで統合判定RED|
|0x88B6 TX0、§4 D|Dの起動から停止までguest送信集計|guest PLN1 frameを1本挿入、空captureも拒否|
|像/map/サイズ、§4 D|Dの成果物・heapと旧新差分|mapへ旧TX/RX領域混入fixture、成果物欠落を拒否|
|外部アプリ・FS|製品シェルから hello/nest/outprobe と保存操作を実行|既存 parent-result/fd1-log/tmp-target を利用|
|欠如環境、PPC_NOTES §6|全対象のcross/socket/QEMU欠如fixtureとrequire=1|常時成功/常時SKIPへ判定変更してRED|
- 表のfixture変異は判定入力を一箇所だけ壊す。製品の変異は既存一意anchorを使い、timeout/一般例外だけをREDにしない。
- G4はPPC-LINK-G4で追跡し、速度は記録のみ (ユーザー判断で変更)、POWは実測合否で受ける。未実施は未確認と明記し、host fixtureを実機受入の代わりにしない。

## 6. 未決 → ユーザー決定 (2026-11-16)
- G4 の受入の時期と物理操作: 「QEMU 合格後すぐ (推奨)」。既定の切替の段 D が QEMU の統合の受入に通ったら、続けて G4 の受入を行う。実施の日時はその時点で改めてユーザーに聞く。それまでは DEFERRED_TESTS の行 PPC-LINK-G4 で未実施を追跡する。物理の操作 (G4 の電源・TFTP の起動・キーボード) はユーザー。
- cp の速さの退行を合否にするか: 「記録だけ (推奨)」。旧 PLN1 と新 link を同じ条件で 2048B・4096B・64KiB を測って残し、合否には使わない。

## 確定 (2026-11-16、Opus 第 2 回、PM が反映)
- N1 (P1): K の正像は link_run.accept を丸ごと呼ぶ (案 a)。OF・serial の照合は accept の後に記録で行い、READY→2 秒の TB と EE は link_run の tick の採取で代える。link_run.py に QMP を渡す口を足す準備段 (案 b) は作らない。




## R の実装で決めたこと (2026-11-16、PM 決定、実装者の停止 2 回を受けて)
- R1: link の capture の合格は HELLO/ALIVE の成立と LOG の完全さ (seq が 0 から連番、drop の報告、KBOOT の段階印の順)。PDIAG の診断は求めない (初期の link-SH の像に PDIAG が無い、platform/powermac/lan_service.c。5 つの診断は K が診断像で確かめる)。
- R2: nest の結果と終了値 5 は RSH の本文で照合する。製品シェルの終了後は起動ループによる再起動を確認する。
- R3: agent の API は足さない。client からは probe (世代・peer・hello)・LOG・RSH を取り、HELLO・ALIVE (ゲストの電文が 2 秒より長く途切れない)・session (不変)・SFS の CALL と応答は受け身の pcap の 0x88B5 の電文から読む。

## U の実装で決めたこと (2026-11-16、PM 決定)
- U-c1: link の `--link --lan --mutate` の変異は no-service だけ。RED は「USB の列挙か打鍵が進まない」と「link の LOG・RSH が生きている」の両方を要し、片方は wrong-reason。syscall-ee は従来の `--cpu both --mutate` (PROBE の私用の写しで USB STALL ticks=0 を観測) に残す。初期の link-SH の像には syscall の入口の EE を観測する口が無く、差し込みは N2 に反するため。
- U-b の直し: 抑止した SFS CALL では、host_agent の壁時計 1.0s (受付と本文の後の両方、tools/host_agent.py) を止める。-icount のゲスト時計は壁時計と食い違う (先走りと凍結) ので、区間はゲストの期限で終わらせる。期限切れの文は `Host is down` (fs/serialfs/sfs_client.c (sfs_exchange_deadline()))。
- 受入の揺れのうち、受信が 1 件目で断られる誤りは共通の net/rx_work.c で本線が直した。

