#!/bin/bash
# SessionStart (compact): 圧縮の後に PM の現在地を再注入する。読むだけ。
root=$(git rev-parse --show-toplevel 2>/dev/null) || exit 0
echo "## 圧縮後の現在地 (自動)"
echo "### git (主リポジトリ)"; git -C "$root" log --oneline -3
echo "### worktree"; git -C "$root" worktree list | head -12
h=$(ls "$root"/docs/tasks/agents/HANDOVER_*.md 2>/dev/null | sort | tail -1)
[ -n "$h" ] && { echo "### $(basename "$h") の冒頭"; head -20 "$h"; }
[ -x /home/hight/os32-tmp/bin/check_slot.sh ] && { echo "### 検査枠"; /home/hight/os32-tmp/bin/check_slot.sh --status; }
exit 0
