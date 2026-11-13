FONT_DIR   = assets/fonts

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
# TASK_FONTPERF ⓐ: 東雲 16 の描き済みの字形の束 (.g16) とその先読み一覧。取得物ではなく
# 派生物 (取得した TTF の EBDT 16px strike からホストで生成、決定的、1〜2 s)。追跡しない。
# 置き場は font crate の paths.rs (FONT_JA_G16) が正典 [C4]。

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


# PPC RAM root product: original 16px dots, uncompressed KCG1.
$(FONT_DIR)/shino16.kcgfont: $(FONT_DIR)/JF-Dot-Shinonome16.ttf tools/gen_font16.py tools/gen_g16.py
	python3 tools/gen_font16.py --ebdt $< $@
