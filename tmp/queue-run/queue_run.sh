#!/usr/bin/env python3
import argparse
from collections import Counter
from dataclasses import dataclass
import fcntl
import os
from pathlib import Path
import re
import stat
import subprocess
import sys
import tempfile
import time

STATES = {"queued", "running", "done", "failed", "held"}
TOKEN = re.compile(r"[A-Za-z0-9][A-Za-z0-9_.-]*")
BUILD_PATH = re.compile(r"/[A-Za-z0-9_./+@%-]+")
PROMPT_END = " を読んで、その指示に従う。"


@dataclass
class Stage:
    index: int
    fields: list
    deps: list
    ending: str

    @property
    def id(self):
        return self.fields[0]

    @property
    def kind(self):
        return self.fields[1]

    @property
    def state(self):
        return self.fields[6]


def warn(message):
    print(f"警告: {message}", file=sys.stderr)


def parse_queue(lines):
    candidates = []
    for index, line in enumerate(lines):
        body = line.rstrip("\r\n")
        if not body.strip() or body.startswith("#"):
            continue
        fields = body.split("\t")
        if len(fields) == 8:
            stage_id, kind, model, workdir, pack, deps, state, _ = fields
            deps = deps.split(",") if deps else []
            if (TOKEN.fullmatch(stage_id) and kind in {"build", "read"}
                    and state in STATES and TOKEN.fullmatch(model)
                    and workdir and pack and all(TOKEN.fullmatch(dep) for dep in deps)):
                candidates.append(Stage(index, fields, deps, line[len(body):]))
                continue
        warn(f"QUEUE.tsv {index + 1} 行目の形式が不正です")
    counts = Counter(stage.id for stage in candidates)
    for stage in candidates:
        if counts[stage.id] > 1:
            warn(f"QUEUE.tsv {stage.index + 1} 行目の id が重複しています: {stage.id}")
    return {stage.id: stage for stage in candidates if counts[stage.id] == 1}


def limits_for(run_dir):
    limits = {"build": "3", "read": "8"}
    config = run_dir / "QUEUE.conf"
    if config.exists():
        for line in config.read_text().splitlines():
            line = line.partition("#")[0].strip()
            if line:
                key, _, value = line.partition("=")
                if key.strip() not in limits:
                    raise ValueError(f"QUEUE.conf の設定名が不正です: {key}")
                limits[key.strip()] = value.strip()
    for kind in limits:
        value = os.environ.get(f"QUEUE_{kind.upper()}_LIMIT", limits[kind])
        if not re.fullmatch(r"[0-9]+", value):
            raise ValueError(f"{kind} の上限が不正です: {value}")
        limits[kind] = int(value)
    return limits


def running_processes(run_dir, stages):
    snapshot = subprocess.check_output(
        ["ps", "-ww", "-eo", "pid=,ppid=,args="], text=True)
    counts = Counter()
    active = set()
    parents = {}
    commands = {}
    for line in snapshot.splitlines():
        parts = line.split(None, 2)
        if len(parts) != 3 or not all(part.isdigit() for part in parts[:2]):
            continue
        pid, parent, command = parts
        parents[pid] = parent
        if re.match(r"^(?:\S*/)?codex exec(?:\s|$)", command):
            commands[pid] = command
        elif re.match(r"^(?:\S*/)?node\s+(?:\S*/)?(?:codex|codex\.js) exec(?:\s|$)", command):
            commands[pid] = command
    wrappers = set()
    for pid in commands:
        parent = parents[pid]
        while parent in parents and parent != pid:
            if parent in commands:
                wrappers.add(parent)
            parent = parents[parent]
    for pid, command in commands.items():
        if pid in wrappers:
            continue
        options, _, prompt = command.partition(" 最初に ")
        stage_id = None
        for directory in re.findall(r"(?:^|\s)--add-dir(?:=|\s+)(\S+)", options):
            path = Path(directory)
            if path.parent == run_dir and path.name in stages:
                stage_id = path.name
                break
        if stage_id is None:
            match = re.search(r"\s\(段 ([A-Za-z0-9_.-]+)\)$", prompt)
            if match and match[1] in stages:
                stage_id = match[1]
        if stage_id is not None:
            active.add(stage_id)
            counts[stages[stage_id].kind] += 1
        else:
            read_only = re.search(r"(?:^|\s)(?:-s|--sandbox)(?:=|\s+)read-only(?:\s|$)", options)
            counts["read" if read_only else "build"] += 1
    return counts, active


def log_result(log):
    try:
        line = subprocess.check_output(["tail", "-n", "1", "--", str(log)],
                                       stderr=subprocess.DEVNULL)
    except subprocess.CalledProcessError:
        return None
    match = re.fullmatch(rb"rc=([0-9]+)\r?\n?", line)
    if match:
        return "done" if int(match[1]) == 0 else "failed"
    return None


