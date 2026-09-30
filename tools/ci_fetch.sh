#!/usr/bin/env bash
# GitHub Actions (build.yml) が作った OS32 のビルド成果物を取ってくる (docs/08_build.md §8-6)。
#
# 実機に繋がったホスト側で使う。要 gh CLI (`gh auth login` を 1 回。public repo でも
# artifact の API ダウンロードには認証が要る)。
#
# 使い方:
#   tools/ci_fetch.sh [--branch feat/gui] [--sha SHA] [--dir DIR] [--repo OWNER/REPO] [--dry-run]
#     --branch  最新の成功した run をこのブランチから選ぶ (既定 feat/gui)
#     --sha     このコミットの成功 run を選ぶ (短縮 SHA 可。--branch と併用すると両方で絞る)
#     --dir     保存先の親 (既定 ./os32-ci)。中に artifact 名のディレクトリを作る
#     --dry-run 選んだ run と artifact を表示するだけでダウンロードしない
#
# 終了コード: 0 = 取得して sha256 一致、1 = 失敗 (該当 run 無し・照合不一致など)、
#             2 = gh が無い / 未認証 / 引数誤り
set -euo pipefail

REPO="ske-studio/os32"
WORKFLOW="build.yml"
BRANCH="feat/gui"
BRANCH_SET=0
SHA=""
DIR="./os32-ci"
DRY=0

usage() { awk 'NR>1 && !/^#/ {exit} NR>1 {sub(/^# ?/, ""); print}' "$0"; }

while [ $# -gt 0 ]; do
    case "$1" in
        --branch)  BRANCH="$2"; BRANCH_SET=1; shift 2 ;;
        --sha)     SHA="$2"; shift 2 ;;
        --dir)     DIR="$2"; shift 2 ;;
        --repo)    REPO="$2"; shift 2 ;;
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
args=(run list -R "$REPO" --workflow "$WORKFLOW" --status success
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
    echo "ci_fetch.sh: $REPO の $WORKFLOW に $what の成功した run が無い" >&2
    exit 1
fi

# --- artifact 名 (os32-<ref>-<sha7>) を API で引く。-n で指定すると保存先直下に展開される ---
ART="$(gh api "repos/$REPO/actions/runs/$RUN_ID/artifacts" \
        --jq '[.artifacts[] | select(.expired == false) | select(.name | startswith("os32-"))][0].name // empty')"

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
