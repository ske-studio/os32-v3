# 82557 LB-2 実ドライバのホスト試験

契約: `docs/tasks/realhw/TASK_LAN_82557.md` §2 L-B、設計 r3 §2 / §4-2 /
§5-7 / §6-3 と PM 補遺 3 (永続の lan.seq)。
`e82557_drv_host.c` は実物の `drivers/e82557.c` を E82557_HOST_TEST で
include し、VFS・起動設定・Image CRC・SCB I/O・irq_save・trace を差し替える。
EEPROM は窓と MAC の判定を実物にし、語読みだけを注入する。
ピンの T/S は `e82557_eeprom_tdd.md` の独立した試験に任せる。
ホストは freestanding ILP32、qemu-i386。`--target` は実ドライバを
本番の i386-elf GNU11 -Os -Werror でコンパイルする。

## LB-1 由来の 46 ケース (以下は追加前の契約)

- pass: software / selective PORT の u32 書き順と各 20 µs、M のバイト、
  pending 0xA000 の W1C、CUS/RUS Idle、CU/RU Base 0 の発行と受理、
  EEPROM 3 語、PASS 行と MAC、DECLINE、印の消去。
- reset_fail: Idle が取れず RESET_FAILED の QUARANTINE、Base/EEPROM 無し。
- pre_wait / post_wait / ru_post_wait: 別々の command 停滞注入、100 µs で戻り、
  前者は pointer/command 書き無し、CU / RU それぞれの発行後停滞を確認。base=timeout。
- no_trace: KDEBUG=0 相当の NULL slot でも PASS、trace 呼び出し無し。
- kill / mkdir / crc_missing: cfg が最初、mkdir・CRC の不成立は I/O 無し。
- bad_bar / io_disabled: 窓の拒否は装置 I/O 無し。
- same / other: 同 CRC は上書きも I/O もせず停止、別 CRC は次の seq。
- seq_missing_old / seq_ahead / monotonic: 印と seq の大きい方を +1。
  印を消した次回でも永続の seq=41 から 42 へ進む。
- try_read / seq_read: NOTFOUND だけが欠落、IO 失敗では書き込みも I/O も無し。
- try_write / seq_write / try_short / seq_short / sync / try_back / seq_back:
  書き失敗・短い書き・sync 失敗・読み戻し不一致は装置に触れない。
- write_zero: ext2 の rc=0 と通常ケースの FAT の rc=len の両方を受理。
- remove: 印の消去失敗でも戻り、印は残る。
- bad_try0–8: 空・CRC のみ・LF 無し・末尾データ・負数・u32 超過・
  非 hex・seq=0・二重 LF は拒否。
- bad_seq0–6: 空・LF 無し・負数・u32 超過・末尾データ・二重 LF・
  UINT32_MAX (増分不能) は拒否。
- ee_fallback / ee_badmac / ee_detect_fail: 固定 6 失敗後に自動判定は
  1 回だけ、幅 8 の 3 語で成功 / 不正 MAC / 判定不能で終了。

## 模型の独立した検査

全 I/O の前に lan.try と lan.seq の書き→sync→読み戻しが必要。
パスは `/var/log/lan.try` と `/var/log/lan.seq` の完全一致だけを許す。
Base は初期 0x1000、Load 発行から 3 回目の command 読みで初めて 0 になる。
command は +2 u8、mask は +3 u8、pending ACK は +1 u8。
command が残っている間の pointer 書き・M を変える command 書きは検出する。
VFS と trace は IF を戻した通常文脈、SCB 発行は irq_save 内。
待ちには操作数の guard と runner の timeout もあり、無限ループは失敗になる。

## 変異 21 本

正常の対照を先に通し、実装の写しの各変異をコンパイルして実行する。
コンパイル失敗は RED に数えない。

| 変異 | 検出ケース / 契約 |
|---|---|
| software / selective reset 削除 (2) | pass、PORT 履歴 |
| software / selective 後の遅延を 1 µs (2) | pass、reset 時計 |
| M を 0 | pass、byte mask |
| ACK を 0xFF 固定 | pass、pending と一致 |
| Idle 検査無し | reset_fail |
| CU / RU Base Load 削除 (2) | pass、非 0 Base と語読み前の受理 |
| command を u16 書き | pass、幅と port の検査 |
| 前 / 後の受理待ち削除 (2) | pre_wait / post_wait と pass |
| READ_FAIL を NONE に | try_read、書き込み禁止 |
| 同 CRC を再試行 | same |
| 旧印の seq 無視 | seq_missing_old |
| seq を毎回 1 に | monotonic / other / seq_ahead |
| seq 桁あふれ許可 | bad_seq6 |
| 正の short write 許可 | try_short / seq_short |
| sync 削除 | pass、永続化前の I/O 禁止 |
| 読み戻し内容を無視 | try_back / seq_back |
| kill switch 無視 | kill |

