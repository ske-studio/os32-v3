# TASK_LAN_OPERATOR — 操作口を LAN が既定・serial が退避先に、PowerPC の PLN1 を link に統合

> 状態: **設計中 (2026-11-14)** — 設計は確定して実装を待つ。詳細設計は [LAN_OPERATOR_DESIGN](../../decisions/network/LAN_OPERATOR_DESIGN.md)。
> 上位: [HOST_SERVICES_PLAN](HOST_SERVICES_PLAN.md)、[TASK_NET_HOSTSVC](TASK_NET_HOSTSVC.md) (§0 の「link と SFS の 2 本を共存し、最後は 1 本を目指す」)。

## 0. ユーザー決定 (2026-11-14)
| 項目 | 決定 (原文の要旨) |
|---|---|
| 既定 | 「LAN の口を既定にしてシリアルをフォールダウン先にしてほしい」。gshell も「同じように」 |
| 計画との関係 | 「ラリーは追加しても良いですがホストサービス計画に照らし合わせてください」 |
| exec の例外 | 「操作口として例外」: OS32 がホストに頼むサービスではなく、ホストから操作する口。サービスの一覧と `os32.` の予約名を使わない |
| PLN1 | 「PLN1 はホストサービスの計画書を読まずに実装した節があるので統合して link として実装してください。流用できる部分は流用しなるべくバイナリを壊さないに越したことは無いですが、変更はやむを得ないです」 |
| G4 実機 | 「古い版で実機の確認が取れればあとは qemu の確認だけで大丈夫」: G4 は今の PLN1 の版で先に確認し、移行の段の受入は QEMU (7450・7455) だけ |
| host_agent | 「host agent をサーバーサービス化」: NIC ごとに常駐 (systemd など)。CLI は loopback の操作口 (TCP 8027) でつなぐ |

## 1. 詳細設計 (確定) の要約
- x86・PowerPC とも、Host Services の link (EtherType 0x88B5) に載せる。ホスト → ゲストは NOTIFY、ゲスト → ホストは host_open/host_write + WDATA。新しい EtherType は足さない。KAPI は con_sink_capture の 1 本を末尾に足す。
- LAN の実行中は rshell_active を下ろす。serial の写しと busy 待ちを止め、キー待ちの期限は保つ。退避は、送る前 (agent がいない・READY が無い) だけ。送った後は UNKNOWN として止める。
- gshell の GSERIAL は、受理を返してから引き渡す。同じ行を NOTIFY に載せ、応答は host_open/host_write で返す。
- PowerPC: BEACON は HELLO に、診断・端末・HostFS・LOG は link の上に移す (共通の `LOG` サービス)。link_tick と GEM の RX・TX は tick の枠で回す。
- 段の表 (terra に出す 1〜3 ファイル・400 行以下) と受入の表は、設計の §6・§7。
