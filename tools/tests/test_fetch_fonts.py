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
import os
import pathlib
import sys
import tempfile
import unittest
import zipfile

ROOT = pathlib.Path(__file__).resolve().parents[2]
SCRIPT = ROOT / "tools" / "fetch_fonts.py"
REAL_LICENSE = ROOT / "assets" / "fonts" / "IPA_Font_License_Agreement_v1.0.txt"


def load_module():
    spec = importlib.util.spec_from_file_location("fetch_fonts", str(SCRIPT))
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
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


if __name__ == "__main__":
    unittest.main(verbosity=1)
