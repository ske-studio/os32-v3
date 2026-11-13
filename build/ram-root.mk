# Sources are compiled with each platform's kernel flags, never module objects.
RAM_ROOT_C := fs/isofs/iso9660.c fs/isofs/iso9660_media.c \
 drivers/block/ram_ro.c init/boot_root.c fs/ramfs/ramfs.c

# The caller supplies ROOT_ISO, ROOT_MOTD and ROOT_BINS. ROOT_BLOB is optional:
# a platform transporting the ISO separately need only build ROOT_ISO.
$(ROOT_ISO): $(ROOT_MOTD) tools/mkroot_image.py $(ROOT_BINS) $(ROOT_MANIFEST) $(ROOT_DEPS)
	python3 -B tools/mkroot_image.py --motd $< $(foreach bin,$(ROOT_BINS),--bin $(bin)) $(if $(strip $(ROOT_MANIFEST)),--manifest $(ROOT_MANIFEST)) --out $@

ifneq ($(strip $(ROOT_BLOB)),)
$(ROOT_BLOB): $(ROOT_ISO)
	$(ROOT_OBJCOPY) -I binary -O $(ROOT_ELF_FORMAT) -B $(ROOT_CPU) --rename-section .data=.rodata.boot_root,alloc,load,readonly,data,contents $< $@
endif
