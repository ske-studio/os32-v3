# PowerMac の root と部品の pair

KB_EXEC の ELF は低位のカーネルと高位の読み取り専用 root 枠を持つ。
pair は LE u32 の entry、低位 LOAD の VA・filesz・memsz、高位 LOAD の VA・filesz・memsz、
枠 VA・枠長、記録 VA・記録長の順に、両 LOAD の file-backed bytes を低位から続けた CRC32。
低位の 16 B の記録と高位枠は同じ長さのゼロ列として計算する。
記録は LE u32 の pair、生 ISO CRC32、圧縮長、生長。部品には pair を書いて部品 CRC を更新する。
entry で照合し、生 ISO の照合後にだけ getter が pair を渡す。

高位 LOAD は `MEM_BOOT_STAGING_BASE` に置き、圧縮長とページ境界に丸めた枠長の両方を
`MEM_BOOT_STAGING_SIZE` 以下とする。未展開の root がある時は共通の
`memory_boot_init` が集積域全体を boot 所有者に予約する。展開・検証と登録の試行後、
恒等写像を保ったまま集積域を池に返す。登録した展開先は rd0 が保持し、登録前の失敗では返す。

PPC の pack は pair 仮値 0 の部品を生成し、root_pair が最終 ELF の pair を結び直す。
L2 の受入は link-sh の root に二種類の部品を入れ、共通 loader で読み込む。
先頭頁の返却・上書き後の export、異なる配置での実行、同じ本文頁への別命令の再配置を確認する。
32 MiB を越えた提供関数へ生成 stub を通してポインタと可変長整数引数を渡し、戻り値を照合する。
別 ELF 用の部品を入れた媒体は ISO CRC が正しくても init 前に pair 不一致で拒否する。

L2 の通常の link-sh 製品にはまだ FEP・KCG からの呼び出しがなく、リンク時に loader が除去される。
常駐時の計測では製品と同じオブジェクトと root を使い、測定用リンクに限って
`module_load`・`module_start`・`module_exports`・`module_abort` を `-u` で保持した。

| link-sh（loader 常駐時） | 実測 | リンカの予算 | 余り |
|---|---:|---:|---:|
| 常駐サイズ（開始 0x100000、resident_end 0x317000） | 0x217000 | 0x275000 | 0x5E000（376 KiB） |
| chain 末尾 | 0x382000 | 0x3E0000 | 0x5E000（376 KiB） |
