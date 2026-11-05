"""Read-only SFS block RPC backend (NET_HOSTSVC_DESIGN §4, addendum #11).

H1 owns framing and duplicate-request caching. This module owns session FDs;
wire identity is size + mtime_ns, with no independent host media counter.
"""
import errno
import os
import struct
from typing import Mapping

# fs/serialfs/sfs_proto.h D1 wire constants; checked by test_hostsvc_image.py.
SFS_T_BOPEN = 0x30
SFS_T_BREAD = 0x31
SFS_T_BCLOSE = 0x32
SFS_BLOCK_ALIAS_MIN = 1
SFS_BLOCK_ALIAS_MAX = 63
SFS_LOGICAL_BLOCK_SIZE = 512
SFS_BREAD_COUNT_MIN = 1
SFS_BREAD_COUNT_MAX = 2
SFS_BREAD_REQ_LEN = 10
SFS_BCLOSE_REQ_LEN = 4
U32_MAX = 0xffffffff
U64_MAX = 0xffffffffffffffff

# sdk/include/os32/os32_kapi_shared.h, same statuses as SerialFS.
ERR_IO = -1
ERR_NOTFOUND = -2
ERR_NOSPC = -4
ERR_INVAL = -9
ERR_STALE = -11


def _identity(fd):
    st = os.fstat(fd)
    return st.st_size, st.st_mtime_ns


class ImageStore:
    def __init__(self, images: Mapping[str, str]) -> None:
        self.images = dict(images)
        self._sessions = {}
        # Never reuse a closed handle, even across session replacement.
        self._next_handle = 1

    def open_session(self, sid: int) -> None:
        self.close_session(sid)
        self._sessions[sid] = {}

    def close_session(self, sid: int) -> None:
        for fd, _ in self._sessions.pop(sid, {}).values():
            os.close(fd)

    def dispatch(self, sid: int, op: int, payload: bytes) -> bytes:
        if sid not in self._sessions:
            return struct.pack('<i', ERR_STALE)
        try:
            if op == SFS_T_BOPEN:
                return self._open(self._sessions[sid], payload)
            if op == SFS_T_BREAD:
                return self._read(self._sessions[sid], payload)
            if op == SFS_T_BCLOSE:
                return self._close(self._sessions[sid], payload)
            return struct.pack('<i', ERR_INVAL)
        except OSError as exc:
            status = {errno.ENOENT: ERR_NOTFOUND, errno.EMFILE: ERR_NOSPC,
                      errno.ENFILE: ERR_NOSPC}.get(exc.errno, ERR_IO)
            return struct.pack('<i', status)

    def _open(self, handles, payload):
        if not SFS_BLOCK_ALIAS_MIN <= len(payload) <= SFS_BLOCK_ALIAS_MAX or b'\0' in payload:
            return struct.pack('<i', ERR_INVAL)
        try:
            alias = payload.decode('utf-8')
        except UnicodeDecodeError:
            return struct.pack('<i', ERR_INVAL)
        if alias not in self.images:
            return struct.pack('<i', ERR_NOTFOUND)
        if self._next_handle > U32_MAX:
            return struct.pack('<i', ERR_NOSPC)
        fd = os.open(self.images[alias], os.O_RDONLY)
        keep = False
        try:
            size, mtime = _identity(fd)
            if (size < 0 or size % SFS_LOGICAL_BLOCK_SIZE or
                    size > U32_MAX * SFS_LOGICAL_BLOCK_SIZE or not 0 <= mtime <= U64_MAX):
                return struct.pack('<i', ERR_INVAL)
            handle = self._next_handle
            response = struct.pack('<iIQQ', 0, handle, size, mtime)
            handles[handle] = fd, (size, mtime)
            self._next_handle += 1
            keep = True
            return response
        finally:
            if not keep:
                os.close(fd)

    def _read(self, handles, payload):
        if len(payload) != SFS_BREAD_REQ_LEN:
            return struct.pack('<i', ERR_INVAL)
        handle, lba, count = struct.unpack('<IIH', payload)
        if handle not in handles:
            return struct.pack('<i', ERR_STALE)
        if not SFS_BREAD_COUNT_MIN <= count <= SFS_BREAD_COUNT_MAX:
            return struct.pack('<i', ERR_INVAL)
        fd, identity = handles[handle]
        if _identity(fd) != identity:
            return struct.pack('<i', ERR_STALE)
        offset = lba * SFS_LOGICAL_BLOCK_SIZE
        length = count * SFS_LOGICAL_BLOCK_SIZE
        # Python arithmetic does not wrap; compare the full end to the image.
        if offset + length > identity[0]:
            return struct.pack('<i', ERR_INVAL)
        data = os.pread(fd, length, offset)
        if _identity(fd) != identity:
            return struct.pack('<i', ERR_STALE)
        if len(data) != length:
            return struct.pack('<i', ERR_IO)
        return struct.pack('<i', 0) + data

    def _close(self, handles, payload):
        if len(payload) != SFS_BCLOSE_REQ_LEN:
            return struct.pack('<i', ERR_INVAL)
        handle, = struct.unpack('<I', payload)
        entry = handles.pop(handle, None)
        if entry is None:
            return struct.pack('<i', ERR_STALE)
        os.close(entry[0])
        return struct.pack('<i', 0)
