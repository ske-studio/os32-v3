"""Host fixtures for separating a consumer name from its build-tree source."""
import subprocess
import sys
import tempfile
from pathlib import Path

from external_paths import ROOT, external, inventory, judgment


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
    if "--mutant" not in sys.argv:
        data = inventory(ROOT / "build/out/deploy-set.json", [])
        assert any(row[:2] == ("ci_fetch", "SHA256SUMS") for row in data)
        assert any(row[0] == "deploy-set" and row[2] == "媒体内" for row in data)
    if "--mutate" in sys.argv:
        source = Path(__file__).with_name("external_paths.py").read_text()
        mutants = [('return UNKNOWN,', 'return KEEP,'),
                   ('kind == "SDK入力" and', 'kind == "SDK入力" or'),
                   ('or not path.startswith("../")', 'and not path.startswith("../")')]
        with tempfile.TemporaryDirectory() as directory:
            tree = Path(directory)
            test = tree / "test_external_paths.py"
            test.write_text(Path(__file__).read_text())
            for old, new in mutants:
                assert source.count(old) == 1
                (tree / "external_paths.py").write_text(source.replace(old, new))
                result = subprocess.run([sys.executable, "-B", test, "--mutant"], capture_output=True)
                assert result.returncode and any(error in result.stderr for error in
                                                 (b"AssertionError", b"KeyError")), result.stderr
                print("RED", old)
    print("PASS external paths")
