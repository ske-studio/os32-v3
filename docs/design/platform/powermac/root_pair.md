# PowerMac の root と部品の pair

KB_EXEC の ELF は低位のカーネルと高位の読み取り専用 root 枠を持つ。
pair は LE u32 の entry、低位 LOAD の VA・filesz・memsz、高位 LOAD の VA・filesz・memsz、
枠 VA・枠長、記録 VA・記録長の順に、両 LOAD の file-backed bytes を低位から続けた CRC32。
低位の 16 B の記録と高位枠は同じ長さのゼロ列として計算する。
記録は LE u32 の pair、生 ISO CRC32、圧縮長、生長。部品には pair を書いて部品 CRC を更新する。
entry で照合し、生 ISO の照合後にだけ getter が pair を渡す。
