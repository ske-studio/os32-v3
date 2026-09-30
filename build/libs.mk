# ============================================================================
#  libs.mk — ユーザー空間ライブラリ定義
#
#  テンプレートマクロで各ライブラリを統一的に定義する。
#  ライブラリの追加は下の INC_<name> を 1 行足し、
#  $(eval $(call DEFINE_LIB,...)) を 1 行追加するだけで完了。
# ============================================================================

# ----------------------------------------------------------------------------
#  ライブラリごとの公開インクルードパス (自分 + 依存先の推移閉包)
#
#  以前は PROGRAM_FLAGS に全ライブラリの -I を一括で並べていたため、
#  どのライブラリからでも他の全ライブラリのヘッダが見えていた。それだと
#  「宣言されていない依存」がビルドを通ってしまい、層の逆流に気づけない
#  (実際 libos32map -> libos32tilemap がそれで潜っていた)。
#
#  ここで依存を明示し、各ライブラリ/プログラムには必要な -I だけを渡す。
#  再帰変数 (=) なので定義順は問わない。-I の重複は無害。
#  依存を足したら必ずこの表も更新すること。
# ----------------------------------------------------------------------------

# 依存なし (KAPI の共有ヘッダだけで完結する)
INC_libos32math     = -Iuserland/lib/math
INC_libos32db       = -Iuserland/lib/db
INC_libos32asset    = -Iuserland/lib/asset
INC_libos32save     = -Iuserland/lib/save
INC_libos32snd      = -Iuserland/lib/snd
INC_libos32mgx      = -Iuserland/lib/mgx -Ilib/zlib
# ランタイム小物 (pkg, dbgserial)。"rt/xxx.h" 形式で引くので -Iuserland/lib。
INC_libos32rt       = -Iuserland/lib
# 設定レジストリ (/etc/settings.db) のクライアント。KAPI の共有ヘッダだけで完結 (票 S2-C)
INC_libos32cfg      = -Iuserland/lib/cfg
# Host Services (host_* KAPI v51) の薄いクライアント。KAPI の共有ヘッダだけで完結 (票 N3)
INC_libos32host     = -Iuserland/lib/host

# 描画層
INC_libos32gfx      = -Iuserland/lib/gfx $(INC_libos32math)
INC_libos32ui       = -Iuserland/lib/ui $(INC_libos32gfx)
INC_libos32filer    = -Iuserland/lib/filer $(INC_libos32gfx)
INC_libos32md       = -Iuserland/lib/md $(INC_libos32gfx)
INC_libos32tilemap  = -Iuserland/lib/tilemap $(INC_libos32gfx) $(INC_libos32asset)

# 入力・汎用ロジック
INC_libos32input    = -Iuserland/lib/input $(INC_libos32math)
INC_libos32ecs      = -Iuserland/lib/ecs $(INC_libos32math)

# DB 駆動のドメインライブラリ
# map_view.c だけが libos32tilemap を直接呼ぶ (BG への転送)

