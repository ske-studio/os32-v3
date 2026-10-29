CHECK_PAR_ORDER += 218:check-video-service-host

check-video-service-host:
	python3 -B tools/tests/test_video_service.py

.PHONY: check-video-service-host
