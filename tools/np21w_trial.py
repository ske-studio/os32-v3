#!/usr/bin/env python3
"""One approved disk trial. Default dry-run does not open files or processes.

The trusted host selects PID/creation/exe, baseline, cwd and the disk set
(HDD1FILE, FDD detach, optional .d88 launch argument). A local LLM must
propose exactly the immutable one-step plan before the executor is constructed.
Normal close may save settings; baseline is NOT proof of previous active ini.
"""
import argparse
import copy
import json
import ntpath
import os
import re
import sys
import uuid
from types import FunctionType
import np21w_ini_live as live
from np21w_ini import (ALLOWED_PATHS, IniError, LIMIT, SECTION, image_name,
                       resolve_image, transform, windows_path)
from emu_agent.playbook import strict_object, exact_equal


def transform_trial(raw, changes):
    """changes は承認済み計画の変更集合。ini 表に無い e_resume だけ別に扱う。"""
    if not isinstance(changes, dict) or changes.get('e_resume') != 'false':
        raise IniError('trial requires an explicit e_resume=false in the plan')
    candidate, diff = transform(raw, {k: v for k, v in changes.items() if k != 'e_resume'})
    # ini.cpp:848 PFTYPE_BOOL e_resume -> np2oscfg.resume; np2.cpp:4655
    # loads saved VM state when true. Require a known explicit value.
    parts = re.split(b'(\r\n|\r|\n)', candidate)
    section, found = None, []
    for i in range(0, len(parts), 2):
        line = parts[i]
        body = line[3:] if i == 0 and line.startswith(b'\xef\xbb\xbf') else line
        stripped = body.strip(b' \t')
        if stripped.startswith(b'['):
            section = stripped.split(b']', 1)[0][1:].strip().lower()
        elif section == SECTION:
            match = re.fullmatch(rb'([ \t]*e_resume[ \t]*=[ \t]*)([^;#]*)([;#].*)?', body, re.I)
            if match:
                token = match[2].rstrip(b' \t')
                if token not in (b'true', b'false'):
                    raise IniError('unknown resume value')
                found.append((i, len(line) - len(body) + match.start(2), token))
    if len(found) != 1:
        raise IniError('missing or duplicate e_resume')
    i, start, token = found[0]
    if token == b'true':
        parts[i] = parts[i][:start] + b'false' + parts[i][start + len(token):]
        diff.append('e_resume: true -> false')
    candidate = b''.join(parts)
    if len(candidate) > LIMIT:
        raise IniError('trial exceeds snapshot limit')
    return candidate, diff


# 失敗理由に出してよい語彙 (固定語・記号のみ)。生の例外文・パス・ini 本文は
# 通さない。IniError の文言はこのモジュール内の固定リテラルだけ。
REASON_CODE = re.compile(r'[a-z][a-z0-9 ,:_-]{0,63}')


def _reason_detail(exc):
    for value in (getattr(exc, 'code', None),
                  str(exc) if isinstance(exc, IniError) else None):
        if type(value) is str and REASON_CODE.fullmatch(value):
            return value
    return None


def _coded(error, code):
    """固定語彙の理由を例外に付ける (結果 JSON の reason に出る唯一の経路)。"""
    error.code = code
    return error


def _rejected(response):
    """executor の失敗応答から、固定語彙の理由と起動 PID **だけ**を取り出す。"""
    error = IniError('trial executor rejected operation')
    if type(response) is dict:
        code = response.get('code')
        if type(code) is str and REASON_CODE.fullmatch(code):
            error.code = 'executor: ' + code
        pid = response.get('pid')
        if type(pid) is int and type(pid) is not bool and 0 < pid <= live.PROCESS_ID_MAX:
            error.started_pid = pid
    return error


def _identity(row):
    if (type(row) is not dict or set(row) != {'pid', 'created', 'exe', 'command'} or
            type(row['pid']) is not int or not 0 < row['pid'] <= live.PROCESS_ID_MAX or
            type(row['created']) is not str or not re.fullmatch(
                r'\d{4}-\d\d-\d\dT\d\d:\d\d:\d\d\.\d{7}Z', row['created']) or
            type(row['command']) is not str or not row['command'] or len(row['command']) > 32767):
        raise IniError('invalid process identity')
    live.path_key(row['exe'])
    return row


