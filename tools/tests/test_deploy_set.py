#!/usr/bin/env python3
"""T2h/h1: E3 配備期待集合、E1-a 全行照合、世代表の相互照合。

正常対照を必ず先に回す。--mutate は写しの Python モジュールだけを変える。
実配備・エミュレータには触れない。
"""
import argparse
import copy
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch
import zlib

ROOT = Path(__file__).resolve().parents[2]
parser = argparse.ArgumentParser()
parser.add_argument("--mutate", action="store_true")
parser.add_argument("--tools-dir", type=Path, default=ROOT / "tools")
args = parser.parse_args()
sys.path.insert(0, str(args.tools_dir))
import check_manifests as cm
import deploy_manifests as dm
import deploy_source_check as sc
import gen_deploy_set as gs

GENERATION = {"build_id": "a" * 64, "kernel_commit": "fixture",
              "kapi_version": 74, "generations": {"memory_layout": 3}}


class DeploySet(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory(prefix="os32-deploy-set-")
        self.addCleanup(self.tmp.cleanup)
        self.root = Path(self.tmp.name)
        self.entries = [
            {"host": "build/out/vmkernel.lz4", "guest": "/boot/vmkernel.lz4"},
            {"host": "build/out/unicode.bin", "guest": "/sys/unicode.bin"},
            {"host": "userland/shell.bin", "guest": "/sys/shell.bin"},
            {"host": "userland/libos32gui.shlib", "guest": "/sys/lib/"},
            {"host": "userland/tests/*.bin", "guest": "/usr/bin/", "type": "glob",
             "exclude": ["excluded.bin"]},
            {"host": "assets/*.dat", "guest": "/data/", "type": "glob"},
            {"host": "assets/filetypes", "guest": "/etc/filetypes"},
        ]
        for name in ("build/out/vmkernel.lz4", "build/out/unicode.bin",
                     "userland/shell.bin", "userland/libos32gui.shlib",
                     "userland/tests/a.bin", "userland/tests/excluded.bin",
                     "assets/a.dat", "assets/filetypes", "build/out/kernel.bin",
                     "build/out/sqlite.bin", "boot/boot_hdd.bin", "boot/loader_hdd.bin",
                     "build/sdk/lib/libos32gfx.a", "apps/a.bin", "game/b.bin"):
            path = self.root / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(name.encode())
        self.merged = {"boot": {"loader": "boot/loader_hdd.bin"},
                       "filesystem": {"files": self.entries}}

    def generate(self):
        return gs.generate(self.root, self.merged, GENERATION)

    def source(self):
        expected = self.generate()
        hostdrv = self.root / "hostdrv"
        rows = []
        for entry in expected["files"]:
            path = hostdrv / entry["guest"].lstrip("/")
            path.parent.mkdir(parents=True, exist_ok=True)
            data = (self.root / entry["host"]).read_bytes()
            path.write_bytes(data)
            rows.append("{} {} {:08x} 0".format(entry["guest"].lstrip("/"),
                        len(data), zlib.crc32(data) & 0xffffffff))
        text = ("format=2\nbuild=fixture\ngenerated=2026-10-10T00:00:00Z\n"
                "kapi=1208\nkapi_version=74\ncount={}\n---\n".format(len(rows)) +
                "\n".join(rows) + "\n")
        return expected, hostdrv, text

    def test_scope_hashes_and_determinism(self):
        result = self.generate()
        self.assertEqual(result, self.generate())
        self.assertEqual(len(result["files"]), 7)
        hosts = {e["host"] for e in result["files"]}
        self.assertIn("build/out/unicode.bin", hosts)
        self.assertIn("build/out/vmkernel.lz4", hosts)
        self.assertIn("userland/libos32gui.shlib", hosts)
        self.assertFalse(any(h.startswith(("boot/", "build/sdk/", "apps/", "game/"))
                             or h.endswith(("kernel.bin", "sqlite.bin", "excluded.bin"))
                             for h in hosts))
        self.assertEqual(result["generation_build_id"], GENERATION["build_id"])
        self.assertEqual(result["allow_list"], [
            {"guest": "/etc/settings.db", "check": "exists"},
            {"guest": "/etc/settings.db-journal", "check": "exists"},
            {"guest": "/etc/settings.db.new", "check": "exists"},
            {"guest": "/etc/settings.db.new-journal", "check": "exists"},
            {"guest": "/etc/system.cfg", "check": "exists"},
            {"guest": "/var/log/*", "check": "exists"}])
        for entry in result["files"]:
            data = (self.root / entry["host"]).read_bytes()
            self.assertEqual(entry["size"], len(data))
            self.assertEqual(entry["sha256"], hashlib.sha256(data).hexdigest())
        (self.root / "assets/a.dat").write_bytes(b"different")
        self.assertNotEqual(result, self.generate())

    def test_settings_have_no_content_expectation(self):
        self.entries += [{"host": "not-built.db", "guest": "/etc/settings.db"},
                         {"host": "not-built.cfg", "guest": "/etc/system.cfg"},
                         {"host": "not-built.log", "guest": "/var/log/boot.log"}]
        self.assertEqual(len(self.generate()["files"]), 7)

    def test_missing_and_duplicate_rejected(self):
        (self.root / "build/out/unicode.bin").unlink()
        with self.assertRaisesRegex(ValueError, "unicode.bin"):
            self.generate()
        (self.root / "build/out/unicode.bin").write_bytes(b"restored")
        self.entries.append(dict(self.entries[0]))
        with self.assertRaisesRegex(ValueError, "重複"):
            self.generate()

    def test_generator_cli_and_merged_layers(self):
        # 既存の load_merged/resolve_entry から全層を読む。外部定義は在っても除く。
        import yaml
        for rel, entries in (("build/core.yaml", self.entries[:2]),
                             ("userland/deploy.yaml", self.entries[2:]),
                             ("apps/deploy.yaml", [{"host": "apps/a.bin", "guest": "/bin/a.bin"}]),
                             ("game/deploy.yaml", [{"host": "game/b.bin", "guest": "/bin/b.bin"}])):
            (self.root / rel).write_text(yaml.safe_dump({"filesystem": {"files": entries}}))
        out = self.root / "build/out/deploy-set.json"
        (out.parent / "generations-manifest.json").write_text(json.dumps(GENERATION))
        cmd = [sys.executable, "-B", str(args.tools_dir / "gen_deploy_set.py"),
               "--root", str(self.root)]
        result = subprocess.run(cmd, capture_output=True, text=True)
        self.assertEqual(result.returncode, 0, result.stderr)
        first = out.read_bytes()
        self.assertEqual(json.loads(first), self.generate())
        self.assertEqual(subprocess.run(cmd, capture_output=True).returncode, 0)
        self.assertEqual(first, out.read_bytes())
        (self.root / "assets/a.dat").write_bytes(b"changed")
        self.assertEqual(subprocess.run(cmd, capture_output=True).returncode, 0)
        self.assertNotEqual(first, out.read_bytes())

    def test_source_all_lines_and_listing(self):
        expected, hostdrv, text = self.source()
        errors, guests = sc.check_source(hostdrv, text, expected)
        self.assertEqual(errors, [])
        self.assertEqual(guests, sorted(e["guest"] for e in expected["files"]))
        # 外部行も照合する。deploy-set の guest 一覧には加えない。
        text = text.replace("count=7", "count=8") + "bin/external.bin 1 00000000 0\n"
        errors, _ = sc.check_source(hostdrv, text, expected)
        self.assertTrue(any("external.bin" in e for e in errors), errors)

    def test_source_legacy_format_and_symlink_escape(self):
        expected, hostdrv, text = self.source()
        legacy = text.replace("format=2", "format=1").replace("kapi=1208\n", "").replace("kapi_version=74\n", "")
        self.assertEqual(sc.check_source(hostdrv, legacy, expected)[0], [])
        path = hostdrv / "sys/unicode.bin"
        data = path.read_bytes()
        path.unlink()
        outside = self.root / "outside.bin"
        outside.write_bytes(data)
        path.symlink_to(outside)
        errors, _ = sc.check_source(hostdrv, text, expected)
        self.assertTrue(any("unicode.bin" in e and "範囲外" in e for e in errors), errors)

    def test_source_lists_all_missing_size_and_crc_failures(self):
        expected, hostdrv, text = self.source()
        (hostdrv / "boot/vmkernel.lz4").unlink()
        original = (hostdrv / "sys/unicode.bin").read_bytes()
        (hostdrv / "sys/unicode.bin").write_bytes(b"x" * len(original))
        (hostdrv / "sys/shell.bin").write_bytes(b"short")
        errors, _ = sc.check_source(hostdrv, text, expected)
        for fragment in ("vmkernel.lz4", "unicode.bin: CRC", "shell.bin: size"):
            self.assertTrue(any(fragment in e for e in errors), errors)

    def test_source_missing_rows_and_bad_rows(self):
        expected, hostdrv, text = self.source()
        lines = text.splitlines()
        removed = lines.pop()
        errors, _ = sc.check_source(hostdrv, "\n".join(lines), expected)
        self.assertTrue(any("count=" in e for e in errors), errors)
        lines = [line.replace("count=7", "count=6") for line in lines]
        errors, _ = sc.check_source(hostdrv, "\n".join(lines), expected)
        self.assertTrue(any(removed.split()[0] in e for e in errors), errors)
        for row in ("../escape 1 00000000 0", "/absolute 1 00000000 0",
                    "bad garbage", "bad 1 zz 0", "bad 1 00000000 -1", ""):
            errors, _ = sc.check_source(hostdrv, text + row + "\n", expected)
            self.assertTrue(any("不正なファイル行" in e for e in errors), errors)
        errors, _ = sc.check_source(hostdrv, text + text.splitlines()[-1] + "\n", expected)
        self.assertTrue(any("重複" in e for e in errors), errors)

    def test_source_cli_return_code_and_paths(self):
        expected, hostdrv, text = self.source()
        manifest = hostdrv / ".deploy/manifest.txt"
        manifest.parent.mkdir()
        manifest.write_text(text)
        deploy_set = self.root / "set.json"
        deploy_set.write_text(json.dumps(expected))
        listing = self.root / "guests.txt"
        cmd = [sys.executable, "-B", str(args.tools_dir / "deploy_source_check.py"),
               "--root", str(hostdrv), "--deploy-set", str(deploy_set),
               "--guest-paths", str(listing)]
        result = subprocess.run(cmd, capture_output=True, text=True)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(result.stdout, listing.read_text())
        self.assertEqual(result.stdout.splitlines(), sorted(e["guest"] for e in expected["files"]))
        (hostdrv / "sys/unicode.bin").write_bytes(b"corrupt")
        result = subprocess.run(cmd, capture_output=True, text=True)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("unicode.bin", result.stderr)
        (hostdrv / "sys/shell.bin").unlink()
        result = subprocess.run(cmd, capture_output=True, text=True)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("shell.bin", result.stderr)

    def test_generation_deploy_both_directions_and_non_executables(self):
        with patch.object(cm, "resolve_entry", side_effect=lambda e: dm.resolve_entry(e, str(self.root))):
            paths = ["userland/shell.bin", "userland/libos32gui.shlib", "userland/tests/a.bin",
                     "build/out/kernel.bin", "build/out/sqlite.bin", "boot/loader_hdd.bin",
                     "build/sdk/lib/libos32gfx.a"]
            manifest = {"files": [{"path": p} for p in paths]}
            self.assertEqual(cm.check_generation_deploy(manifest, self.merged), [])
            missing = copy.deepcopy(manifest)
            missing["files"].pop(0)
            self.assertTrue(cm.check_generation_deploy(missing, self.merged))
            extra = copy.deepcopy(self.merged)
            extra["filesystem"]["files"].pop(2)
            self.assertTrue(cm.check_generation_deploy(manifest, extra))

    def test_crt_and_each_lib_existence(self):
        libraries = cm._dependencies(str(ROOT))["libs"]
        errors = cm.check_crt_libs(self.root, libraries)
        self.assertTrue(errors)
        paths = [error.split(":")[0] for error in errors]
        for name in paths:
            path = self.root / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(b"fixture")
        self.assertEqual(cm.check_crt_libs(self.root, libraries), [])
        for name in paths:
            path = self.root / name
            path.unlink()
            self.assertTrue(any(name in e for e in cm.check_crt_libs(self.root, libraries)))
            path.write_bytes(b"fixture")


def mutate():
    mutations = [
        ("check_manifests.py", "sorted(deployed - recorded)", "[]"),
        ("check_manifests.py", "sorted(recorded - deployed)", "[]"),
        ("deploy_source_check.py", "if actual_crc != crc:", "if False:"),
        ("deploy_source_check.py", "if len(data) != int(size):", "if False:"),
        ("deploy_source_check.py", 'if guest.lstrip("/") not in seen:', "if False:"),
        ("gen_deploy_set.py", '"size": len(data)', '"size": len(data) + 1'),
    ]
    for number, (name, old, new) in enumerate(mutations, 1):
        with tempfile.TemporaryDirectory(prefix="os32-deploy-mut-") as tmp:
            target = Path(tmp)
            for module in ("check_manifests.py", "deploy_manifests.py",
                           "gen_deploy_set.py", "deploy_source_check.py", "check_artifacts.py"):
                shutil.copyfile(args.tools_dir / module, target / module)
            path = target / name
            text = path.read_text()
            if text.count(old) != 1:
                raise RuntimeError("変異の適用点が一意でない: " + old)
            path.write_text(text.replace(old, new))
            result = subprocess.run([sys.executable, "-B", __file__, "--tools-dir", str(target)],
                                    capture_output=True, text=True)
            if result.returncode == 0 or "FAIL:" not in result.stderr:
                print(result.stdout + result.stderr)
                raise RuntimeError("変異を検出できない: " + old)
            print("RED mutant {}: {}".format(number, old), flush=True)


if __name__ == "__main__":
    result = unittest.TextTestRunner(verbosity=2).run(unittest.defaultTestLoader.loadTestsFromTestCase(DeploySet))
    if not result.wasSuccessful():
        sys.exit(1)
    if args.mutate:
        mutate()
