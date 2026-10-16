"""Compile filer's actual launch/modal/quit methods with host GUI boundaries.

The state and its initializer also come from lib.rs, so a stale launch latch is
exercised without duplicating the policy in the fixture. No emulator or real FS.
"""
import argparse
import pathlib
import re
import subprocess
import tempfile

import os32api_host

ROOT = pathlib.Path(__file__).resolve().parents[2]
FILER = ROOT / "userland/rust/filer"
SOURCE = FILER / "src/lib.rs"
TESTS = FILER / "host/launch_tests.rs"
METHODS = ("launch", "busy", "op_mkdir", "ask", "status_msg", "error",
           "win_id", "shutdown", "on_quit")


def method(src, name):
    # Methods end at the impl's four-space indentation, including nested blocks.
    return re.search(r"^    fn " + name + r"\(.*?^    }", src,
                     re.M | re.S).group()


def build_and_run(out, src, api):
    state = src[src.index("struct Filer {"):src.index("\nimpl Filer {")]
    init = src[src.index("        let mut f = Filer {"):]
    init = init[:init.index("\n        };") + len("\n        };")]
    init = init.replace("let mut f =", "let f =").replace("Some(win)", "None")
    constants = src[src.index("const WIN_X:"):src.index("struct Filer {")]
    harness = (
        '#![allow(dead_code, unused_imports)]\n'
        f'#[path="{FILER / "src/model.rs"}"] mod model;\n'
        f'#[path="{FILER / "src/fmt.rs"}"] mod fmt;\n'
        f'#[path="{FILER / "host/model_tests.rs"}"] mod model_tests;\n'
        'use model::*;\nuse os32api::gui::types::Rect;\n'
        'use os32api::gui::proto::*;\n'
        + constants + state
        + 'impl Filer {\nfn fixture() -> Self {\n'
        'let (tree_lb, right_pad, path_pad) = (0, 0, 0);\n'
        + init + '\nf\n}\n'
        + '\n'.join(method(src, name) for name in METHODS)
        + '\n}\n'
        f'include!("{TESTS}");\n'
    )
    root = out / "launch_root.rs"
    root.write_text(harness)
    exe = out / "launch-tests"
    subprocess.run(["rustc", "--edition=2021", "--test", str(root), "-L",
                    str(out), "--extern", "os32api=" + str(api), "-o", str(exe)],
                   check=True, cwd=ROOT)
    return subprocess.run([str(exe), "--test-threads=1"], cwd=ROOT).returncode


def main():
    args = argparse.ArgumentParser()
    args.add_argument("--mutate", action="store_true")
    mutate = args.parse_args().mutate
    src = SOURCE.read_text()
    with tempfile.TemporaryDirectory(prefix="os32-filer-launch-") as tmp:
        out = pathlib.Path(tmp)
        api = os32api_host.build(out)
        rc = build_and_run(out, src, api)
        if mutate and rc == 0:
            mutations = (
                ("relaunch_blocked", "fn launch(&mut self, path: &[u8]) {",
                 "fn launch(&mut self, path: &[u8]) { return;"),
                ("operations_blocked", "fn busy(&mut self) -> bool {",
                 "fn busy(&mut self) -> bool { return true;"),
                ("quit_leaks_fds",
                 "fn shutdown(&mut self) {\n        self.timer = None;\n        self.job.abort();",
                 "fn shutdown(&mut self) {\n        self.timer = None;"),
            )
            for name, before, after in mutations:
                assert src.count(before) == 1, name
                if build_and_run(out, src.replace(before, after), api) == 0:
                    raise AssertionError("survived: " + name)
                print("KILLED " + name, flush=True)
    return rc


if __name__ == "__main__":
    raise SystemExit(main())
