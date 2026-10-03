CHECK_PAR_ORDER += 030:check-le-access
# 外部形式 (LE) の直アクセス検査 (移植性の準備、順序 4-a)。媒体・書庫の上の
# バイト列は include/endian_le.h の le16_rd / le16_wr / le32_rd / le32_wr を
# 通す。`*(u32 *)&buf[off]` は「x86 は LE」「x86 は非アラインを許す」の 2 つに
# 同時に寄りかかる書き方で、ARM では落ち、BE では値が化ける。
# 実ビルドの旗で libclang の canonical type と alignment を見る (解析失敗は NG)。
check-le-access:
	@python3 tools/check_le_access.py

.PHONY: check-le-access
