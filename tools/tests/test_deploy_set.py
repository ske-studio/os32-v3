#!/usr/bin/env python3
"""T2h/h1: E3 配備期待集合、E1-a 全行照合、世代表の相互照合。

正常対照を必ず先に回す。--mutate は写しの Python モジュールだけを変える。
実配備・エミュレータには触れない。
"""
import argparse
import copy
import hashlib
import json
import re
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
sys.path.insert(1, str(ROOT / "tools"))
sys.path.insert(0, str(ROOT / "sdk"))
import check_manifests as cm
import deploy_manifests as dm
import deploy_source_check as sc
import gen_deploy_set as gs
import nhd_deploy as nd
import os32x_hdr as hdr

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
            {"guest": "/etc/settings.db", "check": "optional"},
            {"guest": "/etc/settings.db-journal", "check": "optional"},
            {"guest": "/etc/settings.db.new", "check": "optional"},
            {"guest": "/etc/settings.db.new-journal", "check": "optional"},
            {"guest": "/etc/system.cfg", "check": "optional"},
            {"guest": "/var/log/*", "check": "optional"}])
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

    def deployment_fixture(self):
        """実生成器を temp 木に置く。配備の mount/sudo 境界だけ差し替える。"""
        import yaml
        for rel, entries in (("build/core.yaml", self.entries[:2]),
                             ("userland/deploy.yaml", self.entries[2:])):
            (self.root / rel).write_text(yaml.safe_dump({"filesystem": {"files": entries}}))
        (self.root / "build/out/generations-manifest.json").write_text(json.dumps(GENERATION))
        (self.root / "tools").mkdir(exist_ok=True)
        for name in ("gen_deploy_set.py", "deploy_manifests.py"):
            shutil.copyfile(args.tools_dir / name, self.root / "tools" / name)

    def make_deployment_fixture(self):
        self.deployment_fixture()
        # Execute the actual deploy.mk recipes in a fixture with no OS build,
        # emulator, NHD or real HostDrv. Every invocation rebuilds the kernel.
        (self.root / "build/deploy.mk").write_text((ROOT / "build/deploy.mk").read_text())
        for name in ("os32_boot.d88", "os32_boot144.img"):
            path = self.root / "images" / name
            path.parent.mkdir(exist_ok=True)
        destination = self.root / "deployed"
        destination.mkdir()
        makefile = self.root / "fixture.mk"
        # Parent make exports command-line overrides through MAKEFLAGS. Keep
        # every destination and deployment command inside this temp fixture.
        makefile.write_text('''override BUILD_OUT = build/out
override NP21W_DIR = deployed
override HOSTDRV_DEPLOY = python3 tools/deploy_stub.py
override NHD_DEPLOY = python3 tools/deploy_stub.py --nhd
include build/deploy.mk
override PRUNE_STALE = true
build/out/vmkernel.lz4: FORCE
	@echo rebuilt >> $@
images/os32_boot.d88 images/os32_boot144.img: build/out/vmkernel.lz4
	@cp $< $@
programs unicode_bin FORCE:
.PHONY: programs unicode_bin FORCE
''')
        (self.root / "tools/deploy_stub.py").write_text(
            "import shutil, subprocess, sys\n"
            "if sys.argv[1:] == ['--nhd', 'sync']:\n"
            "    subprocess.run([sys.executable, 'tools/gen_deploy_set.py'], check=True)\n"
            "shutil.copyfile('build/out/vmkernel.lz4', 'deployed/vmkernel.lz4')\n")
        return makefile, destination

    def run_make_deployment(self, makefile, target):
        result = subprocess.run(["make", "-f", str(makefile), "-j4", target],
                                cwd=self.root, capture_output=True, text=True)
        return result

    def test_each_make_deployment_refreshes_actual_bytes(self):
        makefile, destination = self.make_deployment_fixture()
        for target in ("deploy", "deploy-kernel", "deploy-nhd", "deploy-u3"):
            with self.subTest(target=target):
                out = self.root / "build/out/deploy-set.json"
                out.write_text(json.dumps(self.generate()))
                before = out.read_bytes()
                result = self.run_make_deployment(makefile, target)
                self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
                self.assertNotEqual(before, out.read_bytes(), "配備後に古い期待表が残った")
                self.assertEqual(json.loads(out.read_text()), self.generate())
                self.assertEqual((destination / "vmkernel.lz4").read_bytes(),
                                 (self.root / "build/out/vmkernel.lz4").read_bytes())
                self.assertEqual(result.stdout.count("deploy-set:"), 1)

    def test_separate_fd_deploy_preserves_hostdrv_set_and_u3_matches(self):
        makefile, destination = self.make_deployment_fixture()
        # Use the real source/manifest check against the bytes deployed to HostDrv.
        (self.root / "tools/deploy_stub.py").write_text('''import shutil
from pathlib import Path
import deploy_manifests as dm
import zlib
rows = []
for entry in dm.load_merged()['filesystem']['files']:
    for host, guest in dm.resolve_entry(entry, '.'):
        target = Path('deployed') / guest.lstrip('/')
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(host, target)
        data = target.read_bytes()
        rows.append('{} {} {:08x} 0'.format(guest.lstrip('/'), len(data), zlib.crc32(data)))
manifest = Path('deployed/.deploy/manifest.txt')
manifest.parent.mkdir(exist_ok=True)
manifest.write_text('format=1\\nbuild=fixture\\ngenerated=fixture\\ncount={}\\n---\\n'.format(len(rows)) + '\\n'.join(rows))
''')
        out = self.root / "build/out/deploy-set.json"
        result = self.run_make_deployment(makefile, "deploy")
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        hostdrv_set = out.read_bytes()
        self.assertEqual(self.run_make_deployment(makefile, "deploy-fd").returncode, 0)
        self.assertEqual(out.read_bytes(), hostdrv_set, "FD 配備が HostDrv の期待表を変えた")
        fd = (destination / "os32_boot.d88").read_bytes()
        self.assertNotEqual(fd, (destination / "boot/vmkernel.lz4").read_bytes())
        for name in ("os32_boot.d88", "os32_boot144.img"):
            (destination / name).unlink()
        manifest = (destination / ".deploy/manifest.txt").read_text()
        self.assertEqual(sc.check_source(destination, manifest, json.loads(hostdrv_set))[0], [])
        errors, _ = sc.check_source(destination, manifest, self.generate())
        self.assertTrue(any("deploy-set" in e and "vmkernel.lz4" in e for e in errors), errors)
        result = self.run_make_deployment(makefile, "deploy-u3")
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        for name in ("os32_boot.d88", "os32_boot144.img"):
            self.assertEqual((destination / name).read_bytes(),
                             (destination / "boot/vmkernel.lz4").read_bytes())
        manifest = (destination / ".deploy/manifest.txt").read_text()
        # FD images are outside the source root in real deployment.
        for name in ("os32_boot.d88", "os32_boot144.img"):
            (destination / name).unlink()
        self.assertEqual(sc.check_source(destination, manifest, json.loads(out.read_text()))[0], [])

    def test_make_generator_failure_leaves_destinations_untouched(self):
        makefile, destination = self.make_deployment_fixture()
        for target in ("deploy", "deploy-kernel", "deploy-nhd"):
            with self.subTest(target=target):
                (self.root / "build/out/generations-manifest.json").unlink(missing_ok=True)
                sentinel = destination / "vmkernel.lz4"
                sentinel.write_bytes(b"previous deployment")
                result = self.run_make_deployment(makefile, target)
                self.assertNotEqual(result.returncode, 0, result.stdout + result.stderr)
                self.assertEqual(sentinel.read_bytes(), b"previous deployment")
                self.assertEqual(sorted(p.name for p in destination.iterdir()), [sentinel.name])

    def test_nhd_sync_refresh_and_generator_failure(self):
        self.deployment_fixture()
        self.entries[0]["tags"] = ["core"]
        mount = self.root / "mounted"
        mount.mkdir()
        real_run = subprocess.run

        def command(argv, **kw):
            if argv[:3] == ["sudo", "cp", "--"]:
                shutil.copyfile(argv[-2], argv[-1])
                return subprocess.CompletedProcess(argv, 0, "", "")
            if argv == ["sync"]:
                return subprocess.CompletedProcess(argv, 0, "", "")
            return real_run(argv, **kw)

        def ensure_dir(guest):
            path = mount / guest.lstrip("/")
            path.mkdir(parents=True, exist_ok=True)
            return str(path), "ok"

        import contextlib
        with contextlib.ExitStack() as stack:
            for name, value in (("PROJ_DIR", str(self.root)), ("MOUNT_POINT", str(mount)),
                                ("ensure_local_nhd", lambda: True), ("legacy_pt_guard", lambda: True),
                                ("ensure_mounted_for_kernel", lambda: True), ("guard_root", lambda: True),
                                ("load_deploy_yaml", lambda: self.merged), ("do_write_boot", lambda _: True),
                                ("ensure_dir", ensure_dir),
                                ("guard_dest", lambda guest, **kw: (str(mount / guest.lstrip("/")), "ok")),
                                ("resolve_files_from_entry", lambda entry: [
                                    (str(self.root / host), guest)
                                    for host, guest in dm.resolve_entry(entry, str(self.root))])):
                stack.enter_context(patch.object(nd, name, value))
            stack.enter_context(patch.object(nd.subprocess, "run", command))
            out = self.root / "build/out/deploy-set.json"
            out.write_text(json.dumps(self.generate()))
            before = out.read_bytes()
            (self.root / "build/out/vmkernel.lz4").write_bytes(b"new deployment build time")
            self.assertTrue(nd.do_sync())
            self.assertNotEqual(before, out.read_bytes())
            self.assertEqual(json.loads(out.read_text()), self.generate())
            for entry in json.loads(out.read_text())["files"]:
                self.assertEqual(hashlib.sha256((mount / entry["guest"].lstrip("/")).read_bytes()).hexdigest(),
                                 entry["sha256"])
            # A tagged sync keeps the full set, even when its inputs changed.
            (self.root / "build/out/vmkernel.lz4").write_bytes(b"tagged update")
            before = out.read_bytes()
            self.assertTrue(nd.do_sync(tag_filter="core"))
            self.assertEqual(out.read_bytes(), before)
            self.assertEqual((mount / "boot/vmkernel.lz4").read_bytes(), b"tagged update")
            # Generator failure must precede even boot writes or mounting.
            (self.root / "build/out/generations-manifest.json").unlink()
            before = {p.relative_to(mount): p.read_bytes() for p in mount.rglob('*') if p.is_file()}
            with patch.object(nd, "do_write_boot") as boot, patch.object(nd, "ensure_mounted_for_kernel") as mounted:
                self.assertFalse(nd.do_sync())
                boot.assert_not_called()
                mounted.assert_not_called()
            self.assertEqual(before, {p.relative_to(mount): p.read_bytes() for p in mount.rglob('*') if p.is_file()})

    def test_source_rejects_stale_set_even_when_manifest_matches(self):
        expected, hostdrv, text = self.source()
        kernel = self.root / "build/out/vmkernel.lz4"
        kernel.write_bytes(b"x" * kernel.stat().st_size)
        current, hostdrv, text = self.source()
        self.assertEqual(sc.check_source(hostdrv, text, current)[0], [])
        errors, _ = sc.check_source(hostdrv, text, expected)
        self.assertTrue(any("deploy-set" in e and "vmkernel.lz4" in e for e in errors), errors)

    def test_source_extras_match_hsync_two_stages(self):
        hsync = (ROOT / "userland/system/hsync.c").read_text()
        prefix = re.search(r'^#define HS_TEMP_PREFIX\s+"([^"]+)"', hsync, re.M).group(1)
        self.assertEqual(sc.HS_TEMP_PREFIX, prefix)
        expected, hostdrv, text = self.source()
        extras = ("sys/fep.dic", "sys/font/mincho.kcgfont", "usr/man/test_table.1",
                  "old.done", "data/old.ttf", ".deploy/old.txt", "etc/logs/nested/boot.log",
                  "etc/system.cfg", "var/log/boot.log", "etc/logs/boot.log")
        exempt = (".deploy/manifest.txt", "etc/settings.db.bak", "etc/SETTINGS.DB-wal",
                  "sys/.hs~shell.bin", ".hs~unfinished/sub/file.bin")
        expected = copy.deepcopy(expected)
        expected["allow_list"].append({"guest": "/etc/logs/*.log", "check": "optional"})
        for name in extras + exempt:
            path = hostdrv / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(b"unlisted")
        errors, _ = sc.check_source(hostdrv, text, expected)
        self.assertEqual(errors, ["extra: /" + name for name in sorted(extras)])
        # sys is included even though whole-tree hsync alone excludes it.
        self.assertIn("extra: /sys/fep.dic", errors)

    def test_source_extra_cli_dry_run_delete_and_invalid_manifest(self):
        expected, hostdrv, text = self.source()
        manifest = hostdrv / ".deploy/manifest.txt"
        manifest.parent.mkdir()
        manifest.write_text(text)
        set_file = self.root / "set.json"
        set_file.write_text(json.dumps(expected))
        extra = hostdrv / "sys/font/stale.kcgfont"
        extra.parent.mkdir()
        extra.write_bytes(b"old font")
        protected = hostdrv / "etc/settings.db.recover-state"
        protected.write_bytes(b"recovery")
        cmd = [sys.executable, "-B", str(args.tools_dir / "deploy_source_check.py"),
               "--root", str(hostdrv), "--deploy-set", str(set_file)]
        result = subprocess.run(cmd, capture_output=True, text=True)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("extra: /sys/font/stale.kcgfont", result.stderr)
        result = subprocess.run(cmd + ["--prune-extra"], capture_output=True, text=True)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("dry-run:", result.stdout)
        self.assertTrue(extra.exists())
        manifest.write_text(text.replace("count=7", "count=0"))
        result = subprocess.run(cmd + ["--prune-extra", "--delete"], capture_output=True, text=True)
        self.assertNotEqual(result.returncode, 0)
        self.assertTrue(extra.exists())
        manifest.write_text(text)
        result = subprocess.run(cmd + ["--prune-extra", "--delete"], capture_output=True, text=True)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertFalse(extra.exists())
        self.assertEqual(protected.read_bytes(), b"recovery")
        self.assertEqual(subprocess.run(cmd, capture_output=True).returncode, 0)

    def test_source_does_not_descend_into_protected_directory(self):
        expected, hostdrv, text = self.source()
        protected = hostdrv / "etc/settings.db"
        protected.mkdir()
        real_scandir = sc.os.scandir

        def scandir(path):
            if Path(path) == protected:
                raise PermissionError("protected settings directory")
            return real_scandir(path)

        with patch.object(sc.os, "scandir", scandir):
            self.assertEqual(sc.check_source(hostdrv, text, expected)[0], [])

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
        self.assertIn("名札に管理対象の行がない: /" + removed.split()[0], errors)
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

    def test_generation_deploy_ignores_cargo_target_cache(self):
        caches = [
            "userland/libos32term/target/x86_64-unknown-linux-gnu/debug/incremental/fixture/dep-graph.bin",
            "userland/rust/target/debug/incremental/fixture/query-cache.bin",
            "userland/target/debug/cache.shlib",
        ]
        for name in caches:
            path = self.root / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(b"Rust host test cache")
        # 世代表の生成器と同じ glob で、実在する cache も集合に残す。
        paths = {str(p.relative_to(self.root)) for p in self.root.glob("userland/**/*.bin")}
        paths.discard("userland/tests/excluded.bin")
        paths.update(["userland/libos32gui.shlib", caches[-1]])
        self.assertTrue(set(caches).issubset(paths))
        manifest = {"files": [{"path": p} for p in sorted(paths)]}
        with patch.object(cm, "resolve_entry", side_effect=lambda e: dm.resolve_entry(e, str(self.root))):
            self.assertEqual(cm.check_generation_deploy(manifest, self.merged), [])

    def test_generation_deploy_rejects_undeployed_os32x(self):
        # target はディレクトリ要素だけを除く。似た名前の実行物は検出する。
        names = ["userland/unregistered.bin", "userland/target.bin",
                 "userland/target-tools/unregistered.bin",
                 "userland/unregistered.shlib"]
        for name in names:
            path = self.root / name
            path.parent.mkdir(parents=True, exist_ok=True)
            flags = hdr.OS32X_FLAG_SHLIB if name.endswith(".shlib") else hdr.OS32X_FLAG_RING3
            path.write_bytes(hdr.build_header(flags, 0, 1, 0, 0, 0,
                                             hdr.OS32X_APP_LOAD_ADDR, 1) + b"\xc3")
        paths = ["userland/shell.bin", "userland/libos32gui.shlib", "userland/tests/a.bin"]
        manifest = {"files": [{"path": p} for p in paths + names]}
        with patch.object(cm, "resolve_entry", side_effect=lambda e: dm.resolve_entry(e, str(self.root))):
            self.assertEqual(cm.check_generation_deploy(manifest, self.merged),
                             [p + ": 配備定義にない" for p in sorted(names)])

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
        ("check_manifests.py", 'and "/target/" not in path', ""),
        ("check_manifests.py", '"/target/" not in path', '"target" not in path'),
        ("deploy_source_check.py", "if actual_crc != crc:", "if False:"),
        ("deploy_source_check.py", "if len(data) != int(size):", "if False:"),
        ("deploy_source_check.py", 'if guest.lstrip("/") not in seen:', "if False:"),
        ("gen_deploy_set.py", '"size": len(data)', '"size": len(data) + 1'),
        ("deploy_source_check.py", '    return sorted(extras)', '    return []'),
        ("deploy_source_check.py", 'hashlib.sha256(data).hexdigest() != entry["sha256"]', 'False'),
        ("deploy_source_check.py", 'protect.is_protected(str(root), os.path.join(parent, name))', 'False'),
        ("nhd_deploy.py", 'elif not refresh_deploy_set():', 'elif False:'),
    ]
    for number, (name, old, new) in enumerate(mutations, 1):
        with tempfile.TemporaryDirectory(prefix="os32-deploy-mut-") as tmp:
            target = Path(tmp)
            for module in ("check_manifests.py", "deploy_manifests.py",
                           "gen_deploy_set.py", "deploy_source_check.py", "deploy_protect.py", "check_artifacts.py",
                           "nhd_deploy.py"):
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