def launch_command(plan):
    """`exe + /i<trial ini> [+ <d88>]`。np2arg.cpp Np2Arg::Parse は `/i` の直後を
    ini 名として読み、拡張子で判る `.d88` はディスクとして装着する。

    Win32_Process の CommandLine と**バイト単位で**突き合わせる文字列。実走
    (受入 F1) で観測した実物と同じ形: 各引数が二重引用符、区切りは 1 個の空白
    (.NET の BuildCommandLine が `"<FileName>" ` + Arguments を組む)。
    """
    command = '"' + plan['exe'] + '" "/i' + plan['trial'] + '"'
    if plan['fdd_arg']:
        command += ' "' + plan['fdd_arg'] + '"'
    return command


def identity_mismatch(started, old, plan):
    """起動した行が「承認した計画で起動した新しいプロセス」かを見る。

    exe は Windows のパスなので **大小文字を無視** (`path_key`) して比べる。
    CIM の `ExecutablePath` は実体の綴りで返り、操作者が渡した綴りと一致すると
    は限らない (close 前の照合は最初から path_key を使っていた)。
    `created` は「古い行と違うこと」だけを要求する — CIM の `CreationDate` は
    マイクロ秒までしか持たず (`.7896090Z` のように 7 桁目が 0)、.NET の
    `Process.StartTime` の 100ns 精度とは最後の桁が食い違うため、両者の
    文字列一致は要求できない (実走 F1 の start 段の失敗)。
    """
    reasons = []
    if started['created'] == old['created']:
        reasons.append('created')
    if live.path_key(started['exe']) != live.path_key(plan['exe']):
        reasons.append('exe')
    if started['command'] != launch_command(plan):
        reasons.append('command')
    return reasons


def identity_unstable(current, started):
    """起動確認の 2 回の照会で、同じ行が返り続けているかを見る。

    CIM の行が揺れる (同じ PID で `command` の綴りだけ違う、`created` が
    変わる…) ときに、どの項目が揺れたかを固定語彙で返す。exe だけは Windows の
    パスとして大小文字を無視する (start 段の照合と同じ)。
    """
    if type(current) is not list or len(current) != 1:
        return ['rows']
    row = current[0]
    return [key for key in ('pid', 'created', 'exe', 'command')
            if (live.path_key(row[key]) != live.path_key(started[key]) if key == 'exe'
                else row[key] != started[key])]


PLAN_FIELDS = ('exe', 'baseline', 'cwd', 'pid', 'created', 'hdd', 'hdd_host',
               'hdd_path', 'fdd_eject', 'fdd_arg', 'fdd_arg_host')


def _bound_image(name, host, windows, extension):
    """束縛済みの (名前, ホスト側パス, Windows 絶対パス) を**環境変数を読まずに**
    検査する: 3 者の名前が一致し、ホスト側が通常ファイル (非 symlink) であること。
    Windows 表記とホスト側が同じ実体であることまでは見ない — その対応は
    `make_plan()` の `resolve_image()` が解決した時点で決まる (往復 1 の B2)。"""
    image_name(name, extension)
    windows_path(windows)
    if (type(host) is not str or not host.startswith('/') or
            windows.rsplit('\\', 1)[-1] != name or os.path.basename(host) != name):
        raise IniError('bound image paths disagree')
    if os.path.islink(host) or not os.path.isfile(host):
        raise IniError('bound image is not a regular file')


