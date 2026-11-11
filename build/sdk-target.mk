# ============================================================================
#  sdk-target.mk — SDK の target の選択 (分離の第 5 段 S1)
#
#  CRT・link script・Rust target・ELF emulation・call ABI を sdk/target_profile.py
#  からひと組で引く (python の起動は make 1 回につき 1 回)。値の正は profile で、
#  ここと build/config.mk は写しを持たない。
#  USER と完全な SDK を別々の能力として問い合わせる。
#  override: コマンド行・MAKEFLAGS・環境変数 (-e) の SDK_TARGET_*=... や、この
#  ファイルの一覧・補助の変数の上書きで選択をすり替えさせない。
# ============================================================================
override SDK_USER_NAMES := SDK_TARGET_CALL_ABI SDK_TARGET_ELF_EMULATION SDK_TARGET_CRT_USER SDK_TARGET_APP_LINK
override SDK_FULL_NAMES := SDK_TARGET_CRT_RESIDENT SDK_TARGET_APP_SYS_LINK SDK_TARGET_SHLIB_LINK SDK_TARGET_RUST
override SDK_TARGET_NAMES := $(SDK_USER_NAMES) $(SDK_FULL_NAMES)
override SDK_TARGET_INPUTS := sdk/target_profile.py sdk/target_profile_cli.py
override SDK_TARGET := $(ARCH)-$(PLATFORM)
override SDK_USER_TOKENS := $(shell python3 -B sdk/target_profile_cli.py --target $(SDK_TARGET) --make-user --root sdk 2>/dev/null)
override SDK_USER_STATUS := $(.SHELLSTATUS)
ifeq ($(filter 0 3,$(SDK_USER_STATUS)),)
$(error user sdk target profile query failed for $(SDK_TARGET) (status $(SDK_USER_STATUS)))
endif
override SDK_USER_SUPPORTED := $(if $(filter 0,$(SDK_USER_STATUS)),1,0)
override SDK_TARGET_TOKENS := $(shell python3 -B sdk/target_profile_cli.py --target $(SDK_TARGET) --make --root sdk 2>/dev/null)
override SDK_TARGET_STATUS := $(.SHELLSTATUS)
# 0 = 値あり、3 = profile が拒否 (未対応の target)。それ以外は問い合わせの失敗で、
# 空の CRT や link script のまま先へ進ませない。
ifeq ($(filter 0 3,$(SDK_TARGET_STATUS)),)
$(error sdk target profile query failed for $(SDK_TARGET) (status $(SDK_TARGET_STATUS)): python3 -B sdk/target_profile_cli.py)
endif
override SDK_TARGET_SUPPORTED := $(if $(filter 0,$(SDK_TARGET_STATUS)),1,0)
override sdk_target_comma := ,
override sdk_target_name = $(word 1,$(subst =, ,$(1)))
override sdk_target_value = $(subst $(sdk_target_comma), ,$(word 2,$(subst =, ,$(1))))
$(foreach name,$(SDK_TARGET_NAMES),$(eval override $(name) :=))
$(foreach token,$(SDK_USER_TOKENS) $(SDK_TARGET_TOKENS),$(if $(filter $(call sdk_target_name,$(token)),$(SDK_TARGET_NAMES)),\
    $(eval override $(call sdk_target_name,$(token)) := $(call sdk_target_value,$(token))),\
    $(error sdk target profile printed an unknown name: $(token))))
ifeq ($(SDK_USER_SUPPORTED),1)
$(foreach name,$(SDK_USER_NAMES),$(if $($(name)),,$(error user sdk target profile lacks $(name))))
ifneq ($(SDK_TARGET_ELF_EMULATION),$(ELF_EMULATION))
$(error sdk target profile and build/arch disagree on the ELF emulation: $(SDK_TARGET_ELF_EMULATION) / $(ELF_EMULATION))
endif
endif

ifeq ($(SDK_TARGET_SUPPORTED),1)
$(foreach name,$(SDK_FULL_NAMES),$(if $($(name)),,$(error sdk target profile lacks $(name))))
endif
# The full SDK supplies the x86 nano libc; USER-only targets do not.
override SDK_USER_LIBC = $(if $(filter 1,$(SDK_TARGET_SUPPORTED)),$(NANO_LIB))
