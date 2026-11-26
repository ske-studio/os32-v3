# User-space OPNA archive and sound shared library.
OPNA_TARGET := $(abspath sdk/rust/i386-os32-snd.json)
OPNA_CARGO_DIR := $(abspath $(BUILD_OUT)/opna/pic/target)
OPNA_ARCHIVE := $(OPNA_CARGO_DIR)/i386-os32-snd/release/libos32_opna.a

.PHONY: opna-staticlib
opna-staticlib: $(OPNA_ARCHIVE)
	python3 -B tools/gen_opna_layout.py "$(dir $(OPNA_ARCHIVE))deps"

$(OPNA_ARCHIVE): $(wildcard lib/os32_opna/src/*.rs) lib/os32_opna/Cargo.toml lib/os32_opna/Cargo.lock $(OPNA_TARGET) tools/opna_rustc.py sdk/rustc_stamp.py sdk/target_profile.py sdk/os32_generations.py .cargo/config.toml build/os32snd.mk
	OS32_SDK_TARGET=x86-pc98-snd RUSTC_WRAPPER=$(CURDIR)/tools/opna_rustc.py \
	    CARGO_TARGET_DIR="$(OPNA_CARGO_DIR)" cargo build --locked --release \
	    -Zjson-target-spec -Zbuild-std=core,compiler_builtins -Zbuild-std-features=compiler-builtins-mem \
	    --manifest-path lib/os32_opna/Cargo.toml --target "$(OPNA_TARGET)"

SND_DIR := userland/lib/snd
SND_OBJ := $(SND_DIR)/mix.o $(SND_DIR)/panic.o $(SND_DIR)/render.o $(SND_DIR)/pump.o $(SND_DIR)/chip.o $(SND_DIR)/fmvoice.o
ALL_LIB_OBJ += $(SND_OBJ)
# L2/L3 call these private interfaces inside the shared library.
SND_INTERNAL := opna_init opna_reset opna_write opna_read opna_status opna_rhythm \
    opna_next_event opna_count opna_render opna_timer_write opna_timer_count \
    opna_apply_csm sndl_mix_render sndl_mix_finish sndl_mix_played sndl_mix_position

$(SND_OBJ): $(SND_DIR)/%.o: $(SND_DIR)/%.c | $(ABI_CONTRACT_ORDER)
	$(CC) $(PROGRAM_FLAGS) -fPIC -ffunction-sections -c $< -o $@

$(SND_DIR)/header.o: $(SND_DIR)/header.asm $(SND_DIR)/entries.inc sdk/crt/generations.inc
	nasm -f elf32 $< -o $@

userland/libos32snd.elf: $(SND_DIR)/header.o $(SND_OBJ) $(OPNA_ARCHIVE) sdk/link/shlib.ld build/os32snd.mk
	$(LD) -m elf_i386 --defsym=__os32_sound_image=1 -T sdk/link/shlib.ld --nmagic --gc-sections \
	    $(addprefix -u ,$(SND_INTERNAL)) -o $@ $(filter %.o %.a,$^) \
	    $(shell $(CC) -print-libgcc-file-name)

userland/libos32snd.raw: userland/libos32snd.elf
	$(OBJCOPY) -O binary $< $@

userland/libos32snd.shlib: userland/libos32snd.raw userland/libos32snd.elf tools/mkshlib.py sdk/os32x_hdr.py sdk/os32_generations.py $(SND_DIR)/entries.inc sdk/include/os32/os32_sndl_table.h
	python3 tools/mkshlib.py $< $@ --elf userland/libos32snd.elf --entries $(SND_DIR)/entries.inc

programs: userland/libos32snd.shlib


.PHONY: clean-os32snd
clean-programs: clean-os32snd
clean-os32snd:
	rm -f $(SND_OBJ) $(SND_OBJ:.o=.d) $(SND_DIR)/header.o $(addprefix userland/libos32snd.,elf raw shlib)
	rm -f userland/libos32snd.inputs.json
	rm -rf $(OPNA_CARGO_DIR)