def _plan(*, exe, baseline, cwd, pid, created, hdd, hdd_host, hdd_path,
          fdd_eject, fdd_arg, fdd_arg_host, trial=None):
    """計画の組み立てと、環境に依存しない検査。make_plan と _validate_plan が
    共有するので、束縛後は NP21W_DIR / wslpath を一切見ない。"""
    for path in (exe, baseline, cwd):
        live.path_key(path)
    _identity(dict(pid=pid, created=created, exe=exe, command='operator selected'))
    if (ntpath.basename(exe).lower() != 'np21x64w.exe' or
            live.path_key(baseline) != live.path_key(ntpath.splitext(exe)[0] + '.ini') or
            live.path_key(cwd) != live.path_key(ntpath.dirname(exe))):
        raise IniError('require exe-adjacent baseline and explicit exe-directory cwd')
    if fdd_eject is not True and fdd_eject is not False:
        raise IniError('explicit fdd_eject decision required')
    _bound_image(hdd, hdd_host, hdd_path, ALLOWED_PATHS['HDD1FILE'])
    if (fdd_arg is None) != (fdd_arg_host is None):
        raise IniError('invalid trial launch argument')
    if fdd_arg is not None:
        _bound_image(ntpath.basename(fdd_arg), fdd_arg_host, fdd_arg, '.d88')
        if (ntpath.dirname(fdd_arg).lower() != ntpath.dirname(hdd_path).lower() or
                os.path.dirname(fdd_arg_host) != os.path.dirname(hdd_host)):
            raise IniError('trial images must share one NP21W_DIR')
    changes = {'HDD1FILE': hdd_path, 'e_resume': 'false'}
    if fdd_eject:
        changes.update({key: '' for key in ALLOWED_PATHS if key.startswith('FDD')})
    if trial is None:
        trial = ntpath.join(cwd, 'np21w-trial-' + uuid.uuid4().hex + '.ini')
    live.path_key(trial)
    return dict(action='disk-trial', exe=exe, baseline=baseline, cwd=cwd,
                pid=pid, created=created, trial=trial,
                hdd=hdd, hdd_host=hdd_host, hdd_path=hdd_path,
                fdd_eject=fdd_eject, fdd_arg=fdd_arg, fdd_arg_host=fdd_arg_host,
                changes=changes,
                lifecycle={'close': 'normal-only', 'baseline_read': 'after-verified-exit',
                           'baseline_role': 'operator-chosen-not-active-config-proof',
                           'normal_exit_may_save': True, 'exact_vm_ram_preserved': False,
                           'new_ini_and_cwd': 'explicit', 'retry': False, 'restore': False})


def make_plan(*, exe, baseline, cwd, pid, created, hdd, fdd_eject, fdd_arg=None):
    """hdd / fdd_arg は NP21W_DIR 直下の名前。**環境を見るのはここだけ**で、
    解決した Windows 絶対パスとホスト側パスを計画に束縛する (往復 1 の B2)。
    変更集合は明示したキーだけで、Cirrus 系には触れない (票 S3I2-T)。"""
    hdd_host, hdd_path = resolve_image(hdd, ALLOWED_PATHS['HDD1FILE'])
    fdd_arg_host, argument = (resolve_image(fdd_arg, '.d88') if fdd_arg is not None
                              else (None, None))
    return _plan(exe=exe, baseline=baseline, cwd=cwd, pid=pid, created=created,
                 hdd=hdd, hdd_host=hdd_host, hdd_path=hdd_path, fdd_eject=fdd_eject,
                 fdd_arg=argument, fdd_arg_host=fdd_arg_host)


def _validate_plan(plan):
    try:
        if (type(plan['trial']) is not str or
                not re.fullmatch(r'np21w-trial-[a-f0-9]{32}\.ini', ntpath.basename(plan['trial'])) or
                ntpath.dirname(plan['trial']) != plan['cwd']):
            raise IniError('invalid unique trial path')
        expected = _plan(trial=plan['trial'], **{k: plan[k] for k in PLAN_FIELDS})
        if not exact_equal(plan, expected):
            raise IniError('invalid bounded trial plan')
    except (KeyError, TypeError, AttributeError) as exc:
        raise IniError('invalid bounded trial plan') from exc


