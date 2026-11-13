# Sources are compiled with each platform's kernel flags, never module objects.
RAM_ROOT_C := fs/isofs/iso9660.c fs/isofs/iso9660_media.c \
 drivers/block/ram_ro.c init/boot_root.c fs/ramfs/ramfs.c

# The caller supplies ROOT_ISO, ROOT_MOTD and ROOT_BINS. ROOT_BLOB is optional:
# a platform transporting the ISO separately need only build ROOT_ISO.
ROOT_MANIFEST := build/ppc-ram-root.json
ROOT_FONT := assets/fonts/shino16.kcgfont
ROOT_LICENSE := assets/fonts/JF-Dot-Shinonome16-LICENSE.txt
ROOT_UNICODE := build/out/unicode.bin

$(ROOT_FONT): assets/fonts/JF-Dot-Shinonome16.ttf tools/gen_font16.py tools/gen_g16.py
	python3 tools/gen_font16.py --ebdt $< $@

$(ROOT_UNICODE): tools/gen_unicode.c lib/unicode_jis_table.h kernel/console/unicode_file.h include/endian_le.h include/types.h include/callconv.h
	@mkdir -p $(dir $@)
	gcc -std=gnu11 -O2 -I. -Iinclude tools/gen_unicode.c -o $(dir $@)gen_unicode
	$(dir $@)gen_unicode --pairs $@

$(ROOT_ISO): $(ROOT_MOTD) tools/mkroot_image.py $(ROOT_BINS) $(ROOT_MANIFEST) $(ROOT_FONT) $(ROOT_LICENSE) $(ROOT_UNICODE)
	python3 -B tools/mkroot_image.py --motd $< $(foreach bin,$(ROOT_BINS),--bin $(bin)) --manifest $(ROOT_MANIFEST) --out $@

ifneq ($(strip $(ROOT_BLOB)),)
$(ROOT_BLOB): $(ROOT_ISO)
	$(ROOT_OBJCOPY) -I binary -O $(ROOT_ELF_FORMAT) -B $(ROOT_CPU) --rename-section .data=.rodata.boot_root,alloc,load,readonly,data,contents $< $@
endif
