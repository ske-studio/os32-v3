CHECK_PAR_ORDER += 126:check-h3-park-resume-host
# T2h/h3 fixtures and PM script, entirely offline.
check-h3-park-resume-host:
	python3 -B tools/tests/test_h3_park_resume.py $(MUT)