def bind_trial(plan, executor_factory, *, authorized=False, exclusive=False):
    """Expose only this single-use callable to the local model; never executor.

    JSON serialization freezes every nested approval field. Models only supply
    strict JSON text; no paths or settings are taken from the returned proposal.
    A rejected proposal also consumes this binding. No side effects precede gate.
    """
    _validate_plan(plan)
    frozen = json.dumps(plan, sort_keys=True)
    used = False
    def dispatch(proposal):
        nonlocal used
        if used:
            raise IniError('trial approval already consumed')
        used = True
        if authorized is not True or exclusive is not True:
            raise IniError('exact trial approval and exclusive operator required')
        approved = json.loads(frozen)
        try:
            if not exact_equal(strict_object(proposal), approved):
                raise ValueError()
        except (ValueError, TypeError, RecursionError):
            raise IniError('local proposal does not exactly match approval') from None
        return _run(approved, executor_factory)
    return dispatch


def _run(plan, factory):
    result = dict(ok=False, stage='executor', trial=plan['trial'], cwd=plan['cwd'],
                  completed=[], retry=False, restore=False, plan=copy.deepcopy(plan))
    try:
        with factory(copy.deepcopy(plan)) as ex:
            def call(op, **args):
                result['stage'] = op
                value = ex.call(op, **args)
                result['completed'].append(op)
                return value
            def rows():
                value = call('query')
                if type(value) is not list:
                    raise IniError('invalid query')
                for row in value:
                    _identity(row)
                return value
            def absent():
                if rows():
                    raise IniError('emulator remains present')
            call('lock')
            call('preflight')
            current = rows()
            if (len(current) != 1 or any(current[0][k] != plan[k] for k in ('pid', 'created')) or
                    live.path_key(current[0]['exe']) != live.path_key(plan['exe'])):
                raise IniError('selected identity mismatch')
            old = current[0]
            if rows() != [old]:
                raise IniError('identity changed before close')
            call('close', process=old)
            absent()
            before = live.checked_snapshot(call('snapshot'))
            result['stage'] = 'transform'
            candidate, diff = transform_trial(before['data'], plan['changes'])
            result['diff'] = diff
            absent()
            call('create', expected=before, data=candidate)
            absent()
            call('verify', expected=before, data=candidate)
            started = _identity(call('start', expected=before, data=candidate))
            # 起動してしまった以上、以後の失敗でもこの行を結果に残す (自動の
            # 停止・復旧はしないので、操作者が対象を特定できる必要がある)。
            result['process'] = started
            result['started_pid'] = started['pid']
            mismatch = identity_mismatch(started, old, plan)
            if mismatch:
                raise IniError('identity mismatch: ' + ','.join(mismatch))
            for _ in range(2):
                unstable = identity_unstable(rows(), started)
                if unstable:
                    raise IniError('identity unstable: ' + ','.join(unstable))
            result['stage'] = 'dispose'
        result.update(ok=True, stage='verified')
    except Exception as exc:
        # No raw transport/model/ini text; path and completed stages are evidence.
        result['reason'] = 'trial failed; inspect recorded stage and process state; no automatic recovery'
        detail = _reason_detail(exc)
        if detail:
            result['reason'] += '; ' + detail
        started_pid = getattr(exc, 'started_pid', None)
        if type(started_pid) is int and 'started_pid' not in result:
            # executor 側で identity 検査に落ちたときも、起動した PID は残す
            # (起動後の失敗では常に PID が結果に載る)。
            result['started_pid'] = started_pid
    return result


