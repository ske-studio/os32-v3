#!/usr/bin/env python3
"""review_local.py — ローカル / リモートの Ollama に独立レビューを依頼する。

Codex・Antigravity・Fable サブエージェントが枯渇したときの補助レビュアー
(docs/tasks/agents/ROLES.md §5、スキル os32-local-review)。

**opencode は介さない。** 対象ファイルを行番号つきで依頼文に埋め込み、
1 回の要求で返させる。こうするとモデルが何を見たかが確定し、
「読めていなかったので読み直す」空回りが起きない。

使い方:
    python3 tools/review_local.py <依頼文ファイル> <対象ファイル>...
    python3 tools/review_local.py prompt.txt fs/ext2_dir.c fs/ext2_file.c > out.md

環境変数:
    OS32_REVIEW_LLM_URL  Ollama の口 (既定 http://127.0.0.1:11434)。環境変数に無ければ
                         リポジトリ直下の .env の同名の行を読む (REVIEW_HOST は旧名、互換)
    REVIEW_MODEL   モデル名 (既定 gemma4:31b — 実測でいちばん見つける。SKILL.md の表)
    REVIEW_MAXTOK  生成の上限トークン (既定 8192)
    REVIEW_TIMEOUT 待ち時間の秒 (既定 1800)

終了コード: 0 = 応答を得た / 1 = 引数誤り / 2 = 通信・サーバー側の失敗。
所要時間とトークン数は stderr に出る (合否の判断材料として記録に残すこと)。
"""
import sys
import os
import json
import time
import urllib.request
import urllib.error



def _env_file_value(name):
    """リポジトリ直下の .env から name= の値だけを読む (無ければ None)。値は出力しない。"""
    path = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", ".env")
    try:
        with open(path, encoding="utf-8") as fp:
            for line in fp:
                line = line.strip()
                if line.startswith(name + "="):
                    return line[len(name) + 1:].strip().strip('"').strip("'") or None
    except OSError:
        pass
    return None


HOST = (os.environ.get("OS32_REVIEW_LLM_URL") or os.environ.get("REVIEW_HOST")
        or _env_file_value("OS32_REVIEW_LLM_URL") or "http://127.0.0.1:11434")
MODEL = os.environ.get("REVIEW_MODEL", "gemma4:31b")
MAXTOK = int(os.environ.get("REVIEW_MAXTOK", "8192"))
TIMEOUT = int(os.environ.get("REVIEW_TIMEOUT", "1800"))


def numbered(path):
    """ファイルを行番号つきで返す。行番号が無いとモデルが位置を推測で書く。"""
    with open(path, encoding="utf-8", errors="replace") as f:
        return "".join("%5d| %s" % (i, line) for i, line in enumerate(f, 1))


def build_prompt(prompt_file, files):
    parts = [open(prompt_file, encoding="utf-8").read(),
             "\n\n## 対象ファイルの中身 (行番号つき)\n"]
    for path in files:
        parts.append("\n### %s\n```\n%s```\n" % (path, numbered(path)))
    return "".join(parts)


def main(argv):
    if len(argv) < 3:
        sys.stderr.write(__doc__)
        return 1
    prompt = build_prompt(argv[1], argv[2:])
    approx_tok = len(prompt) // 3          # 日本語混じりの粗い見積り
    sys.stderr.write("送信 %d 文字 (約 %d トークン) / モデル %s / %s\n"
                     % (len(prompt), approx_tok, MODEL, HOST))

    body = json.dumps({
        "model": MODEL,
        "think": False,                    # 思考を出すと出力枠を使い切って空になる
        "stream": False,
        "options": {"temperature": 0.2, "top_p": 0.8, "num_predict": MAXTOK},
        "messages": [{"role": "user", "content": prompt}],
    }).encode()
    req = urllib.request.Request(HOST + "/api/chat", data=body,
                                 headers={"Content-Type": "application/json"})
    t0 = time.time()
    try:
        with urllib.request.urlopen(req, timeout=TIMEOUT) as r:
            data = json.loads(r.read().decode())
    except (urllib.error.URLError, OSError) as exc:
        sys.stderr.write("失敗: %s\n" % exc)
        return 2
    elapsed = time.time() - t0

    if "error" in data:
        sys.stderr.write("サーバー側の失敗: %s\n" % str(data["error"])[:300])
        return 2

    ev = data.get("eval_count", 0)
    ev_s = data.get("eval_duration", 1) / 1e9
    sys.stderr.write("prompt %s tok / eval %s tok / %.0f s (%.1f tok/s)\n"
                     % (data.get("prompt_eval_count"), ev, elapsed,
                        ev / max(ev_s, 0.001)))
    content = data.get("message", {}).get("content", "")
    if not content.strip():
        sys.stderr.write("応答が空。think の抑制か文脈長を疑う\n")
        return 2
    print(content)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
