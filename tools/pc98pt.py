"""PC-98 区画表 (LBA 1) の読み書き — drivers/pc98pt.c のホスト側の写し。

票 docs/archive/realhw_v21/TASK_HDD_INSTALL.md 段 1-4 (F10) / §1-v3 N3。
**配置と規則の正典は drivers/pc98pt.h**。ここは同じ値を Python で作るだけで、
tools/tests/test_hdd_stage1.py が C の実物 (drivers/pc98pt.c) と 1 バイトずつ
突き合わせる。

標準配置 (32 B × 16):
  +0 mid  +1 sid  +2-3 予約
  +4 IPL セクタ  +5 IPL ヘッド  +6-7 IPL シリンダ
  +8 開始セクタ  +9 開始ヘッド  +10-11 開始シリンダ
  +12 終了セクタ +13 終了ヘッド +14-15 終了シリンダ
  +16 名前 16B
区画はシリンダ単位。終わり (排他) = (終了シリンダ + 1) × heads × spt。

2026-09-23 までの OS32 の**旧配置** (cdinst / install / 旧 nhd_deploy.py が書いた):
  +6 開始セクタ  +7 開始ヘッド  +8-9 開始シリンダ
  +10 終了セクタ +11 終了ヘッド +12-13 終了シリンダ
旧配置を読むのは移行 (`nhd_deploy.py migrate-pt`) の 1 か所だけ。
"""
import struct

SECTOR_SIZE = 512
ENTRY_SIZE = 32
MAX_ENTRIES = 16
PT_LBA = 1

OFF_MID, OFF_SID = 0, 1
OFF_IPL_SECT, OFF_IPL_HEAD, OFF_IPL_CYL = 4, 5, 6
OFF_SSECT, OFF_SHEAD, OFF_SCYL = 8, 9, 10
OFF_ESECT, OFF_EHEAD, OFF_ECYL = 12, 13, 14
OFF_NAME, NAME_LEN = 16, 16

MID_BOOTABLE = 0x80
SID_OS32 = 0xE2
NAME_OS32 = b"OS32"
MAX_CYL = 0xFFFF


class PtError(ValueError):
    """区画表の値が規則に合わない。"""


def _geom_ok(heads, spt):
    if not (1 <= heads <= 255 and 1 <= spt <= 255):
        raise PtError("幾何が不正: heads={} spt={}".format(heads, spt))


def chs_to_lba(cyl, head, sect, heads, spt):
    """HDD BIOS の CHS (セクタは 0 始まり) → LBA。"""
    _geom_ok(heads, spt)
    if head >= heads or sect >= spt or cyl > MAX_CYL:
        raise PtError("CHS が幾何の外: {}/{}/{} (H={} S={})".format(
            cyl, head, sect, heads, spt))
    return (cyl * heads + head) * spt + sect


def make_os32(start, length, heads, spt):
    """[start, start+length) を覆う OS32 の項目 (32 B)。pc98pt_make_os32 と同じ。"""
    _geom_ok(heads, spt)
    if length <= 0:
        raise PtError("長さが 0")
    cyl = heads * spt
    if start % cyl or length % cyl:
        raise PtError("開始 {} / 長さ {} がシリンダ ({}) の倍数でない".format(
            start, length, cyl))
    scyl = start // cyl
    ecyl = scyl + length // cyl - 1
    if scyl > MAX_CYL or ecyl > MAX_CYL:
        raise PtError("シリンダが 16 ビットを超える: {}..{}".format(scyl, ecyl))
    e = bytearray(ENTRY_SIZE)
    e[OFF_MID] = MID_BOOTABLE
    e[OFF_SID] = SID_OS32
    e[OFF_IPL_SECT] = 0
    e[OFF_IPL_HEAD] = 0
    struct.pack_into("<H", e, OFF_IPL_CYL, scyl)
    e[OFF_SSECT] = 0
    e[OFF_SHEAD] = 0
    struct.pack_into("<H", e, OFF_SCYL, scyl)
    e[OFF_ESECT] = spt - 1
    e[OFF_EHEAD] = heads - 1
    struct.pack_into("<H", e, OFF_ECYL, ecyl)
    e[OFF_NAME:OFF_NAME + NAME_LEN] = NAME_OS32.ljust(NAME_LEN, b" ")
    return bytes(e)


