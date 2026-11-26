#!/usr/bin/env bash
# GitHub Actions (restructure-build.yml) が作った OS32 のビルド成果物を取ってくる (docs/08_build.md §8-6)。
#
# 実機に繋がったホスト側で使う。要 gh CLI (`gh auth login` を 1 回。public repo でも
# artifact の API ダウンロードには認証が要る)。
#
# 使い方:
#   tools/ci_fetch.sh [--branch main] [--sha SHA] [--dir DIR] [--repo OWNER/REPO] [--dry-run] [--wait] [--allow-built] [--evidence JSON]
#     --branch  最新の run をこのブランチから選ぶ (既定 main)
#     --sha     このコミットの run を選ぶ (短縮 SHA 可。--branch と併用すると両方で絞る)
#     --dir     保存先の親 (既定 ./os32-ci)。中に artifact 名のディレクトリを作る
#     --allow-built  実機調査用に BUILT から取得する (既定は VERIFIED 以上)
#     --evidence JSON  land_gate で両機種の記録を照合して ACCEPTED を表示する
#     --wait    指定した状態になるまで待つ
#     --dry-run 選んだ run と artifact を表示するだけでダウンロードしない
#
# 終了コード: 0 = 取得して sha256 一致、1 = 失敗 (該当 run 無し・照合不一致など)、
#             2 = gh が無い / 未認証 / 引数誤り
set -euo pipefail

REPO="ske-studio/os32-v3"
WORKFLOW="restructure-build.yml"
BRANCH="main"
BRANCH_SET=0
SHA=""
DIR="./os32-ci"
DRY=0
WAIT=0
ALLOW_BUILT=0
EVIDENCE=""

usage() { awk 'NR>1 && !/^#/ {exit} NR>1 {sub(/^# ?/, ""); print}' "$0"; }

while [ $# -gt 0 ]; do
    case "$1" in
        --branch)  BRANCH="$2"; BRANCH_SET=1; shift 2 ;;
        --sha)     SHA="$2"; shift 2 ;;
        --dir)     DIR="$2"; shift 2 ;;
        --repo)    REPO="$2"; shift 2 ;;
        --allow-built) ALLOW_BUILT=1; shift ;;
        --evidence) EVIDENCE="$2"; shift 2 ;;
        --wait)    WAIT=1; shift ;;
        --dry-run) DRY=1; shift ;;
        -h|--help) usage; exit 0 ;;
        *) echo "ci_fetch.sh: 不明な引数: $1" >&2; usage >&2; exit 2 ;;
    esac
done

if ! command -v gh >/dev/null 2>&1; then
    cat >&2 <<'MSG'