def save_queue(queue, lines):
    with tempfile.NamedTemporaryFile(mode="w", encoding="utf-8", errors="surrogateescape",
                                     newline="", dir=queue.parent, delete=False) as file:
        temp = Path(file.name)
        try:
            os.fchmod(file.fileno(), stat.S_IMODE(queue.stat().st_mode))
            file.writelines(lines)
            file.flush()
            os.fsync(file.fileno())
            os.replace(temp, queue)
        finally:
            temp.unlink(missing_ok=True)


def read_worker(command):
    try:
        rc = subprocess.call(command)
        if rc < 0:
            rc = 128 - rc
    except OSError as error:
        print(error, file=sys.stderr)
        rc = 127
    print(f"\nrc={rc}", flush=True)
    return rc


def launch(stage, run_dir, workdir, pack):
    model = stage.fields[2]
    if stage.kind == "build":
        subprocess.run([str(run_dir.parent / "bin/codex_run.sh"),
                        stage.id, model, str(workdir), str(pack)],
                       check=True, stdout=subprocess.DEVNULL)
        return

    directory = run_dir / stage.id
    directory.mkdir(exist_ok=True)
    log = directory / "impl.log"
    if log.exists():
        log.rename(directory / f"impl.{time.time_ns()}.log")
    seen = run_dir / "watch.seen"
    if seen.exists():
        key = f"{stage.id}/impl.log"
        subprocess.run(["sed", "-i", "-e", rf"\|^{re.escape(key)}$|d",
                        "-e", rf"\|^stall:{re.escape(key)}:|d",
                        "-e", rf"\|^alert:{re.escape(key)}:|d",
                        str(seen)], check=True)
    command = ["timeout", "7000", "codex", "exec", "--skip-git-repo-check",
               "-m", model, "-s", "read-only", "-C", str(workdir),
               f"最初に {pack}{PROMPT_END} (段 {stage.id})"]
    with log.open("ab") as output:
        subprocess.Popen([sys.executable, str(Path(__file__).resolve()), "--read-worker", *command],
                         cwd=directory, stdin=subprocess.DEVNULL, stdout=output,
                         stderr=subprocess.STDOUT, start_new_session=True)


def run(run_dir, dry_run):
    queue = run_dir / "QUEUE.tsv"
    limits = limits_for(run_dir)
    with queue.open(encoding="utf-8", errors="surrogateescape", newline="") as file:
        lines = file.readlines()
    stages = parse_queue(lines)
    counts, active = running_processes(run_dir, stages)
    prefix = "[dry-run] " if dry_run else ""

    def update(stage, state):
        previous = stage.state
        stage.fields[6] = state
        lines[stage.index] = "\t".join(stage.fields) + stage.ending
        if not dry_run:
            save_queue(queue, lines)
        print(f"{prefix}{stage.id}: {previous} -> {state}")

    for stage in stages.values():
        if stage.state == "running":
            result = log_result(run_dir / stage.id / "impl.log")
            if result:
                update(stage, result)
            elif stage.id not in active:
                # A just-started stage may not have reached codex exec yet.
                counts[stage.kind] += 1

    print(f"{prefix}build {counts['build']}/{limits['build']}, "
          f"read {counts['read']}/{limits['read']}")
    for stage in stages.values():
        if stage.state != "queued":
            continue
        unknown = [dep for dep in stage.deps if dep not in stages]
        if unknown:
            warn(f"{stage.id}: 依存の id が見つかりません: {', '.join(unknown)}")
            continue
        if stage.id in active or any(stages[dep].state != "done" for dep in stage.deps):
            continue
        if counts[stage.kind] >= limits[stage.kind]:
            continue
        try:
            workdir, pack = ((run_dir / Path(field).expanduser()).resolve()
                             for field in stage.fields[3:5])
            if not pack.is_file() or not workdir.is_dir():
                warn(f"{stage.id}: 依頼ファイルまたは作業木がありません")
                continue
            # The existing launcher interpolates these paths into bash -c.
            if stage.kind == "build" and not all(
                    BUILD_PATH.fullmatch(str(path)) for path in (workdir, pack)):
                raise ValueError("codex_run.sh に安全に渡せないパスです")
            if not dry_run:
                launch(stage, run_dir, workdir, pack)
        except (OSError, RuntimeError, ValueError, subprocess.CalledProcessError) as error:
            warn(f"{stage.id}: 起動できません: {error}")
            continue
        update(stage, "running")
        counts[stage.kind] += 1


def main():
    if sys.argv[1:2] == ["--read-worker"]:
        return read_worker(sys.argv[2:])
    parser = argparse.ArgumentParser()
    parser.add_argument("--dry-run", action="store_true")
    args = parser.parse_args()
    run_dir = Path(os.environ.get("QUEUE_RUN_DIR", "~/os32-tmp/run")).expanduser().resolve()
    try:
        lock = os.open(run_dir, os.O_RDONLY | os.O_DIRECTORY)
        try:
            fcntl.flock(lock, fcntl.LOCK_EX)
            run(run_dir, args.dry_run)
        finally:
            os.close(lock)
    except (OSError, ValueError, subprocess.CalledProcessError) as error:
        warn(error)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
