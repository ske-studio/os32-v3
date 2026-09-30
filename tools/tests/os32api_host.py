"""os32api を**ホストで動く rlib** に組み立てる (試験専用)。

`sdk/rust/os32api/src` の実物から `KernelAPI` とアクセサを取り出し、全スロットを
「呼ばれたら panic するスタブ」で埋めた `mock_api()` を生やす。試験は
`os32api::api_ptr()` で必要なスロットだけ差し替える。

gshell の `host/integration.py` と filer の試験が同じ手を使うので、生成規則は
ここ 1 か所に置く (KAPI の形はここでは書かない — 実ソースから読む)。
"""
from pathlib import Path
import re
import subprocess

ROOT = Path(__file__).resolve().parents[2]
SDK = ROOT / "sdk/rust/os32api/src"


def build(out_dir):
    """`out_dir` に libos32api.rlib を作ってそのパスを返す。"""
    out = Path(out_dir)
    out.mkdir(parents=True, exist_ok=True)
    source = (SDK / "lib.rs").read_text()
    abi = (SDK / "kapi_generated.rs").read_text()
    fields = re.findall(
        r"pub (\w+): (.+),",
        abi.split("pub struct KernelAPI {")[1].split("\n}")[0],
    )
    init = []
    for name, ty in fields:
        value = (
            "core::mem::transmute::<*const (), " + ty + ">(unexpected as *const ())"
            if ty.startswith("unsafe ")
            else "core::mem::zeroed()"
        )
        init.append(name + ": " + value)
    adapter = (
        f'''#![allow(dead_code)]
#[path="{SDK / 'kapi_generated.rs'}"] pub mod kapi_generated;
pub use kapi_generated::*;
#[path="{SDK / 'gui/mod.rs'}"] pub mod gui;
#[path="{SDK / 'cfg.rs'}"] pub mod cfg;
use core::cell::UnsafeCell;
'''
        + source[
            source.index("struct ApiHolder") : source.index(
                "/* ================================================================ */",
                source.index("pub unsafe fn api_ptr"),
            )
        ]
        + source[source.index("pub mod gfx {") :]
    )
    adapter += (
        '\nunsafe extern "C" fn unexpected() { panic!("unmocked KAPI reached"); }\n'
        "pub fn mock_api() -> KernelAPI { unsafe { KernelAPI {"
        + ",\n".join(init)
        + "} } }\n"
    )
    (out / "host_api.rs").write_text(adapter)
    rlib = out / "libos32api.rlib"
    subprocess.run(
        [
            "rustc",
            "--edition=2021",
            "--crate-type=rlib",
            "--crate-name=os32api",
            str(out / "host_api.rs"),
            "-o",
            str(rlib),
        ],
        cwd=ROOT,
        check=True,
    )
    return rlib
