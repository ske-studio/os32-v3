CHECK_PAR_ORDER += 028:check-privileged
# ユーザランドの特権命令検査 (リング3 準備)。既定は警告のみ (exit 0) で
# green ビルドを壊さない。リング3 導入後に --strict でゲートする。
check-privileged:
	@python3 tools/check_privileged.py

.PHONY: check-privileged
