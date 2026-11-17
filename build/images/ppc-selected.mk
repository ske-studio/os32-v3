# compile だけの計測の集合を保つ一時の像 (最上位の make ARCH=ppc の既定)。計測と一緒に消す。
IMAGE_ORDER = legacy
IMAGE_TARGET = ppc-powermac
FEATURE_LINK ?= 1
FEATURE_EXEC ?= 1
FEATURE_USERSHELL ?= 1
FEATURE_USB ?= 0
FEATURE_CLI ?= 0
FEATURE_PC98_PENDING ?= 1
# x86 の pc98-devices から共通の組へ移した物は、今の計測に入っていない。
IMAGE_EXCLUDE = net/nic.c net/link.c net/link_notify.c net/host_mailbox.c net/nic_rx.c net/rx_work.c drivers/base/dev.c input/hal/legacy_ring.c input/hal/legacy_mode.c kernel/port_service.c kernel/serial_diag.c
