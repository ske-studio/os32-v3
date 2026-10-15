# 82557 LB-1 実ドライバのホスト試験

契約: `docs/tasks/realhw/TASK_LAN_82557.md` §2 L-B、設計 r3 §2 / §4-2 /
§5-7 / §6-3 と PM 補遺 3 (永続の lan.seq)。
`e82557_drv_host.c` は実物の `drivers/e82557.c` を E82557_HOST_TEST で
include し、VFS・起動設定・Image CRC・SCB I/O・irq_save・trace を差し替える。
EEPROM は窓と MAC の判定を実物にし、語読みだけを注入する。
ピンの T/S は `e82557_eeprom_tdd.md` の独立した試験に任せる。
ホストは freestanding ILP32、qemu-i386。`--target` は実ドライバを
本番の i386-elf GNU11 -Os -Werror でコンパイルする。

## 対照 46 ケース

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
