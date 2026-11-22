# Preserve selector origins before applying compatibility defaults.
KB_FLAGS_GIVEN := $(foreach v,KBOOT_LINK KBOOT_SH KBOOT_SHELL,$(if $(filter-out undefined,$(origin $(v))),$(v)))
KB_PRODUCT := $(if $(filter undefined,$(origin IMAGE)),$(if $(strip $(KB_FLAGS_GIVEN)),,1),$(filter powermac,$(IMAGE)))
ifneq ($(origin IMAGE),undefined)
ifneq ($(words $(IMAGE)),1)
$(error expected one IMAGE name)
endif
ifeq ($(filter powermac kboot-core kboot-link kboot-sh kboot-shell kboot-link-sh,$(IMAGE)),)
$(error unknown IMAGE $(IMAGE))
endif
ifeq ($(strip $(KB_FLAGS_GIVEN)),)
KBOOT_LINK := $(if $(filter powermac kboot-link kboot-link-sh,$(IMAGE)),1,0)
KBOOT_SH := $(if $(filter powermac kboot-sh kboot-link-sh,$(IMAGE)),1,0)
KBOOT_SHELL := $(if $(filter kboot-shell,$(IMAGE)),1,0)
endif
endif
NEGATIVE ?= 0
# An explicit legacy selector keeps its historical defaults. With no selector
# the product includes both the link transport and the resident shell.
ifeq ($(origin KBOOT_LINK)$(origin KBOOT_SHELL),undefinedundefined)
KBOOT_SH ?= 1
else
KBOOT_SH ?= 0
endif
KBOOT_SHELL ?= 0
KBOOT_LINK ?= 1
override KBOOT_LINK := $(strip $(KBOOT_LINK))
override KBOOT_SH := $(strip $(KBOOT_SH))
override KBOOT_SHELL := $(strip $(KBOOT_SHELL))
$(foreach v,KBOOT_LINK KBOOT_SH KBOOT_SHELL,$(if $(and $(filter 1,$(words $($(v)))),$(filter 0 1,$($(v)))),,$(error $(v) must be 0 or 1)))
ifeq ($(KBOOT_LINK),1)
ifeq ($(KBOOT_SHELL),1)
$(error KBOOT_LINK requires the diagnostic image)
endif
ifneq ($(NEGATIVE),0)
$(error KBOOT_LINK does not support NEGATIVE)
endif
endif
ifeq ($(KBOOT_SH),1)
ifeq ($(KBOOT_SHELL),1)
$(error KBOOT_SH and KBOOT_SHELL are mutually exclusive)
endif
ifneq ($(NEGATIVE),0)
$(error KBOOT_SH does not support NEGATIVE)
endif
endif
ifeq ($(KBOOT_SHELL),1)
ifneq ($(NEGATIVE),0)
$(error KBOOT_SHELL does not support NEGATIVE)
endif
endif
ifneq ($(filter 1,$(KBOOT_SH) $(KBOOT_SHELL)),)
override KBOOT_LINK := 1
override KBOOT_SH := 1
override KBOOT_SHELL := 0
endif
KB_IMAGE := $(if $(filter 1,$(KBOOT_SH)),$(if $(filter 1,$(KBOOT_LINK)),kboot-link-sh,kboot-sh),$(if $(filter 1,$(KBOOT_SHELL)),kboot-shell,$(if $(filter 1,$(KBOOT_LINK)),kboot-link,kboot-core)))
ifneq ($(origin IMAGE),undefined)
ifneq ($(if $(filter powermac kboot-sh kboot-shell,$(IMAGE)),kboot-link-sh,$(IMAGE)),$(KB_IMAGE))
$(error IMAGE $(IMAGE) disagrees with the KBOOT_* flags, which select $(KB_IMAGE))
endif
endif
override IMAGE := $(if $(KB_PRODUCT),powermac,$(KB_IMAGE))
