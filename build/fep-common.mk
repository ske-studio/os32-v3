# Set FEP_OUT to the target-specific generation directory before including.
FEP_BODY_NAMES := fep_body ime ime_dict ime_conn ime_user_store ime_dict_info ime_suspend ime_romkana
FEP_DISPLAY_NAMES := ime_render_tvram ime_display
FEP_GEN := $(addprefix $(FEP_OUT)/,fep_imports.h fep_resident.h fep_providers.h fep_imports.json fep_import_slots.c fep_import_stubs.S fep_provider_types.c fep_kexports.c)

# A query goal must not run generators while make reads its makefiles.
# Recover a missing depfile when an FEP output is next requested.
ifeq ($(wildcard $(FEP_OUT)/fep_imports.d),)
.PHONY: fep-missing-depfile
$(FEP_GEN): fep-missing-depfile
endif

FEP_GEN_INPUTS := tools/gen_sqlite_imports.py tools/gen_kdebug_imports.py tools/module_schema.py sdk/kapi.json tools/prototypes/fep_imports.def build/fep-common.mk
# CI restores build/out from caches with fresh mtimes, so a stale generated
# header can look newer than its inputs. Key the outputs on the inputs' content.
FEP_GEN_SUM := $(shell cat $(FEP_GEN_INPUTS) 2>/dev/null | sha256sum | cut -c1-16)
$(FEP_OUT)/fep_inputs.$(FEP_GEN_SUM):
	@mkdir -p $(@D) && rm -f $(@D)/fep_inputs.* && touch $@
$(FEP_GEN) &: $(FEP_GEN_INPUTS) $(FEP_OUT)/fep_inputs.$(FEP_GEN_SUM)
	python3 tools/gen_sqlite_imports.py --module fep --target $(ARCH)-$(PLATFORM) --out-dir $(@D) --depfile $(@D)/fep_imports.d

-include $(wildcard $(FEP_OUT)/fep_imports.d)
