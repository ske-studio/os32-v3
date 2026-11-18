# REVIEW r3 (Opus 5.5 第 2 回、2026-11-18) とユーザー決定
第 1 回の指摘はすべて直った。sound・pcm 以外は確定してよい。
ユーザー決定 (2026-11-18): **新しい口だけ** — PowerMac では昔からの FM の KAPI 6 口 (fm_note_on・fm_play_mml など) は「無し (E)」のまま。アプリは sndl_chip_* を使い、それが AV HAL のソフトウェア FM 音源 (合成器) → PCM で鳴る (docs/decisions/sound/AV_HAL_DESIGN.md の「確定の後の追記 2」どおり)。PowerPC のアプリは全部作り直すので旧バイナリの互換は要らない。
| ID | 重さ | 指摘 | 直し |
|---|---|---|---|
| N1 | P1 | sound の行の「再ビルド用 SDK の共通互換口」は確定設計に無い。fm.c の変換 (音色表・fnumber_table・SSG) を userland に書くと x86 と重複 | ユーザー決定 (a) により削る。旧 6 口は E |
| N2 | P1 | 未決 0 は早い (旧バイナリを根拠にしていた) | ユーザー決定で決着。根拠を「PowerPC は全部再ビルド、新しいアプリは sndl_chip_*」に |
| N3 | P2 | 旧 6 口を「D→B」としながら「HW 専用、不在時は無作用」 | E に書き直す。sndl_chip_* の SOFT の道 (合成器 → PCM) は AV HAL の段で両 arch 共通 |
| N4 | P2 | §3 に音の段が無い。8 行目の「新装置を先行開発する予定は置かない」が pcm の行と食い違う | §3 に 1 行: AV HAL の待つ段 (合成器の照らし直し → os32_opna A〜D → L2、IS → L1 → L3 → L6) の後に、PowerMac の音声装置の名前と PCM の後端のファイル (PowerPC の担当)。8 行目の文言を直す |
| N5 | P3 | pcm の行の照合に合成器 (fmvoice.c) の確かめ方が入っている | 合成器の確かめ方は sound の行へ、pcm の行は pcm_module.c の窓口の定義元の一致に |