# Reuse only fixed read-only/path/CreateNew helpers, never live stop/replace.
PS_SERVER = live.PS_SERVER.split('function Bundle($id) {', 1)[0] + r'''
function CheckFile($path) {
 CheckPath $path
 $item = Get-Item -LiteralPath $path -Force -ErrorAction Stop
 if ($item.PSIsContainer) { throw 'directory where a disk image is required' }
}
function VerifyTrial($a) {
 AssertAbsent
 AssertSnapshot $a.expected
 if ((Snapshot $plan.trial).data -cne $a.data) { throw 'trial changed' }
}
try {
 while ($null -ne ($line = [Console]::ReadLine())) {
  try {
   $code = $null
   $startedPid = $null
   $request = $line | ConvertFrom-Json -ErrorAction Stop
   $serialized = $request.target | ConvertTo-Json -Depth 20 -Compress
   if (!$plan) {
    $plan = $request.target
    $bound = $serialized
    $target = @{exe=$plan.exe; ini=$plan.baseline}
   }
   if ($serialized -cne $bound) { throw 'plan changed' }
   $a = $request.args
   $value = $true
   if ($request.op -ne 'lock' -and !$locked) { throw 'lock required' }
   switch ($request.op) {
    'lock' {
     if ($locked) { throw 'already locked' }
     $mutex = [System.Threading.Mutex]::new($false, 'Global\OS32.NP21W.Ini.Live')
     if (!$mutex.WaitOne(0)) { throw 'workflow busy' }
     $locked = $true
    }
    'preflight' {
     CheckPath $plan.exe
     CheckPath $plan.cwd
     $drive = [IO.DriveInfo]::new([IO.Path]::GetPathRoot($plan.exe))
     if ($drive.DriveType -ne [IO.DriveType]::Fixed -or $drive.DriveFormat -ne 'NTFS') { throw 'local NTFS required' }
     if (Test-Path -LiteralPath $plan.trial) { throw 'trial collision' }
     CheckFile $plan.hdd_path
     if ($plan.fdd_arg) { CheckFile $plan.fdd_arg }
    }
    'query' { $value = @(Query) }
    'close' {
     $p = [Diagnostics.Process]::GetProcessById([int]$a.process.pid)
     try {
      $handle = $p.Handle
      AssertProcess $a.process
      if (!$p.CloseMainWindow()) { throw 'normal close refused' }
      if (!$p.WaitForExit(10000)) { throw 'normal exit timeout' }
      AssertAbsent
     } finally { $p.Dispose() }
    }
    'snapshot' { AssertAbsent; $value = Snapshot $plan.baseline }
    'create' {
     AssertAbsent
     AssertSnapshot $a.expected
     NewFile $plan.trial ([Convert]::FromBase64String($a.data))
     VerifyTrial $a
    }
    'verify' { VerifyTrial $a }
    'start' {
     VerifyTrial $a
     CheckPath $plan.exe
     CheckPath $plan.cwd
     $arguments = '"/i' + $plan.trial + '"'
     CheckFile $plan.hdd_path
     if ($plan.fdd_arg) {
      CheckFile $plan.fdd_arg
      $arguments = $arguments + ' "' + $plan.fdd_arg + '"'
     }
     # UseShellExecute = $true (リダイレクトは一切しない): CreateProcess を
     # bInheritHandles=true で呼ぶ経路を避け、**起動した NP21/W に PowerShell の
     # stdout パイプを継承させない**。継承すると reader が EOF に届かず、起動が
     # 成功しても dispose 段が必ず失敗していた (実走 F3、PM 判断 ③)。
     # 起動する exe・引数・作業ディレクトリは変えない。
     $si = [Diagnostics.ProcessStartInfo]::new()
     $si.UseShellExecute = $true
     $si.FileName = $plan.exe
     $si.Arguments = $arguments
     $si.WorkingDirectory = $plan.cwd
     $p = [Diagnostics.Process]::Start($si)
     try {
      # ShellExecute 経由では $p.Handle を取れないことがあるので PID を要にする。
      # 生存確認も WaitForExit ではなく HasExited (ハンドル不要) で行う。
      $startedPid = $p.Id
      Start-Sleep -Milliseconds 1000
      if ($p.HasExited) { $code = 'started process exited'; throw 'started process exited' }
      $rows = @(Query)
      # PID が同一性の要 (直後に照会するので再利用は実質起こらない)。
      # created は CIM (マイクロ秒) と Process.StartTime (100ns) で最後の桁が
      # 食い違うため、文字列一致ではなく 2 秒の許容で見る。exe は Windows の
      # パスなので大小文字を無視する (CIM は実体の綴りを返す)。
      $mismatch = @()
      if ($rows.Count -ne 1) { $mismatch += 'rows' }
      elseif ($rows[0].pid -ne $startedPid) { $mismatch += 'pid' }
      else {
       $rowUtc = [DateTime]::Parse($rows[0].created, [Globalization.CultureInfo]::InvariantCulture,
                                   [Globalization.DateTimeStyles]::RoundtripKind)
       # ShellExecute 起動では StartTime が読めないことがある。読めたときだけ
       # 突き合わせ、読めなければ CIM の行 (Python 側が古い行との差を見る) に任せる。
       $startUtc = $null
       try { $startUtc = $p.StartTime.ToUniversalTime() } catch { $startUtc = $null }
       if ($null -ne $startUtc -and [Math]::Abs(($rowUtc - $startUtc).TotalSeconds) -gt 2) { $mismatch += 'created' }
       if ($rows[0].exe -ine $plan.exe) { $mismatch += 'exe' }
       if ($rows[0].command -cne ('"' + $plan.exe + '" ' + $arguments)) { $mismatch += 'command' }
      }
      if ($mismatch.Count) {
       $code = 'identity mismatch: ' + ($mismatch -join ',')
       throw 'new identity mismatch'
      }
      $value = $rows[0]
     } finally { $p.Dispose() }
    }
    default { throw 'unknown trial operation' }
   }
   @{ok=$true; value=$value} | ConvertTo-Json -Depth 24 -Compress | ForEach-Object { [Console]::WriteLine($_) }
  } catch {
   # 生の例外文は返さない。固定語彙の $code と、起動してしまった PID だけ。
   $failure = @{ok=$false}
   if ($code) { $failure.code = $code }
   if ($startedPid) { $failure.pid = $startedPid }
   [Console]::WriteLine(($failure | ConvertTo-Json -Compress))
   break
  }
 }
} finally {
 if ($locked) { $mutex.ReleaseMutex() }
 if ($mutex) { $mutex.Dispose() }
}
'''


