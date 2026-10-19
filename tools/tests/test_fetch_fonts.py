#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""tools/fetch_fonts.py (IPAex フォントのビルド時取得) のホスト試験。ネットワーク無し。

見るもの:
  * 同意の判定 — 記録済み / OS32_ACCEPT_IPA_LICENSE=1 / 端末で y / 端末で N /
    端末でない、の 5 通り。同意しないときは rc=2 と 1 行の案内。
  * SHA-256 の照合 — zip 全体、zip の中の ttf、の両方で不一致を拒む (rc=1、
    置き先に何も残さない)。
  * .license_accepted の扱い — 同意したら書く、あれば聞かない、別のライセンス文の
    記録なら聞き直す。
  * 揃っていれば聞かず・取らず mtime だけ更新する。--force は取り直す。

取得は fetch= で差し替え、zip は temp dir に偽物 (MEMBERS / ZIP_SHA256 を
差し替えて照合が通る形) を作る。実ネットワーク・実 assets/fonts には触れない。

  python3 -B tools/tests/test_fetch_fonts.py
"""
import contextlib
import hashlib
import importlib.util
import io
import json
import tomllib
import os
import pathlib
import sys
import tempfile
import unittest
import zipfile

ROOT = pathlib.Path(__file__).resolve().parents[2]
SCRIPT = ROOT / "tools" / "fetch_fonts.py"
MUTATION_SOURCE = None
REAL_LICENSE = ROOT / "assets" / "fonts" / "IPA_Font_License_Agreement_v1.0.txt"


def load_module():
    spec = importlib.util.spec_from_file_location("fetch_fonts", str(SCRIPT))
    mod = importlib.util.module_from_spec(spec)
    if MUTATION_SOURCE is None:
        spec.loader.exec_module(mod)
    else:
        exec(compile(MUTATION_SOURCE, str(SCRIPT), "exec"), mod.__dict__)
    return mod


class TtyIn(io.StringIO):
    """端末のふりをする stdin。"""
    def isatty(self):
        return True


class PipeIn(io.StringIO):
    def isatty(self):
        return False


FAKE_G = b"\x00\x01\x00\x00fake-gothic-ttf" * 10
FAKE_M = b"\x00\x01\x00\x00fake-mincho-ttf" * 10
LICENSE_TEXT = "﻿---\r\nIPA Font License Agreement v1.0 (test copy)\r\n---\r\n".encode("utf-8")


def sha(b):
    return hashlib.sha256(b).hexdigest()


class FetchFontsTest(unittest.TestCase):
    def setUp(self):
        self.mod = load_module()
        self.tmp = tempfile.TemporaryDirectory()
        self.font_dir = os.path.join(self.tmp.name, "fonts")
        os.makedirs(self.font_dir)
        # ライセンス文は試験用の短い写し。モジュールの期待 SHA をそれに合わせる。
        with open(os.path.join(self.font_dir, self.mod.LICENSE_NAME), "wb") as f:
            f.write(LICENSE_TEXT)
        self.mod.LICENSE_SHA256 = sha(LICENSE_TEXT)
        # 偽 zip。中身の名前は本物と同じ。
        buf = io.BytesIO()
        with zipfile.ZipFile(buf, "w") as z:
            z.writestr("IPAexfont00401/ipaexg.ttf", FAKE_G)
            z.writestr("IPAexfont00401/ipaexm.ttf", FAKE_M)
            z.writestr("IPAexfont00401/" + self.mod.LICENSE_NAME, LICENSE_TEXT)
        self.zip_bytes = buf.getvalue()
        self.mod.ZIP_SHA256 = sha(self.zip_bytes)
        self.mod.MEMBERS = {
            "IPAexfont00401/ipaexg.ttf": ("ipaexg.ttf", sha(FAKE_G)),
            "IPAexfont00401/ipaexm.ttf": ("ipaexm.ttf", sha(FAKE_M)),
        }
        self.fetch_calls = []

    def tearDown(self):
        self.tmp.cleanup()

    # --- 道具 ---
    def fetch(self, url, stderr):
        self.fetch_calls.append(url)
        return self.zip_bytes

    def run_script(self, argv=(), env=None, stdin=None, fetch=None):
        out, err = io.StringIO(), io.StringIO()
        rc = self.mod.run(["--font-dir", self.font_dir] + list(argv),
                          environ=env if env is not None else {},
                          stdin=stdin if stdin is not None else PipeIn(""),
                          stdout=out, stderr=err,
                          fetch=fetch if fetch is not None else self.fetch)
        return rc, out.getvalue(), err.getvalue()

    def font_path(self, name):
        return os.path.join(self.font_dir, name)

    def accepted_path(self):
        return os.path.join(self.font_dir, self.mod.ACCEPTED_NAME)

    def assert_fonts_present(self):
        with open(self.font_path("ipaexg.ttf"), "rb") as f:
            self.assertEqual(f.read(), FAKE_G)
        with open(self.font_path("ipaexm.ttf"), "rb") as f:
            self.assertEqual(f.read(), FAKE_M)

    def assert_fonts_absent(self):
        self.assertFalse(os.path.exists(self.font_path("ipaexg.ttf")))
        self.assertFalse(os.path.exists(self.font_path("ipaexm.ttf")))
        self.assertFalse(os.path.exists(self.font_path("ipaexg.ttf.part")))

    # --- 同意の判定 ---
    def test_non_tty_without_env_refuses(self):
        rc, out, err = self.run_script()
        self.assertEqual(rc, 2)
        self.assertEqual(self.fetch_calls, [])
        self.assert_fonts_absent()
        self.assertFalse(os.path.exists(self.accepted_path()))
        self.assertTrue(err.rstrip("\n").endswith(self.mod.GUIDE_LICENSE), err)
        self.assertIn("OS32_ACCEPT_IPA_LICENSE=1", err.splitlines()[-1])
        self.assertIn("make fonts", err.splitlines()[-1])
        self.assertNotIn("License Agreement", out)   # 端末でなければ全文は出さない

    def test_env_accepts_non_interactively(self):
        rc, out, err = self.run_script(env={"OS32_ACCEPT_IPA_LICENSE": "1"})
        self.assertEqual(rc, 0, err)
        self.assertEqual(self.fetch_calls, [self.mod.ZIP_URL])
        self.assert_fonts_present()
        self.assertTrue(os.path.exists(self.accepted_path()))
        with open(self.accepted_path(), encoding="utf-8") as f:
            rec = f.read()
        self.assertIn("license-sha256: " + sha(LICENSE_TEXT), rec)
        self.assertIn("accepted: ", rec)
        self.assertIn("how: env OS32_ACCEPT_IPA_LICENSE=1", rec)

    def test_env_other_values_do_not_accept(self):
        for v in ("0", "", "yes", "true"):
            self.fetch_calls = []
            rc, _, err = self.run_script(env={"OS32_ACCEPT_IPA_LICENSE": v})
            self.assertEqual(rc, 2, v)
            self.assertEqual(self.fetch_calls, [])
        self.assert_fonts_absent()

    def test_tty_yes_shows_full_license_then_fetches(self):
        rc, out, err = self.run_script(stdin=TtyIn("y\n"))
        self.assertEqual(rc, 0, err)
        self.assertIn("IPA Font License Agreement v1.0 (test copy)", out)  # 全文が標準出力に
        self.assertNotIn("﻿", out)                                      # BOM は出さない
        self.assertIn("同意しますか [y/N]", out)
        self.assertEqual(self.fetch_calls, [self.mod.ZIP_URL])
        self.assert_fonts_present()
        with open(self.accepted_path(), encoding="utf-8") as f:
            self.assertIn("how: interactive y", f.read())

    def test_tty_no_and_empty_refuse(self):
        for answer in ("n\n", "\n", "", "N\n", "yes please\n"):
            self.fetch_calls = []
            rc, out, err = self.run_script(stdin=TtyIn(answer))
            self.assertEqual(rc, 2, repr(answer))
            self.assertEqual(self.fetch_calls, [])
            self.assertTrue(err.rstrip("\n").endswith(self.mod.GUIDE_LICENSE), err)
        self.assert_fonts_absent()
        self.assertFalse(os.path.exists(self.accepted_path()))

    def test_tty_yes_variants(self):
        for answer in ("Y\n", "yes\n", "  y  \n"):
            for n in ("ipaexg.ttf", "ipaexm.ttf"):
                if os.path.exists(self.font_path(n)):
                    os.remove(self.font_path(n))
            if os.path.exists(self.accepted_path()):
                os.remove(self.accepted_path())
            rc, _, err = self.run_script(stdin=TtyIn(answer))
            self.assertEqual(rc, 0, (answer, err))
            self.assert_fonts_present()

    def test_env_wins_over_non_tty_but_tty_prompt_not_shown_with_env(self):
        rc, out, _ = self.run_script(env={"OS32_ACCEPT_IPA_LICENSE": "1"}, stdin=TtyIn("n\n"))
        self.assertEqual(rc, 0)
        self.assertNotIn("同意しますか", out)

    def test_license_file_missing_or_tampered_refuses(self):
        os.remove(os.path.join(self.font_dir, self.mod.LICENSE_NAME))
        rc, _, err = self.run_script(stdin=TtyIn("y\n"))
        self.assertEqual(rc, 2)
        self.assertEqual(self.fetch_calls, [])
        with open(os.path.join(self.font_dir, self.mod.LICENSE_NAME), "wb") as f:
            f.write(LICENSE_TEXT + b"tampered")
        rc, _, err = self.run_script(stdin=TtyIn("y\n"))
        self.assertEqual(rc, 2)
        self.assertIn("一致しない", err)
        self.assertEqual(self.fetch_calls, [])

    # --- .license_accepted の扱い ---
    def test_recorded_acceptance_skips_prompt(self):
        with open(self.accepted_path(), "w", encoding="utf-8") as f:
            f.write("accepted: 2026-09-30T00:00:00+0900\n"
                    "license-sha256: %s\n" % sha(LICENSE_TEXT))
        rc, out, err = self.run_script(stdin=PipeIn(""))  # 端末でなくても env 無しでも通る
        self.assertEqual(rc, 0, err)
        self.assertNotIn("同意しますか", out)
        self.assert_fonts_present()

    def test_recorded_acceptance_for_other_license_asks_again(self):
        with open(self.accepted_path(), "w", encoding="utf-8") as f:
            f.write("license-sha256: %s\n" % ("0" * 64))
        rc, _, _ = self.run_script(stdin=PipeIn(""))
        self.assertEqual(rc, 2)
        self.assertEqual(self.fetch_calls, [])
        rc, out, _ = self.run_script(stdin=TtyIn("y\n"))
        self.assertEqual(rc, 0)
        self.assertIn("同意しますか", out)
        with open(self.accepted_path(), encoding="utf-8") as f:
            self.assertIn("license-sha256: " + sha(LICENSE_TEXT), f.read())

    def test_garbage_record_is_not_acceptance(self):
        with open(self.accepted_path(), "wb") as f:
            f.write(b"\xff\xfe\x00garbage")
        rc, _, _ = self.run_script(stdin=PipeIn(""))
        self.assertEqual(rc, 2)

    def test_acceptance_recorded_even_if_fetch_fails(self):
        def bad_fetch(url, stderr):
            raise OSError("no route")
        rc, _, err = self.run_script(stdin=TtyIn("y\n"), fetch=bad_fetch)
        self.assertEqual(rc, 1)
        self.assertTrue(os.path.exists(self.accepted_path()))
        self.assertIn("フォント取得失敗", err.splitlines()[-1])
        self.assertIn("--zip", err.splitlines()[-1])
        self.assert_fonts_absent()

    # --- SHA-256 の照合 ---
    def test_zip_sha_mismatch_rejected(self):
        self.mod.ZIP_SHA256 = "0" * 64
        rc, _, err = self.run_script(env={"OS32_ACCEPT_IPA_LICENSE": "1"})
        self.assertEqual(rc, 1)
        self.assertIn("zip の SHA-256 が合わない", err)
        self.assert_fonts_absent()

    def test_member_sha_mismatch_rejected_and_nothing_written(self):
        self.mod.MEMBERS["IPAexfont00401/ipaexm.ttf"] = ("ipaexm.ttf", "1" * 64)
        rc, _, err = self.run_script(env={"OS32_ACCEPT_IPA_LICENSE": "1"})
        self.assertEqual(rc, 1)
        self.assertIn("ipaexm.ttf の SHA-256 が合わない", err)
        self.assert_fonts_absent()   # 先に通ったゴシックも置かない

    def test_member_missing_rejected(self):
        self.mod.MEMBERS["IPAexfont00401/ipaexb.ttf"] = ("ipaexb.ttf", sha(b""))
        rc, _, err = self.run_script(env={"OS32_ACCEPT_IPA_LICENSE": "1"})
        self.assertEqual(rc, 1)
        self.assertIn("zip に IPAexfont00401/ipaexb.ttf が無い", err)

    def test_local_zip_option_skips_download(self):
        zpath = os.path.join(self.tmp.name, "local.zip")
        with open(zpath, "wb") as f:
            f.write(self.zip_bytes)
        rc, _, err = self.run_script(["--zip", zpath], env={"OS32_ACCEPT_IPA_LICENSE": "1"})
        self.assertEqual(rc, 0, err)
        self.assertEqual(self.fetch_calls, [])
        self.assert_fonts_present()

    def test_corrupt_zip_rejected(self):
        self.zip_bytes = b"PK\x03\x04not really a zip"
        self.mod.ZIP_SHA256 = sha(self.zip_bytes)
        rc, _, err = self.run_script(env={"OS32_ACCEPT_IPA_LICENSE": "1"})
        self.assertEqual(rc, 1)
        self.assert_fonts_absent()

    # --- 揃っているとき / --force ---
    def test_present_and_valid_neither_asks_nor_fetches(self):
        for n, b in (("ipaexg.ttf", FAKE_G), ("ipaexm.ttf", FAKE_M)):
            with open(self.font_path(n), "wb") as f:
                f.write(b)
            os.utime(self.font_path(n), (1000000000, 1000000000))
        rc, out, err = self.run_script(stdin=PipeIn(""))
        self.assertEqual(rc, 0, err)
        self.assertEqual(self.fetch_calls, [])
        self.assertEqual(out, "")
        self.assertFalse(os.path.exists(self.accepted_path()))
        for n in ("ipaexg.ttf", "ipaexm.ttf"):
            self.assertGreater(os.stat(self.font_path(n)).st_mtime, 1000000000)  # touch した

    def test_present_but_wrong_content_refetches(self):
        with open(self.font_path("ipaexg.ttf"), "wb") as f:
            f.write(b"stale")
        with open(self.font_path("ipaexm.ttf"), "wb") as f:
            f.write(FAKE_M)
        rc, _, _ = self.run_script(stdin=PipeIn(""))
        self.assertEqual(rc, 2)             # 同意が要る
        rc, _, err = self.run_script(env={"OS32_ACCEPT_IPA_LICENSE": "1"})
        self.assertEqual(rc, 0, err)
        self.assert_fonts_present()

    def test_force_refetches_even_when_valid(self):
        rc, _, _ = self.run_script(env={"OS32_ACCEPT_IPA_LICENSE": "1"})
        self.assertEqual(rc, 0)
        self.assertEqual(len(self.fetch_calls), 1)
        rc, _, _ = self.run_script(["--force"], stdin=PipeIn(""))   # 記録済みなので聞かない
        self.assertEqual(rc, 0)
        self.assertEqual(len(self.fetch_calls), 2)

    def test_check_and_print_sha(self):
        rc, _, _ = self.run_script(["--check"])
        self.assertEqual(rc, 1)
        rc, _, _ = self.run_script(env={"OS32_ACCEPT_IPA_LICENSE": "1"})
        self.assertEqual(rc, 0)
        rc, _, _ = self.run_script(["--check"])
        self.assertEqual(rc, 0)
        rc, out, _ = self.run_script(["--print-zip-sha256"])
        self.assertEqual(rc, 0)
        self.assertEqual(out.strip(), self.mod.ZIP_SHA256)

    # --- 定数が同梱物と一致すること (ネットワークは要らない) ---
    def test_bundled_license_matches_pinned_sha(self):
        mod = load_module()   # 差し替え前の定数
        with open(REAL_LICENSE, "rb") as f:
            self.assertEqual(sha(f.read()), mod.LICENSE_SHA256)
        self.assertTrue(mod.ZIP_URL.startswith("https://moji.or.jp/"))
        self.assertEqual(len(mod.ZIP_SHA256), 64)
        for _, (name, digest) in mod.MEMBERS.items():
            self.assertEqual(len(digest), 64, name)


class InconsolataTest(unittest.TestCase):
    def setUp(self):
        self.mod = load_module()
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.dest = pathlib.Path(self.tmp.name) / "fonts"
        self.ofl = b"Copyright Inconsolata authors; OFL test fixture"
        self.ttf = b"Inconsolata Regular fixture"
        buf = io.BytesIO()
        with zipfile.ZipFile(buf, "w") as z:
            z.writestr("fonts/ttf/Inconsolata-Regular.ttf", self.ttf)
        self.zip = buf.getvalue()
        self.mod.INCONSOLATA_ZIP_SHA256 = sha(self.zip)
        self.mod.INCONSOLATA_MEMBERS = {
            "fonts/ttf/Inconsolata-Regular.ttf": ("Inconsolata-Regular.ttf", sha(self.ttf))}
        self.mod.OFL_SHA256 = sha(self.ofl)
        self.calls = []

    def fetch(self, url, stderr):
        self.calls.append(url)
        if url == self.mod.INCONSOLATA_ZIP_URL:
            return self.zip
        self.assertEqual(url, self.mod.OFL_URL)
        return self.ofl

    def run_fetch(self, *args):
        return self.mod.run(["--family", "inconsolata", "--font-dir", str(self.dest), *args],
                            environ={}, stdin=PipeIn(""), stdout=io.StringIO(),
                            stderr=io.StringIO(), fetch=self.fetch)

    def test_fetch_and_cache_require_font_and_ofl(self):
        self.assertEqual(self.run_fetch("--check"), 1)
        self.assertEqual(self.run_fetch(), 0)
        self.assertEqual((self.dest / "Inconsolata-Regular.ttf").read_bytes(), self.ttf)
        self.assertEqual((self.dest / self.mod.OFL_NAME).read_bytes(), self.ofl)
        self.assertEqual(self.run_fetch("--check"), 0)
        self.calls.clear()
        self.assertEqual(self.run_fetch(), 0)
        self.assertEqual(self.calls, [])
        (self.dest / self.mod.OFL_NAME).unlink()
        self.assertEqual(self.run_fetch("--check"), 1)
        self.assertEqual(self.run_fetch(), 0)
        (self.dest / self.mod.OFL_NAME).write_bytes(b"stale license")
        self.assertEqual(self.run_fetch("--check"), 1)
        self.assertEqual(self.run_fetch(), 0)
        (self.dest / "Inconsolata-Regular.ttf").write_bytes(b"stale font")
        self.assertEqual(self.run_fetch("--check"), 1)

    def test_mismatched_zip_ttf_and_ofl_fail_without_outputs(self):
        original = (self.mod.INCONSOLATA_ZIP_SHA256,
                    dict(self.mod.INCONSOLATA_MEMBERS), self.mod.OFL_SHA256)
        for which in ("zip", "ttf", "ofl"):
            with self.subTest(which=which):
                self.mod.INCONSOLATA_ZIP_SHA256, members, self.mod.OFL_SHA256 = original
                self.mod.INCONSOLATA_MEMBERS = dict(members)
                if which == "zip":
                    self.mod.INCONSOLATA_ZIP_SHA256 = "0" * 64
                elif which == "ttf":
                    self.mod.INCONSOLATA_MEMBERS["fonts/ttf/Inconsolata-Regular.ttf"] = (
                        "Inconsolata-Regular.ttf", "0" * 64)
                else:
                    self.mod.OFL_SHA256 = "0" * 64
                self.assertEqual(self.run_fetch(), 1)
                self.assertFalse(self.dest.exists())

    def test_offline_zip_and_ofl(self):
        archive = pathlib.Path(self.tmp.name) / "font.zip"
        license_path = pathlib.Path(self.tmp.name) / "OFL.txt"
        archive.write_bytes(self.zip)
        license_path.write_bytes(self.ofl)
        self.assertEqual(self.run_fetch("--zip", str(archive), "--ofl", str(license_path)), 0)
        self.assertEqual(self.calls, [])

    def test_release_pins(self):
        mod = load_module()
        self.assertEqual(mod.INCONSOLATA_ZIP_SHA256,
                         "626e8ee07501dbb544b50aa59ac2e4b9ec86b810670158a59c7a3cbaf475548a")
        self.assertEqual(mod.INCONSOLATA_MEMBERS["fonts/ttf/Inconsolata-Regular.ttf"][1],
                         "127875d255d4c5973ca57267a43bb9d1c04397e6c7d236984a595b6cdcb12b7c")
        self.assertEqual(mod.OFL_SHA256,
                         "5d362a6f8690517fd9a5573128a081d8bbbb2f92714cf00556e08fbbe9600426")


class JfDotTest(unittest.TestCase):
    def setUp(self):
        self.mod = load_module()
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.dest = pathlib.Path(self.tmp.name) / "fonts"
        self.ttf = b"Shinonome16 fixture"
        buf = io.BytesIO()
        with zipfile.ZipFile(buf, "w") as z:
            z.writestr("JF-Dot-Shinonome16.ttf", self.ttf)
            z.writestr("JF-Dot-Shinonome16B.ttf", b"unwanted bold font")
            z.writestr("other/ignored.txt", b"unwanted document")
        self.zip = buf.getvalue()
        self.mod.JFDOT_ZIP_SHA256 = sha(self.zip)
        self.mod.JFDOT_MEMBERS = {
            "JF-Dot-Shinonome16.ttf": ("JF-Dot-Shinonome16.ttf", sha(self.ttf))}
        self.calls = []
        self.err = io.StringIO()
        self.out = io.StringIO()

    def fetch(self, url, stderr):
        self.calls.append(url)
        self.assertEqual(url, self.mod.JFDOT_ZIP_URL)
        return self.zip

    def run_fetch(self, *args):
        return self.mod.run(["--family", "jfdot-shinonome16", "--font-dir", str(self.dest), *args],
                            environ={}, stdin=PipeIn(""), stdout=self.out,
                            stderr=self.err, fetch=self.fetch)

    def test_only_one_font_and_full_license_without_consent(self):
        self.assertEqual(self.run_fetch(), 0, self.err.getvalue())
        self.assertEqual(self.calls, [self.mod.JFDOT_ZIP_URL])
        self.assertEqual(set(p.name for p in self.dest.iterdir()),
                         {"JF-Dot-Shinonome16.ttf", self.mod.JFDOT_LICENSE_NAME})
        self.assertEqual((self.dest / "JF-Dot-Shinonome16.ttf").read_bytes(), self.ttf)
        self.assertEqual((self.dest / self.mod.JFDOT_LICENSE_NAME).read_bytes(),
                         self.mod.JFDOT_LICENSE_TEXT)

    def test_cache_check_force_and_repairs(self):
        self.assertEqual(self.run_fetch("--check"), 1)
        self.assertEqual(self.calls, [])
        self.assertEqual(self.run_fetch(), 0)
        self.calls.clear()
        for p in self.dest.iterdir():
            os.utime(p, (1000000000, 1000000000))
        self.assertEqual(self.run_fetch("--check"), 0)
        self.assertEqual(self.run_fetch(), 0)
        self.assertEqual(self.calls, [])
        self.assertTrue(all(p.stat().st_mtime > 1000000000 for p in self.dest.iterdir()))
        self.assertEqual(self.run_fetch("--force"), 0)
        self.assertEqual(len(self.calls), 1)
        for name in ("JF-Dot-Shinonome16.ttf", self.mod.JFDOT_LICENSE_NAME):
            for replacement in (None, b"stale"):
                with self.subTest(name=name, replacement=replacement):
                    p = self.dest / name
                    if replacement is None:
                        p.unlink()
                    else:
                        p.write_bytes(replacement)
                    self.assertEqual(self.run_fetch("--check"), 1)
                    self.calls.clear()
                    self.assertEqual(self.run_fetch(), 0)
                    self.assertEqual(self.calls, [self.mod.JFDOT_ZIP_URL])
                    self.assertEqual(self.run_fetch("--check"), 0)

    def test_zip_and_ttf_hash_mismatches_leave_no_outputs(self):
        original = self.mod.JFDOT_ZIP_SHA256
        for which in ("zip", "ttf"):
            with self.subTest(which=which):
                self.mod.JFDOT_ZIP_SHA256 = "0" * 64 if which == "zip" else original
                if which == "ttf":
                    self.mod.JFDOT_MEMBERS["JF-Dot-Shinonome16.ttf"] = (
                        "JF-Dot-Shinonome16.ttf", "0" * 64)
                self.assertEqual(self.run_fetch(), 1)
                self.assertIn("SHA-256 が合わない", self.err.getvalue())
                self.assertFalse(self.dest.exists())

    def test_missing_member_and_corrupt_zip_fail(self):
        self.mod.JFDOT_MEMBERS = {"missing.ttf": ("missing.ttf", sha(self.ttf))}
        self.assertEqual(self.run_fetch(), 1)
        self.assertFalse(self.dest.exists())
        self.zip = b"not a zip"
        self.mod.JFDOT_ZIP_SHA256 = sha(self.zip)
        self.assertEqual(self.run_fetch(), 1)
        self.assertFalse(self.dest.exists())

    def test_offline_zip_and_print_sha(self):
        archive = pathlib.Path(self.tmp.name) / "font.zip"
        archive.write_bytes(self.zip)
        self.assertEqual(self.run_fetch("--print-zip-sha256"), 0)
        self.assertEqual(self.out.getvalue(), self.mod.JFDOT_ZIP_SHA256 + "\n")
        self.assertFalse(self.dest.exists())
        self.assertEqual(self.run_fetch("--zip", str(archive)), 0)
        self.assertEqual(self.calls, [])
        self.assertEqual((self.dest / "JF-Dot-Shinonome16.ttf").read_bytes(), self.ttf)

    def test_release_pins(self):
        mod = load_module()
        self.assertEqual(mod.JFDOT_ZIP_URL,
                         "https://ftp.iij.ad.jp/pub/osdn.jp/users/8/8542/jfdotfont-20150527.zip")
        self.assertEqual(mod.JFDOT_ZIP_SHA256,
                         "8e574faaaa7e27294aec66464aaf427447b2f0ccec8fe00c50e69bbf509f14b1")
        self.assertEqual(mod.JFDOT_MEMBERS, {"JF-Dot-Shinonome16.ttf": (
            "JF-Dot-Shinonome16.ttf",
            "5e265e45349b3328afa67dc3905a3ca3c628cf7c7e0eccea9c7ce8a8acc127cc")})
        self.assertEqual(mod.JFDOT_ZIP_SIZE, 41637317)
        self.assertEqual(mod.JFDOT_TTF_SIZE, 1954728)
        self.assertEqual(sha(mod.JFDOT_LICENSE_TEXT), "a38a9a330458c11317ec5028df7cd4c7b44210446300e20020f942390d5d5dd2")


class FontVendorTest(unittest.TestCase):
    def test_version_license_provenance_and_unmodified_sources(self):
        vendor = ROOT / "userland/lib/font/vendor/ttf-parser-0.21.1"
        record = json.loads((vendor / "UPSTREAM.json").read_text())
        manifest = tomllib.loads((vendor / "Cargo.toml").read_text())
        self.assertEqual(record["name"], "ttf-parser")
        self.assertEqual(record["version"], "0.21.1")
        self.assertEqual(manifest["package"]["version"], record["version"])
        self.assertEqual(record["license"], "MIT")
        self.assertEqual(record["source"],
                         "https://static.crates.io/crates/ttf-parser/ttf-parser-0.21.1.crate")
        self.assertEqual(record["sha256"],
                         "2c591d83f69777866b9126b24c6dd9a18351f177e49d625920d19f989fd31cf8")
        license_text = (vendor / "LICENSE-MIT").read_text()
        self.assertIn("Permission is hereby granted, free of charge", license_text)
        self.assertIn("Copyright", license_text)
        self.assertIn("LICENSE-MIT", record["files"])
        self.assertIn("src/lib.rs", record["files"])
        for rel, digest in record["files"].items():
            self.assertEqual(sha((vendor / rel).read_bytes()), digest, rel)
        self.assertEqual(set(record["files"]), {
            str(p.relative_to(vendor)) for p in vendor.rglob("*")
            if p.is_file() and p.name != "UPSTREAM.json"})
        # T7b-a5 D36 patch: only src/parser.rs, and reverting the recorded hunks
        # reproduces the crates.io original byte for byte.
        patched = {}
        for hunk in record["patches"]:
            patched.setdefault(hunk["file"], []).append(hunk)
        self.assertEqual(set(patched), {"src/parser.rs"})
        for rel, hunks in patched.items():
            text = (vendor / rel).read_text()
            for hunk in hunks:
                self.assertEqual(text.count(hunk["os32"]), 1, hunk["os32"])
                text = text.replace(hunk["os32"], hunk["upstream"])
                self.assertEqual(hunk["upstream_sha256"], hunks[0]["upstream_sha256"])
            self.assertEqual(sha(text.encode()), hunks[0]["upstream_sha256"], rel)
        manifest_path = ROOT / "userland/lib/font/Cargo.toml"
        dependency = tomllib.loads(manifest_path.read_text())["dependencies"]["ttf-parser"]
        self.assertFalse(dependency["default-features"])
        self.assertEqual((manifest_path.parent / dependency["path"]).resolve(), vendor)
        # font_test uses ttf-parser only through the product os32-font crate.
        font_test = tomllib.loads((ROOT / "userland/rust/font_test/Cargo.toml").read_text())["dependencies"]
        self.assertNotIn("ttf-parser", font_test)
        self.assertEqual((ROOT / "userland/rust/font_test" / font_test["os32-font"]["path"]).resolve(),
                         manifest_path.parent.resolve())


def check_mutations():
    # Run the same behavioral tests against compiled copies; never edit production.
    global MUTATION_SOURCE
    source = SCRIPT.read_text()
    mutations = [
        ("zip-sha", "if got != expected:", "if False:"),
        ("ttf-sha", "if got != digest:", "if False:"),
        ("license-output", "[(JFDOT_LICENSE_NAME, JFDOT_LICENSE_TEXT)])", "[])"),
        ("license-cache", "members[JFDOT_LICENSE_NAME] = (JFDOT_LICENSE_NAME, sha256_bytes(JFDOT_LICENSE_TEXT))",
         "pass"),
    ]
    for name, old, new in [("control", "if got != expected:", "if got != expected:")] + mutations:
        if source.count(old) != 1:
            raise AssertionError("mutation anchor: " + name)
        MUTATION_SOURCE = source.replace(old, new, 1)
        compile(MUTATION_SOURCE, str(SCRIPT), "exec")
        suite = unittest.defaultTestLoader.loadTestsFromTestCase(JfDotTest)
        result = unittest.TextTestRunner(stream=io.StringIO()).run(suite)
        if result.errors or result.wasSuccessful() != (name == "control"):
            raise AssertionError((name, result.errors, result.failures))
        print("MUTATE %s: %s" % (name, "GREEN" if name == "control" else "RED"))
    MUTATION_SOURCE = None


if __name__ == "__main__":
    mutations_requested = "--mutate" in sys.argv
    argv = [a for a in sys.argv if a != "--mutate"]
    program = unittest.main(argv=argv, verbosity=1, exit=False)
    if not program.result.wasSuccessful():
        sys.exit(1)
    if mutations_requested:
        check_mutations()
