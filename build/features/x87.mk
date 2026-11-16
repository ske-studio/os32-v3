# feature/x87 owns the complete emulator and its measured integer closure.
X87_EMU_SOURCES := arch/x86/kernel/x87_emu_decode.c \
    arch/x86/kernel/x87_emu_state.c \
    arch/x86/kernel/x87_emu_ops.c \
    arch/x86/kernel/x87_emu_esc03.c \
    arch/x86/kernel/x87_emu_esc47.c \
    arch/x86/kernel/x87_emu_exception.c \
    arch/x86/kernel/x87_emu_memory.c \
    arch/x86/kernel/x87_emu_dispatch.c
C_FEATURES += arch/x86/kernel/x87.c $(X87_EMU_SOURCES) $(filter-out $(SOFTFLOAT_RESIDENT_SOURCES),$(SOFTFLOAT_X87_SOURCES))
X87_KERNEL_FLAGS = -DOS32_X87_EMU_LINKED
ifeq ($(TEST_NOFPU_BUILD),1)
ifeq ($(filter %/testnofpu,$(BUILD_OUT)),)
$(error TEST_NOFPU_BUILD=1 requires BUILD_OUT ending in /testnofpu)
endif
X87_KERNEL_FLAGS += -DOS32_TEST_NOFPU
endif
$(addprefix $(KERNEL_OBJ_PREFIX),$(X87_EMU_SOURCES:.c=.o) $(SOFTFLOAT_X87_SOURCES:.c=.o)) $(addprefix $(BUILD_OUT)/r1/obj/,$(X87_EMU_SOURCES:.c=.o) $(SOFTFLOAT_X87_SOURCES:.c=.o)): private KERNEL_CFLAGS += $(SOFTFLOAT_FLAGS) -Os -ffunction-sections -fdata-sections
