#!/usr/bin/env bash
# Foreground jobs with atomic JSON records. Requires Python 3 and flock.
set -uo pipefail
jobs_dir=${HOME:?}/os32-tmp/jobs
usage() {
    echo '使い方: jobs.sh run <名札> [--next "<次の工程>"] -- <コマンド...> | status [--prune] | ack <名札>' >&2
    exit 2
}
valid_label() { [[ -n $1 && $1 != .* && $1 != *[/\\]* && $1 != *[$'\t\r\n']* ]]; }
record() {
    python3 -B - "$jobs_dir" "$@" <<'PY'
import datetime
import fcntl
import json
import os
from pathlib import Path
import shlex
import sys
import time

root, action, *args = sys.argv[1:]
root = Path(root)

def read(path):
    return json.loads(path.read_text())

def write(path, data):
    temp = path.with_suffix(f'.{os.getpid()}.tmp')
    try:
        temp.write_text(json.dumps(data, ensure_ascii=False) + '\n')
        os.replace(temp, path)
    finally:
        temp.unlink(missing_ok=True)

def alive(pid):
    if not isinstance(pid, int) or pid <= 0:
        return False
    try:
        os.kill(pid, 0)
        return True
    except ProcessLookupError:
        return False
    except PermissionError:
        return True

def ended(data):
    return data.get('end') is not None and data.get('rc') is not None

def stamp(epoch):
    return datetime.datetime.fromtimestamp(epoch).astimezone().isoformat(timespec='seconds')

try:
    if action == 'start':
        label, pid, cwd, next_step, *command = args
        write(root / (label + '.json'), dict(label=label, command=command, cwd=cwd,
              start=time.time(), end=None, rc=None, pid=int(pid),
              log=str(root / (label + '.log')), next=next_step, ack=False))
    elif action in ('finish', 'ack'):
        path = root / (args[0] + '.json')
        data = read(path)
        if action == 'finish':
            data.update(end=time.time(), rc=int(args[1]))
        else:
            if not ended(data):
                raise ValueError('終了記録がない仕事は確認済みにできません')
            data['ack'] = True
        write(path, data)
    else:
        records = []
        for path in root.glob('*.json'):
            try:
                data = read(path)
                records.append((data['start'], path, data))
            except (OSError, ValueError, KeyError, TypeError):
                print(f'不明: {path.name} (記録を読めません)')
        for _, path, data in sorted(records, key=lambda row: row[0], reverse=True):
            # Pruning shares the runner's lock; re-read after acquiring it.
            if args and args[0] == '--prune':
                with path.with_suffix('.lock').open('a') as lock:
                    try:
                        fcntl.flock(lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
                    except BlockingIOError:
                        pass
                    else:
                        data = read(path)
                        if (ended(data) and data.get('ack') and
                                data['end'] < time.time() - 7 * 86400):
                            path.with_suffix('.log').unlink(missing_ok=True)
                            path.unlink()
                            continue
            if ended(data):
                state = '終了・確認済み' if data.get('ack') else '*** 終了・未確認 ***'
                state += f" rc={data['rc']} 終了={stamp(data['end'])}"
            else:
                state = '走行中' if alive(data.get('pid')) else '不明'
            # One job per line, even when arguments contain tabs or newlines.
            line = (f"{data['label']}: {state} 開始={stamp(data['start'])} "
                    f"次={data.get('next', '')} cwd={data['cwd']} "
                    f"コマンド={shlex.join(data['command'])} ログ={data['log']}")
            print(line.replace('\r', '\\r').replace('\n', '\\n').replace('\t', '\\t'))
except (OSError, ValueError, KeyError, TypeError) as error:
    print(f'jobs: {error}', file=sys.stderr)
    sys.exit(1)
PY
}
(($#)) || usage
action=$1; shift
case $action in
    run)
        (($# >= 3)) || usage
        label=$1; shift
        valid_label "$label" || usage
        next_step=
        if [[ $1 == --next ]]; then
            (($# >= 4)) || usage
            next_step=$2; shift 2
        fi
        [[ $1 == -- ]] || usage
        shift
        (($#)) || usage
        mkdir -p -- "$jobs_dir" || exit 1
        exec 9>"$jobs_dir/$label.lock" || exit 1
        flock -n 9 || { echo "jobs: $label は使用中です" >&2; exit 1; }
        record start "$label" "$$" "$PWD" "$next_step" "$@" || exit 1
        "$@" </dev/null >"$jobs_dir/$label.log" 2>&1 9>&-
        rc=$?
        record finish "$label" "$rc" || exit 1
        exit "$rc"
        ;;
    status)
        [[ $# == 0 || ( $# == 1 && $1 == --prune ) ]] || usage
        record status "$@"
        ;;
    ack)
        [[ $# == 1 ]] || usage
        valid_label "$1" || usage
        [[ -d $jobs_dir ]] || { echo 'jobs: 記録がありません' >&2; exit 1; }
        exec 9>"$jobs_dir/$1.lock" || exit 1
        flock -n 9 || { echo "jobs: $1 は使用中です" >&2; exit 1; }
        record ack "$1"
        ;;
    *) usage ;;
esac
