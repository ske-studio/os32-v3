"""Host dispatch regression: no emulator, shared build, or device access."""
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]


class NativeBootTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.tmp = tempfile.TemporaryDirectory(prefix="os32-boot-native-")
        cls.binary = Path(cls.tmp.name) / "boot-native"
        command = ["gcc", "-std=gnu89", "-Wall", "-Wextra", "-Werror",
                   "-Wno-unused-function", "-D__KERNEL_BUILD__"]
        command += ["-I" + str(ROOT / p) for p in
                    ("include", "sdk/include", "sdk/include/os32", "gfx", "lib", "kernel")]
        command += [str(ROOT / "tools/tests/boot_splash_native_host.c"),
                    "-o", str(cls.binary)]
        subprocess.run(command, cwd=ROOT, check=True)

    @classmethod
    def tearDownClass(cls):
        cls.tmp.cleanup()

    def test_native_boot_preserves_each_gui_preference(self):
        for pref in range(4):
            with self.subTest(preference=pref):
                result = subprocess.run([str(self.binary), str(pref)],
                                        capture_output=True, text=True, timeout=60)
                self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_invalid_native_state_cleans_up_and_can_retry(self):
        for pref in range(4):
            with self.subTest(preference=pref):
                result = subprocess.run([str(self.binary), str(pref), "fault"],
                                        capture_output=True, text=True, timeout=60)
                self.assertEqual(result.returncode, 0, result.stdout + result.stderr)


if __name__ == "__main__":
    unittest.main()
