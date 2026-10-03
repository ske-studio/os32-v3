CHECK_PAR_ORDER += 018:check-docs-orphans
# 孤児文書の検出 — docs/INDEX.md から辿れない docs/*.md と、どの票からも
# 参照されていない tools/tests/*_tdd.md。lychee の守備範囲外なので自前。
#
# **`check` の列にはまだ入れていない。** 2026-09-15 の棚卸し時点で 31 本 +
# 20 本が未参照で、これは検査の不備ではなく索引の取りこぼし (票を書いて
# INDEX.md に載せ忘れたもの) の実数。今これを門にすると、通すために
# `docs/.orphans-allow` へ全部書き写すことになり、例外表が「黙らせる表」に
# 化けて二度と減らない。**PM が文書整理の段階 F で索引を直し (載せるか
# archive へ移すか)、0 になった時点で `check` の列へ移すこと。**
check-docs-orphans:
	@python3 tools/check_docs_orphans.py

.PHONY: check-docs-orphans
