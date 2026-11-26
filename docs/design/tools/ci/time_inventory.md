# 旧 CI のシャード別計測

旧 CI では成功した実行の検査別秒数から重みを更新し、最も遅いシャードとジョブ全体の所要を比べた。
測定条件と過去値は [計測記録](gate.md)、撤去前の採取手順は `git show dafe81642:docs/design/tools/ci/time_inventory.md` にある。
現行 CI では同じ run と attempt のジョブ所要、結果表の bundle 別秒数を使う。
