# 旧 CI の選択と実行

旧実行器は Make に検査名と依存辺を渡し、変更ファイル・所要の重みから実行対象とシャードを選んでいた。
撤去前の仕様は `git show dafe81642:docs/design/tools/checks/run.md` で参照できる。

現行の登録は [登録仕様](registry_v2.md)、構成別の計画・実行・結果回収は [CI 設計](../ci/ci_v2.md) に従う。
