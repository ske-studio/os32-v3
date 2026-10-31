# tools/tests/fatfs_stat_host.c — FatFs VFS の境界回帰

> 状態: **現行 (2026-10-31)**

実物 fatfs_vfs.c の全体を組み、FatFs API と kernel/device 境界をスタブ化する。
stat/get_size の FR_INVALID_NAME→NOTFOUND と open 系の INVAL の区別、
サイズ・モード・時刻、8.3名での journal 探索、一覧の成功・空・途中エラー、
書込み close 失敗と書込み本体エラー優先、起動ログの失敗保持を確認する。
入口は tools/tests/test_fatfs_stat.py。

読み取り保持が参照する fdc_media_gen は0固定の境界スタブとする。
時計や FD 下位キャッシュは模擬しない。保持・世代・実 FatFs の内容検証は
`専用ホスト試験` が担当する。
