"""HMD1 virtual files (HSYNC_MANIFEST_DESIGN §§2,3,6; M1-M3).

C owns one TreeManifest per HostFS and calls close() from HostFS.close().
stat/read return None for ordinary paths, otherwise HostFS-style (0, bytes).
Translate ManifestError.code using the caller's SfsError; backend exceptions
are left intact outside generation. SFS replay caching belongs to the caller.
Only the six M3 HostFS helpers are used; no transport module is imported.
"""
import os
import stat
import struct
import time
import zlib

ERR_IO, ERR_NOTFOUND, ERR_INVAL, ERR_STALE, ERR_FULL = -1, -2, -9, -11, -13
BUSY, READY, ERROR = 0, 1, 2
RESERVED = b'.os32-manifest'
MAX_BYTES, MAX_ENTRIES = 1048576, 8192
CHUNK = 65536
BUDGET_NS, TTL_NS = 500000000, 120000000000


class ManifestError(Exception):
    def __init__(self, code):
        super().__init__(code)
        self.code = code


def _identity(st):
    return (st.st_dev, st.st_ino, stat.S_IFMT(st.st_mode), st.st_size,
            st.st_mtime_ns, st.st_ctime_ns)


class TreeManifest:
    def __init__(self, fs, *, clock=time.monotonic_ns, nonce=None):
        self.fs = fs
        self.clock = clock
        self.nonce = os.urandom(8) if nonce is None else nonce
        if len(self.nonce) != 8:
            raise ValueError('nonce must contain eight bytes')
        self.sequence = 0
        self.job = None

    def close(self):
        """Release the iterator (including its suspended file fd) and bytes."""
        if self.job is not None:
            iterator = self.job['iterator']
            if iterator is not None:
                iterator.close()
            self.job = None

    def _route(self, wire):
        comps = self.fs._comps(wire)
        if RESERVED not in comps:
            return None
        index = comps.index(RESERVED)
        root = comps[:index]
        try:
            self.fs._stat_raw(root + [RESERVED])
        except Exception as exc:
            if not (getattr(exc, 'code', None) == ERR_NOTFOUND or
                    isinstance(exc, FileNotFoundError)):
                raise
        else:
            return None  # An existing real name takes precedence.
        tail = comps[index + 1:]
        if tail == [b'new']:
            return root, b'new', None
        if len(tail) == 2 and tail[0] in (b'status', b'data'):
            token = tail[1]
            if len(token) == 32 and all(c in b'0123456789abcdef' for c in token):
                return root, tail[0], token
        raise ManifestError(ERR_NOTFOUND)

    def _lookup(self, root, token):
        job = self.job
        now = self.clock()
        if job is not None and now - job['access'] >= TTL_NS:
            self.close()
            job = None
        if job is None or job['root'] != root or job['token'].hex().encode() != token:
            raise ManifestError(ERR_NOTFOUND)
        job['access'] = now
        return job

    def stat(self, wire):
        route = self._route(wire)
        if route is None:
            return None
        root, kind, token = route
        size = 32
        if kind != b'new':
            job = self._lookup(root, token)
            if kind == b'data':
                if job['state'] != READY:
                    raise ManifestError(ERR_IO)
                size = len(job['data'])
        return 0, struct.pack('<BII', 1, size, 0)

    def read(self, wire, offset, count):
        route = self._route(wire)
        if route is None:
            return None
        if offset < 0 or count < 0:
            raise ManifestError(ERR_INVAL)
        root, kind, token = route
        if kind == b'new':
            if offset != 0:
                raise ManifestError(ERR_INVAL)
            self.close()  # new replaces the previous job, even while BUSY.
            self.sequence += 1
            token_bytes = self.nonce + self.sequence.to_bytes(8, 'little')
            now = self.clock()
            job = dict(root=root, token=token_bytes, state=BUSY, error=0,
                       data=b'', iterator=None, access=now, started=now,
                       elapsed_ns=0, crc_bytes=0, files=0, busy_reads=0,
                       skipped_names=0)
            self.job = job
            job['iterator'] = self._generate(job)
        else:
            job = self._lookup(root, token)
        if kind == b'data':
            if job['state'] != READY:
                raise ManifestError(ERR_IO)
            return 0, job['data'][offset:offset + count]
        self._advance(job)
        descriptor = struct.pack('<4sHH16sIi', b'HMD1', 1, job['state'],
                                 job['token'], len(job['data']), job['error'])
        return 0, descriptor[offset:offset + count]

    def _advance(self, job):
        deadline = self.clock() + BUDGET_NS
        try:
            while job['state'] == BUSY and self.clock() < deadline:
                next(job['iterator'])
        except StopIteration:
            job['iterator'] = None
        except Exception as exc:
            job['state'] = ERROR
            job['error'] = getattr(exc, 'code', ERR_IO)
            job['data'] = b''
            job['iterator'].close()
            job['iterator'] = None
        job['elapsed_ns'] = self.clock() - job['started']
        if job['state'] == BUSY:
            job['busy_reads'] += 1

    def _file_crc(self, job, comps, original):
        fd = self.fs.open_read_fd(comps)
        try:
            before = os.fstat(fd)
            if _identity(original) != _identity(before):
                raise ManifestError(ERR_STALE)
            crc, length = 0, 0
            while True:
                yield  # Budget is checked immediately before each <=64 KiB read.
                block = os.read(fd, CHUNK)
                length += len(block)
                job['crc_bytes'] += len(block)
                crc = zlib.crc32(block, crc)
                yield  # A slow single OS read can exceed the budget (note 1).
                if not block:
                    break
            after = os.fstat(fd)
            if _identity(before) != _identity(after) or length != before.st_size:
                raise ManifestError(ERR_STALE)
            job['files'] += 1
            return crc & 0xffffffff
        finally:
            os.close(fd)

    def _generate(self, job):
        records = bytearray()
        count = 0

        def walk(comps, relative):
            nonlocal count
            before = self.fs._stat_raw(comps)
            if self.fs._kind(before)[0] != 2:
                raise ManifestError(ERR_IO)
            entries = self.fs._entries(comps)
            if _identity(before) != _identity(self.fs._stat_raw(comps)):  # enumeration
                raise ManifestError(ERR_STALE)
            for name, _kind, _size in sorted(entries):
                yield
                if len(name) > 255 or b'\\' in name:
                    job['skipped_names'] += 1
                    continue
                path = relative + (b'/' if relative else b'') + name
                if not name or len(path) > 65535:
                    raise ManifestError(ERR_IO)
                if count >= MAX_ENTRIES or 24 + len(records) + 16 + len(path) > MAX_BYTES:
                    raise ManifestError(ERR_FULL)
                child = comps + [name]
                st = self.fs._stat_raw(child)
                kind, size = self.fs._kind(st)
                if kind not in (1, 2, 3) or not 0 <= size <= 0x7fffffff:
                    raise ManifestError(ERR_IO)
                crc = 0
                if kind == 1:
                    crc = yield from self._file_crc(job, child, st)
                else:
                    size = 0
                records.extend(struct.pack('<BBHIII', kind, 0, len(path), size,
                                           self.fs.wire_mtime(st), crc) + path)
                count += 1
                if kind == 2:
                    yield from walk(child, path)
            if _identity(before) != _identity(self.fs._stat_raw(comps)):  # subtree
                raise ManifestError(ERR_STALE)

        yield from walk(job['root'], b'')
        header = struct.pack('<4sHHIIII', b'HMT1', 1, 24, 24 + len(records),
                             count, zlib.crc32(records), job['skipped_names'])
        job['data'] = header + bytes(records)
        job['state'] = READY
