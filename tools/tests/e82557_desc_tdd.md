# 82557 LB-3 記述子・停止証拠のホスト試験

契約: [TASK_LAN_82557](../../docs/tasks/realhw/TASK_LAN_82557.md) §2 LB-3。
実物 `drivers/net/e82557/e82557_desc.c` を include。I/Oなし、freestanding ILP32/qemu-i386。
48ケースの正典は `test_e82557_desc.py` CASES。

| 番号 | 独立oracle |
|---|---|
| 0–2 | SCB command CU/RU組合せ、全65536 status、非整列LE16/32とガードbyte |
| 3–13 | EL/S/Iの全組合せ、link、IA+8/MAC、Configure22B全部、loopback差分、隣接ガード |
| 14–18 | TX14/60/1518、13/1519拒否して不変、TBD/EOF/count/閾値/+16本文 |
| 19–26 | この停止手順のreset後Idleだけ許可、古いreset/Start後Idle/active/FFFF拒否、volatile C/OK |
| 27–32,42–43 | MDI opcode/phy/reg/Ready/data、ID0/FFFF、PHY decode、AN無効、リンク期限境界 |
| 50 | RFD8本、巡回link、tailだけEL、size1536、rbd FFFFFFFF、status/actual0 |
| 51–58 | 1〜8本scanと全消費、添字巡回、空/満杯の区別、末尾ELの移動 |
| 59–62 | 満杯reownは無操作、bit9→RNR、部分reownはCPU所有枠保持、再利用でEOF/F消去 |

STATIC_ASSERTがCB/TCB/RFDの配置・16B整列・16KiB spanへの収納を固定する。
環のEL順序と障壁は実ドライバ模型でも検査 ([e82557_drv_tdd](e82557_drv_tdd.md))。
21変異の正典は `test_e82557_desc.py` MUTATIONS。正常対照後に各版を組んで実行し、
REDだけを数える。コンパイル失敗は検査失敗。等価変異は別欄で達成数に含めない。
実行ログ: `/home/hight/os32-tmp/run/lan/lb3-evidence/`。
