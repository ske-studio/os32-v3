# ============================================================================
#  assets.mk — ホスト側アセットの生成
#
#  assets/ には性質の違う 3 種類が混ざっている:
#
#    上流       第三者の配布物そのもの。再生成できないので git で追跡する。
#               ipadic/*.csv
#               例外: fonts/*.ttf とそのライセンス文は追跡せず、ビルド時に
#               上流の配布から tools/fetch_fonts.py が取る (ユーザー決定
#               2026-09-30。IPAex は初回にライセンスの同意が要る — 下の `fonts`)。
#    設定       人が書くもの。git で追跡する。
#               filetypes, profile, profile_fdd, joyo_kanji.txt
#    派生物     上流から生成できるもの。git では追跡せず、ここで作る。
#               fonts/*.kcgfont, fonts/*.g16, fep*.db, fep.dic
#
#  派生物を追跡しないのは、履歴上位の巨大 blob の大半がこれだったため。
#  生成は数秒で終わる (FEP 辞書 5.8MB で 1 秒未満)。
#
#  TrueType フォントはリポジトリに持たず、取得物から配備する (ユーザー決定
#  2026-09-30)。ゲストに配るのは GUI の標準 set (T7b-b3): JF ドット東雲ゴシック16
#  と Inconsolata Regular の原本とライセンス文 (userland/deploy.yaml →
#  /usr/share/fonts/)。IPAex は配備しない (比較と font_test の任意入力だけ)。
#  以前あった JIS X 0208 のサブセット TTF (tools/subset_font.py) は廃止した。
# ============================================================================

include build/font-fetch.mk
IPADIC_DIR = assets/ipadic
GUI_FONT_G16 = $(FONT_DIR)/JF-Dot-Shinonome16.g16
PRELOAD16 = $(BUILD_OUT)/preload16.txt

