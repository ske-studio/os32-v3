# Set FEP_OUT to the target-specific generation directory before including.
FEP_BODY_NAMES := fep_body ime ime_dict ime_conn ime_user_store ime_dict_info ime_suspend ime_romkana
FEP_DISPLAY_NAMES := ime_render_tvram ime_display
FEP_GEN := $(addprefix $(FEP_OUT)/,fep_imports.h fep_imports.json fep_import_slots.c fep_import_stubs.S fep_provider_types.c fep_kexports.c)

# A query goal must not run generators while make reads its makefiles.
# Recover a missing depfile when an FEP output is next requested.
ifeq ($(wildcard $(FEP_OUT)/fep_imports.d),)
.PHONY: fep-missing-depfile
$(FEP_GEN): fep-missing-depfile
endif

$(FEP_GEN) &: tools/gen_sqlite_imports.py tools/gen_kdebug_imports.py tools/module_schema.py sdk/kapi.json tools/prototypes/fep_imports.def build/fep-common.mk
	python3 tools/gen_sqlite_imports.py --module fep --target $(ARCH)-$(PLATFORM) --out-dir $(FEP_OUT) --depfile $(FEP_OUT)/fep_imports.d

-include $(wildcard $(FEP_OUT)/fep_imports.d)