2026-10-15: 対照 46/46、target コンパイル rc=0。
変異 21/21 RED は追加 EEPROM 3 ケースの直前の実行で確認、最終集約でも回す。
最初の変異実行では READ_FAIL 変異が生存したため、初回読み失敗時の
書き込み禁止を oracle に追加した (再読にも失敗を注入していたため)。
実機の I/O 復号・リセット精度・受理時間、エミュレータは未確認。


## LB-2 の追加 (対照 68 ケース、変異 30 本)

既存46ケース + table / dma_fail / bm_fail / bm_present / stop_retry / stop_fail /
stop_bm_present / cb_edge / cb_late / bm_present_ng と、cfg/ia/tx 各段への
C timeout・OK=0・発行前停滞・発行後停滞の12ケース。
3 ms 後に C を書く模型で逐次実行を検査し、20 ms の境界で C を読む場合も成功、
その次の時刻は timeout。CU Start 直後の CUS Idle は完了や解放の証拠にならない。
模型が CB 全バイト、Configure +8、TCB +16、HELLO broadcast/type/mark/MAC を独立照合。
Base は software reset で非0、selective reset では保つ。dma_free は開始より後の
selective reset・20 us・Idle を必須とし、BM を復元する前の free も拒否。
2回停止失敗時は M→ACK→BM off→leaked、free なし。元から BM がある正常/CB失敗では
書き戻さず、QUARANTINE では元から BM があっても安全停止のため落とす。

障壁: descriptor store→barrier→CU Start、C status read→barrier→次の本文読み/再利用。
ACCESS hook は書き/読みを、BARRIER hook は順序を記録 (hook 内に asm なし)。
本番マクロの memory clobber は Python が文字列でも確認する。
追加変異8本: same の印削除 (same)、Base失敗の古いstatus (pre/post_wait)、
公開障壁削除 (pass)、取得障壁削除 (pass)、停止reset省略 (pass)、
BM無条件復元 (bm_present/ng)、BM復元より先にfree (pass)、未停止free (stop_fail)。
L2-1: PORT の両 reset で模型の M を 0 に戻す。probe 終了時の M=1 と、
free 前の M=1・STAT 消去を確認し、停止後の mask 省略変異を追加。
修正前の pass は両条件で RED (rc=1)。正常停止でも M→ACK→BM復元→free とする。
F4: kernel/kernel.c の pci_drivers 表をそのまま抽出してコンパイルし、実物の
pci_bind_one から probe へ進め、DECLINED の結果も確認する。起動側の表呼び出しも照合。
F2: same は lan.try の内容/present、両ファイルwrites=0、rm=0 を確認。
F1: Base受理失敗時に模型は status を2040へ変え、その新値がログに出ることを確認。

段の印は LB2 dma/cfg/ia/tx/stop を含む。部品のバッファは全11段をu32最大桁で
収めるため256→384 B (本体BSSには加算されない)。nic_trace_host はその全段も検査。
実機の合否・配備・エミュレータ起動は本依頼の対象外であり、このホスト試験からは未確認。

## LB-2b (r3b_PHY.r2 + r2a)

追加30ケース、合計98: TX2のC timeout/OK=0/pre/post (4)、PHY15、統計11。
模型はMDIの32 bit命令・IE/Ready=0・遅延Ready、32番地のPHY表、BMSR latch-low、
reset自己消去/消えない、bit15/9の書込み禁止、読み戻し不一致、PHYFIX=0の書込み0回を検査。
リンク即時/50 tick/永久down/AN offを注入し、downでも2本送り、good1=0/good2=1は
2本目成功としてtxdone=yesになる。印は#1/#2、初期行・観測値と通常行78桁以下も照合。
DumpはLoad一回の後だけ。初期領域にも古いA005を置き、毎回の0書き→公開障壁→Dump、
完了語読取り→取得障壁→計数読取りを事象列で検査する。C後30msの線上完了で再取得、
永久未送信でtxdone=no、Dump15msを含む100ms期限、25ms遅延/永遠に未完了、
Load/Dumpのpre/post失敗を注入。Dump未完了時は再発行禁止、停止成功でpending取消、
停止失敗では遅れたDMA完了も解放前に扱い、leaked/QUARANTINEとする。
追加変異17本 (合計47): MDI無期限(phy_timeout)、BMSR一読(phy_latch)、reset待ち無し(phy_reset)、
readback無し(phy_fix*)、PHYFIX無視(phy_kill)、done消去無し・公開/取得障壁無し(pass)、
再取得無し(stat_delay)、#2期待値2固定(phy_late)、good2累積(pass)、待ちを#1前へ(phy_late)、
down時#2省略(phy_down)、link期限0(phy_late)、未完了Dump再発行(stat_timeout)、
Dump中に停止証拠無しfree(stat_late)、Dump時間を期限から除外(stat_never)。
traceはPHY/LINK/TX2/STATを追加し、部品領域512 Bに全15段+STAT追加2回を最大桁で検査。

