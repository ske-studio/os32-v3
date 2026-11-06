"""Host service names and generation-local LIST ids (design H7).

The agent supplies builtin/plugin handlers. SFS rows advertise the separate
VFS/SFS path; they do not forward requests to lanfs_host.
"""
import logging
import re
from typing import Callable


MAX_SERVICE_ID = 65534
BUILTINS = ('os32.alive', 'os32.clip', 'os32.print', 'os32.http')
SFS_SERVICES = ('os32.file', 'os32.iso9660')
STUBS = ('os32.udf', 'os32.mail', 'os32.feed', 'os32.chat')
LOG = logging.getLogger(__name__)


class Registry:
    def __init__(self, generation: int) -> None:
        self.generation = generation
        self._services = {}
        for origin, names in (('builtin', BUILTINS), ('sfs', SFS_SERVICES),
                              ('stub', STUBS)):
            for name in names:
                self.register(name, 1, origin, None)

    def register(self, name: str, version: int, origin: str,
                 handler: Callable | None) -> int:
        if re.fullmatch(r'[a-z0-9.]+', name) is None:
            raise ValueError('invalid service name: %r' % name)
        if origin not in ('builtin', 'plugin', 'sfs', 'stub'):
            raise ValueError('invalid service origin: %r' % origin)
        previous = self._services.get(name)
        if previous is None:
            service_id = len(self._services) + 1
            if service_id > MAX_SERVICE_ID:
                raise RuntimeError('service id range exhausted')
        else:
            service_id = previous[1]
            LOG.warning('duplicate service %s: last registration wins', name)
        if origin == 'plugin' and name.startswith('os32.'):
            LOG.warning('plugin uses OS32 reserved name %s', name)
        self._services[name] = (version, service_id, origin, handler)
        return service_id

    def resolve(self, name: str) -> int:
        return self._services[name][1]

    def list_bytes(self) -> bytes:
        lines = ['generation %d\n' % self.generation]
        for name, (version, service_id, origin, _) in self._services.items():
            lines.append('%s %d %d %s\n' % (name, version, service_id, origin))
        return ''.join(lines).encode('utf-8')

    def dispatch(self, line: str, data: bytes) -> tuple[int, bytes]:
        if line == 'LIST':
            return 200, self.list_bytes()
        parts = line.split(None, 1)
        service = self._services.get(parts[0]) if parts else None
        if service is None:
            return 404, b''
        handler = service[3]
        if handler is None:
            return 501, b''
        return handler(line, data)
