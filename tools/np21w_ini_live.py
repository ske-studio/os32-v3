#!/usr/bin/env python3
"""Bounded NP21/W configuration workflow. WSL Python + Windows PowerShell 5.1.

Only the trusted operator binds absolute executable/ini paths. Models receive a
bound Live instance and one of the listed operations, never an executor or shell.
Default is a live READ-ONLY preview; --live-apply and exclusive operator use are
both required for mutation. No HTTP, environment loader or deployment helper.
"""
import argparse
import base64
import json
import ntpath
import re
import sys
from np21w_ini import IniError, LIMIT, transform

# pegc-* changes only USEPEGCP and cirrus-* only the two WAB fields, so the
# two backends can be selected independently. USEPEGCP is the PEGC gate:
# np21w-src win9x/ini.cpp:687 binds it to np2cfg.usepegcplane and io/pegc.c:375
# copies that into pegc.enable, which mem/memvga.c checks on every PEGC VRAM
# access. pc_model does not gate PEGC (OS32 reads BIOS 0x045C bit6 / 0x0597
# bit2, both already set with pc_model=VX; measured on the guest 2026-09-09).
OPERATIONS = {'cirrus-on': {'USEGD5430': 'true', 'GD5430TYPE': '91'},
              'cirrus-off': {'USEGD5430': 'false', 'GD5430TYPE': '91'},
              'pegc-on': {'USEPEGCP': 'true'},
              'pegc-off': {'USEPEGCP': 'false'},
              # ExMemory (ini の綴りは ExMemory、照合は大文字) は MB 単位の
              # 拡張メモリ。8MB は **CUI の**最低動作環境で、memory_boot が
              # 台帳の置き場を FIXED 型 (0x2F9000、低位 RAM の末尾に置けない構成)
              # にする経路を通すための構成 (T1a で legacy の pgalloc_init は撤去)。
              # GUI の最低要件ではない (INSTALL.md / docs/02_memory.md /
              # tasks/gui/DESIGN.md)。16 はゲスト 15360KB = 15MB (実測)。
              # NP21/W は 0xF00000〜0xFFFFFF (16MB システム空間) を RAM にしない
              # ので、ExMemory >= 16 では使える拡張メモリ = ExMemory - 1 MB
              # (np21w-src mem/memory.c、bios.c:241 の 0x401/0x594)。
              # 32MB / 128MB はゲストが報告する量で命名する (33 / 129)。
              'ram-8mb': {'EXMEMORY': '7'},
              'ram-9mb': {'EXMEMORY': '8'},
              'ram-15mb': {'EXMEMORY': '16'},
              'ram-32mb': {'EXMEMORY': '33'},
              'ram-128mb': {'EXMEMORY': '129'}}
SIGNATURE_LIMIT = 256  # FileIdentity.Read: seven decimal integers + separators.
SIGNATURE_JSON_BYTES = 12 * SIGNATURE_LIMIT  # Escaped UTF-16 surrogate pair per character.
PROCESS_ID_MAX = 2147483647  # Query casts ProcessId to signed Int32.
PROCESS_CREATED_LENGTH = 28  # Query: UTC DateTime.ToString('o').
EXCHANGE_TIMEOUT = 30  # seconds per PS request (bounds PS_SERVER's $TransientMs retries).


def path_key(path):
    if (not isinstance(path, str) or len(path.encode('utf-16le', errors='surrogatepass')) // 2 >= 240 or
            not re.fullmatch(r'[A-Za-z]:\\[^"<>|?*\x00-\x1f:]+', path) or
            any(p in ('', '.', '..') or p.endswith((' ', '.')) for p in path[3:].split('\\')) or
            any(0xd800 <= ord(c) <= 0xdfff for c in path)):
        raise IniError('unsupported absolute Windows path')
    if any(re.fullmatch(r'(con|prn|aux|nul|com[1-9]|lpt[1-9])(?:\..*)?', part, re.I)
           for part in path[3:].split('\\')):
        raise IniError('reserved Windows device path')
    return path.lower()


def check_derived_paths(ini):
    """Reserve full GUID suffixes before opening the executor, including restore.

    Use the same conservative <240 UTF-16-unit policy for every full path.
    This also keeps every component below 255 and bundle directories below
    the legacy CreateDirectory limit, without relying on long-path opt-in.
    """
    bundle = ini + '.np21w-live-' + '0' * 32
    for path in (bundle, bundle + '\\original.bin', bundle + '\\receipt.json',
                 ini + '.pending-' + '0' * 32):
        path_key(path)


