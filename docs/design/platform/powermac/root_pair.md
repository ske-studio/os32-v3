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
