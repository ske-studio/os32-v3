CHECK_PAR_ORDER += 013:check-key-inject-host
# キー注入 (票 docs/archive/tools/TASK_KEY_INJECT.md)。**np21w-src の
# aidebug_keys.cpp を実物のまま g++ にリンクして**変換表を照合する
# (エミュレータのソースはホストで試験できる)。np21w-src が無ければ SKIP。
# 併せて tools/gui_gate.py の逃がし記法と、**既定で既存の送信バイト列が
# 変わらないこと** (受入 K6) を送信の差し替えで見る。
check-key-inject-host:
	python3 -B tools/tests/test_key_inject.py

