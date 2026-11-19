# Fixed i386 symbol closures measured from the wave-A vendor subset.
# SQLite and the x87 integration share these port flags, not mode globals.
SOFTFLOAT_FLAGS = -msoft-float -mno-fp-ret-in-387
SOFTFLOAT_CFLAGS = $(C_STD) $(C_DIALECT_ERRORS) $(CFLAGS_MACHINE) -Os -ffunction-sections -fdata-sections $(SOFTFLOAT_FLAGS)
SOFTFLOAT_SQLITE_SOURCES := lib/softfloat/f32_to_f64.c \
    lib/softfloat/f64_add.c \
    lib/softfloat/f64_div.c \
    lib/softfloat/f64_eq.c \
    lib/softfloat/f64_lt.c \
    lib/softfloat/f64_mul.c \
    lib/softfloat/f64_sub.c \
    lib/softfloat/f64_to_f32.c \
    lib/softfloat/f64_to_i32.c \
    lib/softfloat/f64_to_i64.c \
    lib/softfloat/f64_to_ui32.c \
    lib/softfloat/f64_to_ui64.c \
    lib/softfloat/i32_to_f64.c \
    lib/softfloat/i64_to_f64.c \
    lib/softfloat/s_addMagsF64.c \
    lib/softfloat/s_approxRecip32_1.c \
    lib/softfloat/s_approxRecip_1Ks.c \
    lib/softfloat/s_commonNaNToF32UI.c \
    lib/softfloat/s_commonNaNToF64UI.c \
    lib/softfloat/s_countLeadingZeros64.c \
    lib/softfloat/s_countLeadingZeros8.c \
    lib/softfloat/s_f32UIToCommonNaN.c \
    lib/softfloat/s_f64UIToCommonNaN.c \
    lib/softfloat/s_mul64To128.c \
    lib/softfloat/s_normRoundPackToF64.c \
    lib/softfloat/s_normSubnormalF32Sig.c \
    lib/softfloat/s_normSubnormalF64Sig.c \
    lib/softfloat/s_propagateNaNF64UI.c \
    lib/softfloat/s_roundPackToF32.c \
    lib/softfloat/s_roundPackToF64.c \
    lib/softfloat/s_roundToI32.c \
    lib/softfloat/s_roundToI64.c \
    lib/softfloat/s_roundToUI32.c \
    lib/softfloat/s_roundToUI64.c \
    lib/softfloat/s_subMagsF64.c \
    lib/softfloat/softfloat_raiseFlags.c \
    lib/softfloat/softfloat_state.c \
    lib/softfloat/ui32_to_f64.c \
    lib/softfloat/ui64_to_f64.c
SOFTFLOAT_RESIDENT_SOURCES := lib/softfloat/f64_to_i32.c lib/softfloat/s_roundToI32.c lib/softfloat/softfloat_raiseFlags.c lib/softfloat/softfloat_state.c
# Fixed ext80 closure; union with the resident bit API, never vendor wildcard.
SOFTFLOAT_X87_SOURCES := lib/softfloat/extF80_add.c \
    lib/softfloat/extF80_div.c \
    lib/softfloat/extF80_eq.c \
    lib/softfloat/extF80_isSignalingNaN.c \
    lib/softfloat/extF80_lt.c \
    lib/softfloat/extF80_mul.c \
    lib/softfloat/extF80_roundToInt.c \
    lib/softfloat/extF80_sqrt.c \
    lib/softfloat/extF80_sub.c \
    lib/softfloat/extF80_to_f32.c \
    lib/softfloat/extF80_to_f64.c \
    lib/softfloat/extF80_to_i16.c \
    lib/softfloat/extF80_to_i32.c \
    lib/softfloat/extF80_to_i64.c \
    lib/softfloat/f32_to_extF80.c \
    lib/softfloat/f64_to_extF80.c \
    lib/softfloat/i32_to_extF80.c \
    lib/softfloat/i64_to_extF80.c \
    lib/softfloat/s_addMagsExtF80.c \
    lib/softfloat/s_approxRecip32_1.c \
    lib/softfloat/s_approxRecipSqrt32_1.c \
    lib/softfloat/s_approxRecipSqrt_1Ks.c \
    lib/softfloat/s_approxRecip_1Ks.c \
    lib/softfloat/s_commonNaNToExtF80UI.c \
    lib/softfloat/s_commonNaNToF32UI.c \
    lib/softfloat/s_commonNaNToF64UI.c \
    lib/softfloat/s_countLeadingZeros64.c \
    lib/softfloat/s_countLeadingZeros8.c \
    lib/softfloat/s_extF80UIToCommonNaN.c \
    lib/softfloat/s_f32UIToCommonNaN.c \
    lib/softfloat/s_f64UIToCommonNaN.c \
    lib/softfloat/s_mul64To128.c \
    lib/softfloat/s_normRoundPackToExtF80.c \
    lib/softfloat/s_normSubnormalExtF80Sig.c \
    lib/softfloat/s_normSubnormalF32Sig.c \
    lib/softfloat/s_normSubnormalF64Sig.c \
    lib/softfloat/s_propagateNaNExtF80UI.c \
    lib/softfloat/s_roundPackToExtF80.c \
    lib/softfloat/s_roundPackToF32.c \
    lib/softfloat/s_roundPackToF64.c \
    lib/softfloat/s_roundToI16.c \
    lib/softfloat/s_roundToI32.c \
    lib/softfloat/s_roundToI64.c \
    lib/softfloat/s_shiftRightJam128.c \
    lib/softfloat/s_subMagsExtF80.c \
    lib/softfloat/softfloat_raiseFlags.c \
    lib/softfloat/softfloat_state.c
ifeq ($(ARCH),x86)
# flags/config are also available to standalone host make queries.
# kmath.o is built soft here; the kernel has no softfloat_abi, so calling fabs from the kernel would fail to link (__ltdf2). Callers must stay in SQLite.
C_ARCH += lib/softfloat_bits.c $(SOFTFLOAT_RESIDENT_SOURCES)
$(addprefix $(KERNEL_OBJ_PREFIX),lib/kmath.o lib/softfloat_bits.o $(SOFTFLOAT_RESIDENT_SOURCES:.c=.o)) $(addprefix $(BUILD_OUT)/r1/obj/,lib/kmath.o lib/softfloat_bits.o $(SOFTFLOAT_RESIDENT_SOURCES:.c=.o)): private KERNEL_CFLAGS += $(SOFTFLOAT_FLAGS)
endif

# Integer conversions retained by the PowerPC hardware-FP SQLite build.
SOFTFLOAT_CONV_SOURCES := $(addprefix lib/softfloat/,f64_to_i64.c f64_to_ui64.c i64_to_f64.c \
    s_roundToI64.c s_roundToUI64.c s_normRoundPackToF64.c s_roundPackToF64.c \
    s_countLeadingZeros64.c s_countLeadingZeros8.c)
