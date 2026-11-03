"""Successful external ext2 checks, shared only within one suite invocation."""
import hashlib
import os
from pathlib import Path
import shutil
import subprocess


class OracleCache:
    def __init__(self, verify):
        self.verify = verify
        self.successes = {}
        self.versions = {}
        self.hits = 0
        self.misses = 0

    def __call__(self, image, clean):
        # Re-read every input: the producer overwrites the same image pathname.
        # No compiler result is cached; all C assertions and image writes run.
        data = image.read_bytes()
        env = dict(os.environ, LC_ALL='C')
        environment = tuple(sorted(env.items()))
        checker = Path(shutil.which('e2fsck', path=env.get('PATH')) or 'e2fsck').resolve()
        identity = (str(checker), hashlib.sha256(checker.read_bytes()).digest())
        version_key = (identity, environment)
        if version_key not in self.versions:
            version = subprocess.run([str(checker), '-V'], env=env,
                                     capture_output=True, check=True, timeout=30)
            self.versions[version_key] = (version.stdout, version.stderr)
        config = Path(env.get('E2FSCK_CONFIG', '/etc/e2fsck.conf'))
        config_data = config.read_bytes() if config.is_file() else None
        key = (hashlib.sha256(data).digest(), clean, identity,
               self.versions[version_key], environment, config_data)
        if key in self.successes:
            self.hits += 1
            return self.successes[key]
        self.misses += 1
        # Publish only after the scan, diagnostic classification, repair and
        # repaired-media checks all succeed. Zero allowed diagnostics is a hit.
        result = self.verify(image, clean, env=env, checker=str(checker))
        self.successes[key] = result
        return result

    def report(self):
        print(f'external oracle: checked={self.misses}, reused={self.hits}', flush=True)


def check_cache(directory):
    """Exercise invalidation and success-only publication without real media."""
    from unittest.mock import patch

    directory.mkdir()
    image = directory / 'sample.img'
    other = directory / 'other.img'
    checker = directory / 'e2fsck'
    config = directory / 'e2fsck.conf'
    checker.write_bytes(b'version 1')
    config.write_bytes(b'config 1')
    image.write_bytes(b'media 1')
    other.write_bytes(b'media 1')
    calls = []
    failure = None

    def verify(path, clean, *, env, checker):
        assert env['LC_ALL'] == 'C'
        calls.append((path.read_bytes(), clean, checker))
        if failure is not None:
            raise failure
        return 0 if clean else 3

    version = subprocess.CompletedProcess([], 0, b'e2fsck test', b'library test')
    with patch('ext2_oracle_cache.shutil.which', return_value=str(checker)), \
            patch('ext2_oracle_cache.subprocess.run', return_value=version), \
            patch.dict(os.environ, E2FSCK_CONFIG=str(config),
                       OS32_ORACLE_CACHE_TEST='initial'):
        cache = OracleCache(verify)
        assert cache(image, True) == cache(other, True) == 0
        assert len(calls) == 1  # Full content matches across different paths.
        stamp = image.stat()
        image.write_bytes(b'media 2')
        os.utime(image, ns=(stamp.st_atime_ns, stamp.st_mtime_ns))
        assert cache(image, True) == 0 and len(calls) == 2
        assert cache(image, False) == cache(image, False) == 3
        assert len(calls) == 3  # clean and diagnostic count must survive reuse.
        with patch.dict(os.environ, OS32_ORACLE_CACHE_TEST='changed'):
            assert cache(image, False) == 3 and len(calls) == 4
        config.write_bytes(b'config 2')
        assert cache(image, False) == 3 and len(calls) == 5
        stamp = checker.stat()
        checker.write_bytes(b'version 2')
        os.utime(checker, ns=(stamp.st_atime_ns, stamp.st_mtime_ns))
        assert cache(image, False) == 3 and len(calls) == 6
        image.write_bytes(b'failure')
        for failure in (AssertionError('failed oracle'),
                        subprocess.TimeoutExpired('e2fsck', 30)):
            try:
                cache(image, True)
            except type(failure):
                pass
            else:
                raise AssertionError('failed oracle was reused')
        failure = None
        assert cache(image, True) == cache(image, True) == 0
        assert len(calls) == 9
        assert OracleCache(verify)(image, True) == 0 and len(calls) == 10
