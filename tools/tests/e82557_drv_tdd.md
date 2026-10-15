# 82557 LB-3 実ドライバのホスト試験

契約: [TASK_LAN_82557](../../docs/tasks/realhw/TASK_LAN_82557.md) §2 LB-3、
`PACK_lb3.md` > `LB_MAIN_DESIGN.r3d_LB3.r2.md` > r3/r3a (run/lan)。
`e82557_drv_host.c` が実物を include し、freestanding ILP32/qemu-i386 で走る。
`--target` は実物を i386-elf GNU11 -Os -Werror で組む。timer の結線順も検査。
EEPROM のピン T/S は独立の `check-e82557-eeprom-host` に任せる。

## 正常対照と注入

149 ケース。名前と全リストの正典は `test_e82557_drv.py` の CASES。

| 群 | 観測する契約 |
|---|---|
| LB-1由来46 | kill switch、窓、reset/20µs/M、Base発行前後の受理、EEPROM固定6→自動8の1回保険、lan.try/lan.seqの永続化と読み戻し、同CRC skip、単調増加、桁溢れ、I/O失敗は不在と区別 |
| probe / CU | 起動表経由BOUND、DMA失敗、IRQ登録失敗、BMの所有権、5 CBそれぞれのC待ち/OK/発行前後timeout、初回RU/通常RU失敗、停止再試行/QUARANTINE |
| PHY / PCR | ID1/ID2、修正しない非NSC、旧値保持の0562、同値省略、予約bitを検証しない、0062の照合 (ngは観測のみ)、PHYVENDOR=0、PCR初回read/write/readbackとPARのtimeout、BMSR二回、リンク0/500/3000ms、EEPROM幅を語6へ引継ぎ |
| loopback / SWI | cfg(LB)→ia→RU→TX→60B比較→rst2/M→cfg(NORMAL)→ia2→RU→SWI→BOUNDの列、RFD未完了、本文相違、TCB異常値、SWI不達でもBOUND、IRQ0先行でもtickはackしない |
| RX | 空、1〜8本×3周、満杯から1本だけ返す、NOSPCでは保持、不良OK/13/1519/資源切れbit9のdrop、Readyでは再開せず、RUSだけでも再開、scan後RUS読み前の最終DMA、コピー中の復旧でもCPU所有枠を保持 |
| IRQ | CX/CNA/FR/RNR/MDI/SWI、同時bit、2回目のack、3回目は読まない、0xFF00はdeadだけ、STARTING/STOPPINGはI/Oなし、RECOVER/FAILEDはackだけ |
| TX / 復旧 | C∧OKはtx_dma_done、C∧¬OKはtx_fail/IDLEで復旧なし、2回目予約のtimestamp消去、予約中に別sendとtick/RUtimeout、世代差で発行しない、50tick境界、send発行前後timeoutと次send抑止 |
| R1〜R3 | 8本満杯→TXtimeout→R1/R2/R3→RX_WAIT→recv1本→次tickでRUNNING、reset直前のDMAを最終scanで確定、停止証拠不成立は次tick再試行、R3失敗のreset上限3、復旧10回超FAILED/leaked |

待ちの上限は通常70,000µs未満。phy_late 600,000、phy_down 3,100,000、
cb_edge 110,000、lb_timeout 80,000だけ個別に拡張。tickは300µs・SCB発行1回まで。
TCBのCはDMA完了 (Intel SDM :4648-4663)。!OK注入は防御の試験で、実機underrun検出ではない。

## 模型と障壁

CU/RU Baseを非0で起動し、受理前pointer変更・CU/RU同時命令・16bit commandを拒否。
RUはIdle/Ready/NoResources、EL/長枠で停止。RU Start時に巡回link、size1536、
rbd FFFFFFFF、I1/I2/I3、空き件数とrx_devを検査。Readyと満杯でのStartは失敗。
PORT resetはM=0へ戻す。M/pendingから導くだけの恒真なα/β検査は置かず、登録済み非RUNNINGで
未マスクpendingを放置する操作を拒否する。tick/IRQ3の呼出し順を試験が決める。
R3直前にpendingを注入し、M=0への書込み前にack済みであることを検査する。
設計§6の表の `unclaimed IRQ3` は、tickが先にackしてPICに残ったIRQ3が後から届く場合にも1回出るため、単発ではack漏れやIRQ経路不良の証拠にならない。
停止reset履歴・20µs・Idle・M・unregister・BM復元を満たさないfreeを拒否する。

wr16/rd16と本文アクセス、障壁を事象として記録する。C→障壁→本文、
zero(status/EOF/F)→障壁→EL、満杯の旧EL→新EL、非満杯の新EL→旧EL、
最終store→障壁→SCBを検査。hook自身は本番の障壁を保証しないため、
Python側が本番のasm memory clobber定義を別に検査する。

## 変異と証拠

`test_e82557_drv.py` の MUTATIONS が実行する各変更・ラベルの正典。
正常対照を先に通し、変異版をコンパイルしてケースを実行し、失敗したケース名を記録。
コンパイル失敗はREDに数えず検査自体を失敗にする。等価変異は達成数に入れない。
LB-1/LB-2由来の12変異を復元し、base timeoutの最新SCB値、rstのM=1も観測する。lbのCB失敗はngだけtxngへ変換し、timeout/pre/postを保持する。
除外: `BMSR first read discarded` は初回値を出力する経路がLB-3で撤去され、初回値の格納先変更が等価になったため (二回read自体は `BMSR read once` で検査)。
traceの768B容量と25段全ID/u32最悪長の変異は `check-kdebug-init-host`。
同条件像比較・コマンド・rc・ケース別REDは `/home/hight/os32-tmp/run/lan/lb3-evidence/`。

未実施の実機/NP21/W/nativeは
[DEFERRED_TESTS](../../docs/tasks/DEFERRED_TESTS.md) の LAN-LB3。