# Accepted launch command lines (CIM Win32_Process.CommandLine), source basis:
# np21w-src src/win9x/np2arg.cpp Np2Arg::Parse tokenizes GetCommandLine() with
# common/milstr.c milstr_getarg (blank separated, '"' toggles quoting and is
# removed). An argument starting with '/' or '-' is a switch keyed by its second
# character, case-folded ('i' -> ini path = rest of the token, last one wins);
# any other argument is classified by extension: .ini (and cfg variants) -> ini,
# CD image extensions -> CD, everything else -> FDD 1..4 in order
# (np2.cpp WinMain: diskdrv_readyfdd for each disk(i)). initgetfile (ini.cpp)
# then uses that ini path. Only these three shapes are accepted:
#   "<exe>" "<ini>"                 positional ini (operator/earlier launches)
#   "<exe>" "/i<ini>"               tools/np21w_ctl.py start --ini
#   "<exe>" "/i<ini>" "<fd image>"  tools/np21w_ctl.py start --ini --fd
# ctl builds them with Start-Process -ArgumentList @('"/i<abs ini>"', '"<abs fd>"').
# Every path is a quoted drive-absolute path; trailing blanks are insignificant.
# Everything else is rejected even where NP21/W would accept it: '-i', '/I',
# an unquoted '/i' token, a second '/i', relative paths, other switches such as
# '/f', a CD/cfg argument, more than one disk, or a disk with the positional form.
COMMAND_RE = re.compile(r'"([^"\r\n]+)"[ \t]+"([^"\r\n]+)"(?:[ \t]+"([^"\r\n]+)")?[ \t]*')
INI_SWITCH = '/i'  # exactly what np21w_ctl.py emits
# fddfile.c: d88/88d/d98/98d (D88), fdi, nfd; anything else is a raw/BETA image
# (OS32 ships os32_boot.d88 and os32_boot144.img). Keeps cfg/CD extensions out.
FD_EXTENSIONS = ('.d88', '.88d', '.d98', '.98d', '.fdi', '.nfd', '.hdm', '.img')


def launch_of(command, target):
    """Parse an accepted launch command line into {'form', 'fd'} or reject it."""
    match = COMMAND_RE.fullmatch(command) if isinstance(command, str) else None
    if not match or path_key(match[1]) != path_key(target['exe']):
        raise IniError('unsupported or mismatched explicit process/config identity')
    token, fd = match[2], match[3]
    if token.startswith(INI_SWITCH):
        form, ini = 'switch', token[len(INI_SWITCH):]
    elif token[0] in '/-':
        raise IniError('unsupported or mismatched explicit process/config identity')
    else:
        form, ini = 'positional', token
    if path_key(ini) != path_key(target['ini']):
        raise IniError('unsupported or mismatched explicit process/config identity')
    if fd is not None:
        if form != 'switch' or not path_key(fd).endswith(FD_EXTENSIONS):
            raise IniError('unsupported or mismatched explicit process/config identity')
    return {'form': form, 'fd': fd}


def launch_arguments(target, launch):
    """Arguments string the restart passes (same text the PS 'start' builds)."""
    text = ('"' + INI_SWITCH if launch['form'] == 'switch' else '"') + target['ini'] + '"'
    if launch['fd'] is not None:
        text += ' "' + launch['fd'] + '"'
    return text


def identify(rows, target, pid=None):
    """Conservative: require exactly one NP21-family process on this host.

    The command line must be one of the launch shapes listed at COMMAND_RE,
    naming this exe and this ini. Reject switches, implicit paths, additional
    args, and unfamiliar quoting.
    """
    if not isinstance(rows, list):
        raise IniError('process query failed or malformed')
    if not rows:
        if pid is not None:
            raise IniError('selected process absent')
        return None
    if len(rows) != 1:
        raise IniError('multiple emulator processes; exclusive target required')
    row = rows[0]
    if (not isinstance(row, dict) or set(row) != {'pid', 'exe', 'command', 'created'} or
            type(row['pid']) is not int or row['pid'] <= 0 or
            not isinstance(row['created'], str) or not row['created'] or
            not isinstance(row['command'], str)):
        raise IniError('incomplete process identity')
    # Real CreateProcess command lines end with a trailing blank after the last
    # quoted token (measured 2026-09-09). milstr_getarg tokenizes on blanks, so
    # trailing whitespace is insignificant; anything else is still rejected.
    launch_of(row['command'], target)
    if (path_key(row['exe']) != path_key(target['exe']) or
            (pid is not None and row['pid'] != pid)):
        raise IniError('unsupported or mismatched explicit process/config identity')
    return row


FILE_KEY_RE = re.compile(r'[0-9]+(?::[0-9]+){6}')
CANDIDATE_KEY_RE = re.compile(r'[0-9]+:[0-9]+:[0-9]+')


