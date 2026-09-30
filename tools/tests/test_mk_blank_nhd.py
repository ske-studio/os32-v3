"""Host-only tests for tools/mk_blank_nhd.py (票 S3I2-T 2b).

すべて temp dir の合成ファイル。実 NHD・実 ini・実プロセス・NP21W_DIR には
触れない。ジオメトリの根拠は np21w-src `src/fdd/sxsihdd.h` の NHDHDR
(sig[16] + comment[0x100] + headersize[4] + cylinders[4] + surfaces[2] +
sectors[2] + sectorsize[2] + reserved[0xe2] = 512B)、`sxsihdd.c:13` の
`sig_nhd[15] = "T98HDDIMAGE.R0"`、総セクタ数 = C*H*S と
`(cylinders == 0) || (cylinders >= 65536)` の拒否。
"""
import contextlib
import importlib.util
import io
import os
from pathlib import Path
import re
import struct
import tempfile
import unittest

TOOLS = Path(__file__).resolve().parents[1]
SCRIPT = TOOLS / 'mk_blank_nhd.py'
if SCRIPT.exists():
    spec = importlib.util.spec_from_file_location('mk_blank_nhd', SCRIPT)
    nhd = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(nhd)
else:
    nhd = None

MB = 1024 * 1024


class Availability(unittest.TestCase):
    def test_implementation_exists(self):
        self.assertTrue(SCRIPT.is_file(), 'mk_blank_nhd.py must implement the tested contract')


