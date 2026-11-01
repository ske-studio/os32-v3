# tools/tests/splash_wire_host.c — 結合試験ハーネス

状態: **現行 (2026-11-01)**。

実際のboot/PC98描画/画像所有/OSRIローダー/共通再生をリンクする。VFSとVRAM転送だけを偽物にする。生成OSRIの37byte短読、CRC破損、EOF、イベント拒否、同期/時計故障、IF=0、GUI希望値4種と共通期限を検査する。