# 先読み一覧 (設計 §4): JIS 1〜8 区 + UI の字 (tools/g16/ui_chars.txt、手で更新して
# コミット) + IPADIC のコスト順。
$(PRELOAD16): $(FONT_DIR)/JF-Dot-Shinonome16.ttf tools/g16/gen_preload16.py tools/gen_g16.py \
              tools/g16/ui_chars.txt $(wildcard $(IPADIC_DIR)/*.csv)
	@mkdir -p $(dir $@)
	python3 tools/g16/gen_preload16.py --ttf $< -o $@

# 束 (設計 §2)。EBDT が無い (rc 3)・fontTools が無い (rc 4)・cell に収まらない字 (rc 5) は止める。
$(GUI_FONT_G16): $(FONT_DIR)/JF-Dot-Shinonome16.ttf tools/gen_g16.py $(PRELOAD16)
	python3 tools/gen_g16.py --ttf $< --preload $(PRELOAD16) -o $@

fonts: $(FONT_TTFS) $(GUI_FONT_FILES) $(GUI_FONT_G16)
	@echo "=== フォント (IPAex 2 本 + GUI 標準 set、$(FONT_DIR)/) ==="

# 取得した ttf と同意の記録は `make clean` (clean-assets) では消さない (ユーザー指示
# 2026-09-30: ライセンスの確認は初回ビルドの 1 回だけ)。消すのはこの明示の目標だけ。
fonts-clean:
	rm -f $(FONT_TTFS) $(GUI_FONT_FILES) $(GUI_FONT_G16) $(PRELOAD16) $(FONT_DIR)/*.part $(FONT_DIR)/.license_accepted

# --- 旧ビットマップ形式: host 画面比較 fixture 専用 (配布しない) ---
# 本文用はゴシック。明朝は 16x16 だと細い横画が飛ぶ (CLAUDE.md の Known Gotchas)。
# ipaexg.ttf が無ければ上の規則で取得が走る。
$(FONT_DIR)/ipaexg16.kcgfont: $(FONT_DIR)/ipaexg.ttf tools/gen_font16.py
	python3 tools/gen_font16.py $< $@

# --- FEP (かな漢字変換) 辞書 ---
# M がゲストに載る既定。S/L はコスト閾値違いで、kernel/ime/ime.c が
# /db/fep_s.db /db/fep_l.db として参照する (現在は配備していない)。
assets/fep.db: $(IPADIC_DIR)/Noun.csv tools/fep_to_sqlite.py
	python3 tools/fep_to_sqlite.py -i $(IPADIC_DIR) -o $@

assets/fep_s.db: $(IPADIC_DIR)/Noun.csv tools/fep_to_sqlite.py
	python3 tools/fep_to_sqlite.py --size S

assets/fep_l.db: $(IPADIC_DIR)/Noun.csv tools/fep_to_sqlite.py
	python3 tools/fep_to_sqlite.py --size L

# --- 旧形式のバイナリ辞書 (現在ゲストは使っていない) ---
assets/fep.dic: $(IPADIC_DIR)/Noun.csv tools/fep_compiler.py
	python3 tools/fep_compiler.py -o $@

# --- 設定レジストリの初期値マスタ (settings.db) ---
# 正典は assets/settings/defaults.tsv (人が読み書きする側)。生成した DB は
# インストール媒体 (FDD / CD) だけが持ち、既存システムには tsv を通常配備して
# `cfg init` (S2) が明示的に生成する (TASK_S0 §3)。
# FORCE 依存 = ビルド毎に必ず作り直す (ユーザー決裁)。生成は決定的
# (同じ tsv + 同じ epoch → 同じバイト列) なので、毎回作っても媒体の中身は動かない。
SETTINGS_TSV = assets/settings/defaults.tsv
SETTINGS_DB  = $(BUILD_OUT)/settings.db

$(SETTINGS_DB): $(SETTINGS_TSV) tools/mk_settings_db.py FORCE
	@mkdir -p $(dir $@)
	python3 tools/mk_settings_db.py --tsv $(SETTINGS_TSV) --out $@

# 版 2 の試験 fixture (票 S4 の受入 G5 = VERSION 状態の gshell)。通常配備は
# /etc/settings.db* を保護してスキップするので、保護対象でない名前で配備し、
# ゲストで `cp /etc/settings.v2.fixture /etc/settings.db` して使う。
SETTINGS_V2_FIXTURE = $(BUILD_OUT)/settings.v2.fixture

$(SETTINGS_V2_FIXTURE): $(SETTINGS_TSV) tools/mk_settings_db.py FORCE
	@mkdir -p $(dir $@)
	python3 tools/mk_settings_db.py --tsv $(SETTINGS_TSV) --out $@ --schema-version 2

# 配備に必要な最小限。make all はこれに依存する (GUI の標準 font set は取得物)。
# 束 (GUI_FONT_G16) は派生物なので GUI_FONT_FILES (取得物) とは別に足し、clean-assets で消す。
ASSETS_DEPLOYED = assets/fep.db $(GUI_FONT_FILES) $(GUI_FONT_G16)

# 開発時に使うものも含めた全部。
# settings.db は通常配備の対象ではない (媒体だけが持つ) ので ASSETS_DEPLOYED
# には入れず、ここと `all` / 媒体ターゲットから引く。
ASSETS_ALL = $(ASSETS_DEPLOYED) \
             assets/fep_s.db assets/fep_l.db assets/fep.dic \
             $(SETTINGS_DB) $(SETTINGS_V2_FIXTURE)

# `all` からも直接引く (媒体ターゲットの依存とは別に、単体で必ず出来ていること)。
all: $(SETTINGS_DB) $(SETTINGS_V2_FIXTURE)

assets-deployed: $(ASSETS_DEPLOYED)
	@echo "=== 配備用アセット $(words $(ASSETS_DEPLOYED)) 件 ==="

assets-all: $(ASSETS_ALL)
	@echo "=== 派生アセット $(words $(ASSETS_ALL)) 件 ==="

# 取得したフォント (GUI_FONT_FILES) は消さない (fonts-clean だけが消す)。束と先読み一覧は消す。
clean-assets:
	rm -f $(filter-out $(GUI_FONT_FILES),$(ASSETS_ALL)) $(PRELOAD16)

.PHONY: fonts fonts-clean assets-deployed assets-all clean-assets

# Explicit experimental host encoder; never linked into a guest image.
HOSTCC ?= gcc
ENC2_CORE = $(BUILD_OUT)/osri-enc2-core
$(ENC2_CORE): tools/osri_enc2_core.c
	@mkdir -p $(dir $@)
	$(HOSTCC) -std=c11 -O3 -Wall -Wextra -Werror -Wvla -pthread $< -o $@

osri-enc2-core: $(ENC2_CORE)
.PHONY: osri-enc2-core
