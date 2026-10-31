# fs/fat/fatfs_vfs_read_cache.inc — 読み取り FIL の保持

> 状態: **現行 (2026-10-31)**

`VFS 接続` の各マウントに読み専用 FIL を4本保持する。
単一利用者・単一タスク・協調切替が前提。FatFs 本体や VFS FD の所有権は変えない。

## 状態と読み取り

FatReadCache は4個の FatReadSlot と FD の入口世代を持つ。スロットのキーは
ff_make_path が検査・完成したボリューム付きパスのバイト完全一致。
有効ビットと小さい LRU 順位を持ち、空き、なければ最古の1本を閉じて再利用する。
大小文字の別綴りは別スロットになりうる。

rc_read は実読込長を0で初期化する。ミス時だけ FA_READ で開き、
f_tell と要求 offset が異なるときだけ f_lseek する。位置・長さの正は FIL。
f_read の実読込長を返し、EOF・size=0・部分読み取りでも要求量から位置を推算しない。
同じパスの別 FD も明示 offset に合わせる。一括読み取りの要求位置は0。

FD では fdc_media_gen(VFS_MOUNT_DEV_ID(dev_id)) を入口で比較し、差があれば
全 FIL を閉じて要求パスを開き直す。pdrv と物理 FD 番号を混同しない。
読み取り中に世代が進んでも失敗扱いにせず、入口の世代を維持して次回破棄する。
読み側の再試行・再マウント・時計参照はない。HDD に世代検査はない。

## 破棄と失敗

rc_invalidate_all は有効な全 FIL を閉じて無効にする。close の失敗は元の結果に
上書きしない。rc_drop_on_umount はこれに加えて世代を初期化する。
どちらも f_mount は呼ばない。接続先の順序は `VFS 接続` を参照。

open/seek/read が失敗すればその要求を負値に変換し、該当スロットを破棄する。
FR_DISK_ERR・FR_NOT_READY・FR_INVALID_OBJECT・FR_INT_ERR では全スロットを破棄する。
失敗した open の FIL を close しない。成功した読み取りは追い出し時の close エラーに
影響されない。FF_FS_LOCK=0 が前提で、違えばコンパイルを拒否する。

i386 で FIL=40B、スロット=304B、保持状態=1,220B。最大2マウントで2,440B増。
セクタバッファ・クラスタ表は追加しない。VFS close 後も最大4本を保持する。
媒体交換は既存契約どおり umount/mount を挟む。世代差だけでは FATFS の共有窓を
更新しない。マウント中の raw HDD 書き込みとの混在は保証範囲外。

## 検証

[実 FatFs のホスト試験](../../tools/tests/fatfs_read_cache_host.md) で
読み取り内容、LRU、全無効化経路、失敗、世代、寿命と I/O 要求回数を検証する。
実装置のコマンド数や処理時間はこのホスト試験から換算しない。