def file_key(signature):
    """Volume:IndexHigh:IndexLow of a FileIdentity.Read signature.

    ReplaceFile leaves the replacement (candidate) file's volume/index on the
    target name, so the post-replace readback must carry the candidate's key.
    """
    if not isinstance(signature, str) or not FILE_KEY_RE.fullmatch(signature):
        raise IniError('invalid file identity')
    return ':'.join(signature.split(':')[:3])


def checked_snapshot(value):
    if (not isinstance(value, dict) or set(value) != {'data', 'signature'} or
            not isinstance(value['data'], bytes) or len(value['data']) > LIMIT or
            not isinstance(value['signature'], str) or not value['signature'] or
            len(value['signature']) > SIGNATURE_LIMIT):
        raise IniError('invalid snapshot')
    return value


def receipt_size_bound(record):
    """Upper bound for the compressed PS JSON read by Snapshot (also LIMIT).

    Base64 is ASCII and needs no JSON escapes. Budget other fields at six
    bytes per ASCII JSON byte (even HTML/non-ASCII escapes fit). Both signatures
    reserve the maximum JSON encoding of any checked_snapshot string: restore
    swaps applied into original, and replacement generates another signature.
    Restart changes PID/creation time; reserve the larger of the current row
    and the fixed Query/Start output envelope. Paths stay bound to target;
    restore only reverses data/diff and shortens the operation name. Thus its
    preflight bound cannot grow across apply -> restore with this executor.
    This deliberately rejects some inputs that might fit. The receipt schema
    contains only strings, an integer, lists/dicts.
    """
    metadata = dict(record)
    for key in ('original', 'applied'):
        metadata[key] = {'data': '', 'signature': ''}
    restarted = {'pid': PROCESS_ID_MAX, 'exe': record['target']['exe'],
                 'command': '"' + record['target']['exe'] + '" ' + launch_arguments(
                     record['target'], launch_of(record['process']['command'], record['target'])),
                 'created': '0' * PROCESS_CREATED_LENGTH}
    metadata['process'] = max((record['process'], restarted),
                              key=lambda row: len(json.dumps(row, ensure_ascii=True)))
    return (sum(4 * ((len(record[k]['data']) + 2) // 3) for k in ('original', 'applied')) +
            2 * SIGNATURE_JSON_BYTES +
            6 * len(json.dumps(metadata, ensure_ascii=True).encode('ascii')))


class Live:
    def __init__(self, executor, target):
        if (set(target) != {'exe', 'ini'} or
                ntpath.basename(path_key(target['exe'])) != 'np21x64w.exe' or
                not path_key(target['ini']).endswith('.ini')):
            raise IniError('unsupported operator target')
        check_derived_paths(target['ini'])
        self.executor, self.target = executor, dict(target)

    def run(self, operation, *, live_apply=False, exclusive=False, receipt=None):
        if operation not in (*OPERATIONS, 'restore'):
            raise IniError('unsupported predefined operation')
        if live_apply and exclusive is not True:
            raise IniError('exclusive operator prerequisite required')
        if operation == 'restore':
            if not isinstance(receipt, str) or not re.fullmatch('[a-f0-9]{32}', receipt):
                raise IniError('restore requires opaque receipt ID')
        elif receipt is not None:
            raise IniError('receipt only accepted for restore')
        with self.executor as ex:
            if live_apply:
                ex.call('lock')
            def query(pid=None):
                return identify(ex.call('query'), self.target, pid)
            def absent():
                if query() is not None:
                    raise IniError('process still running or restarted')
            process = query()
            was_running = process is not None
            if process is None and operation != 'restore':
                raise IniError('running explicit target required for restart provenance')
            before = checked_snapshot(ex.call('snapshot'))
            if operation == 'restore':
                record = ex.call('load', receipt=receipt)
                try:
                    if (set(record) != {'target', 'operation', 'original', 'applied', 'diff', 'process'} or
                            record['target'] != self.target or record['operation'] not in OPERATIONS):
                        raise IniError('invalid receipt target or operation')
                    original = checked_snapshot(record['original'])
                    expected, forward = transform(original['data'], OPERATIONS[record['operation']])
                    if (checked_snapshot(record['applied'])['data'] != expected or
                            record['diff'] != forward):
                        raise IniError('invalid receipt transformation')
                    proven = identify([record['process']], self.target)
                    if process is None:
                        process = proven
                except (KeyError, TypeError, ValueError) as exc:
                    raise IniError('invalid restore receipt') from exc
                candidate = record['original']['data']
                diff = record['diff']
                diff = [line.split(': ')[0] + ': ' + ' -> '.join(
                    reversed(line.split(': ')[1].split(' -> '))) for line in diff]
                if before != record['applied']:
                    raise IniError('intervening modification; refusing restore')
            else:
                candidate, diff = transform(before['data'], OPERATIONS[operation])
            result = {'diff': diff, 'applied': False}
            if not live_apply or candidate == before['data']:
                return result
            planned_record = {
                'target': self.target, 'operation': operation, 'original': before,
                'applied': checked_snapshot({'data': candidate, 'signature': 'pending'}),
                'diff': diff, 'process': process}
            if receipt_size_bound(planned_record) > LIMIT:
                raise IniError('receipt exceeds snapshot size limit; refusing before stop')
            if was_running:
                if query(process['pid']) != process:
                    raise IniError('process identity changed before stop')
                ex.call('stop', process=process)
            absent()
            if checked_snapshot(ex.call('snapshot')) != before:
                raise IniError('snapshot changed on exit; retain stopped state and review again')
            backup = ex.call('backup', snapshot=before)
            replaced = False
            try:
                absent()
                if checked_snapshot(ex.call('snapshot')) != before:
                    raise IniError('intervening modification before replacement')
                # The fixed PS 'replace' writes receipt.json itself right after
                # its full readback (applied = that readback), before any other
                # check can fail, so every later failure leaves a receipt that
                # restore accepts. A failed 'replace' ends the PS session, so
                # Python could not write it afterwards (incident 2026-10-01).
                done = ex.call('replace', expected=before, data=candidate,
                               receipt=backup, record=planned_record)
                applied = checked_snapshot(done['applied'])
                # PS already refuses this before writing the receipt; a
                # readback of another file with the same bytes (swapped while
                # the open retried) must never become the restorable state.
                if file_key(applied['signature']) != done['candidate']:
                    raise IniError('readback identity differs from the candidate; '
                                   'do not restore this bundle')
                replaced = True
                result['receipt'] = backup
                if applied['data'] != candidate:
                    raise IniError('readback mismatch; retain backup and stopped state')
                absent()
                if checked_snapshot(ex.call('snapshot')) != applied:
                    raise IniError('changed after replacement; retain backup and stopped state')
                # Restart in the shape the stopped process was launched with.
                launch = launch_of(process['command'], self.target)
                started = ex.call('start', process=process, launch=launch)
                first = query(started)
                if query(started) != first:
                    raise IniError('restart identity unstable')
                if launch_of(first['command'], self.target) != launch:
                    raise IniError('restart launch shape changed')
                result.update(applied=True, pid=started)
                return result
            except IniError as exc:
                if replaced and operation == 'restore':
                    # Its receipt has operation 'restore', which restore refuses.
                    raise IniError(str(exc) + '; restore-operation receipt ID '
                                   '(not restorable; investigate): ' + backup) from exc
                if replaced:
                    raise IniError(str(exc) + '; receipt ID for restore: ' + backup) from exc
                # original.bin exists; receipt.json only if 'replace' failed
                # after writing it. restore refuses a bundle without a receipt.
                raise IniError(str(exc) + '; retained backup ID (receipt not confirmed): ' +
                               backup) from exc


# Fixed program only; requests travel as JSON on stdin, never as PS source.
# Source references and host/live validation boundary: np21w_ini_live_tdd.md.
PS_SERVER = r'''
$ErrorActionPreference = 'Stop'
$ProgressPreference = 'SilentlyContinue'
[Console]::OutputEncoding = [Text.UTF8Encoding]::new($false)
Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;
using Microsoft.Win32.SafeHandles;
public static class FileIdentity {
 [StructLayout(LayoutKind.Sequential, Pack=4)] public struct Info {
  public uint Attributes; public long Creation; public long Access; public long Write;
  public uint Volume, SizeHigh, SizeLow, NumberOfLinks, IndexHigh, IndexLow;
 }
 [DllImport("kernel32.dll", SetLastError=true)]
 public static extern bool GetFileInformationByHandle(SafeFileHandle h, out Info info);
 public static string Read(SafeFileHandle h) {
  Info i; if (!GetFileInformationByHandle(h, out i) || i.NumberOfLinks != 1)
   throw new Exception("identity unavailable or hardlink");
  return String.Join(":", new object[]{i.Volume,i.IndexHigh,i.IndexLow,i.Creation,
   i.Write,i.SizeHigh,i.SizeLow});
 }
}
'@
$locked = $false
$mutex = $null
function Query {
 $rows = @(Get-CimInstance -ClassName Win32_Process -Filter "Name LIKE 'np21%' OR Name LIKE 'np2%'" -ErrorAction Stop)
 foreach ($p in $rows) {
  if (!$p.ExecutablePath -or !$p.CommandLine -or !$p.CreationDate) { throw 'incomplete process query' }
  @{pid=[int]$p.ProcessId; exe=[string]$p.ExecutablePath; command=[string]$p.CommandLine;
    created=$p.CreationDate.ToUniversalTime().ToString('o')}
 }
}
function AssertAbsent {
 if (@(Query).Count -ne 0) { throw 'emulator still present' }
}
function AssertProcess($p) {
 $rows = @(Query)
 if ($rows.Count -ne 1 -or $rows[0].pid -ne $p.pid -or
     $rows[0].exe -cne $p.exe -or $rows[0].command -cne $p.command -or
     $rows[0].created -cne $p.created) { throw 'process changed' }
}
function CheckPath($path) {
 $part = $path
 while ($part) {
  $item = Get-Item -LiteralPath $part -Force -ErrorAction Stop
  if ($item.Attributes -band [IO.FileAttributes]::ReparsePoint) { throw 'reparse point' }
  $part = [IO.Path]::GetDirectoryName($part)
 }
}
# Right after the kill / File.Replace another handle (an on-access scanner,
# cf. POLICY_DEBUG 4-60) can briefly deny our FileShare.Read open. Retry only
# ERROR_SHARING_VIOLATION (32) / ERROR_LOCK_VIOLATION (33), bounded per open;
# 'replace' opens up to five files, all within one EXCHANGE_TIMEOUT. The path
# is rechecked (reparse points, parents included) before every attempt and
# after the successful open: it may be swapped while we wait.
$TransientMs = 4000
function OpenShared($path) {
 $clock = [Diagnostics.Stopwatch]::StartNew()
 while ($true) {
  CheckPath $path
  try { $f = [IO.File]::Open($path, [IO.FileMode]::Open, [IO.FileAccess]::Read, [IO.FileShare]::Read) }
  catch {
   $e = $_.Exception
   while ($e.InnerException) { $e = $e.InnerException }
   $left = $TransientMs - $clock.ElapsedMilliseconds
   if ($e -isnot [IO.IOException] -or (($e.HResult -band 0xFFFF) -notin @(32,33)) -or $left -le 0) { throw }
   Start-Sleep -Milliseconds ([Math]::Min(250, $left))
   if ($clock.ElapsedMilliseconds -ge $TransientMs) { throw }
   continue
  }
  try { CheckPath $path } catch { $f.Dispose(); throw }
  return $f
 }
}
function Snapshot($path) {
 $f = OpenShared $path
 try {
  $sig = [FileIdentity]::Read($f.SafeFileHandle)
  if ($f.Length -gt 4194304) { throw 'oversized file' }
  $mem = [IO.MemoryStream]::new()
  try {
   $f.CopyTo($mem)
   if ([FileIdentity]::Read($f.SafeFileHandle) -cne $sig) { throw 'changed during read' }
   return @{data=[Convert]::ToBase64String($mem.ToArray()); signature=$sig}
  } finally { $mem.Dispose() }
 } finally { $f.Dispose() }
}
function AssertSnapshot($expected) {
 $current = Snapshot $target.ini
 if ($current.data -cne $expected.data -or $current.signature -cne $expected.signature) {
  throw 'intervening modification'
 }
}
function NewFile($path, [byte[]]$bytes) {
 if ($bytes.Length -gt 4194304) { throw 'oversized file' }
 CheckPath ([IO.Path]::GetDirectoryName($path))
 $security = [Security.AccessControl.FileSecurity]::new()
 $security.SetAccessRuleProtection($true,$false)
 $sid = [Security.Principal.WindowsIdentity]::GetCurrent().User
 $security.AddAccessRule([Security.AccessControl.FileSystemAccessRule]::new($sid,'FullControl','Allow'))
 $f = [IO.FileStream]::new($path, [IO.FileMode]::CreateNew, [Security.AccessControl.FileSystemRights]::Write,
  [IO.FileShare]::None, 4096, [IO.FileOptions]::WriteThrough, $security)
 try { $f.Write($bytes,0,$bytes.Length); $f.Flush($true) } finally { $f.Dispose() }
 $s = Snapshot $path
 if ($s.data -cne [Convert]::ToBase64String($bytes)) { throw 'Readback failed' }
 return $s
}
# Volume:IndexHigh:IndexLow of a FileIdentity.Read signature (file_key).
function FileKey($sig) {
 if ($sig -cnotmatch '^[0-9]+(:[0-9]+){6}$') { throw 'invalid file identity' }
 $parts = $sig.Split(':')
 return $parts[0] + ':' + $parts[1] + ':' + $parts[2]
}
$CandidateIdentity = 'readback identity differs from the candidate'
function Bundle($id) {
 if ($id -cnotmatch '^[a-f0-9]{32}$') { throw 'invalid receipt id' }
 return $target.ini + '.np21w-live-' + $id
}
function WriteReceipt($id, $record, $applied) {
 $dir = Bundle $id
 if ((Snapshot ($dir + '\original.bin')).data -cne $record.original.data) { throw 'backup changed' }
 $record.applied = $applied
 $json = $record | ConvertTo-Json -Depth 20 -Compress
 $null = NewFile ($dir + '\receipt.json') ([Text.Encoding]::UTF8.GetBytes($json))
}
try {
 while ($null -ne ($line = [Console]::ReadLine())) {
  try {
   $request = $line | ConvertFrom-Json -ErrorAction Stop
   if (!$target) { $target = $request.target }
   if ($target.exe -cne $request.target.exe -or $target.ini -cne $request.target.ini) { throw 'target changed' }
   $a = $request.args
   $value = $true
   if ($request.op -notin @('query','snapshot','load','lock') -and !$locked) { throw 'lock required' }
   switch ($request.op) {
    'lock' {
     $mutex = [System.Threading.Mutex]::new($false, 'Global\OS32.NP21W.Ini.Live')
     if (!$mutex.WaitOne(0)) { throw 'another controlled workflow owns lock' }
     $locked = $true
    }
    'query' { $value=@(Query) }
    'snapshot' { $value = Snapshot $target.ini }
    'stop' {
     $p = [Diagnostics.Process]::GetProcessById([int]$a.process.pid)
     try {
      $handle = $p.Handle # pin process handle before identity recheck (PID reuse)
      AssertProcess $a.process
      $p.Kill()
      if (!$p.WaitForExit(10000)) { throw 'exit timeout' }
      AssertAbsent
     } finally { $p.Dispose() }
    }
    'backup' {
     AssertAbsent
     AssertSnapshot $a.snapshot
     $value = [Guid]::NewGuid().ToString('N')
     $dir = Bundle $value
     if ([IO.Directory]::Exists($dir)) { throw 'backup collision' }
     $acl = [Security.AccessControl.DirectorySecurity]::new()
     $acl.SetAccessRuleProtection($true,$false)
     $sid = [Security.Principal.WindowsIdentity]::GetCurrent().User
     $rule = [Security.AccessControl.FileSystemAccessRule]::new($sid,'FullControl','ContainerInherit,ObjectInherit','None','Allow')
     $acl.AddAccessRule($rule)
     [void][IO.Directory]::CreateDirectory($dir,$acl)
     $null = NewFile ($dir + '\original.bin') ([Convert]::FromBase64String($a.snapshot.data))
    }
    'replace' {
     $temp = $target.ini + '.pending-' + [Guid]::NewGuid().ToString('N')
     $candidate = NewFile $temp ([Convert]::FromBase64String($a.data))
     AssertAbsent
     AssertSnapshot $a.expected
     [IO.File]::Replace($temp, $target.ini, [System.Management.Automation.Language.NullString]::Value)
     $applied = Snapshot $target.ini
     if ($applied.data -cne $a.data) { throw 'Readback failed' }
     # ReplaceFile keeps the candidate's volume/index: same bytes from another
     # file (swapped while the open retried) is not our replacement.
     if ((FileKey $applied.signature) -cne (FileKey $candidate.signature)) { throw $CandidateIdentity }
     # Receipt first: a failure below (or any later request) ends the session.
     WriteReceipt $a.receipt $a.record $applied
     AssertAbsent
     $value = @{applied=$applied; candidate=(FileKey $candidate.signature)}
    }
    'load' {
     $dir = Bundle $a.receipt
     $raw = Snapshot ($dir + '\receipt.json')
     $value = [Text.Encoding]::UTF8.GetString([Convert]::FromBase64String($raw.data)) | ConvertFrom-Json
     if ((Snapshot ($dir + '\original.bin')).data -cne $value.original.data) { throw 'backup changed' }
    }
    'start' {
     AssertAbsent
     CheckPath $target.exe
     # ShellExecute, no redirection (as np21w_trial, F3): without it .NET
     # starts NP21/W via CreateProcess(bInheritHandles=TRUE), so it kept this
     # session's stdout pipe and the host reader never saw EOF: every apply /
     # restore that restarted it ended in 'cleanup timeout' (2026-10-01).
     # Executable, arguments and working directory are unchanged.
     $si = [Diagnostics.ProcessStartInfo]::new()
     $si.UseShellExecute=$true
     $si.FileName=$target.exe
     # Same launch shape as the stopped process (launch_of / launch_arguments).
     $l = $a.launch
     if ($l.form -ceq 'switch') { $arg = '"/i' + $target.ini + '"' }
     elseif ($l.form -ceq 'positional') { $arg = '"' + $target.ini + '"' }
     else { throw 'invalid launch' }
     if ($null -ne $l.fd) {
      if ($l.form -cne 'switch' -or $l.fd -isnot [string] -or
          $l.fd -cnotmatch '^[A-Za-z]:\\[^"\r\n]+$') { throw 'invalid launch' }
      CheckPath $l.fd
      $arg += ' "' + $l.fd + '"'
     }
     $si.Arguments=$arg
     $si.WorkingDirectory=[IO.Path]::GetDirectoryName($target.exe)
     $p = [Diagnostics.Process]::Start($si)
     if ($null -eq $p) { throw 'no process started' }
     try {
      # ShellExecute may not expose $p.Handle: the PID is the key (Python
      # re-queries CIM for exe / command / created), liveness is HasExited.
      $startedPid = $p.Id
      Start-Sleep -Milliseconds 1000
      if ($p.HasExited) { throw 'started process exited' }
      $value = $startedPid
     } finally { $p.Dispose() }
    }
    default { throw 'unknown operation' }
   }
   @{ok=$true; value=$value} | ConvertTo-Json -Depth 24 -Compress | ForEach-Object { [Console]::WriteLine($_) }
  } catch {
   # Never echo raw exception text, file bytes or command-line contents.
   # ok=$false: explicit failure, never empty success; only fixed reason codes.
   if ($_.Exception.Message -ceq $CandidateIdentity) { [Console]::WriteLine('{"ok":false,"reason":"candidate-identity"}') }
   else { [Console]::WriteLine('{"ok":false}') }
   break
  }
 }
} finally {
 if ($locked) { $mutex.ReleaseMutex() }
 if ($mutex) { $mutex.Dispose() }
}
'''


def wire(value, decode=False):
    if isinstance(value, bytes):
        return base64.b64encode(value).decode('ascii')
    if isinstance(value, dict):
        return {k: (base64.b64decode(v, validate=True) if decode and k == 'data'
                    else wire(v, decode)) for k, v in value.items()}
    if isinstance(value, list):
        return [wire(v, decode) for v in value]
    return value


class PowerShellTransport:
    """Private fixed-script channel; WSL transport, no caller-supplied code.

    A single PS process holds the named mutex until stdin closes. All request
    errors, missing/invalid frames and transport timeouts terminate the session.
    """
    def __init__(self):
        import queue
        import subprocess
        import threading
        self.queue = queue.Queue()
        encoded = base64.b64encode(PS_SERVER.encode('utf-16le')).decode('ascii')
        try:
            self.process = subprocess.Popen(
                ['powershell.exe', '-NoLogo', '-NoProfile', '-NonInteractive', '-EncodedCommand', encoded],
                stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.DEVNULL)
        except OSError as exc:
            raise IniError('Windows PowerShell unavailable') from exc
        def read():
            try:
                # Only this thread may close the buffered stream: another
                # thread's close can wait indefinitely for its read lock.
                with self.process.stdout:
                    for line in self.process.stdout:
                        self.queue.put(line)
            finally:
                self.queue.put(b'')
        self.reader = threading.Thread(target=read, daemon=True)
        self.reader.start()

    def exchange(self, request):
        import queue
        try:
            self.process.stdin.write(json.dumps(request).encode('ascii') + b'\n')
            self.process.stdin.flush()
            line = self.queue.get(timeout=EXCHANGE_TIMEOUT)
            if not line or len(line) > 32 * LIMIT:
                raise ValueError('missing/oversized response')
            if self.process.poll() not in (None, 0):
                raise ValueError('Windows executor exited unsuccessfully')
            return json.loads(line.decode('utf-8-sig'))
        except (OSError, ValueError, queue.Empty) as exc:
            raise IniError('Windows executor failed; retain backup; inspect process state') from exc

    def _close_reader(self, *, failed=False):
        # Descendants may retain the pipe after the PS script/parent exits.
        # Leave eventual stream cleanup to the reader, never wait for EOF here.
        self.reader.join(timeout=0.1)
        # Preserve a wait/kill failure already propagating out of close().
        if self.reader.is_alive() and not failed:
            raise IniError('Windows executor cleanup timeout; inspect process state')

    def close(self):
        import subprocess
        self.process.stdin.close()
        try:
            try:
                self.process.wait(timeout=3)
            except subprocess.TimeoutExpired:
                self.process.kill()
                self.process.wait(timeout=3)
                raise IniError('Windows executor cleanup timeout; inspect process state') from None
        finally:
            self._close_reader(failed=sys.exc_info()[0] is not None)


class WindowsExecutor:
    """Trusted adapter. Do not expose call() or construction to a model."""
    OPS = {'lock', 'query', 'snapshot', 'stop', 'backup', 'replace', 'load', 'start'}

    def __init__(self, target, transport=None):
        self.target, self.transport = dict(target), transport

    def __enter__(self):
        if self.transport is None:
            self.transport = PowerShellTransport()
        return self

    def __exit__(self, *args):
        if self.transport is not None:
            self.transport.close()
            self.transport = None

    def call(self, op, **args):
        if op not in self.OPS:
            raise IniError('unsupported executor operation')
        try:
            response = self.transport.exchange({'op': op, 'target': self.target, 'args': wire(args)})
            # The only failure detail PS reports: a fixed code, never raw text.
            if op == 'replace' and response == {'ok': False, 'reason': 'candidate-identity'}:
                raise IniError('Windows operation failed: replace; readback identity differs from '
                               'the candidate (no receipt written); retain backup and verify process state')
            if (not isinstance(response, dict) or set(response) != {'ok', 'value'} or
                    response['ok'] is not True):
                raise IniError('Windows operation failed: ' + op + '; retain backup and verify process state')
            value = wire(response['value'], decode=True)
            if op == 'query':
                identify(value, self.target)
            elif op == 'snapshot':
                checked_snapshot(value)
            elif op == 'replace':
                if (not isinstance(value, dict) or set(value) != {'applied', 'candidate'} or
                        not isinstance(value['candidate'], str) or
                        not CANDIDATE_KEY_RE.fullmatch(value['candidate'])):
                    raise IniError('invalid replace result')
                checked_snapshot(value['applied'])
            elif op == 'backup':
                if not isinstance(value, str) or not re.fullmatch('[a-f0-9]{32}', value):
                    raise IniError('invalid backup receipt ID')
            elif op == 'start':
                if type(value) is not int or value <= 0:
                    raise IniError('invalid started process ID')
            elif op != 'load' and value is not True:
                raise IniError('unconfirmed executor operation')
            return value
        except IniError:
            # IniError subclasses ValueError; keep its precise operator
            # diagnostic instead of the generic transport message below.
            raise
        except (ValueError, TypeError, KeyError, AttributeError) as exc:
            raise IniError('invalid Windows executor response for ' + op) from exc


def bind_model_operation(executor, target, *, authorized_apply=False, exclusive=False, approved_request=None):
    """Trusted host binds target and per-operation authorization before exposure.

    Return only this callable to the model/FLM. Request schema has no paths,
    executor operation, command, stopped flag, or authorization override.
    A mutating binding is single use: approval is not reusable for later edits.
    """
    service = Live(executor, target)
    approved_request = dict(approved_request) if approved_request is not None else None
    used = False
    def dispatch(request):
        nonlocal used
        if authorized_apply and used:
            raise IniError('mutating authorization already consumed')
        if (not isinstance(request, dict) or 'operation' not in request or
                set(request) - {'operation', 'receipt'}):
            raise IniError('bounded operation and optional receipt ID only')
        if authorized_apply:
            if approved_request is None or request != approved_request:
                raise IniError('request lacks exact operator approval')
            used = True
        return service.run(request['operation'], receipt=request.get('receipt'),
                           live_apply=authorized_apply, exclusive=exclusive)
    return dispatch


def main(argv=None, executor_factory=WindowsExecutor):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('operation', choices=(*OPERATIONS, 'restore'))
    parser.add_argument('--exe', required=True, help='operator-selected absolute Windows np21x64w.exe')
    parser.add_argument('--ini', required=True, help='operator-selected absolute explicit .ini')
    parser.add_argument('--receipt', help='opaque ID returned by apply, only for restore')
    parser.add_argument('--live-apply', action='store_true', help='stop, write and restart (default: read-only preview)')
    parser.add_argument('--exclusive-operator', action='store_true', help='operator owns exclusive use through restart')
    args = parser.parse_args(argv)
    target = {'exe': args.exe, 'ini': args.ini}
    try:
        result = Live(executor_factory(target), target).run(
            args.operation, live_apply=args.live_apply, exclusive=args.exclusive_operator, receipt=args.receipt)
        for line in result['diff']:
            print(line)
        if result.get('receipt'):
            print('receipt: ' + result['receipt'])
        if result.get('pid'):
            print('verified process PID: ' + str(result['pid']))
        return 0
    except (IniError, OSError) as exc:
        print('error: ' + (str(exc) if isinstance(exc, IniError) else 'host operation failed'), file=sys.stderr)
        return 2


if __name__ == '__main__':
    sys.exit(main())
