"""Host dispatch regression: no emulator, shared build, or device access."""
CHECK = dict(name='boot-splash-host', tier=1,
    covers=['drivers/video/pc98/pc98_route.h', 'gfx/gfx_vram.c', 'drivers/video/pc98/backend_pc98.c', 'gfx/gfx_core.c', 'init/boot_splash.c', 'tools/tests/boot_splash_native_host.c', 'tools/tests/serial_diag_host.h', 'lib/serial_diag.h', 'include/platform_splash.h', 'platform/pc98/splash.c', 'tools/tests/kernel_includes.py'],
    needs=[], budget=1.400906)

from kernel_includes import include_flags
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
        # CPL0 fixture aborts on any unexpected USER bridge/B1 access.
        command = ["gcc", "-std=gnu11", "-Wall", "-Wextra", "-Werror",
                   "-Wno-unused-function", "-D__KERNEL_BUILD__",
                   "-Wl,--gc-sections"]
        command += include_flags(ROOT)
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


def run(ctx) -> int:
    import os
    import shlex
    import subprocess
    base_env = dict(os.environ, HOST32_RUNNERS=' '.join(ctx.runners))
    env = dict(base_env)
    argv = ['python3', '-B', 'tools/tests/test_boot_splash_native.py']
    rc = subprocess.call(argv, cwd=ctx.root, env=env, close_fds=False)
    if rc:
        return rc
    return 0
