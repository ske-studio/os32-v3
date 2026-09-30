#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""emu_agent MCP サーバ (stdio JSON-RPC 2.0)

ローカル小型 LLM (flm serve) に OS32 実機操作をさせる agent.py の薄いラッパー。
Claude Code から「このタスクをローカルモデルにやらせて結果を見る」ために使う。
CLI でも同じことができる (agent.py run/tail)。

.mcp.json 登録例:
  "emu_agent": {"command": "python3", "args": ["tools/emu_agent/mcp_server.py"]}
"""

import json
import os
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
AGENT = os.path.join(HERE, "agent.py")

TOOLS = [
    {
        "name": "emu_agent_run",
        "description": "ローカル LLM (flm serve) に OS32 実機のタスクを 1 つ実行させ、"
                       "ステップ列と最終 RESULT 行を返す。flm serve と NP21/W が"
                       "起動している必要がある。同期実行 (数分かかる)。",
        "inputSchema": {
            "type": "object",
            "properties": {
                "task": {"type": "string", "description": "モデルに与えるタスク文"},
                "model": {"type": "string",
                          "description": "flm のモデル名 (既定 gemma4-it:e4b)。"
                                         "未ロードのモデルを指定すると flm が"
                                         "ダウンロードを始めるので注意"},
                "max_steps": {"type": "integer", "description": "最大行動回数 (既定 12)"},
                "session": {"type": "string", "description": "ログ名"},
                "verbose": {"type": "boolean",
                            "description": "true でステップごとの行動と観測も返す "
                                           "(既定 false: RESULT 行だけ。詳細は steps.jsonl)"},
            },
            "required": ["task"],
        },
    },
    {
        "name": "emu_agent_suite",
        "description": "タスクファイル (tools/emu_agent/tasks/*.txt) の各タスクを順に"
                       "ローカル LLM に実行させ、SUMMARY と RESULT 行を返す。"
                       "既定は tasks/regress.txt。同期実行 (十数分かかることがある)。",
        "inputSchema": {
            "type": "object",
            "properties": {
                "file": {"type": "string", "description": "タスクファイルのパス"},
                "model": {"type": "string"},
                "max_steps": {"type": "integer"},
                "session": {"type": "string"},
                "verbose": {"type": "boolean"},
            },
        },
    },
    {
        "name": "emu_agent_tail",
        "description": "直近 (または指定) セッションのステップログ末尾を返す",
        "inputSchema": {
            "type": "object",
            "properties": {
                "session": {"type": "string"},
                "n": {"type": "integer"},
            },
        },
    },
]


def _run(argv, timeout):
    p = subprocess.run([sys.executable, AGENT] + argv, capture_output=True,
                       text=True, timeout=timeout, cwd=HERE)
    out = p.stdout
    if p.stderr.strip():
        out += "\n[stderr]\n" + p.stderr
    return out


def call(name, args):
    if name == "emu_agent_run":
        argv = ["run", args["task"]]
        if args.get("model"):
            argv += ["--model", args["model"]]
        if args.get("max_steps"):
            argv += ["--max-steps", str(args["max_steps"])]
        if args.get("session"):
            argv += ["--session", args["session"]]
        if not args.get("verbose"):
            argv.append("--quiet")
        return _run(argv, timeout=3600)
    if name == "emu_agent_suite":
        argv = ["suite", args.get("file") or os.path.join(HERE, "tasks", "regress.txt")]
        if args.get("model"):
            argv += ["--model", args["model"]]
        if args.get("max_steps"):
            argv += ["--max-steps", str(args["max_steps"])]
        if args.get("session"):
            argv += ["--session", args["session"]]
        if not args.get("verbose"):
            argv.append("--quiet")
        return _run(argv, timeout=7200)
    if name == "emu_agent_tail":
        argv = ["tail"]
        if args.get("session"):
            argv += ["--session", args["session"]]
        if args.get("n"):
            argv += ["-n", str(args["n"])]
        return _run(argv, timeout=30)
    raise ValueError("unknown tool: " + name)


def reply(id_, result=None, error=None):
    msg = {"jsonrpc": "2.0", "id": id_}
    if error is not None:
        msg["error"] = error
    else:
        msg["result"] = result
    sys.stdout.write(json.dumps(msg, ensure_ascii=False) + "\n")
    sys.stdout.flush()


def main():
    for line in sys.stdin:
        line = line.strip()
        if not line:
            continue
        try:
            req = json.loads(line)
        except ValueError:
            continue
        m = req.get("method")
        id_ = req.get("id")
        if m == "initialize":
            reply(id_, {"protocolVersion": req.get("params", {}).get("protocolVersion", "2024-11-05"),
                        "capabilities": {"tools": {}},
                        "serverInfo": {"name": "emu_agent", "version": "0.1"}})
        elif m == "notifications/initialized":
            continue
        elif m == "tools/list":
            reply(id_, {"tools": TOOLS})
        elif m == "tools/call":
            p = req.get("params", {})
            try:
                text = call(p.get("name"), p.get("arguments") or {})
                reply(id_, {"content": [{"type": "text", "text": text}]})
            except Exception as e:  # noqa: BLE001
                reply(id_, {"content": [{"type": "text", "text": "error: %s" % e}],
                            "isError": True})
        elif id_ is not None:
            reply(id_, error={"code": -32601, "message": "method not found: %s" % m})


if __name__ == "__main__":
    main()
