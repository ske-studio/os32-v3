# 82557 R5 EEPROM ホスト試験

票: `docs/tasks/realhw/TASK_LAN_82557.md` §2 L-B・§5-2 R5。
契約: `/home/hight/os32-tmp/run/lan/LB_R5_DESIGN.r3.md` §3–5 と
`PACK_lb_r5.md` の PM 決定 (2026-10-15)。実機 R5 の合否をここでは主張しない。

実物の `drivers/e82557_eeprom.c`・`drivers/e82557.c` と PCI 復号・結線を
ハーネスに include する。ホストも `-m32 -static` で組み、明示した
`HOST32_RUNNERS` (この依頼では qemu) で実行し、u32=4を静的assertする。模型は試験側だけに置き、93C46 (6 bit / 64 語)、
93C66 (8 bit / 256 語)、dummy の SK↑ / SK↓ 出力を独立に持つ。
`--target` は実物の EEPROM と probe を i386-elf GNU11、`-Os -Werror` で通す。
本体も起動時だけの処理として `-Os` にする (既存オブジェクトの旗は変えない)。

## 正常の対照 (43 ケース)

- window0–9: BAR 0x6001 / 1 / 0 / 0x20410008 / 0x6004 / 0x6005 /
  0xFFE1 / 0x10001 / 0xFFFFFFE1 と Command bit0=0。順序・理由・出力引数の
  不変を確認。skip は実物の probe を通し、ピン read/write/delay 全て 0 回。
- frame: SDM の `110 000010`、8 bit の非対称アドレス、範囲外。
- auto6rise/fall・auto8rise/fall、nowin・width1・width7:
  自動判定の値・標本位置・終了、全て CS=0。
- read6rise/fall・read8rise/fall: 全語を模型の非対称データと照合し、
  語 0–2 の下位バイト先、全語の和 (最後の語を含む)、64 語だけの格納と前後ガード。
- erased・empty・stuck1fixed・stuck0fixed: 内容と dummy を分け、固定 6 bit の救済。
- interpret・badchecksum: zero/ff/mcast/ローカルMAC、和 0xBABA と 1 語だけ異なる像。
- story0–5: §5-1 の 6 行を実物 observe/next/verdict で実行。
  story6–7: try1 A は addr=6/form=ff、try2 S(A) で幅を再検出し、
  常時1 → FAIL(nowin)、幅7 → FAIL(width)。最大3回、最初のPASSで停止。
- format: 観測/最終/ダンプ/マーカーの桁、5種のFAIL理由、和badでもPASS、
  幅異常時の `-`、4行×16語、dump=64/256、cap=0/1を含む境界。
- probe・gated: ポートは基底+0x0Eのみ、beginは最初のread/write前、
  endはCS解除後、bootlogとpolled serialの両経路、gate有効時はserial抑止。
- proberelearn: PM追加の筋をprobeで通し、FAIL(nowin)の際に古いダンプを出さない。
  probeff・probezero: 常時1/0の3回実行と最終FAIL・ダンプ。
- binding: 実物pci_bind_allの列挙0件で出力/I/Oなし。Trident/CA0106は
  表を0→1件に変えても結果の全フィールドが同じ、82557だけ一致してDECLINE。

## 独立した模型の検査

- プロトコル: CS↑で初期化、SK↑で命令を取り込み、READ以外はfatal。
  無限ループ変異は試験側の操作数上限でfatal。CS=0でSKを上げてもfatal。
- T: 仮想時計と `e82557_regs.h` の最小時間を照合。
  high≥1 / low≥3 / 周期≥4 / CS low≥1 µs、終了時CS=0を検査。
- S: 実装のframe/read関数を使わず、契約のW/D/R列を組み立てて全イベントを照合。
  通常は先頭W0/D・CS/D・命令3×(3+n)・データ2×16・末尾W0/D。
  自動判定はデータなしで終了する別の列:
  早期成功は3+6または3+8クロック、nowinは3+8、幅異常は3+1または3+7、
  それぞれ最後のアドレスの立ち下がり後RからW0/Dへ進む。
  Tだけ/Sだけを選んで独立に検査できる。

## 変異 (24本)

実装の写しだけを1か所ずつ変える。必ず正常の対照43件を先に通す。
コンパイル失敗はREDに数えない。各変異で検出ケースと検査を標準出力へ残す。

| 変異 | 検出する契約 / 検査 |
|---|---|
| opcode READ→WRITE | 模型fatal、frame |
| MSB→LSB | 模型fatal、非対称語 |
| dummyをSK↑後に読む | SK↓出力模型の幅検出、S |
| 幅の1 bitずれ | 幅・終了値 |
| 0xBABA→0xBABB | 和 |
| MACバイト順 | 非対称MAC |
| 末尾W0を削除 | Tの終了CS=0、S |
| 自動判定の上限を削除 | 常時1で模型の操作数上限 |
| step 4→1 / 4→0 (2本) | T |
| 遅延削除8本: 先頭、CS選択、命令low/high/fall、データhigh/low、末尾 | 全件SでRED。Tを通る削除もSで検出 |
| BAR上限を排他的にする | 0xFFE1 |
| BAR比較を加算に戻す | 0xFFFFFFE1の桁あふれ |
| 整列 / 種別検査を削除 (2本) | 0x6005 / 0x6004 |
| PASSで停止しない / 固定6を飛ばす (2本) | story全行 |

開発中の対照は43/43 GREEN。最初のtarget試行はkernelヘッダ探索パス不足で失敗し、
probeのincludeを `kernel/cpu_calibrate.h` に合わせて修正した。
変異の初回試行は上限削除のアンカーが2か所一致して停止したため、命令ループを
一意に指すアンカーへ修正した (コンパイル/実行前の不備、REDには数えない)。
初回の最終変異試行ではBAR加算変異がLP64ホストで生き残った。u32が実機と
同じ幅になるILP32ビルド＋明示qemu runnerへ修正し、桁あふれを実行で検証する。
32bit glibcがないため、試験内の文字列・整形・Linux syscallだけの小さな補助を使う。
最終の検査ログと前後の像サイズは `/home/hight/os32-tmp/run/lb-r5/`。
NP21/W・実機・配備は依頼パックの指定どおり実行しない。実機のI/O復号、dummy、
MAC/OUI、チェックサム規則、遅延精度、マーカー受信速度はR5の実機受入で確認する。
