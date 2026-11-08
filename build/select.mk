# Source ownership and ordered selection, shared by make and host queries.
ifeq ($(filter $(PLATFORM),pc98 powermac),)
$(error unsupported PLATFORM $(PLATFORM))
endif
include build/sources/order.mk
include build/sources/common.mk
include build/arch/$(ARCH)-sources.mk
include build/platform/$(PLATFORM).mk
ifneq ($(ARCH),$(SUPPORTED_ARCH))
$(error unsupported ARCH/PLATFORM $(ARCH)/$(PLATFORM))
endif
FEATURES = v86 x87 pc98-devices geforce trident cirrus pcm fm lan kcg cd fep sqlite hwdev kdebug
FEATURE_V86 ?= $(DEFAULT_FEATURE_VALUE)
ifneq ($(words $(filter 0 1,$(FEATURE_V86))),1)
$(error invalid FEATURE_V86)
endif
ifeq ($(FEATURE_V86),1)
ifneq ($(ARCH)-$(PLATFORM),x86-pc98)
$(error unsupported FEATURE_V86 on $(ARCH)-$(PLATFORM))
endif
include build/features/v86.mk
else
C_FEATURES += platform/none/legacy_guest.c
endif
FEATURE_X87 ?= $(DEFAULT_FEATURE_VALUE)
ifneq ($(words $(filter 0 1,$(FEATURE_X87))),1)
$(error invalid FEATURE_X87)
endif
ifeq ($(FEATURE_X87),1)
ifneq ($(ARCH)-$(PLATFORM),x86-pc98)
$(error unsupported FEATURE_X87 on $(ARCH)-$(PLATFORM))
endif
include build/features/x87.mk
endif
FEATURE_PC98_DEVICES ?= $(DEFAULT_FEATURE_VALUE)
ifneq ($(words $(filter 0 1,$(FEATURE_PC98_DEVICES))),1)
$(error invalid FEATURE_PC98_DEVICES)
endif
ifeq ($(FEATURE_PC98_DEVICES),1)
ifneq ($(ARCH)-$(PLATFORM),x86-pc98)
$(error unsupported FEATURE_PC98_DEVICES on $(ARCH)-$(PLATFORM))
endif
include build/features/pc98-devices.mk
endif
FEATURE_GEFORCE ?= $(DEFAULT_FEATURE_VALUE)
ifneq ($(words $(filter 0 1,$(FEATURE_GEFORCE))),1)
$(error invalid FEATURE_GEFORCE)
endif
ifeq ($(FEATURE_GEFORCE),1)
ifneq ($(ARCH)-$(PLATFORM),x86-pc98)
$(error unsupported FEATURE_GEFORCE on $(ARCH)-$(PLATFORM))
endif
include build/features/geforce.mk
endif
FEATURE_TRIDENT ?= $(DEFAULT_FEATURE_VALUE)
ifneq ($(words $(filter 0 1,$(FEATURE_TRIDENT))),1)
$(error invalid FEATURE_TRIDENT)
endif
ifeq ($(FEATURE_TRIDENT),1)
ifneq ($(ARCH)-$(PLATFORM),x86-pc98)
$(error unsupported FEATURE_TRIDENT on $(ARCH)-$(PLATFORM))
endif
include build/features/trident.mk
endif
FEATURE_CIRRUS ?= $(DEFAULT_FEATURE_VALUE)
ifneq ($(words $(filter 0 1,$(FEATURE_CIRRUS))),1)
$(error invalid FEATURE_CIRRUS)
endif
ifeq ($(FEATURE_CIRRUS),1)
ifneq ($(ARCH)-$(PLATFORM),x86-pc98)
$(error unsupported FEATURE_CIRRUS on $(ARCH)-$(PLATFORM))
endif
include build/features/cirrus.mk
endif
FEATURE_PCM ?= $(DEFAULT_FEATURE_VALUE)
ifneq ($(words $(filter 0 1,$(FEATURE_PCM))),1)
$(error invalid FEATURE_PCM)
endif
ifeq ($(FEATURE_PCM),1)
ifneq ($(ARCH)-$(PLATFORM),x86-pc98)
$(error unsupported FEATURE_PCM on $(ARCH)-$(PLATFORM))
endif
include build/features/pcm.mk
endif
FEATURE_FM ?= $(DEFAULT_FEATURE_VALUE)
ifneq ($(words $(filter 0 1,$(FEATURE_FM))),1)
$(error invalid FEATURE_FM)
endif
ifeq ($(FEATURE_FM),1)
ifneq ($(ARCH)-$(PLATFORM),x86-pc98)
$(error unsupported FEATURE_FM on $(ARCH)-$(PLATFORM))
endif
include build/features/fm.mk
endif
FEATURE_LAN ?= $(DEFAULT_FEATURE_VALUE)
ifneq ($(words $(filter 0 1,$(FEATURE_LAN))),1)
$(error invalid FEATURE_LAN)
endif
ifeq ($(FEATURE_LAN),1)
ifneq ($(ARCH)-$(PLATFORM),x86-pc98)
$(error unsupported FEATURE_LAN on $(ARCH)-$(PLATFORM))
endif
include build/features/lan.mk
endif
FEATURE_KCG ?= $(DEFAULT_FEATURE_VALUE)
ifneq ($(words $(filter 0 1,$(FEATURE_KCG))),1)
$(error invalid FEATURE_KCG)
endif
ifeq ($(FEATURE_KCG),1)
ifneq ($(ARCH)-$(PLATFORM),x86-pc98)
$(error unsupported FEATURE_KCG on $(ARCH)-$(PLATFORM))
endif
include build/features/kcg.mk
endif
FEATURE_CD ?= $(DEFAULT_FEATURE_VALUE)
ifneq ($(words $(filter 0 1,$(FEATURE_CD))),1)
$(error invalid FEATURE_CD)
endif
ifeq ($(FEATURE_CD),1)
ifneq ($(ARCH)-$(PLATFORM),x86-pc98)
$(error unsupported FEATURE_CD on $(ARCH)-$(PLATFORM))
endif
include build/features/cd.mk
endif
FEATURE_FEP ?= $(DEFAULT_FEATURE_VALUE)
ifneq ($(words $(filter 0 1,$(FEATURE_FEP))),1)
$(error invalid FEATURE_FEP)
endif
ifeq ($(FEATURE_FEP),1)
ifneq ($(ARCH)-$(PLATFORM),x86-pc98)
$(error unsupported FEATURE_FEP on $(ARCH)-$(PLATFORM))
endif
include build/features/fep.mk
endif
FEATURE_SQLITE ?= $(DEFAULT_FEATURE_VALUE)
ifneq ($(words $(filter 0 1,$(FEATURE_SQLITE))),1)
$(error invalid FEATURE_SQLITE)
endif
ifeq ($(FEATURE_SQLITE),1)
ifneq ($(ARCH)-$(PLATFORM),x86-pc98)
$(error unsupported FEATURE_SQLITE on $(ARCH)-$(PLATFORM))
endif
include build/features/sqlite.mk
endif
FEATURE_HWDEV ?= $(DEFAULT_FEATURE_VALUE)
ifneq ($(words $(filter 0 1,$(FEATURE_HWDEV))),1)
$(error invalid FEATURE_HWDEV)
endif
ifeq ($(FEATURE_HWDEV),1)
ifneq ($(ARCH)-$(PLATFORM),x86-pc98)
$(error unsupported FEATURE_HWDEV on $(ARCH)-$(PLATFORM))
endif
include build/features/hwdev.mk
endif
FEATURE_KDEBUG ?= $(DEFAULT_FEATURE_VALUE)
ifneq ($(words $(filter 0 1,$(FEATURE_KDEBUG))),1)
$(error invalid FEATURE_KDEBUG)
endif
ifeq ($(FEATURE_KDEBUG),1)
ifneq ($(ARCH)-$(PLATFORM),x86-pc98)
$(error unsupported FEATURE_KDEBUG on $(ARCH)-$(PLATFORM))
endif
include build/features/kdebug.mk
endif
ifeq ($(FEATURE_V86),1)
ifneq ($(FEATURE_PC98_DEVICES),1)
$(error FEATURE_V86 requires FEATURE_PC98_DEVICES)
endif
endif
ifeq ($(FEATURE_PC98_DEVICES),1)
ifneq ($(FEATURE_PC98_DEVICES),1)
$(error FEATURE_PC98_DEVICES requires FEATURE_PC98_DEVICES)
endif
endif
ifeq ($(FEATURE_GEFORCE),1)
ifneq ($(FEATURE_PC98_DEVICES),1)
$(error FEATURE_GEFORCE requires FEATURE_PC98_DEVICES)
endif
endif
ifeq ($(FEATURE_GEFORCE),1)
ifneq ($(FEATURE_HWDEV),1)
$(error FEATURE_GEFORCE requires FEATURE_HWDEV)
endif
endif
ifeq ($(FEATURE_TRIDENT),1)
ifneq ($(FEATURE_PC98_DEVICES),1)
$(error FEATURE_TRIDENT requires FEATURE_PC98_DEVICES)
endif
endif
ifeq ($(FEATURE_CIRRUS),1)
ifneq ($(FEATURE_PC98_DEVICES),1)
$(error FEATURE_CIRRUS requires FEATURE_PC98_DEVICES)
endif
endif
ifeq ($(FEATURE_PCM),1)
ifneq ($(FEATURE_PC98_DEVICES),1)
$(error FEATURE_PCM requires FEATURE_PC98_DEVICES)
endif
endif
ifeq ($(FEATURE_FM),1)
ifneq ($(FEATURE_PC98_DEVICES),1)
$(error FEATURE_FM requires FEATURE_PC98_DEVICES)
endif
endif
ifeq ($(FEATURE_LAN),1)
ifneq ($(FEATURE_PC98_DEVICES),1)
$(error FEATURE_LAN requires FEATURE_PC98_DEVICES)
endif
endif
ifeq ($(FEATURE_KCG),1)
ifneq ($(FEATURE_PC98_DEVICES),1)
$(error FEATURE_KCG requires FEATURE_PC98_DEVICES)
endif
endif
ifeq ($(FEATURE_CD),1)
ifneq ($(FEATURE_PC98_DEVICES),1)
$(error FEATURE_CD requires FEATURE_PC98_DEVICES)
endif
endif
ifeq ($(FEATURE_FEP),1)
ifneq ($(FEATURE_PC98_DEVICES),1)
$(error FEATURE_FEP requires FEATURE_PC98_DEVICES)
endif
endif
ifeq ($(FEATURE_FEP),1)
ifneq ($(FEATURE_SQLITE),1)
$(error FEATURE_FEP requires FEATURE_SQLITE)
endif
endif
ifeq ($(FEATURE_SQLITE),1)
ifneq ($(FEATURE_PC98_DEVICES),1)
$(error FEATURE_SQLITE requires FEATURE_PC98_DEVICES)
endif
endif
ifeq ($(FEATURE_HWDEV),1)
ifneq ($(FEATURE_KDEBUG),1)
$(error FEATURE_HWDEV requires FEATURE_KDEBUG)
endif
endif

