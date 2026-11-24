# User-space PIC archive for libos32snd. L5 owns the final shlib link.
OPNA_TARGET := $(abspath sdk/rust/i386-os32-snd.json)
OPNA_CARGO_DIR := $(abspath $(BUILD_OUT)/opna/target)
OPNA_ARCHIVE := $(OPNA_CARGO_DIR)/i386-os32-snd/release/libos32_opna.a

.PHONY: opna-staticlib
opna-staticlib: $(OPNA_ARCHIVE)
	python3 -B tools/gen_opna_layout.py "$(dir $(OPNA_ARCHIVE))deps"

$(OPNA_ARCHIVE): $(wildcard lib/os32_opna/src/*.rs) lib/os32_opna/Cargo.toml lib/os32_opna/Cargo.lock $(OPNA_TARGET) build/os32snd.mk
	CARGO_TARGET_DIR="$(OPNA_CARGO_DIR)" cargo build --locked --release \
	    -Zjson-target-spec -Zbuild-std=core,compiler_builtins -Zbuild-std-features=compiler-builtins-mem \
	    --manifest-path lib/os32_opna/Cargo.toml --target "$(OPNA_TARGET)"
