"""Fixed T2h image names and conservative guest-start invalidation."""
import json
import os
from pathlib import Path

PROFILE = 't2h'
REMOTE_NAME = 'os32_t2h_install.nhd'
LOCAL_NAME = 'os32_t2h.nhd'
MOUNT = '/tmp/os32_t2h'
STAMP_SUFFIX = '.pulled'


def paths(root, remote_dir):
    local = os.path.join(root, 'build', 'nhd', LOCAL_NAME)
    return (os.path.join(remote_dir, REMOTE_NAME), local, MOUNT,
            local + STAMP_SUFFIX)


def check_paths(root, remote_dir, remote, local, mount, stamp):
    actual = (remote, local, mount, stamp)
    expected = paths(root, remote_dir)
    defaults = (os.path.join(remote_dir, 'os32.nhd'),
                os.path.join(root, 'build', 'nhd', 'os32.nhd'),
                '/tmp/os32', os.path.join(root, 'build', 'nhd', 'os32.nhd.pulled'))
    for got, want, default in zip(actual, expected, defaults):
        if os.path.abspath(got) != os.path.abspath(want):
            raise ValueError('t2h profile path mismatch: ' + got)
        # Reject aliases to production even when their lexical names are correct.
        if os.path.realpath(got) == os.path.realpath(default) or (
                os.path.exists(got) and os.path.exists(default) and os.path.samefile(got, default)):
            raise ValueError('t2h profile aliases production: ' + got)


def mark_guest_started(root=None):
    """Invalidate before launching: failure to launch is conservatively stale too.

    Any emulator start invalidates this worktree's T2h pull. No ini is edited.
    A missing receipt needs no marker: verify-set already refuses it.
    """
    root = root or Path(__file__).resolve().parents[1]
    stamp = Path(root) / 'build' / 'nhd' / (LOCAL_NAME + STAMP_SUFFIX)
    if not stamp.exists():
        return
    # Deletion is sufficient and fail-closed if an old/corrupt stamp is encountered.
    try:
        data = json.loads(stamp.read_text())
        if not isinstance(data, dict):
            raise ValueError('invalid stamp')
    except (ValueError, OSError):
        stamp.unlink()
        return
    data['guest_started'] = True
    temp = stamp.with_name(stamp.name + '.tmp')
    with temp.open('w') as f:
        json.dump(data, f, sort_keys=True)
        f.flush()
        os.fsync(f.fileno())
    temp.replace(stamp)
