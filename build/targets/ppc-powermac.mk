# PowerPC products. The kernel image rules live in build/ppc-kboot.mk and are reached by
# sub-make, never by include: build/select.mk must be read once per make, and kb-user in
# build/ppc-kboot.mk re-enters this Makefile for the user binaries.
.PHONY: all kernel kernel-link module modules sdk programs libs boot deploy deploy-kernel kernel-r1
all: kernel
# IMAGE (build/images/kboot-*.mk) chooses the image; the flags below are how build/ppc-kboot.mk spells it.
kernel:
	$(MAKE) --no-print-directory -f build/ppc-kboot.mk KBOOT_LINK=$(FEATURE_LINK) KBOOT_SH=$(FEATURE_USERSHELL) KBOOT_SHELL=$(FEATURE_CLI)
kernel-link:
	@echo 'unsupported kernel link target ppc-powermac' >&2; exit 1
# The image is compiled only by build/ppc-kboot.mk (its flags and output tree).
kernel-compile:
	@echo 'unsupported compile-only target ppc-powermac: use kernel' >&2; exit 1
module modules sdk programs libs boot deploy deploy-kernel kernel-r1 sqlite-module-proto kdebug hwdev:
	@echo 'unsupported product target ppc-powermac: $@' >&2; exit 1
%-module deploy-%:
	@echo 'unsupported product target ppc-powermac: $@' >&2; exit 1

include build/ppc-user.mk
