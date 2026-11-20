# PowerPC の像の選び方 (build/select.mk が ARCH=ppc の時に、像のファイルより先に読む)。
# 像は名前 (IMAGE=kboot-*) でも、道具の旗 (KBOOT_LINK・KBOOT_SH・KBOOT_SHELL) でも選べる。
# 両方をコマンドラインで与えて食い違えば止める。旗は規則 (build/targets/ppc-powermac.mk) が
# 出力の置き場や -D に使う。設計は docs/design/build/select.md §3-1。
ifneq ($(findstring command,$(origin IMAGE)),)
ifeq ($(findstring command,$(origin KBOOT_LINK))$(findstring command,$(origin KBOOT_SH))$(findstring command,$(origin KBOOT_SHELL)),)
# 名前だけで選んだ時は、旗を名前から決める。
KBOOT_LINK := $(if $(filter kboot-link kboot-link-sh,$(IMAGE)),1,0)
KBOOT_SH := $(if $(filter kboot-sh kboot-link-sh,$(IMAGE)),1,0)
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
ifneq ($(words $(filter 0 1,$(KBOOT_LINK) $(KBOOT_SH) $(KBOOT_SHELL))),3)
$(error KBOOT_LINK, KBOOT_SH and KBOOT_SHELL must be 0 or 1)
endif
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
ifneq ($(findstring command,$(origin IMAGE)),)
ifneq ($(if $(filter kboot-sh kboot-shell,$(IMAGE)),kboot-link-sh,$(IMAGE)),$(KB_IMAGE))
$(error IMAGE $(IMAGE) disagrees with the KBOOT_* flags, which select $(KB_IMAGE))
endif
endif
override IMAGE := $(KB_IMAGE)
