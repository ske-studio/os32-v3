
## Selected kernel compile measurement

`CROSS_DIR=/home/hight/opt/cross HOST32_RUNNERS=qemu TMPDIR=<disk-directory>` で
`python3 -B tools/ppc/check_compile.py --source-profile selected --accept --json`
を実行する。PPC_CROSS_DIR の既定は `/home/hight/opt/cross-ppc`。
新しい隔離 snapshot で make の実規則から argv を取得し、選択した全 C/GAS を試す。
必須 7 本と selected_success.json の初回成功集合の減少、ELF32/BE/EM_PPC 以外、
成功 TU の .d 不在、非選択ヘッダ依存と known_header_dependencies.json の完全一致ペアの差分は非 0。
既知依存が減った場合は一覧も縮めて再検査する。コンパイラ不在は通常 SKIP、--accept は失敗。
残る共通候補のコンパイル失敗は各 TU の error/classification に記録する。
この入口は NOTEST（実クロスコンパイラが必要）。host CHECK は query と監査を検査し、
実コンパイルの代わりにはしない。旧 x86 一覧による ARM 計測は別系列として維持する。