C_SELECTED = $(C_COMMON) $(C_ARCH) $(C_PLATFORM) $(C_FEATURES)
C_ORDER = $(C_ORDER_LEGACY) $(C_ARCH_NEW) $(C_PLATFORM_NEW)
C_KERNEL = $(filter $(C_SELECTED),$(C_ORDER)) $(C_PLATFORM_NEW) $(C_DIAGNOSTICS)
ASM_KERNEL = $(filter $(ASM_ARCH) $(ASM_FEATURES),$(ASM_ORDER_LEGACY))
KERNEL_SRC_DIRS = $(filter $(INCLUDE_COMMON) $(INCLUDE_ARCH) $(INCLUDE_PLATFORM) $(INCLUDE_FEATURES),$(INCLUDE_ORDER_LEGACY))
ifneq ($(words $(C_SELECTED)),$(words $(sort $(C_SELECTED))))
$(error duplicate source owner)
endif
C_DIAGNOSTICS = $(DIAGNOSTIC_SPLASH_DIAG_BUILD) $(DIAGNOSTIC_SPLASH_OBS_BUILD) $(DIAGNOSTIC_PCM_E4_DIAG_BUILD) $(DIAGNOSTIC_TICK_PROBE_BUILD)
ifeq ($(TICK_PROBE_BUILD),1)
ifneq ($(ARCH)-$(PLATFORM)-$(FEATURE_KDEBUG),x86-pc98-1)
$(error tick probe requires x86-pc98 kdebug)
endif
DIAGNOSTIC_TICK_PROBE_BUILD = kernel/time/tick_probe.c
endif
KNOWN_FEATURE_VARIABLES = $(addprefix FEATURE_,$(shell echo $(FEATURES) | tr 'a-z-' 'A-Z_'))
ifneq ($(filter-out $(KNOWN_FEATURE_VARIABLES),$(filter FEATURE_%,$(.VARIABLES))),)
$(error unknown feature variable $(filter-out $(KNOWN_FEATURE_VARIABLES),$(filter FEATURE_%,$(.VARIABLES))))
endif

ifeq ($(SPLASH_DIAG_BUILD)$(SPLASH_OBS_BUILD),11)
$(error SPLASH_DIAG_BUILD and SPLASH_OBS_BUILD are mutually exclusive)
endif
ifneq ($(filter 1,$(SPLASH_DIAG_BUILD) $(SPLASH_OBS_BUILD)),)
ifneq ($(FEATURE_PC98_DEVICES),1)
$(error splash diagnostics require FEATURE_PC98_DEVICES)
endif
endif
ifeq ($(PCM_E4_DIAG_BUILD),1)
ifneq ($(FEATURE_PCM),1)
$(error PCM diagnostics require FEATURE_PCM)
endif
endif
