# 82557 EEPROM ホスト試験

票: `docs/tasks/realhw/TASK_LAN_82557.md` §2 L-B。
契約: `/home/hight/os32-tmp/run/lan/LB_MAIN_DESIGN.r3.md` §2 LB-1、§5。
LB-1 で R5 の全語読み・和・診断整形・3方式の再試行を撤去する。

実物の `drivers/e82557_eeprom.c` と `drivers/pci_decode.c` を include する。
ホストは `-m32 -static`、`HOST32_RUNNERS=qemu` で実行し、u32=4 を静的 assert。
模型は 93C46 (6 bit) / 93C66 (8 bit)、dummy の SK↑ / SK↓ 出力を持つ。
`--target` は EEPROM 実装を i386-elf GNU11、`-Os -Werror` で通す。
probe、reset、SCB、試行の印、固定6から自動判定1回への分岐は
`tools/tests/e82557_drv_tdd.md` の別試験で扱う。

## 正常の対照 (27 ケース)

- window0–9: BAR 0x6001 / 1 / 0 / 0x20410008 / 0x6004 / 0x6005 /
  0xFFE1 / 0x10001 / 0xFFFFFFE1 と Command bit0=0。
  優先順位・理由・失敗時の出力引数不変を確認。
- frame: SDM の `110 000010`、8 bit の非対称アドレス、範囲外。
- auto6rise/fall・auto8rise/fall、nowin・width1・width7:
  自動判定の値・標本位置・有界終了、全て CS=0。
- read6rise/fall・read8rise/fall: 固定幅で語 0〜2 だけを読み、
  3 トランザクション、前後ガード、非対称データ、MAC の下位バイト先を照合。
  8 bit は自動判定後の読み取りに使う同じプリミティブの検査。
- erased・empty・stuck1fixed・stuck0fixed: 内容と dummy を区別する。
- interpret: zero / ff / multicast の拒否とローカル MAC の受理。

## 独立した模型の検査

- プロトコル: CS↑で初期化、SK↑で命令を取り込み、READ以外は fatal。
  無限ループ変異は操作数上限で fatal。CS=0 で SK を上げても fatal。
- T: 仮想時計と最小時間を照合。high≥1 / low≥3 / 周期≥4 / CS low≥1 µs、
  終了時 CS=0 を検査。
- S: 実装の frame/read 関数を使わず、契約の W/D/R 列を組み立てて全イベントを照合。
  固定読みは先頭 W0/D・CS/D・命令 3×(3+n)・データ 2×16・末尾 W0/D。
  自動判定はデータなし: 成功は 3+6 / 3+8、nowin は 3+8、幅異常は
  3+1 / 3+7 クロック。最後のアドレスの立下り後 R から W0/D へ進む。
  T だけ / S だけの選択でも実物を実行し、検査を独立に行う。

## 変異 (21 本)

実装の写しだけを 1 か所ずつ変える。正常の対照 27 件を必ず先に通す。
コンパイル失敗は RED に数えず、各変異の検出ケースと検査を出力する。

| 変異 | 検出する契約 / 検査 |
|---|---|
| opcode READ→WRITE | 模型 fatal、frame |
| MSB→LSB | 模型 fatal、非対称語 |
| dummy を SK↑後に読む | SK↓出力模型の幅検出、S |
| 幅の 1 bit ずれ | 幅・終了値 |
| MAC バイト順 | 非対称 MAC |
| 末尾 W0 を削除 | T の終了 CS=0、S |
| 自動判定の上限を削除 | 常時1で模型の操作数上限 |
| step 4→1 / 4→0 (2 本) | T |
| 遅延削除 8 本: 先頭、CS選択、命令 low/high/fall、データ high/low、末尾 | 全件 S で RED。T を通る削除も S で検出 |
| BAR 上限を排他的にする | 0xFFE1 |
| BAR 比較を加算に戻す | 0xFFFFFFE1 の 32 bit 桁あふれ |
| 整列 / 種別検査を削除 (2 本) | 0x6005 / 0x6004 |

検査ログは `/home/hight/os32-tmp/run/lan/`。NP21/W・実機・配備は依頼範囲外。
このホスト模型では実機の I/O 復号・MAC・遅延精度を確認したとは扱わない。
