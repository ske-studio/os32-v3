あなたは OS32 (PC-9821 用 32bit OS、PowerPC (PowerMac G4) へ移植中) の設計者 (astra) です。作業木 /home/hight/wt/os32-v3/ppc-kapi-fill (main a76398a35) は読むだけで、ソースは変えない。書くのは ~/os32-tmp/run/ppc-kapi-fill/DESIGN.r1.md だけ。

何の計画か: **PowerPC の KAPI の「提供者無し」190 本を埋める計画** (棚卸しと段の切り方)。KAPI は sdk/kapi.json の 298 本。PowerPC では sdk/kapi_target_ppc.py の PROVIDER_GROUPS (26 の組、計 190 本) が「PowerMac に提供者が無い」として空の wrap に繋いでいる。アプリが PowerPC で動くかは主にここで決まる。
ユーザー決定 (2026-11-18、原文): 「KAPI の提供者無し 190 本を埋める計画を、先に作ってよいか。先に。ただし、x86 と重複するような実装はだめです」。

**最重要の決まり — x86 と重複する実装をしない:**
- 機能の本体 (OS としての処理) は共通の置き場に 1 本だけ置き、両 arch が同じソースを使う。PowerPC 用に同じ処理を書き直さない、写さない。
- PowerPC が作るのは、HAL の口・契約 (include/<名>_contract.h・固定名 platform_<名>.h・arch_<名>.h) の後ろの、機種・装置・CPU の後端だけ。
- x86 の実装が x86・PC-98 の置き場にあるが中身が機種に依らない物は、写さずに「共通の置き場へ移す段」(本線の担当、docs/SRC_MOVE.md の手順) を計画に入れ、その後に PowerPC で結線する。
- HAL の口が無い物は、「口を作る段」(共通、本線と協調) と「後端の段」に分ける。
- 各組で「重複しないこと」の確かめ方を書く (例: 同じ関数名・同じ処理が arch/ppc・platform/powermac に新しくできていないか、共通のソースが両方の像のリンク一覧に入っているか — tools/check_layers.py・build/select.mk・make ppc-ci-image の common-compile で)。
前提 (議論しない): シングルタスク・協調切替。整数演算が第一 (POLICY_DEV §2)。HAL 中心期 (POLICY_DEV §1): 装置は HAL の口の実装として作る、arch の拡張と HAL と関係の薄い新装置は着手しない。層の規則 (docs/design/tools/check_layers.md §3・§4・§8、.claude/skills/os32-arch-sep/SKILL.md)。KAPI の ABI ([ABI]、docs/KAPI_SPEC.md §3-1、スキル .claude/skills/os32-kapi-add/SKILL.md) — 表の結線を変えるだけで KAPI の形 (kapi.json) は変えないのが基本。**操作は各 arch の標準 OS が基準** (ユーザー 11-15)。
トークン節約が最優先: 下に挙げた所だけ読み、実コードは grep で行を絞る (全文読みはしない)。docs/archive/ は読まない。
読む所:
- sdk/kapi_target_ppc.py 全部 (PROVIDER_GROUPS・ZERO_SLOTS など)、sdk/kapi.json (関数名と組の対応だけ grep で)、生成の道筋 (sdk/gen_kapi.py で PROVIDER_GROUPS を使う所)
- 各組の提供者の今の置き場: KAPI の関数ごとに、x86 の像で実体がどのファイルか (kapi/kapi_generated.c の wrap → 呼ぶ関数 → そのファイル) を grep で。置き場が共通 (kernel/・fs/・gfx/・input/・net/・lib/・mm/・exec/) か、機種 (platform/pc98/)・装置 (drivers/・hwdev/) か、arch か
- PowerPC にある後端: platform/powermac/ (display_provider・console・fbcon・input_usb・usb_*・hostfs・link・gmac・escc・pci_config・root_*)、drivers/video/of_fb/、kernel/console/fbcon.c、drivers/video/kcg/ (KCG1)、build/platform/powermac.mk の所有者集合、build/select.mk の C_COMMON_PC98_PENDING (PowerPC の像に入っていない共通のソース 44 本)
- 計画と票: docs/tasks/ppc/TASK_PPC_SEP.md の「残件」の表 (#4 exec のフック 14 本、#5 contract の装置の名前、#6 C_COMMON_PC98_PENDING、#7 KAPI の名前に装置が出る関数)、docs/tasks/v3/ の HAL の票 (TASK_INPUT_HAL・TASK_AV_HAL・TASK_IO_TIER の題と状態行だけ)、docs/decisions/ppc/PPC_SQLITE_STACK_DESIGN.md (db の組の前提、段 S)、~/os32-tmp/run/ppc-reset/DESIGN.r1.md (再起動の設計の起草中の版。sys の組の sys_reboot と、後で clock の rtc_read を同じ PMU/Cuda の装置で載せる計画)
書く物: DESIGN.r1.md (日本語、200〜300 行、表と箇条書き中心)。章立て:
1. 実物の確認 (前提と実コードの食い違いを file:line で)。統合の判断 3 点 (POLICY_DEV §1)。
2. 棚卸しの表: 26 の組ごとに 1 行 — 本数、x86 での実体の置き場 (共通 / 機種 / 装置 / arch)、PowerPC で埋める道 (A: 共通の実体をそのまま結線するだけ / B: 共通の口があり PowerPC の後端を作る / C: 共通へ移す段が先 (本線) / D: 口を作る段が先 / E: PowerMac に無くて正しい — 理由)、要る後端、依存する段、重複しないことの確かめ方。組の中で関数ごとに道が分かれるなら分けて書く。
3. 道ごとの段の切り方 (1 段 = 半日〜1 日・1 ファイル = 1 段)、順 (アプリが動くために先に要る物から — 例えば path・system_info・shm・port・pointer・console_font・gfx)、本線の担当の段と PowerPC の担当の段の分け方、並列にできる所。
4. KAPI の表 (sdk/kapi_target_ppc.py) の変え方と、KAPI の指紋・ABI・x86 の生成物が変わらないことの確かめ方 (os32-kapi-add のどの手順が要るか)。
5. 受入: 組ごとに、PowerPC の QEMU (mac99、7450・7455) で何を観測すれば「埋まった」と言えるか (アプリ・shell のコマンド・host 試験)、変異。x86 の退行が無いことの確かめ (build_compare)。
6. 未決 (ユーザー判断が要るものだけ、推奨付き)。
返答本文は「書いた / 未決の数 / 実物との食い違いの数」の 3 行だけ。
