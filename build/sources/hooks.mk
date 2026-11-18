# exec のフックの名前と、在るかどうかを決める値 (docs/design/exec/exec_hooks.md)。build/select.mk が
# 像のファイルの後で読む。今は全部「未移行の物が入っている像か」に連動する (x86-pc98 は 1、kboot の像は
# 0)。フックの提供者を PowerPC の像に入れた段で、その行だけ本来の機能の値に替える。
# コマンドラインで KERNEL_CFLAGS_EXTRA= を与える組み方では HOOK_DEFS が落ち、ヘッダの既定 (在る) になる:
# x86 のフックを 0 にする段が来たら、HOOK_DEFS は KERNEL_CFLAGS に直に足すこと。
EXEC_HOOKS = gfx gfx_poll surface gui font_guard font_lease ime snd shlib legacy r1 reclaim kapi_db serial_diag
$(foreach h,$(EXEC_HOOKS),$(eval HOOK_$(h) = $$(FEATURE_PC98_PENDING)))
HOOK_surface = $(FEATURE_EXEC)
hook_macro = OS32_HOOK_$(shell echo $(1) | tr 'a-z' 'A-Z')
HOOK_DEFS := $(foreach h,$(EXEC_HOOKS),-D$(call hook_macro,$(h))=$(HOOK_$(h)))
C_COMMON_HOOKS_ABSENT = $(foreach h,$(EXEC_HOOKS),$(if $(filter 0,$(HOOK_$(h))),exec/absent/exec_$(h).c))
