# platform/pc98 owns this selection.
C_PLATFORM += sys/platform/pc98/kernel.c sys/platform/pc98/boot_token_pc98.c sys/input/keymaps/pc98_jp.c sys/input/keymaps/hid_jp.c sys/platform/pc98/input_keymap.c sys/platform/pc98/boot_irq.c sys/platform/pc98/boot_platform.c sys/platform/pc98/boot_display.c sys/platform/pc98/emergency.c sys/platform/pc98/boot.c sys/platform/pc98/boot_splash.c sys/platform/pc98/splash.c sys/platform/pc98/splash_image.c sys/platform/pc98/clock.c sys/platform/pc98/timer.c sys/platform/pc98/lan_match.c sys/platform/pc98/device_host.c sys/dev/kcg/device_kcg.c sys/platform/pc98/pci_config.c sys/platform/pc98/pci_resources.c sys/platform/pc98/pci_mmio.c sys/platform/pc98/boot_disk.c sys/platform/pc98/memory.c sys/platform/pc98/d2_bind.c sys/platform/pc98/boot_names.c sys/platform/pc98/console.c sys/platform/pc98/display.c sys/platform/pc98/display_dual.c sys/platform/pc98/gfx_scroll.c sys/platform/pc98/gfx_init.c sys/platform/pc98/gfx_selector.c sys/platform/pc98/gfx_surfaces.c sys/platform/pc98/display_bundle.c sys/platform/pc98/link_platform.c sys/platform/pc98/system.c sys/platform/pc98/pic.c sys/platform/pc98/irq_policy.c sys/platform/pc98/irq_complete.c sys/platform/pc98/boot_diag.c sys/platform/pc98/ver_backend.c sys/platform/pc98/console_attr.c
C_PLATFORM += sys/platform/pc98/machcfg.c
C_ORDER_TAIL += sys/platform/pc98/machcfg.c
SUPPORTED_ARCH = x86
C_PLATFORM += sys/platform/pc98/tsc_math.c sys/platform/pc98/tsc_clock.c
DEFAULT_FEATURE_VALUE = 1
C_PLATFORM += sys/platform/pc98/pit.c sys/platform/pc98/tick_budget.c sys/platform/pc98/pit_math.c sys/platform/pc98/pit_time.c sys/platform/pc98/clock_source.c

C_PLATFORM += sys/platform/pc98/module_pair.c sys/platform/pc98/bootinfo_capture.c sys/platform/pc98/bootinfo_geom.c sys/platform/pc98/bootinfo_report.c sys/platform/pc98/bootinfo_check.c sys/platform/pc98/bios_snapshot.c sys/platform/pc98/boot_capture.c
C_PLATFORM += sys/platform/pc98/storage/ext2_medium_ide.c
C_ORDER_TAIL += sys/platform/pc98/storage/ext2_medium_ide.c
C_PLATFORM += sys/platform/pc98/usb_config.c sys/platform/pc98/usb_platform.c sys/dev/usb/ehci_quiesce.c sys/dev/usb/ehci_handoff.c
C_ORDER_TAIL += sys/platform/pc98/usb_config.c sys/platform/pc98/usb_platform.c sys/dev/usb/ehci_quiesce.c sys/dev/usb/ehci_handoff.c
C_PLATFORM += sys/platform/pc98/usb_resident.c sys/platform/pc98/usb_load.c sys/hal/input/usb_input.c
C_ORDER_TAIL += sys/platform/pc98/usb_resident.c sys/platform/pc98/usb_load.c sys/hal/input/usb_input.c
# Relocatable USB body; pairing and installation belong to the image rules.
PC98_USB_BODY_SOURCES = sys/platform/pc98/usb_module.c sys/platform/pc98/usb_module_runtime.c sys/input/keymaps/hid_jp.c $(filter-out sys/dev/usb/ehci_quiesce.c sys/hal/input/usb_input.c,$(C_COMMON_USB))
PC98_USB_BODY_OBJECTS = $(addprefix $(BUILD_OUT)/usb/,$(PC98_USB_BODY_SOURCES:.c=.o))
ifeq ($(TICK_PROBE_BUILD),1)
$(PC98_USB_BODY_OBJECTS): private CFLAGS_BASE += -DTICK_PROBE
endif
$(BUILD_OUT)/usb/%.o: %.c
	@mkdir -p $(@D)
	$(CC) $(CFLAGS_BASE) -Os $(INC_KERNEL) -c $< -o $@
$(BUILD_OUT)/usb/usb_module.o: $(PC98_USB_BODY_OBJECTS)
	$(MODULE_LD) -r -o $@ $(PC98_USB_BODY_OBJECTS)
-include $(PC98_USB_BODY_OBJECTS:.o=.d)