ci_fetch.sh: gh (GitHub CLI) が見つからない。
  Ubuntu: sudo apt install gh   (古ければ https://cli.github.com/ の apt リポジトリ)
  その後: gh auth login   (GitHub.com / HTTPS / ブラウザ認証で 1 回だけ)
MSG
    exit 2
fi
if ! gh auth status >/dev/null 2>&1; then
    cat >&2 <<'MSG'
ci_fetch.sh: gh が未認証。`gh auth login` を 1 回実行してから再度どうぞ。
  (public repo でも artifact のダウンロードは認証が要る。
   認証なしで取りたいときは tag v* の Release から curl で取る — docs/08_build.md §8-6)
MSG
    exit 2
fi

# --- run を選ぶ ---
args=(run list -R "$REPO" --workflow "$WORKFLOW"
      --json databaseId,headSha,headBranch,createdAt,displayTitle)
if [ -n "$SHA" ]; then
    if [ "${#SHA}" -eq 40 ]; then
        args+=(--commit "$SHA" --limit 1)
    else
        # --commit は完全な SHA しか受けないので、最近の run を前方一致で探す
        args+=(--limit 100)
    fi
    [ "$BRANCH_SET" = 1 ] && args+=(--branch "$BRANCH")
else
    args+=(--branch "$BRANCH" --limit 1)
fi

RUN_JSON="$(gh "${args[@]}")"
if [ -n "$SHA" ]; then
    RUN_JSON="$(printf '%s' "$RUN_JSON" | SHA="$SHA" python3 -c '
import json, os, sys
runs = [r for r in json.load(sys.stdin) if r["headSha"].startswith(os.environ["SHA"].lower())]
print(json.dumps(runs[:1]))')"
fi

RUN_ID="" HEAD_SHA="" HEAD_BRANCH="" CREATED=""
read -r RUN_ID HEAD_SHA HEAD_BRANCH CREATED < <(printf '%s' "$RUN_JSON" | python3 -c '
import json, sys
runs = json.load(sys.stdin)
if runs:
    r = runs[0]
    print(r["databaseId"], r["headSha"], r["headBranch"], r["createdAt"])
') || true

if [ -z "$RUN_ID" ]; then
    if [ -n "$SHA" ]; then what="commit $SHA"; else what="branch $BRANCH"; fi
    echo "ci_fetch.sh: $REPO の $WORKFLOW に $what の run が無い" >&2
    exit 1
fi

while true; do
    RUN=$(gh api "repos/$REPO/actions/runs/$RUN_ID")
    read -r ATTEMPT STATUS < <(python3 -c 'import json,sys
r=json.load(sys.stdin)
print(r["run_attempt"],r["status"])' <<< "$RUN")
    ART="os32-${HEAD_BRANCH//\//-}-${HEAD_SHA:0:7}-a$ATTEMPT"
    MARKER="verified-$HEAD_SHA-a$ATTEMPT"
    NAMES=$(gh api --paginate "repos/$REPO/actions/runs/$RUN_ID/artifacts?per_page=100" \
        --jq '.artifacts[] | select(.expired == false) | .name')
    STATE=MISSING
    if grep -Fxq "$ART" <<< "$NAMES"; then
        STATE=BUILT
        if grep -Fxq "$MARKER" <<< "$NAMES"; then STATE=VERIFIED; fi
    fi
    if [[ "$STATE" == VERIFIED || ( "$STATE" == BUILT && "$ALLOW_BUILT" == 1 ) ]]; then break; fi
    echo "ci_fetch.sh: run $RUN_ID attempt $ATTEMPT state=$STATE" >&2
    [[ "$WAIT" == 1 && "$STATUS" != completed ]] || exit 1
    sleep 10
done
if [[ -n "$EVIDENCE" ]]; then
    python3 -c 'import json,sys
e=json.load(open(sys.argv[1]))
sys.exit((e["run_id"],e["attempt"]) != (int(sys.argv[2]),int(sys.argv[3])))' "$EVIDENCE" "$RUN_ID" "$ATTEMPT"
    GITHUB_REPOSITORY="$REPO" python3 -B "$(dirname "$0")/ci/land_gate.py" "$HEAD_SHA" --evidence "$EVIDENCE"
    STATE=ACCEPTED
fi
echo "state:    $STATE (attempt $ATTEMPT)"
echo "run:      $RUN_ID  ($HEAD_BRANCH @ ${HEAD_SHA:0:7}, $CREATED)"
echo "url:      https://github.com/$REPO/actions/runs/$RUN_ID"
if [ -z "$ART" ]; then
    echo "ci_fetch.sh: この run に有効な artifact が無い (保持 30 日を過ぎた?)" >&2
    exit 1
fi
echo "artifact: $ART"

DEST="$DIR/$ART"
if [ "$DRY" = 1 ]; then
    echo "dry-run:  gh run download $RUN_ID -R $REPO -n $ART -D $DEST"
    exit 0
fi

if [ -e "$DEST" ]; then
    echo "ci_fetch.sh: $DEST は既にある。消すか --dir を変えてから再度どうぞ" >&2
    exit 1
fi
mkdir -p "$DEST"
gh run download "$RUN_ID" -R "$REPO" -n "$ART" -D "$DEST"

echo
cat "$DEST/BUILD_INFO.txt"
echo
echo "=== sha256 照合 ==="
if (cd "$DEST" && sha256sum --quiet -c SHA256SUMS); then
    echo "OK: $(wc -l < "$DEST/SHA256SUMS") ファイル一致 -> $DEST"
else
    echo "ci_fetch.sh: sha256 が一致しない ($DEST)" >&2
    exit 1
fi