# ---------------------------------------------------------------------------
#  DEFINE_LIB — ライブラリ定義テンプレート
# ---------------------------------------------------------------------------
#  $(1) = アーカイブ基底名 (例: libos32math)。INC_$(1) と $(1).a に対応する
#  $(2) = ソースディレクトリ (例: userland/lib/math)
#  $(3) = 追加コンパイルフラグ (通常は空。INC_$(1) で足りないときだけ使う)
#  $(4) = サブディレクトリリスト (スペース区切り, 例: draw text geom)
#
#  アーカイブ名とディレクトリ名を分けているのは、分割後のディレクトリが
#  userland/lib/math / game/lib/board のように層を表す一方で、
#  アーカイブ名は libos32math のまま外部プログラムへの名前として残るため。
define DEFINE_LIB
$(1)_SRC := $$(wildcard $(2)/*.c) $(foreach d,$(4),$$(wildcard $(2)/$(d)/*.c))
$(1)_OBJ := $$($(1)_SRC:.c=.o)
ALL_LIB_OBJ += $$($(1)_OBJ)

$(2)/%.o: $(2)/%.c
	$$(CC) $$(PROGRAM_FLAGS) $$(INC_$(1)) $(3) -c $$< -o $$@

$(foreach d,$(4),
$(2)/$(d)/%.o: $(2)/$(d)/%.c
	$$(CC) $$(PROGRAM_FLAGS) $$(INC_$(1)) $(3) -c $$< -o $$@
)

$(LIBDIR)/$(1).a: $$($(1)_OBJ)
	@rm -f $$@
	$$(AR) rcs $$@ $$^

lib-$(1): $(LIBDIR)/$(1).a

.PHONY: lib-$(1)
endef

# === 全ライブラリOBJ集約変数 (初期化) ===
ALL_LIB_OBJ :=

# === 各ライブラリの登録 ===
# libos32math — 整数数学ライブラリ (最も基底)
$(eval $(call DEFINE_LIB,libos32math,userland/lib/math,,))
LIBMATH_OBJ = $(LIBDIR)/libos32math.a

# libos32gfx — グラフィックスライブラリ (ASMソース含む, 特殊処理)
GFX_SRC = $(wildcard userland/lib/gfx/*.c) \
          $(wildcard userland/lib/gfx/draw/*.c) \
          $(wildcard userland/lib/gfx/text/*.c) \
          $(wildcard userland/lib/gfx/geom/*.c)
ASM_GFX_SRC = $(wildcard userland/lib/gfx/asm/*.asm)
ASM_GFX_OBJ = $(ASM_GFX_SRC:.asm=.o)
# lib/utf8_prog.o もここに入れる。ユーザー空間で utf8 を引くのは gfx の
# 文字描画 (lconsole.c / gfx_kcg.c) だけなので、gfx の一部として配る。
GFX_ARCHIVE_OBJ = $(GFX_SRC:.c=.o) $(ASM_GFX_OBJ) lib/utf8_prog.o
GFX_OBJ = $(LIBDIR)/libos32gfx.a $(LIBMATH_OBJ)
ALL_LIB_OBJ += $(GFX_SRC:.c=.o) $(ASM_GFX_OBJ)

$(LIBDIR)/libos32gfx.a: $(GFX_ARCHIVE_OBJ)
	@rm -f $@
	$(AR) rcs $@ $^

userland/lib/gfx/%.o: userland/lib/gfx/%.c
	$(CC) $(PROGRAM_FLAGS) $(INC_libos32gfx) -Ilib -c $< -o $@

userland/lib/gfx/draw/%.o: userland/lib/gfx/draw/%.c
	$(CC) $(PROGRAM_FLAGS) $(INC_libos32gfx) -Ilib -c $< -o $@

userland/lib/gfx/text/%.o: userland/lib/gfx/text/%.c
	$(CC) $(PROGRAM_FLAGS) $(INC_libos32gfx) -Ilib -c $< -o $@

userland/lib/gfx/geom/%.o: userland/lib/gfx/geom/%.c
	$(CC) $(PROGRAM_FLAGS) $(INC_libos32gfx) -Ilib -c $< -o $@

userland/lib/gfx/asm/%.o: userland/lib/gfx/asm/%.asm userland/lib/gfx/asm/gfx_const.inc
	$(AS) -f elf32 -Iuserland/lib/gfx/asm/ $< -o $@

lib-libos32gfx: $(LIBDIR)/libos32gfx.a
.PHONY: lib-libos32gfx

# libos32db — SQLite ユーザー空間ライブラリ
LIBOS32DB_SRC = userland/lib/db/libos32db.c
LIBOS32DB_ARCHIVE_OBJ = $(LIBOS32DB_SRC:.c=.o)
LIBOS32DB_OBJ = $(LIBDIR)/libos32db.a
ALL_LIB_OBJ += $(LIBOS32DB_ARCHIVE_OBJ)

$(LIBDIR)/libos32db.a: $(LIBOS32DB_ARCHIVE_OBJ)
	@rm -f $@
	$(AR) rcs $@ $^

userland/lib/db/%.o: userland/lib/db/%.c
	$(CC) $(PROGRAM_FLAGS) $(INC_libos32db) -c $< -o $@

lib-libos32db: $(LIBDIR)/libos32db.a
.PHONY: lib-libos32db

# libos32input — 入力抽象化
$(eval $(call DEFINE_LIB,libos32input,userland/lib/input,,))
LIBINPUT_OBJ = $(LIBDIR)/libos32input.a

# libos32ui — microUI (rxi) OS32移植版
$(eval $(call DEFINE_LIB,libos32ui,userland/lib/ui,,))
LIBUI_OBJ = $(LIBDIR)/libos32ui.a

# libos32asset — アセット管理
$(eval $(call DEFINE_LIB,libos32asset,userland/lib/asset,,))
LIBASSET_OBJ = $(LIBDIR)/libos32asset.a

# libos32ecs — ECS ゲームオブジェクト管理
$(eval $(call DEFINE_LIB,libos32ecs,userland/lib/ecs,,))

# libos32cfg — 設定レジストリのクライアント (票 S2-C)。cfg.bin と libos32gui.shlib がリンクする
$(eval $(call DEFINE_LIB,libos32cfg,userland/lib/cfg,,))
LIBCFG_OBJ = $(LIBDIR)/libos32cfg.a
LIBECS_OBJ = $(LIBDIR)/libos32ecs.a

# libos32host — Host Services (host_* KAPI v51) の薄いクライアント (票 N3)。
# wget / lpr / hclip / hdate がリンクする。
$(eval $(call DEFINE_LIB,libos32host,userland/lib/host,,))
LIBHOST_OBJ = $(LIBDIR)/libos32host.a

# libos32save — セーブデータ管理 (DB不要)
$(eval $(call DEFINE_LIB,libos32save,userland/lib/save,,))
LIBSAVE_OBJ = $(LIBDIR)/libos32save.a

# libos32tilemap — タイルマップエンジン (ASMソース含む)
TILEMAP_SRC = $(wildcard userland/lib/tilemap/*.c)
TILEMAP_ASM_SRC = $(wildcard userland/lib/tilemap/*.asm)
TILEMAP_ASM_OBJ = $(TILEMAP_ASM_SRC:.asm=.o)
TILEMAP_ARCHIVE_OBJ = $(TILEMAP_SRC:.c=.o) $(TILEMAP_ASM_OBJ)
TILEMAP_OBJ = $(LIBDIR)/libos32tilemap.a
ALL_LIB_OBJ += $(TILEMAP_ARCHIVE_OBJ)

$(LIBDIR)/libos32tilemap.a: $(TILEMAP_ARCHIVE_OBJ)
	@rm -f $@
	$(AR) rcs $@ $^

userland/lib/tilemap/%.o: userland/lib/tilemap/%.c
	$(CC) $(PROGRAM_FLAGS) $(INC_libos32tilemap) -c $< -o $@

userland/lib/tilemap/%.o: userland/lib/tilemap/%.asm
	$(AS) -f elf32 $< -o $@

lib-libos32tilemap: $(LIBDIR)/libos32tilemap.a
.PHONY: lib-libos32tilemap

# zlib inflate (展開のみ) — lib/zlib/ に無改変で取り込んだ zlib 公式ソース。
#   ゲスト側は展開だけを行うので deflate/gzip/crc32 は取り込んでいない。
#   -DNO_GZIP で gzip ストリーム対応を、-DZ_SOLO で libc 依存部を落としている。
#   Z_SOLO を付けると zlib が malloc を直接呼ばなくなるので、
#   アロケータは userland/lib/mgx/mgx_decode.c が z_stream に渡す。
#   詳細と出所は lib/zlib/README.OS32 を参照。
ZLIB_DIR = lib/zlib
ZLIB_INFLATE_OBJ = $(ZLIB_DIR)/inflate_prog.o $(ZLIB_DIR)/inffast_prog.o \
                   $(ZLIB_DIR)/inftrees_prog.o $(ZLIB_DIR)/adler32_prog.o \
                   $(ZLIB_DIR)/zutil_prog.o

$(ZLIB_DIR)/%_prog.o: $(ZLIB_DIR)/%.c
	$(CC) $(PROGRAM_FLAGS) -Os -DNO_GZIP -DZ_SOLO -I$(ZLIB_DIR) -c $< -o $@

ALL_LIB_OBJ += $(ZLIB_INFLATE_OBJ)

# vendored zlib は OS32 のライブラリと混ぜず独立したアーカイブにする
# (出所とライセンスの見通しのため。lib/zlib/README.OS32 を参照)
$(LIBDIR)/libzinflate.a: $(ZLIB_INFLATE_OBJ)
	@rm -f $@
	$(AR) rcs $@ $^

# libos32mgx — MGX (漫画専用グレースケール画像形式) デコーダ
$(eval $(call DEFINE_LIB,libos32mgx,userland/lib/mgx,,))
LIBMGX_OBJ = $(LIBDIR)/libos32mgx.a $(LIBDIR)/libzinflate.a

# libos32snd — サウンドライブラリ
LIBSND_ARCHIVE_OBJ = userland/lib/snd/libos32snd.o
LIBSND_OBJ = $(LIBDIR)/libos32snd.a

$(LIBDIR)/libos32snd.a: $(LIBSND_ARCHIVE_OBJ)
	@rm -f $@
	$(AR) rcs $@ $^

userland/lib/snd/libos32snd.o: userland/lib/snd/libos32snd.c userland/lib/snd/libos32snd.h
	$(CC) $(PROGRAM_FLAGS) $(INC_libos32snd) -c $< -o $@

ALL_LIB_OBJ += $(LIBSND_ARCHIVE_OBJ)

# libos32md — Markdownパーサー + レンダラー
userland/lib/md/md_parse.o: userland/lib/md/md_parse.c userland/lib/md/libos32md.h
	$(CC) $(PROGRAM_FLAGS) $(INC_libos32md) -c $< -o $@

userland/lib/md/md_render.o: userland/lib/md/md_render.c userland/lib/md/md_render.h userland/lib/md/libos32md.h
	$(CC) $(PROGRAM_FLAGS) $(INC_libos32md) $(INC_libos32filer) -c $< -o $@

MDLIB_ARCHIVE_OBJ = userland/lib/md/md_parse.o userland/lib/md/md_render.o
MDLIB_OBJ = $(LIBDIR)/libos32md.a
ALL_LIB_OBJ += $(MDLIB_ARCHIVE_OBJ)

$(LIBDIR)/libos32md.a: $(MDLIB_ARCHIVE_OBJ)
	@rm -f $@
	$(AR) rcs $@ $^

# libos32filer — GFXファイラーライブラリ + TVRAM描画
userland/lib/filer/filer_core.o: userland/lib/filer/filer_core.c userland/lib/filer/libos32filer.h
	$(CC) $(PROGRAM_FLAGS) $(INC_libos32filer) -c $< -o $@

userland/lib/filer/filer_draw.o: userland/lib/filer/filer_draw.c userland/lib/filer/filer_draw.h
	$(CC) $(PROGRAM_FLAGS) $(INC_libos32filer) -c $< -o $@

# filer_core.o (GFX版, filer_*) と filer_draw.o (TVRAM版, fldraw_*) を
# 1 つのアーカイブに入れる。両者はシンボルが重ならないので、ld は
# 参照された方のメンバだけを引き込む。
FILER_ARCHIVE_OBJ = userland/lib/filer/filer_core.o userland/lib/filer/filer_draw.o
FILER_OBJ = $(LIBDIR)/libos32filer.a
FILER_DRAW_OBJ = $(LIBDIR)/libos32filer.a
ALL_LIB_OBJ += $(FILER_ARCHIVE_OBJ)

$(LIBDIR)/libos32filer.a: $(FILER_ARCHIVE_OBJ)
	@rm -f $@
	$(AR) rcs $@ $^

# === ライブラリ全ビルドターゲット ===
# 全ライブラリのアーカイブを作る。SDK 切り出し時はこの LIBDIR がそのまま
# sysroot の lib/ になる。
ALL_LIB_ARCHIVES = $(LIBDIR)/libos32math.a \
                   $(LIBDIR)/libos32gfx.a \
                   $(LIBDIR)/libos32db.a \
                   $(LIBDIR)/libos32asset.a \
                   $(LIBDIR)/libos32save.a \
                   $(LIBDIR)/libos32snd.a \
                   $(LIBDIR)/libos32mgx.a \
                   $(LIBDIR)/libzinflate.a \
                   $(LIBDIR)/libos32ui.a \
                   $(LIBDIR)/libos32filer.a \
                   $(LIBDIR)/libos32md.a \
                   $(LIBDIR)/libos32tilemap.a \
                   $(LIBDIR)/libos32input.a \
                   $(LIBDIR)/libos32ecs.a \
                   $(LIBDIR)/libos32cfg.a \
                   $(LIBDIR)/libos32host.a

libs: $(ALL_LIB_ARCHIVES)

# === ライブラリクリーン ===
clean-libs:
	rm -f $(LIBDIR)/*.a
	rm -f userland/lib/math/*.o
	rm -f userland/lib/input/*.o userland/lib/asset/*.o
	rm -f userland/lib/ecs/*.o userland/lib/ui/*.o
	rm -f userland/lib/gfx/*.o userland/lib/gfx/asm/*.o
	rm -f userland/lib/gfx/draw/*.o userland/lib/gfx/text/*.o userland/lib/gfx/geom/*.o
	rm -f userland/lib/tilemap/*.o
	rm -f userland/lib/db/*.o userland/lib/snd/*.o
	rm -f userland/lib/md/*.o userland/lib/filer/*.o
	rm -f userland/lib/save/*.o
	rm -f userland/lib/mgx/*.o lib/zlib/*.o
	rm -f userland/lib/cfg/*.o
	rm -f userland/lib/host/*.o

.PHONY: libs clean-libs
