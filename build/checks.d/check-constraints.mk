CHECK_PAR_ORDER += 024:check-constraints
# プロジェクト制約 (docs/CONSTRAINTS.md) と、それを参照する CLAUDE.md /
# SOUL.md のずれを検査する。ID での照合なので文言は場所ごとに変えてよい。
check-constraints:
	@python3 tools/check_constraints.py

.PHONY: check-constraints