class PowerShellTransport(live.PowerShellTransport):
    def __init__(self):
        namespace = dict(vars(live), PS_SERVER=PS_SERVER)
        FunctionType(live.PowerShellTransport.__init__.__code__, namespace)(self)

    def close(self):
        # Even the transport has no kill fallback. Closing stdin lets its fixed
        # finally release the mutex. A timeout is reported, never retried.
        #
        # 失敗はそのまま上げる (dispose 段の失敗は成功に変えない) が、どちらの
        # 失敗かが判るよう固定語彙の code を付ける。**起動した NP21/W は
        # PowerShell の stdout ハンドルを継承する**ので、trial では EOF が
        # ゲスト終了まで来ない = 'inherited pipe still open' が通常の帰結。
        self.process.stdin.close()
        try:
            try:
                self.process.wait(timeout=3)
            except BaseException as exc:
                # 例外そのもの (wait の TimeoutExpired) は隠さず、印だけ付ける。
                _coded(exc, 'cleanup: executor exit timeout')
                raise
            finally:
                self._close_reader(failed=sys.exc_info()[0] is not None)
        except IniError as exc:
            if getattr(exc, 'code', None) is None:
                exc.code = 'cleanup: inherited pipe still open'
            raise


class WindowsExecutor:
    """Private trusted adapter; fixed trial operations, no generic actions."""
    OPS = {'lock', 'preflight', 'query', 'close', 'snapshot', 'create', 'verify', 'start'}
    def __init__(self, plan, transport=None):
        _validate_plan(plan)
        self.plan = copy.deepcopy(plan)
        self.transport = transport
    def __enter__(self):
        if self.transport is None:
            self.transport = PowerShellTransport()
        return self
    def __exit__(self, *args):
        self.transport.close()
    def call(self, op, **args):
        if op not in self.OPS:
            raise IniError('unknown trial operation')
        try:
            response = self.transport.exchange(dict(op=op, target=self.plan, args=live.wire(args)))
            if type(response) is not dict or response.get('ok') is not True:
                raise _rejected(response)
            if set(response) != {'ok', 'value'}:
                raise IniError('invalid trial executor response')
            value = live.wire(response['value'], decode=True)
            if op == 'snapshot':
                live.checked_snapshot(value)
            elif op == 'start':
                _identity(value)
            elif op == 'query':
                if type(value) is not list:
                    raise IniError('invalid query')
                for row in value:
                    _identity(row)
            elif value is not True:
                raise IniError('unconfirmed trial operation')
            return value
        except IniError:
            # 既に content-free な診断 (拒否理由 / 起動 PID を含む) なのでそのまま。
            raise
        except (ValueError, TypeError, KeyError, AttributeError):
            raise IniError('invalid trial executor response') from None


