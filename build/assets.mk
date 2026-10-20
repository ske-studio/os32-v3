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
#               fonts/*.kcgfont, fep*.db, fep.dic
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

FONT_DIR   = assets/fonts
IPADIC_DIR = assets/ipadic

# --- フォントの取得 (ビルド時ダウンロード) ---
# 家族ごとに 1 回の実行で揃う (grouped target `&:`、GNU make 4.3 以上)。揃っていて
# SHA-256 が合えば fetch_fonts.py は何も聞かず mtime だけ更新する。
#
# IPAex: 未取得ならライセンス全文を出して同意を聞く (端末が無ければ
# OS32_ACCEPT_IPA_LICENSE=1 で非対話に同意)。失敗のときは案内を make の失敗文の
# 直前にもう 1 度出す。
IPAEX_TTFS = $(FONT_DIR)/ipaexg.ttf $(FONT_DIR)/ipaexm.ttf
# GUI の標準 set (T7b-b3、配備する)。名前は fetch_fonts.py の取得物、ゲスト側の
# 置き場は font crate の paths.rs (FONT_DIR / FONT_JA / FONT_LATIN) が正典 [C4]。
INCONSOLATA_FILES = $(FONT_DIR)/Inconsolata-Regular.ttf $(FONT_DIR)/Inconsolata-OFL.txt
JFDOT_FILES = $(FONT_DIR)/JF-Dot-Shinonome16.ttf $(FONT_DIR)/JF-Dot-Shinonome16-LICENSE.txt
GUI_FONT_FILES = $(INCONSOLATA_FILES) $(JFDOT_FILES)
FONT_TTFS = $(IPAEX_TTFS) $(FONT_DIR)/Inconsolata-Regular.ttf $(FONT_DIR)/JF-Dot-Shinonome16.ttf

$(IPAEX_TTFS) &: tools/fetch_fonts.py
	@python3 tools/fetch_fonts.py $(FONT_FETCH_FLAGS) || { rc=$$?; \
	  if [ $$rc -eq 2 ]; then \
	    echo "*** フォント未取得: \`make fonts\` でライセンスに同意して取得するか、OS32_ACCEPT_IPA_LICENSE=1 を付けて非対話で同意" >&2; \
	  else \
	    echo "*** フォント取得失敗 (tools/fetch_fonts.py rc=$$rc): 上の理由を見る。オフラインなら zip を手で置いて \`python3 tools/fetch_fonts.py --zip PATH\`" >&2; \
	  fi; exit $$rc; }

# Inconsolata (OFL) / JF ドット東雲16 (東雲フォントライセンス) は同意の確認なし。
# オフラインでは FONT_FETCH_INCONSOLATA_FLAGS="--zip PATH --ofl PATH" /
# FONT_FETCH_JFDOT_FLAGS="--zip PATH" で手で取った配布物を渡す。
$(INCONSOLATA_FILES) &: tools/fetch_fonts.py
	@python3 tools/fetch_fonts.py --family inconsolata $(FONT_FETCH_INCONSOLATA_FLAGS) || { rc=$$?; \
	  echo "*** Inconsolata 取得失敗 (tools/fetch_fonts.py rc=$$rc): オフラインなら FONT_FETCH_INCONSOLATA_FLAGS=\"--zip PATH --ofl PATH\"" >&2; exit $$rc; }

$(JFDOT_FILES) &: tools/fetch_fonts.py
	@python3 tools/fetch_fonts.py --family jfdot-shinonome16 $(FONT_FETCH_JFDOT_FLAGS) || { rc=$$?; \
	  echo "*** JF ドット東雲16 取得失敗 (tools/fetch_fonts.py rc=$$rc): オフラインなら FONT_FETCH_JFDOT_FLAGS=\"--zip PATH\"" >&2; exit $$rc; }

fonts: $(FONT_TTFS) $(GUI_FONT_FILES)
	@echo "=== フォント (IPAex 2 本 + GUI 標準 set、$(FONT_DIR)/) ==="

# 取得した ttf と同意の記録は `make clean` (clean-assets) では消さない (ユーザー指示
# 2026-09-30: ライセンスの確認は初回ビルドの 1 回だけ)。消すのはこの明示の目標だけ。
fonts-clean:
	rm -f $(FONT_TTFS) $(GUI_FONT_FILES) $(FONT_DIR)/*.part $(FONT_DIR)/.license_accepted

# --- 旧ビットマップ形式: host 画面比較 fixture 専用 (配布しない) ---
# 本文用はゴシック。明朝は 16x16 だと細い横画が飛ぶ (CLAUDE.md の Known Gotchas)。
# ipaexg.ttf が無ければ上の規則で取得が走る。
$(FONT_DIR)/ipaexg16.kcgfont: $(FONT_DIR)/ipaexg.ttf tools/gen_font16.py
	python3 tools/gen_font16.py $< $@

# --- FEP (かな漢字変換) 辞書 ---
# M がゲストに載る既定。S/L はコスト閾値違いで、kernel/ime.c が
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
ASSETS_DEPLOYED = assets/fep.db $(GUI_FONT_FILES)

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

# 取得したフォント (GUI_FONT_FILES) は消さない (fonts-clean だけが消す)。
clean-assets:
	rm -f $(filter-out $(GUI_FONT_FILES),$(ASSETS_ALL))

.PHONY: fonts fonts-clean assets-deployed assets-all clean-assets
