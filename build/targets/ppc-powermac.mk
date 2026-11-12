# Compile-only graph: no x86 modules, layout generator, SDK or deploy rules.
include build/kernel-objects.mk
.PHONY: all kernel kernel-link module modules sdk programs libs boot deploy deploy-kernel kernel-r1
all: kernel-compile
kernel: kernel-compile
	@echo 'unsupported kernel link target ppc-powermac' >&2; exit 1
kernel-link:
	@echo 'unsupported kernel link target ppc-powermac' >&2; exit 1
module modules sdk programs libs boot deploy deploy-kernel kernel-r1 sqlite-module-proto kdebug hwdev:
	@echo 'unsupported product target ppc-powermac: $@' >&2; exit 1
%-module deploy-%:
	@echo 'unsupported product target ppc-powermac: $@' >&2; exit 1
-include $(C_KERNEL_OBJ:.o=.d) $(GAS_KERNEL_OBJ:.o=.d)

include build/ppc-user.mk
