# Shared build ID source; preserve its contents and mtime when the ID is unchanged.
BUILD_ID_OBJ = $(BUILD_OUT)/build_id.o

$(BUILD_ID_SRC): .FORCE
	@mkdir -p $(dir $@)
	@python3 tools/gen_build_id.py -o $@

.FORCE:
