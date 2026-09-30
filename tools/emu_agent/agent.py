#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""emu_agent — ローカル小型 LLM に NP21/W 上の OS32 を操作させる最小ドライバ

flm serve (FastFlowLM, OpenAI 互換 API, 127.0.0.1:52625) のモデルに
「JSON 1 個で行動を返す」プロトコルでエミュレータを触らせる。
4B 級のモデル向けに、ツール定義は system prompt の平文で渡し、
ネイティブ function calling には依存しない (FLM の tools 対応が
モデルごとに不安定なため)。tool_calls が返ってきた場合はそれも受ける。

実機との通信は NP21/W ai-debug 版の HTTP API。HTTP 層は
tools/np21w_mcp/np21w_client.py を流用する (curl.exe フォールバック込み)。

使い方:
  python3 agent.py run "ver を実行してバージョンを報告して" \
        [--model gemma4-it:e4b] [--max-steps 12] [--session NAME]
  python3 agent.py tail [--session NAME] [-n 20]

ログ: tools/emu_agent/logs/<session>/steps.jsonl (1 行 1 ステップ)

モデルの行動 (JSON):
  {"action":"cmd","cmd":"ls /"}        シェルコマンドを実行し出力を得る
  {"action":"key","seq":"ENTER"}       キーイベント注入 (seq または text)
  {"action":"tvram"}                   画面 80x25 をテキストで得る
  {"action":"status"}                  エミュレータの状態
  {"action":"done","report":"..."}     終了。report は人間向けの報告
ホスト側 (ビルド/配備。許可リスト方式で自由なシェルは与えない):
  {"action":"make","target":"kernel"}  make <target> (kernel/kernel-lgy98/kernel-lgy98-link/kernel-nolgy98/check-net-m2/check-net-m2-cpl3/check-net-m4/check-net-l0/check-net-l1/check-net-l2/check-net-l3/programs/sdk/all/check/clean/apps/game/external)
  {"action":"cmd","cmd":"hsync"}       配備後にゲスト側で /host -> / を同期 (sys は除く)
  {"action":"deploy"}                  os32-cycle deploy (停止→NHD 配備→起動→ver)
"""

import argparse
import json
import os
import re
import subprocess
import sys
import time
import threading
import urllib.error
import urllib.parse
import urllib.request

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "..", "np21w_mcp"))
import np21w_client as emu  # noqa: E402
from symbols import SymbolTable  # noqa: E402

LOGS = os.path.join(HERE, "logs")

FLM_BASE = os.environ.get("FLM_URL", "http://127.0.0.1:52625")
FLM_MODEL = os.environ.get("FLM_MODEL", "gemma4-it:e4b")

# [V3] リモート実行の curl タイムアウトは短くしない (15s 最小、長いものは 60s+)
CMD_TIMEOUT = int(os.environ.get("EMU_CMD_TIMEOUT", "90"))
LLM_TIMEOUT = int(os.environ.get("FLM_TIMEOUT", "600"))
# 小型モデルの文脈を食い潰さないよう、実機出力はこの長さで切る
OBS_LIMIT = 1600
BUILD_OBS_LIMIT = 2400

ROOT = os.environ.get("OS32_ROOT", os.path.abspath(os.path.join(HERE, "..", "..")))
OS32_CYCLE = os.environ.get("OS32_CYCLE", os.path.expanduser("~/.local/bin/os32-cycle"))
MAKE_TARGETS = ("kernel", "kernel-lgy98", "kernel-lgy98-link", "kernel-nolgy98", "check-net-m2", "check-net-m2-cpl3", "check-net-m4", "check-net-l0", "check-net-l1", "check-net-l2", "check-net-l3", "programs", "sdk", "all", "check", "check-fast", "check-changed", "check-map", "check-check-select-host", "clean", "deploy",
                "apps", "game", "external", "gshell",
                # `make check` の個別ターゲット。いずれもホスト側の読み取り専用試験で、
                # 落ちた 1 本だけを回し直すのに要る。build/sdk.mk に足したら
                # ここにも足す (2026-09-09: 落ちていて回し直せなかった)。
                "check-boot-splash-host", "check-constraints", "check-db-owned-host",
                "check-gui-proto", "check-kapi-version", "check-manifests",
                "check-memory-host", "check-privileged", "check-sqlite-groups-host",
                "check-t5a-host", "check-gshell-host", "check-term-model",
                "check-term-render", "check-tools-host", "check-vfs-fd-sqlite-host",
                "check-vfs-mount-dev-host", "check-multiapp-model-host",
                "check-con-sink-host", "check-kbd-inject-host", "check-launch-host",
                "check-sh-launch-host", "check-sh-shell-host",
                "check-sh-truncation-host", "check-sh-status-host",
                "check-host-agent", "check-net-link-host",
                "check-fs-kind-callers-host", "check-cat-linenum-host",
                # 票 H2 (一時ファイル + 検証 + 置換)
                "check-vfs-excl-host", "check-hsync-h2-host",
                # 票 H4 (配備マニフェストと世代の確認)
                "check-h4-manifest-host",
                # 票 TASK_KSTRING_BENCH (kstring の実測プログラムの枠組み)
                "check-kstr-bench-host", "kstr_bench",
                # 票 TASK_TEST_RESULT (試験の合否を機械が読める形にする約束事)
                "check-result-conv-host",
                # 票 TASK_TEST_RUNNER (ゲストで一括実行してホストで集計する)。
                # check-guest は **NP21/W が動いていないと失敗する** (ホスト
                # だけで完結しないので `make check` の列には入っていない)。
                # check-guest-host はホストだけのランナー試験。
                "check-guest", "check-guest-host",
                "check-hsync-h1-host", "check-hsync-h3-host", "check-b8-open-host",
                "check-ring3-str-host", "nhd-pull", "nhd-umount", "nhd-init", "deploy-nhd", "deploy-kernel", "images/os32_boot.d88", "iso", "check-settings-protect-host", "check-db-v50-host", "check-cfg-host", "check-gui-host", "check-install-recover-host", "check-install-fresh-host", "clean-external")
MAP_PATH = os.environ.get("OS32_KERNEL_MAP", os.path.join(ROOT, "build", "out", "kernel.map"))
SELFTEST_SYMS = ("kselftest_pass", "kselftest_fail")
WAIT_MAX = 60
# 実行中セッションの文脈 (screenshot の保存先などに使う)
CTX = {"session": "adhoc", "step": 0}
QUIET = False


def say(msg):
    if not QUIET:
        print(msg)
MAKE_TIMEOUT = 1800
DEPLOY_TIMEOUT = 1200

SYSTEM_PROMPT = """You operate a retro computer (NEC PC-9801 running the OS32 shell) through an emulator.
You cannot see or touch it directly. You act ONLY by replying with exactly one JSON object and no other text.

Actions:
{"action":"cmd","cmd":"<shell command line>"}  run one shell command on OS32 and receive its output
{"action":"key","seq":"<KEY>"}                   press a key (ENTER, ESC, SPACE, UP, DOWN, F1 ...); or {"action":"key","text":"abc"} to type text
{"action":"tvram"}                               read the current text screen (80x25)
{"action":"status"}                              check whether the emulator is running
{"action":"cmd_nowait","cmd":"v86 -b /host/dos5hd.nhd"}  start a shell command that will NOT return (a DOS/V86 session); use tvram/key afterwards
{"action":"wait","seconds":20}                   pause up to 60 seconds (boot, long output)
{"action":"screenshot"}                          save a screen image on the host and get its path (you cannot see it; report the path)
{"action":"selftest"}                            read the kernel self-test counters (kselftest_pass / kselftest_fail)
{"action":"done","report":"<what you found>"}    finish the task and report
Host-side build actions (they run on the development PC, not on OS32):
{"action":"make","target":"kernel"}              run `make <target>`; target is one of kernel, kernel-lgy98 (LAN 有効カーネル、以後の deploy も有効のまま), kernel-nolgy98 (無効に戻す), check-net-m2 (LAN の M2 試験、実機が LAN 有効で起動中のこと), check-net-m2-cpl3 (同じ試験を CPL3 プログラム (less) 常駐中に回す), check-net-m4 (リング飽和での自己回復), programs, sdk, all, check, any single check-* subtarget from build/sdk.mk (e.g. check-memory-host, check-gshell-host, check-constraints), clean, deploy, apps, game, external (apps/game = the external submodule repos under apps/ and game/; external = both; deploy = HostDrv sync only, NOT a verification)
{"action":"deploy"}                              full deploy: stop emulator, write NHD, restart, wait for OS32 (takes minutes)

Rules:
- One action per reply. Never explain, never add markdown, never wrap in code fences.
- Use "cmd" for anything the shell can do (ver, ls, cat, cd, echo, mkdir ...). Paths use "/" like Unix.
- Read the observation you get back before deciding the next action.
- When the task is complete, reply with "done" and quote the relevant output in the report.
- If something fails twice, stop with "done" and report the failure honestly.
- For make/deploy: do not interpret the log. Quote the "exit=" line and the "RESULT:" line verbatim in your report, plus any line containing "error".
- Never run "make clean" unless the task explicitly asks for it.
- DOS/V86 sessions: start them with "cmd_nowait" (a normal "cmd" would hang), then "wait" ~20 seconds, read "tvram" until the DOS prompt "A>" appears, type with {"action":"key","text":"dir"} followed by {"action":"key","seq":"ENTER"}, read "tvram" again, and leave DOS with {"action":"key","seq":"CTRL+STOP"}. Afterwards confirm OS32 is back with "cmd" ver."""


# ---------------------------------------------------------------------------
#  実機側アクション
# ---------------------------------------------------------------------------
def act_cmd(a):
    cmd = str(a.get("cmd", "")).strip()
    if not cmd:
        return "error: empty cmd"
    return emu.post("/api/cmd", cmd, timeout=CMD_TIMEOUT).decode("utf-8", "replace")


def act_key(a):
    body = []
    if a.get("seq"):
        body.append("seq=" + urllib.parse.quote(str(a["seq"]), safe=""))
    if a.get("text"):
        body.append("text=" + urllib.parse.quote(str(a["text"]), safe=""))
    if not body:
        return "error: key needs seq or text"
    return emu.post("/api/key", "&".join(body)).decode("utf-8", "replace")


def act_tvram(_):
    d = json.loads(emu.get("/api/tvram"))
    lines = d.get("lines", [])
    return "\n".join(l.rstrip() for l in lines)


def act_status(_):
    return emu.get("/api/status").decode("utf-8", "replace")


def act_cmd_nowait(a):
    """返ってこないコマンド (V86 セッション等) を投げっぱなしにする。
    aidebug 側は EOT 待ちで最終的にタイムアウトするが、その後の /api/cmd は
    正常に使える (2026-09-04 実測)。"""
    cmd = str(a.get("cmd", "")).strip()
    if not cmd:
        return "error: empty cmd"

    def fire():
        try:
            emu.post("/api/cmd", cmd, timeout=900)
        except Exception:  # noqa: BLE001
            pass
    threading.Thread(target=fire, daemon=True).start()
    time.sleep(2)
    return "started (not waiting for completion): %s" % cmd


def act_wait(a):
    try:
        sec = int(a.get("seconds", 10))
    except (TypeError, ValueError):
        sec = 10
    sec = max(1, min(WAIT_MAX, sec))
    time.sleep(sec)
    return "waited %ds" % sec


def act_screenshot(_):
    d = os.path.join(LOGS, CTX["session"], "shots")
    os.makedirs(d, exist_ok=True)
    path = os.path.join(d, "step%02d.bmp" % CTX["step"])
    emu.get_to_file("/api/screenshot", path)
    return "screenshot saved: %s (%d bytes)" % (path, os.path.getsize(path))


_SYMS = None


def act_selftest(_):
    global _SYMS
    if _SYMS is None:
        if not os.path.exists(MAP_PATH):
            return "error: kernel.map not found at %s (run make kernel)" % MAP_PATH
        _SYMS = SymbolTable(os.path.normpath(MAP_PATH))
    out = []
    for name in SELFTEST_SYMS:
        addr = _SYMS.resolve(name)
        if addr is None:
            out.append("%s=<symbol not in kernel.map>" % name)
            continue
        d = json.loads(emu.get("/api/mem?addr=0x%x&len=4&space=phys" % addr))
        raw = bytes.fromhex(d.get("hex", "00000000"))
        out.append("%s=%d" % (name, int.from_bytes(raw, "little")))
    return " ".join(out)


# ---------------------------------------------------------------------------
#  ホスト側アクション (ビルド/配備)。モデルに自由なシェルは渡さず、
#  引数を許可リストで縛った固定コマンドだけを実行する。
# ---------------------------------------------------------------------------
def _host_run(argv, timeout):
    t0 = time.time()
    try:
        p = subprocess.run(argv, cwd=ROOT, capture_output=True, text=True,
                           timeout=timeout, errors="replace")
    except subprocess.TimeoutExpired:
        return "exit=timeout after %ds: %s" % (timeout, " ".join(argv))
    out = (p.stdout or "") + (p.stderr or "")
    lines = out.splitlines()
    # 判定に要る行だけ残す: RESULT / エラー / 末尾
    key = [l for l in lines if re.search(r"RESULT:|\berror\b|Error|undefined reference|No rule|\*\*\*", l)]
    tail_lines = lines[-15:]
    picked = []
    for l in key[-20:] + tail_lines:
        if l not in picked:
            picked.append(l)
    body = "\n".join(picked)
    if len(body) > BUILD_OBS_LIMIT:
        body = body[-BUILD_OBS_LIMIT:]
    return "$ %s\nexit=%d (%.0fs)\n%s" % (" ".join(argv), p.returncode, time.time() - t0, body)


def act_make(a):
    target = str(a.get("target", "")).strip()
    if target not in MAKE_TARGETS:
        return "error: target must be one of %s" % ", ".join(MAKE_TARGETS)
    return _host_run(["make", "-C", ROOT, target], MAKE_TIMEOUT)


def act_deploy(_):
    if not os.path.exists(OS32_CYCLE):
        return "error: os32-cycle not found at %s" % OS32_CYCLE
    return _host_run([OS32_CYCLE, "deploy"], DEPLOY_TIMEOUT)


ACTIONS = {"cmd": act_cmd, "key": act_key, "tvram": act_tvram, "status": act_status,
           "cmd_nowait": act_cmd_nowait, "wait": act_wait, "screenshot": act_screenshot,
           "selftest": act_selftest,
           "make": act_make, "deploy": act_deploy}


# ---------------------------------------------------------------------------
#  LLM 呼び出しと応答パース
# ---------------------------------------------------------------------------
def llm_chat(model, messages):
    req = {"model": model, "messages": messages, "max_tokens": 300,
           "temperature": 0.2}
    data = json.dumps(req).encode("utf-8")
    r = urllib.request.Request(FLM_BASE + "/v1/chat/completions", data=data,
                               headers={"Content-Type": "application/json"},
                               method="POST")
    t0 = time.time()
    with urllib.request.urlopen(r, timeout=LLM_TIMEOUT) as resp:
        d = json.loads(resp.read().decode("utf-8", "replace"))
    msg = d["choices"][0]["message"]
    usage = d.get("usage", {})
    return msg, usage, time.time() - t0


_JSON_RE = re.compile(r"\{.*\}", re.S)


def parse_action(msg):
    """assistant メッセージから行動 dict を取り出す。取れなければ None"""
    # ネイティブ tool_calls が来たらそれを優先
    tc = msg.get("tool_calls") or []
    if tc:
        fn = tc[0].get("function", {})
        try:
            args = json.loads(fn.get("arguments") or "{}")
        except ValueError:
            args = {}
        name = fn.get("name", "")
        if name.startswith("emu_"):
            name = name[4:]
        args["action"] = name
        return args
    text = (msg.get("content") or "").strip()
    # ```json ... ``` を剥がす
    text = re.sub(r"^```[a-zA-Z]*\s*|\s*```$", "", text)
    m = _JSON_RE.search(text)
    if not m:
        return None
    frag = m.group(0)
    # 最初の完全な JSON オブジェクトだけ採用 (連続で 2 個出す個体がいる)
    dec = json.JSONDecoder()
    try:
        obj, _ = dec.raw_decode(frag)
    except ValueError:
        return None
    return obj if isinstance(obj, dict) else None


_OBS_FAIL = re.compile(r'^error:|"ok":\s*false|\[aidebug:|serial bridge|exit=(?!0 )\S+|RESULT: FAIL|connection (error|refused|reset)', re.I | re.M)


def obs_failed(obs):
    """ドライバ/実機/ホスト側の失敗を示す観測か (シェルの通常のエラー表示は含めない)"""
    return bool(_OBS_FAIL.search(obs or ""))


def clip(s):
    if len(s) <= OBS_LIMIT:
        return s
    return s[:OBS_LIMIT] + "\n...[truncated %d chars]" % (len(s) - OBS_LIMIT)


# ---------------------------------------------------------------------------
#  メインループ
# ---------------------------------------------------------------------------
def run(task, model, max_steps, session):
    os.makedirs(os.path.join(LOGS, session), exist_ok=True)
    log_path = os.path.join(LOGS, session, "steps.jsonl")
    log = open(log_path, "a", encoding="utf-8")

    def rec(**kw):
        kw["t"] = time.time()
        log.write(json.dumps(kw, ensure_ascii=False) + "\n")
        log.flush()

    messages = [{"role": "system", "content": SYSTEM_PROMPT},
                {"role": "user", "content": "Task: " + task}]
    rec(event="start", task=task, model=model)
    say("[emu_agent] model=%s session=%s" % (model, session))
    say("[emu_agent] task: %s" % task)

    bad = 0
    fails = 0
    CTX["session"] = session
    for step in range(1, max_steps + 1):
        CTX["step"] = step
        try:
            msg, usage, dt = llm_chat(model, messages)
        except (urllib.error.URLError, OSError, KeyError, ValueError) as e:
            rec(event="llm_error", step=step, error=str(e))
            say("[emu_agent] LLM error: %s" % e)
            return {"ok": False, "reason": "llm_error", "error": str(e), "steps": step}
        raw = msg.get("content") or json.dumps(msg.get("tool_calls"))
        action = parse_action(msg)
        rec(event="llm", step=step, raw=raw, action=action, usage=usage, secs=round(dt, 1))
        say("[%2d] llm %.0fs -> %s" % (step, dt, (raw or "").strip()[:200]))

        # 会話に載せるのは assistant の生テキスト (tool_calls 形式は平文化)
        messages.append({"role": "assistant", "content": raw if isinstance(raw, str) else json.dumps(raw)})

        if action is None or "action" not in action:
            bad += 1
            obs = ("Your reply was not a single JSON action object. Reply with exactly one JSON "
                   "object like {\"action\":\"cmd\",\"cmd\":\"ver\"}.")
            if bad >= 3:
                rec(event="abort", reason="unparseable")
                return {"ok": False, "reason": "unparseable", "steps": step}
            messages.append({"role": "user", "content": obs})
            continue
        bad = 0

        kind = str(action.get("action"))
        if kind == "done":
            report = str(action.get("report", ""))
            rec(event="done", step=step, report=report, obs_failures=fails)
            say("[emu_agent] DONE: %s" % report)
            # ok はモデルの自己申告ではなく「完走かつ観測に失敗が無い」
            return {"ok": fails == 0, "report": report, "steps": step, "obs_failures": fails}

        fn = ACTIONS.get(kind)
        if fn is None:
            obs = "Unknown action '%s'. Valid: cmd, key, tvram, status, cmd_nowait, wait, screenshot, selftest, make, deploy, done." % kind
        else:
            try:
                obs = fn(action)
            except Exception as e:  # noqa: BLE001 — 実機側の失敗はそのまま観測として返す
                obs = "error: %s" % e
        if kind not in ("make", "deploy"):
            obs = clip(obs)
        if obs_failed(obs):
            fails += 1
        rec(event="obs", step=step, action=action, obs=obs, failed=obs_failed(obs))
        say("     obs: %s" % obs.strip().replace("\n", " | ")[:200])
        messages.append({"role": "user", "content": "Observation:\n" + obs})

    rec(event="abort", reason="max_steps")
    return {"ok": False, "reason": "max_steps", "steps": max_steps}


def load_tasks(path):
    """タスクファイル: '#' 行はコメント、空行区切りで 1 タスク (複数行可)"""
    tasks, cur = [], []
    with open(path, encoding="utf-8") as f:
        for line in f:
            line = line.rstrip("\n")
            if line.startswith("#"):
                continue
            if not line.strip():
                if cur:
                    tasks.append(" ".join(x.strip() for x in cur))
                    cur = []
                continue
            cur.append(line)
    if cur:
        tasks.append(" ".join(x.strip() for x in cur))
    return tasks


def suite(path, model, max_steps, session):
    tasks = load_tasks(path)
    if not tasks:
        print("no tasks in %s" % path)
        return 1
    results = []
    for i, task in enumerate(tasks, 1):
        sub = "%s-%02d" % (session, i)
        say("\n===== [%d/%d] %s" % (i, len(tasks), sub))
        res = run(task, model, max_steps, sub)
        res["session"] = sub
        res["task"] = task
        results.append(res)
    print("\n===== SUMMARY (%s)" % session)
    n_ok = 0
    for r in results:
        flag = "ok " if r.get("ok") else "NG "
        n_ok += 1 if r.get("ok") else 0
        print("%s %-22s steps=%-2s %s" % (flag, r["session"], r.get("steps"),
                                          (r.get("report") or r.get("reason") or "")[:110]))
    summary = {"ok": n_ok == len(results), "passed": n_ok, "total": len(results),
               "session": session}
    print("RESULT: " + json.dumps(summary, ensure_ascii=False))
    return 0 if summary["ok"] else 1


def tail(session, n):
    p = os.path.join(LOGS, session, "steps.jsonl")
    if not os.path.exists(p):
        print("no log: %s" % p)
        return 1
    with open(p, encoding="utf-8") as f:
        lines = f.readlines()
    sys.stdout.write("".join(lines[-n:]))
    return 0


def main():
    ap = argparse.ArgumentParser()
    sub = ap.add_subparsers(dest="cmd", required=True)
    r = sub.add_parser("run")
    r.add_argument("task")
    r.add_argument("--model", default=FLM_MODEL)
    r.add_argument("--max-steps", type=int, default=12)
    r.add_argument("--session", default=time.strftime("%Y%m%d-%H%M%S"))
    r.add_argument("--quiet", action="store_true", help="RESULT 行だけ出す (詳細は steps.jsonl)")
    su = sub.add_parser("suite")
    su.add_argument("file", help="タスクファイル (tools/emu_agent/tasks/*.txt)")
    su.add_argument("--model", default=FLM_MODEL)
    su.add_argument("--max-steps", type=int, default=12)
    su.add_argument("--session", default=None)
    su.add_argument("--quiet", action="store_true")
    t = sub.add_parser("tail")
    t.add_argument("--session", default=None)
    t.add_argument("-n", type=int, default=20)
    args = ap.parse_args()
    global QUIET
    QUIET = bool(getattr(args, "quiet", False))

    if args.cmd == "run":
        res = run(args.task, args.model, args.max_steps, args.session)
        res["session"] = args.session
        print("RESULT: " + json.dumps(res, ensure_ascii=False))
        return 0 if res.get("ok") else 1
    if args.cmd == "suite":
        name = args.session or (os.path.splitext(os.path.basename(args.file))[0]
                                + "-" + time.strftime("%Y%m%d-%H%M%S"))
        return suite(args.file, args.model, args.max_steps, name)
    if args.cmd == "tail":
        s = args.session
        if s is None:
            sessions = sorted(os.listdir(LOGS)) if os.path.isdir(LOGS) else []
            if not sessions:
                print("no sessions")
                return 1
            s = sessions[-1]
        return tail(s, args.n)
    return 2


if __name__ == "__main__":
    sys.exit(main())
