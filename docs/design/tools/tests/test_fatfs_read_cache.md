# tools/tests/test_fatfs_read_cache.py — 保持試験の実行

> 状態: **現行 (2026-10-31)**

CHECK 名は fatfs-read-cache-host。入力は入口の CHECK に宣言し、製品 .inc を含む
依存を run.py が追う。成果物は TMPDIR の TemporaryDirectory 内に置く。

実 FatFs は第三者ソースを変えず、一時 translation unit から include する。
試験ビルドだけ -finstrument-functions で関数入口を数え、API 名を real_f_* に
置き換えてホストの回数計測・失敗注入と結ぶ。製品 VFS は全体を直接 include する。
ホスト C は GNU11・警告をエラーとして組む。i386 クロスコンパイルでは
FIL=40B・スロット=304B・状態=1,220B の配置を静的に確認する。
FF_FS_LOCK=1 の試験用設定では製品 .inc のガードによるコンパイル失敗を要求する。

--mutate は製品 wrapper と .inc の全体コピーで、保持なし・無効化なし・LRU 順位の穴・
seek なし・世代比較なし・read エラー無視を注入する。各変異はコンパイル成功後の実行時 RED を
要求し、通常対照は全ケース GREEN を要求する。
メモリ媒体の内容と検証条件は `C ハーネス`（fatfs_read_cache_host.md） を参照。
