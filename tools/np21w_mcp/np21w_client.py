"""HTTP client for the NP21/W embedded AI debug server.

Prefers a direct connection from WSL. That works when WSL runs in mirrored
networking mode (``networkingMode=mirrored`` in ``.wslconfig``), where the
Windows loopback is reachable as 127.0.0.1. Under NAT mode -- or when a
firewall rule blocks the port -- it falls back to the Windows curl.exe, which
always reaches the emulator because it runs on the Windows side.

The probe result is cached for the life of the process.

Endpoints that touch the host (POST /api/quit, /api/fdd, /api/state/save)
require the per-process token NP21/W writes next to its exe
(np21w_aidebug_<port>.token, readable by the user only). token_headers()
finds and reads it; callers pass the result as ``headers=``. The token
itself is never logged.
"""

import json
import os
import re
import subprocess
import urllib.error
import urllib.request

CURL = "/mnt/c/Windows/System32/curl.exe"
# must match aidbport= in np21x64w.ini
BASE = os.environ.get("NP21W_AIDEBUG_URL", "http://127.0.0.1:8025")
TOKEN_HEADER = "X-Aidebug-Token"

_direct = None  # None = not probed yet, True/False = probe result


class EmuError(Exception):
    pass


def _hint(rc):
    """Translate a curl exit code into something actionable, or None."""
    if rc == 7:
        return ("cannot connect to %s -- NP21/W is probably not running, or "
                "its aidebug server is off (check aidebug=true / aidbport in "
                "np21x64w.ini)" % BASE)
    if rc == 28:
        return ("request timed out -- the emulator may be stopped at a "
                "breakpoint or busy; try emu_status")
    if rc == 56:
        return "connection reset by the emulator (it may have just exited)"
    return None


def _run(args, timeout):
    try:
        p = subprocess.run(args, capture_output=True, timeout=timeout)
    except FileNotFoundError:
        raise EmuError("curl.exe not found at %s" % CURL)
    except subprocess.TimeoutExpired:
        raise EmuError("request timed out")
    if p.returncode != 0:
        raise EmuError(_hint(p.returncode) or
                       "curl failed (rc=%d): %s"
                       % (p.returncode,
                          p.stderr.decode("utf-8", "replace")[:200]))
    return p.stdout


def _direct_available():
    """Probe once whether WSL can reach the emulator without Windows curl."""
    global _direct
    if _direct is None:
        try:
            urllib.request.urlopen(BASE + "/api/status", timeout=3).read()
            _direct = True
        except Exception:
            _direct = False
    return _direct


def _http(path, body=None, timeout=20, headers=None):
    """Direct request. body=None -> GET, otherwise POST. Returns bytes."""
    data = None
    if body is not None:
        data = body if isinstance(body, bytes) else body.encode("utf-8")
    req = urllib.request.Request(BASE + path, data=data,
                                 method="POST" if data is not None else "GET",
                                 headers=dict(headers or {}))
    try:
        with urllib.request.urlopen(req, timeout=timeout) as r:
            return r.read()
    except urllib.error.HTTPError as e:
        # The API reports its own errors as JSON bodies; hand them back
        # rather than raising, so callers see the message.
        return e.read()
    except Exception as exc:
        raise EmuError("direct request to %s failed: %s" % (BASE + path, exc))


def get(path, timeout=15):
    """GET path (may include ?query); returns response bytes."""
    if _direct_available():
        return _http(path, None, timeout)
    return _run([CURL, "-s", "-m", str(timeout), BASE + path], timeout + 5)


def post(path, body=None, timeout=20, headers=None):
    """POST path with an optional raw string body; returns response bytes.
    headers: extra request headers ({name: value}), e.g. token_headers()."""
    if _direct_available():
        return _http(path, body if body is not None else b"", timeout, headers)
    args = [CURL, "-s", "-m", str(timeout), "-X", "POST", BASE + path]
    for k, v in (headers or {}).items():
        args += ["-H", "%s: %s" % (k, v)]
    if body is not None:
        args += ["--data-binary", body]
    return _run(args, timeout + 5)


def _np21w_dir():
    """NP21W_DIR: environment, then .env / .env.sample at the repo root
    (same rule as tools/np21w_ctl.py). None when unknown."""
    if os.environ.get("NP21W_DIR"):
        return os.environ["NP21W_DIR"]
    root = os.path.dirname(os.path.dirname(os.path.dirname(
        os.path.abspath(__file__))))
    for name in (".env", ".env.sample"):
        try:
            with open(os.path.join(root, name), "r", errors="replace") as f:
                for line in f:
                    if line.strip().startswith("NP21W_DIR="):
                        return line.strip().split("=", 1)[1].strip()
        except OSError:
            pass
    return None


def token_file():
    """WSL path of the token file: NP21W_AIDEBUG_TOKEN_FILE, then the
    token_file reported by GET /api/instance (C: path -> /mnt/c/...), then
    NP21W_DIR/np21w_aidebug_<port>.token."""
    env = os.environ.get("NP21W_AIDEBUG_TOKEN_FILE")
    if env:
        return env
    try:
        inst = json.loads(get("/api/instance", timeout=5))
        m = re.match(r"^([A-Za-z]):[\\/](.*)$", inst.get("token_file") or "")
        if m:
            return "/mnt/%s/%s" % (m.group(1).lower(),
                                   m.group(2).replace("\\", "/"))
    except (EmuError, ValueError, AttributeError):
        pass
    d = _np21w_dir()
    if not d:
        raise EmuError("cannot locate the aidebug token file: set "
                       "NP21W_AIDEBUG_TOKEN_FILE or NP21W_DIR")
    m = re.search(r":(\d+)/*$", BASE)
    port = int(m.group(1)) if m else 8025
    return os.path.join(d.rstrip("/"), "np21w_aidebug_%d.token" % port)


def token_headers():
    """{TOKEN_HEADER: token} for /api/quit, /api/fdd, /api/state/save."""
    path = token_file()
    try:
        with open(path, "r", errors="replace") as f:
            tok = f.read().strip()
    except OSError as exc:
        raise EmuError("cannot read the aidebug token %s (%s): NP21/W writes "
                       "it next to its exe when the aidebug server starts; "
                       "GET /api/instance token_error says why it is missing"
                       % (path, exc.__class__.__name__))
    if not re.fullmatch(r"[0-9A-Fa-f]{16,}", tok):
        raise EmuError("aidebug token file %s has an unexpected format" % path)
    return {TOKEN_HEADER: tok}


def get_to_file(path, out_path, timeout=20):
    """GET path, writing the (possibly binary) body to out_path."""
    if _direct_available():
        with open(out_path, "wb") as f:
            f.write(_http(path, None, timeout))
        return out_path
    _run([CURL, "-s", "-m", str(timeout), "-o", out_path, BASE + path],
         timeout + 5)
    return out_path
