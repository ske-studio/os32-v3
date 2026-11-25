"""Host fixtures for separating a consumer name from its build-tree source."""
import csv
import subprocess
import sys
import tempfile
from pathlib import Path

import external_paths
from external_paths import ROOT, external, inventory, judgment, report


def cases():
    assert judgment("arch/x86/license", "配備元")[0] == "変わる"
    assert judgment("/sys/license", "媒体内")[0] == "変わらない"
    assert judgment("tools/mixed.py", "操作手順")[0] == "未確定"
    assert judgment("../build/sdk/lib", "SDK入力")[0] == "変わらない"
    assert judgment("../private/lib", "SDK入力")[0] == "未確定"
    with tempfile.TemporaryDirectory() as directory:
        tree = Path(directory)
        (tree / "Makefile").write_text("OS32_SDK ?= ../build/sdk\nx = $(OS32_SDK)/crt/crt0.o\n")
        (tree / "test.c").write_text('#include "../kernel/private.h"\n')
        subprocess.run(["git", "init", "-q", tree], check=True)
        subprocess.run(["git", "-C", tree, "add", "."], check=True)
        # write-tree gives git grep a snapshot without committing.
        ref = subprocess.check_output(["git", "-C", tree, "write-tree"], text=True).strip()
        paths = {}
        external(lambda who, path, kind, source: paths.update({path: kind}), tree, "apps", ref)
        assert paths["../build/sdk/crt/crt0.o"] == "SDK入力"
        assert paths["../kernel/private.h"] == "木外参照"


if __name__ == "__main__":
    cases()
    if "--mutant" in sys.argv:
        external_paths.ROOT = Path(sys.argv[-1])
    root = external_paths.ROOT
    with tempfile.TemporaryDirectory() as directory:
        tree = Path(directory)
        subprocess.run(["git", "init", "-q", tree], check=True)
        (tree / "usage.md").write_text("os32-v3/exec/appslot.h and os32-v3/tools/nhd_deploy.py")
        subprocess.run(["git", "-C", tree, "add", "usage.md"], check=True)
        subprocess.run(["git", "-C", tree, "-c", "user.name=test", "-c", "user.email=test@localhost",
                        "-c", "commit.gpgsign=false", "commit", "-qm", "fixture"], check=True)
        data = inventory(root / "build/out/deploy-set.json", [("np21w-src", tree)])
        assert any(row[0] == "np21w-src" for row in data)
        assert any(row[:2] == ("ci_fetch", "SHA256SUMS") for row in data)
        assert any(row[0] == "deploy-set" and row[2] == "媒体内" for row in data)
        report(data, tree)
        def table(name):
            with (tree / name).open() as f:
                return list(csv.DictReader(f, delimiter="\t"))
        headers = {"sys/kern/exec/appslot.h", "sys/include/app_ids.h", "sys/include/memmap.h", "sys/include/con_sink.h",
                   "sys/kern/module/module_runtime.h", "sys/kern/module/module_schema.h"}
        assert headers <= {r["パス"] for r in table("paths.tsv") if r["相手"] == "pc98emu" and
                           r["判定"] == "変わる" and "tools/np21w_mcp/gui.py:" in r["参照元"]}
        notice = next(r for r in table("notify.tsv") if r["知らせる相手"] == "pc98emu" and r["判定"] == "変わる")
        assert headers <= set(notice["照合するパス"].split(" | "))
        for r in table("waves.tsv"):
            if r["相手"] == "pc98emu" and r["波"].startswith("W"):
                assert headers <= set(r["照合候補"].split(" | "))
        for path in ("tools/nhd_deploy.py", "tools/hostdrv_deploy.py"):
            assert {row[0] for row in data if row[1:3] == (path, "操作手順")} == {"NP21/W 予行"}
    if "--mutate" in sys.argv:
        source = Path(__file__).with_name("external_paths.py").read_text()
        mutants = [('literal = node.right', 'literal = None'),
                   ('who = "NP21/W 予行"', 'who = "実機ノート"'),
                   ('return UNKNOWN,', 'return KEEP,'),
                   ('kind == "SDK入力" and', 'kind == "SDK入力" or'),
                   ('or not path.startswith("../")', 'and not path.startswith("../")')]
        with tempfile.TemporaryDirectory() as directory:
            tree = Path(directory)
            test = tree / "test_external_paths.py"
            test.write_text(Path(__file__).read_text())
            for old, new in mutants:
                assert source.count(old) == 1
                (tree / "external_paths.py").write_text(source.replace(old, new))
                result = subprocess.run([sys.executable, "-B", test, "--mutant", str(ROOT)], capture_output=True)
                assert result.returncode and any(error in result.stderr for error in
                                                 (b"AssertionError", b"KeyError")), result.stderr
                print("RED", old)
    print("PASS external paths")