def entry_at(sector, idx):
    return bytes(sector[idx * ENTRY_SIZE:(idx + 1) * ENTRY_SIZE])


def entry_empty(ent):
    return ent[OFF_MID] == 0 and ent[OFF_SID] == 0


def used_entries(sector):
    """空でない項目の添字の一覧 (mid / sid は旧配置でも同じ位置)。"""
    return [i for i in range(MAX_ENTRIES) if not entry_empty(entry_at(sector, i))]


def entry_range(ent, heads, spt, disk_total=0):
    """標準配置の項目の (開始 LBA, 長さ)。pc98pt_entry_range と同じ規則。"""
    scyl = struct.unpack_from("<H", ent, OFF_SCYL)[0]
    start = chs_to_lba(scyl, ent[OFF_SHEAD], ent[OFF_SSECT], heads, spt)
    ecyl = struct.unpack_from("<H", ent, OFF_ECYL)[0]
    end = (ecyl + 1) * heads * spt
    if end <= start:
        raise PtError("終わり {} <= 開始 {}".format(end, start))
    if disk_total and end > disk_total:
        raise PtError("終わり {} がディスク ({}) の外".format(end, disk_total))
    return start, end - start


def find_os32(sector, heads, spt, disk_total=0):
    """OS32 の区画 (sid = 0xE2 の最初の項目) → (添字, 開始, 長さ)。無ければ None。"""
    for i in range(MAX_ENTRIES):
        ent = entry_at(sector, i)
        if ent[OFF_SID] != SID_OS32:
            continue
        start, length = entry_range(ent, heads, spt, disk_total)
        return i, start, length
    return None


def legacy_entry_range(ent, heads, spt, disk_total=0):
    """**旧配置** (+6/+7/+8-9 開始、+10/+11/+12-13 終了) の (開始, 長さ)。移行専用。"""
    scyl = struct.unpack_from("<H", ent, 8)[0]
    start = chs_to_lba(scyl, ent[7], ent[6], heads, spt)
    ecyl = struct.unpack_from("<H", ent, 12)[0]
    end = (ecyl + 1) * heads * spt
    if end <= start:
        raise PtError("旧配置: 終わり {} <= 開始 {}".format(end, start))
    if disk_total and end > disk_total:
        raise PtError("旧配置: 終わり {} がディスク ({}) の外".format(end, disk_total))
    return start, end - start


# ---- NHD (T98-NEXT) のヘッダ -------------------------------------------------

NHD_SIG = b"T98HDDIMAGE.R0\x00"


def nhd_geometry(header):
    """NHD ヘッダ (先頭 512 B 以上) → dict(header_size, cylinders, heads, spt, sector_size, total)。"""
    if header[:len(NHD_SIG)] != NHD_SIG:
        raise PtError("NHD の署名が無い")
    header_size, cylinders = struct.unpack_from("<II", header, 0x110)
    heads, spt, sector_size = struct.unpack_from("<HHH", header, 0x118)
    if sector_size != SECTOR_SIZE:
        raise PtError("セクタ長 {} は扱わない".format(sector_size))
    return {
        "header_size": header_size,
        "cylinders": cylinders,
        "heads": heads,
        "spt": spt,
        "sector_size": sector_size,
        "total": cylinders * heads * spt,
    }


def make_nhd_header(cylinders, heads, spt, comment=b"os32 test"):
    """試験用の NHD ヘッダ (512 B)。"""
    h = bytearray(512)
    h[:len(NHD_SIG)] = NHD_SIG
    h[0x10:0x10 + len(comment)] = comment
    struct.pack_into("<II", h, 0x110, 512, cylinders)
    struct.pack_into("<HHH", h, 0x118, heads, spt, SECTOR_SIZE)
    return bytes(h)
