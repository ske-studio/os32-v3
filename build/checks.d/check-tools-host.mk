CHECK_PAR_ORDER += 041:check-tools-host
# ctl / ini_live は専用列で実行する。残る3本だけを discover する。
# 新しい test_np21w_*.py はここへ足す (専用列で実行するものを除く)。
check-tools-host:
	python3 -B -m unittest discover -s tools/tests -p 'test_np21w_ini.py'
	python3 -B -m unittest discover -s tools/tests -p 'test_np21w_transport.py'
	python3 -B -m unittest discover -s tools/tests -p 'test_np21w_trial.py'
	python3 -B tools/tests/test_nhd_deploy_failure.py
	python3 -B tools/tests/test_filer_normalize.py
	python3 -B tools/tests/test_filer_copy_abort.py
	python3 -B tools/tests/test_about_info.py $(MUT)
	python3 -B tools/tests/test_gui_button_dispatch.py
	PYTHONPATH=. python3 -B tools/tests/test_emu_playbook.py
	python3 -B tools/tests/test_mk_settings_db.py
	python3 -B tools/tests/test_fetch_fonts.py
	python3 -B tools/tests/test_mk_blank_nhd.py
	python3 -B tools/tests/test_stat_cmd.py
	python3 -B tools/tests/test_tar_cmd.py

.PHONY: check-tools-host