@unittest.skipIf(nhd is None, 'implementation not yet written (assertion RED)')
class Geometry(unittest.TestCase):
    def test_fixed_geometry(self):
        self.assertEqual((nhd.HEADS, nhd.SECTORS, nhd.SECTOR_SIZE, nhd.HEADER_SIZE),
                         (8, 17, 512, 512))
        self.assertEqual(nhd.SIGNATURE, b'T98HDDIMAGE.R0\x00')

    def test_cylinders_floor_to_a_whole_cylinder(self):
        per_cylinder = 8 * 17 * 512
        for size_mb, expected in ((1, 15), (100, 1505), (200, 3011), (4351, 65520)):
            self.assertEqual(nhd.cylinders(size_mb), expected)
            self.assertEqual(expected, size_mb * MB // per_cylinder)
            self.assertLessEqual(expected * per_cylinder, size_mb * MB)
            self.assertGreater((expected + 1) * per_cylinder, size_mb * MB)

    def test_cylinder_count_outside_np21w_range_is_refused(self):
        # sxsihdd.c: (cylinders == 0) || (cylinders >= 65536) -> open fails.
        for size_mb in (0, -1, 4352, 5000, 1.5, '200', None):
            with self.subTest(size_mb=size_mb), self.assertRaises(ValueError):
                nhd.cylinders(size_mb)
        self.assertEqual(nhd.cylinders(4351), 65520)

    def test_header_is_512_bytes_of_the_documented_layout(self):
        raw = nhd.header(3011)
        self.assertEqual(len(raw), 512)
        self.assertEqual(raw[:16], b'T98HDDIMAGE.R0\x00\x00')
        self.assertEqual(raw[16:0x110], bytes(0x100))
        self.assertEqual(struct.unpack_from('<I', raw, 0x110)[0], 512)
        self.assertEqual(struct.unpack_from('<I', raw, 0x114)[0], 3011)
        self.assertEqual(struct.unpack_from('<HHH', raw, 0x118), (8, 17, 512))
        self.assertEqual(raw[0x11e:], bytes(512 - 0x11e))
        self.assertEqual(raw, b'T98HDDIMAGE.R0\x00\x00' + bytes(0x100) +
                         struct.pack('<IIHHH', 512, 3011, 8, 17, 512) + bytes(0xe2))


@unittest.skipIf(nhd is None, 'implementation not yet written (assertion RED)')
class Image(unittest.TestCase):
    def setUp(self):
        temp = tempfile.TemporaryDirectory()
        self.addCleanup(temp.cleanup)
        self.root = Path(temp.name)
        self.out = self.root / 'os32_fresh.nhd'

    def run_cli(self, args):
        out, err = io.StringIO(), io.StringIO()
        with contextlib.redirect_stdout(out), contextlib.redirect_stderr(err):
            try:
                code = nhd.main(args)
            except SystemExit as exc:  # argparse の引数エラーも同じ 2
                code = exc.code
        return code, out.getvalue(), err.getvalue()

    def test_body_is_exactly_c_by_h_by_s_sectors_of_zero(self):
        count = nhd.create(self.out, 1)
        self.assertEqual(count, 15)
        data = self.out.read_bytes()
        self.assertEqual(len(data), 512 + 15 * 8 * 17 * 512)
        self.assertEqual(data[:512], nhd.header(15))
        self.assertEqual(data[512:], bytes(len(data) - 512))

    def test_default_size_is_200mb(self):
        code, out, _ = self.run_cli(['--out', str(self.out)])
        self.assertEqual(code, 0)
        self.assertEqual(out, 'C=3011 capacity=%d\n' % (3011 * 8 * 17 * 512))
        self.assertEqual(self.out.stat().st_size, 512 + 3011 * 8 * 17 * 512)

    def test_existing_file_is_kept_without_force(self):
        self.out.write_bytes(b'precious')
        code, _, err = self.run_cli(['--out', str(self.out), '--size-mb', '1'])
        self.assertEqual(code, 2)
        self.assertIn('exists', err)
        self.assertEqual(self.out.read_bytes(), b'precious')
        with self.assertRaises(nhd.NhdError):
            nhd.create(self.out, 1)
        code, out, _ = self.run_cli(['--out', str(self.out), '--size-mb', '1', '--force'])
        self.assertEqual((code, out), (0, 'C=15 capacity=%d\n' % (15 * 8 * 17 * 512)))
        self.assertEqual(self.out.stat().st_size, 512 + 15 * 8 * 17 * 512)

    def test_cli_refuses_a_cylinder_count_np21w_cannot_open(self):
        for size in ('0', '4352', 'x'):
            with self.subTest(size=size):
                code, _, _ = self.run_cli(['--out', str(self.out), '--size-mb', size])
                self.assertEqual(code, 2)
                self.assertFalse(self.out.exists())

    def test_symlink_destination_is_refused(self):
        target = self.root / 'elsewhere.bin'
        target.write_bytes(b'keep')
        link = self.root / 'link.nhd'
        link.symlink_to(target)
        code, _, _ = self.run_cli(['--out', str(link), '--size-mb', '1', '--force'])
        self.assertEqual(code, 2)
        self.assertEqual(target.read_bytes(), b'keep')


@unittest.skipIf(nhd is None, 'implementation not yet written (assertion RED)')
class DeployConstants(unittest.TestCase):
    """tools/nhd_deploy.py と同じ値であること。環境変数を読む import は避け、
    定数はソースの字面から読む ([D3]: .env / 資格情報に触れない)。"""

    def setUp(self):
        self.source = (TOOLS / 'nhd_deploy.py').read_text(encoding='utf-8')

    def find(self, pattern):
        match = re.search(pattern, self.source)
        self.assertIsNotNone(match, pattern)
        return match

    def test_header_and_sector_size_match_the_deploy_tool(self):
        header_sectors = int(self.find(r'NHD_HEADER_SECTORS = (\d+)')[1])
        sector = int(self.find(r'PARTITION_OFFSET = PARTITION_SKIP \* (\d+)')[1])
        self.assertEqual(nhd.SECTOR_SIZE, sector)
        self.assertEqual(nhd.HEADER_SIZE, header_sectors * sector)

    def test_geometry_matches_the_deploy_tool(self):
        heads, sectors, per_cylinder = (int(v) for v in
                                        self.find(r'(\d+)H x (\d+)SPT = (\d+)sec/cyl').groups())
        self.assertEqual((nhd.HEADS, nhd.SECTORS), (heads, sectors))
        self.assertEqual(nhd.HEADS * nhd.SECTORS, per_cylinder)
        partition_lba = int(self.find(r'HDD_PARTITION_LBA = (\d+)')[1])
        self.assertEqual(partition_lba % per_cylinder, 0, 'ext2 start stays cylinder aligned')

    def test_a_200mb_image_holds_the_deploy_partition(self):
        partition_lba = int(self.find(r'HDD_PARTITION_LBA = (\d+)')[1])
        self.assertGreater(nhd.cylinders(200) * nhd.HEADS * nhd.SECTORS, partition_lba)


if __name__ == '__main__':
    unittest.main()
