# 82557 LB-2 記述子・停止証拠のホスト試験

票: `docs/tasks/realhw/TASK_LAN_82557.md` §2 L-B、設計 r3 §3 / §5、PM 補遺。
実物 `drivers/e82557_desc.c` を include。I/O なし、ILP32 + qemu-i386。
`--target` は実ドライバと純粋関数を i386-elf GNU11 -Os -Werror で検査する。

## 対照 27 ケースと独立 oracle

0: SCB command の CU/RU 組合せ、1: 全 65536 status の CUS/RUS/STAT。
2: 非整列 LE 16/32 bit の書き・読みとガードバイト。
3–10: EL/S/I の全 8 組合せ、cmd/link、ヘッダ直後を変更しないこと。
11: IA +8 の MAC 順序と +14 のガード。
12–13: Configure 全 22 B、normal / loopback は byte 10 だけ差分、+8 と +30 のガード。
14–18: TX 14/60/1518 B、13/1519 B は拒否して全領域不変。
TBD=FFFFFFFF、EOF/count の LE、threshold、TBD count=0、+16 の本文を照合。
19–25: この手順の reset 後 Idle だけ許可。reset 無し、CU Start 直後の Idle、
以前の reset、CUS active、RUS active、FFFF を拒否。
26: volatile status の C/OK のビットと装置による値の更新。
配置はソースの STATIC_ASSERT で CB=0/32、TCB=64/1552、RFD=1616/1552×8、
末尾14032、16 B 整列、16384 B span 内を固定する。RFD 操作は LB-3。

## 変異 14 本

EL/S/I (3): ケース3–10、TBD=0 / count endian (2): 14–16、
Configure +16 / MII=0 / loopback 位 / padding (4): 12–13、
IA/Configure opcode (2): 11–13、old reset / CU Start 後 Idle / RU active (3): 19–25。
各変異は独立した実装の写しをコンパイルし、正常対照の後に全ケースで RED を要求。
コンパイル失敗は RED に数えない。実行ログは依頼パック指定の run/lan 配下。

## LB-2b (r3b_PHY.r2 + r2a)

追加22ケース (27–48)、合計49。MDI read/write の数値 oracle と phy/reg の5 bit切断、
Ready bit28、data16 bit、IDの0/FFFF拒否、PHY decode、BMCR各禁止bit/組合せ、
速度/AN/duplex保持、reset優先、bit9消去、readback、強制モードのlink、期限境界、
統計の+64完了語・LE計数・差分期待値・68 Bの配置を検査する。
追加変異10本 (合計24): opcode/phy欄/phy mask/reg mask/Ready/data、強制モード無視、
AN落とし、reset/restart持越し、完了語+60。捕捉先は順に27/28/28/28/29/30/42/33–36/36・40/44。
実行ログ: `/home/hight/os32-tmp/run/lb2b/`。正常対照を先に実行し、変異は実行時REDを要求する。

最初のLB-2b変異実行でphy/reg mask変異が生存。63/95の上位bitが他欄の1と重なる
入力だけだったため、read/phy=63/reg=0とread/phy=0/reg=63を28の独立oracleに追加した。
