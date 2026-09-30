"""filer の CopyJob を、KAPI を差し替えたホスト上で動かす挙動試験。

実 FS・実エミュレータには触れない。`userland/rust/filer/src/model.rs` を
**そのまま**取り込み、open/close/read/write/unlink だけを差し替える。

背景: 中断 (ESC / 閉じる / Session Quit / タイマ失敗) では abort() が fd を
閉じるだけで、自分が作った途中までのコピー先を消していなかった
(2026-09-10 のレビュー指摘)。
"""
import pathlib
import re
import subprocess
import sys
import tempfile

ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools/tests"))
import os32api_host  # noqa: E402

MODEL = ROOT / "userland/rust/filer/src/model.rs"
TESTS = ROOT / "userland/rust/filer/host/model_tests.rs"

if __name__ == "__main__":
    with tempfile.TemporaryDirectory(prefix="os32-filer-host-") as tmp:
        out = pathlib.Path(tmp)
        rlib = os32api_host.build(out)
        root = out / "model_root.rs"
        root.write_text(
            "#![allow(dead_code)]\n"
            f'#[path="{MODEL}"] pub mod model;\n'
            f'#[path="{TESTS}"] mod model_tests;\n'
        )
        subprocess.run(
            [
                "rustc",
                "--edition=2021",
                "--test",
                str(root),
                "-L",
                str(out),
                "--extern",
                "os32api=" + str(rlib),
                "-o",
                str(out / "filer-model-tests"),
            ],
            cwd=ROOT,
            check=True,
        )
        print("COMPILE PASS", flush=True)
        rc = subprocess.run(
            [str(out / "filer-model-tests"), "--test-threads=1"], cwd=ROOT
        ).returncode
        print(f"EXIT {rc}", flush=True)
        sys.exit(rc)
