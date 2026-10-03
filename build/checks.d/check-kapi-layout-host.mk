CHECK_PAR_ORDER += 095:check-kapi-layout-host
# KAPI データ欄の固定配置と OS32X ヘッダ v3 (票 docs/archive/kernel_v21/TASK_KAPI_DATA_FIELDS.md)。
# exec / shlib ローダ / 常駐シェルの判定関数 (exec/os32x_hdr.c を
# tools/tests/os32x_layout_host.c が #include)、gen_kapi.py の容量拒否、
# mkos32x.py / mkshlib.py のヘッダ v3 (値 = ELF の .os32_kapi_layout、刻印が
# 無ければ失敗、min_api_ver >= 63)、crt の kapi 改名で作り直し忘れの .o が
# リンクで落ちること。--mutate は判定・拒否を崩した版で落ちることを見る
# (変異は一時ディレクトリの写しの木に当てるので並列で回せる)。i386-elf の道具を使う。
check-kapi-layout-host:
	python3 -B tools/tests/test_kapi_layout.py $(MUT)

.PHONY: check-kapi-layout-host