def local_proposal(plan, url, model):
    """One loopback OpenAI-compatible request; no env, auth, retries or tools."""
    import http.client
    import urllib.parse
    parsed = urllib.parse.urlsplit(url)
    if (parsed.scheme != 'http' or parsed.hostname not in ('127.0.0.1', '::1', 'localhost') or
            parsed.username or parsed.password or parsed.query or parsed.fragment or
            parsed.path != '/v1/chat/completions'):
        raise IniError('explicit loopback chat-completions URL required')
    connection = http.client.HTTPConnection(parsed.hostname, parsed.port or 80, timeout=60)
    try:
        body = json.dumps(dict(model=model, stream=False, messages=[
            dict(role='system', content='Return exactly the approved one-step JSON object, without tools or extra text.'),
            dict(role='user', content=json.dumps(plan))]))
        connection.request('POST', parsed.path, body=body, headers={'Content-Type': 'application/json'})
        response = connection.getresponse()
        raw = response.read(65537)
        if response.status != 200 or len(raw) > 65536:
            raise IniError('local proposal unavailable')
        value = strict_object(raw.decode('utf-8'))
        choices = value['choices']
        if type(choices) is not list or len(choices) != 1:
            raise IniError('one local proposal required')
        message = choices[0]['message']
        if message.get('tool_calls') is not None or message.get('function_call') is not None:
            raise IniError('local proposal tools forbidden')
        return message['content']
    except Exception:
        raise IniError('local proposal failed') from None
    finally:
        connection.close()


def main(argv=None, executor_factory=WindowsExecutor, llm=None):
    parser = argparse.ArgumentParser(description=__doc__, allow_abbrev=False)
    for key in ('exe', 'baseline', 'cwd', 'created'):
        parser.add_argument('--' + key, required=True)
    parser.add_argument('--pid', required=True, type=int)
    parser.add_argument('--hdd', required=True, metavar='NAME',
                        help='HDD1FILE image name directly under NP21W_DIR (*.nhd)')
    parser.add_argument('--fdd-eject', action='store_true',
                        help='blank FDD1FILE/FDD2FILE in the trial ini (detach)')
    parser.add_argument('--fdd-arg', metavar='NAME',
                        help='optional *.d88 under NP21W_DIR passed as a launch argument')
    parser.add_argument('--execute', action='store_true')
    parser.add_argument('--exclusive-operator', action='store_true')
    parser.add_argument('--llm-url', default='http://127.0.0.1:1234/v1/chat/completions')
    parser.add_argument('--model', default='local-model', help='operator-selected local model ID')
    args = parser.parse_args(argv)
    try:
        plan = make_plan(**{k: getattr(args, k) for k in ('exe', 'baseline', 'cwd', 'pid',
                                                         'created', 'hdd', 'fdd_eject',
                                                         'fdd_arg')})
        if not args.execute:
            print(json.dumps(dict(mode='dry_run', executed=False, plan=plan)))
            return 0
        if not args.exclusive_operator:
            raise IniError('exclusive operator prerequisite required')
        gate = bind_trial(plan, executor_factory, authorized=True, exclusive=True)
        # Freeze approval before giving a separate copy to the local LLM.
        proposal = (llm or local_proposal)(copy.deepcopy(plan), args.llm_url, args.model)
        result = gate(proposal)
        print(json.dumps(result))
        return 0 if result['ok'] else 2
    except Exception:
        print(json.dumps(dict(ok=False, stage='approval', reason='invalid setup or rejected local proposal; no lifecycle execution')))
        return 2


if __name__ == '__main__':
    sys.exit(main())
