# A request for either member repairs an incomplete publication.
ifneq ($(words $(wildcard $(KB_ELF) $(KB_PAIR_OUTPUT))),2)
.PHONY: ppc-root-pair-missing
$(KB_ELF) $(KB_PAIR_OUTPUT): ppc-root-pair-missing
endif
$(KB_ELF) $(KB_PAIR_OUTPUT) &: $(ROOT_ISO) $(KB_PAIR_TOOL) tools/module_schema.py tools/mkmod.py tools/ppc/root_lz4_layout.py sdk/target_profile.py $(KB_OBJ) $(KB_EXTRA) $(KB_OUT)/kernel.ld $(KB_SELECT_MK)
	$(KB_LINK_COMMAND)
