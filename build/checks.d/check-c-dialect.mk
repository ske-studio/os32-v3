CHECK_PAR_ORDER += 026:check-c-dialect
# 言語モードの検査 ([C1]、票 docs/archive/v3/TASK_C11_MIGRATION.md §6 段 4)。
# check-constraints は ID の整合だけなので、こちらが**実際の旗とコンパイル結果**を見る:
# make -n -B all のコンパイル行ごとに効いている言語モード (本体・ブート・userland は
# gnu11、SQLite 系は gnu89)、暗黙宣言・暗黙 int・VLA・偽の STATIC_ASSERT の拒否、
# 公開 SDK ヘッダを gnu89 と gnu11 の両方で取り込めること (C99 以降の構文の混入)、
# 配布ライブラリヘッダと gnu89 の例 (sdk/example/hello)、内部実装の AST 型・宣言・include。
# 要クロスコンパイラ (ビルドは要らない)。
check-c-dialect:
	@python3 -B tools/check_c_dialect.py

.PHONY: check-c-dialect