前景の待ち上限は保守的に約3,900 ms (I/O命令・ログ/VFS処理時間を除いた待機時間)。
初期MDI最大40回×2ms=80、reset窓500+末尾MDI2、fix write/readback/BMSR=8、
link窓3000+末尾BMSR/ANLPAR=6、最後BMSR=4、統計2回計約202.4、
4 CB=80.8、Base/Load受理=0.6、PORT=0.08、EEPROM(フォールバック込み)=1.952 ms。
各統計の100msはDump待ち込みのtick経過。総期限時点でDump未完了ならstat=timeoutとして停止する。
同条件の前後測定は基点f3c525c・worktree既定make・同じフォント/クロスコンパイラ、
`before.json` と最終ビルド後の `after.json` を run/lb2b に保存する。見積との差は実測で報告する。
実機/NP21/Wとnative runnerは依頼対象外。ホスト試験を線上の送信証拠とは扱わない。

LB-2bの初回driver変異では#2期待値2固定が生存。最終goodの値だけでは余計な待ちを
見逃すため、phy_lateで#2のw2=0も照合する (累積2を待つ版はw2=100になる)。


## LB-2c (PACK_lb2c)

HELLO #2 の C/OK/pre/post の 4 ケースと差分・2 本目専用の 4 変異を撤去。
HELLO はリンク待ち後の 1 本 (Configure/IA より前に待つ)、印は `OS32-LB-HELLO`。
PCR/PAR/SRR/DCR/FCSCR/RECR を独立模型へ追加。PCR 書込みは literal oracle
`old | 0x0562` と ID と PHYVENDOR で照合し、違反を fatal にする。
TXOFF/REPEATER/NRZI/LED1 の保存、revision 違い、ID1/ID2 各不一致、82555、
既設定時の書込み 0 回、PHYVENDOR=0 の書込み 0 回、予約 bit 0500 の変化、
R/W bit 0002/0020/0040 各欠落、vendor MDI の 6 箇所の timeout を検査する。
PCR 書込み前に BMCR fix の印があり、link の印がまだないこと、PCR の初回読出しが
1 回あること、書込み後の管理 read が 1 回あることを確認する。
ID が違えば vendor reg は読まない。カウンタは統計後に各 1 回だけ読み、生値を照合。
phy_latch は BMSR 初回だけ 0000、2 回目 782d とし、link と tx 両方のログに残す。
PHY の非対象・未検出、MDI timeout、永久 down、good=0、pcr=ng も停止・合否を照合。
全 nsc/link/tx 行は 78 桁以下、stat_max は全 32 bit 統計に UINT32_MAX を注入する。
依頼の tx 全項目は 1 行に入らないため tx を 3 行に分割する (合計 5 行)。
新規変異: PCR の 5 bit 各欠落、他 bit の破壊、ID guard 全体/ID1/ID2、readback 削除、
同値書込み、PHYVENDOR 無視、予約 bit の照合、PCR を link 待ちの後へ、BMSR 初回の破棄、
link 待ち削除。統計の完了語 0・両障壁・100ms (Dump 込み)・timeout 再発行禁止の
既存変異は 1 本の HELLO に適用。停止の証拠と free/leaked の oracle は維持。
証拠: `/home/hight/os32-tmp/run/lb2c/`。最初の対照実行の失敗は模型が MDI WRITE の
完了確認まで PCR READ 回数に数えたためで、READ opcode の時だけ計数するよう修正。
対照 111/111、変異 59/59 RED、target GNU11 -Werror は rc=0。
同条件 (基点 41ba30a、worktree 既定 make all、同じフォント・CROSS_DIR) で
vmkernel.lz4 500446→500437 B (−9)、kernel.bin 389652→389724 B (+72)、
text/data/bss 730484/34091/612256→730420/34227/612256 B、
__bss_end 00194080→001940c0 (+64)。KDEBUG のビルド条件は変更していない。
