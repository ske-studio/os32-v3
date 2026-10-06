CHECK_PAR_ORDER += 068:check-multiapp-model-host
# K5a (4 アプリ、契約 T2a) の設計をホストの純粋状態機械で固定したもの。カーネル実装の
# 正しさは何も言わない (実装は K5b)。docs/archive/gui_v13/TASK_K5_multiapp.md §設計。
check-multiapp-model-host:
	python3 -B tools/tests/test_multiapp_model.py
	python3 -B tools/tests/test_multiapp_impl.py
	MUTATE=$(MUTATE) python3 -B tools/tests/test_owner_reclaim.py

.PHONY: check-multiapp-model-host
