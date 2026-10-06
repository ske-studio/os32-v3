#!/usr/bin/env bash
# Environment diagnostics only; --fix-fonts copies verified fonts, never builds.
set -uo pipefail
cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." || exit 1
rc=0
ok() { printf 'OK %s\n' "$*"; }
ng() { printf 'NG %s\n' "$*"; rc=1; }
base= fix_fonts=0
while (($#)); do
    case $1 in
        --base) [[ $# -ge 2 && -n $2 ]] || { ng '--base <SHA> が必要'; exit 1; }; base=$2; shift 2 ;;
        --fix-fonts) fix_fonts=1; shift ;;
        *) ng '使い方: tools/preflight.sh [--base <SHA>] [--fix-fonts]'; exit 1 ;;
    esac
done
head=$(git rev-parse HEAD 2>/dev/null) || { ng 'Git 作業木で実行してください'; exit 1; }
common=$(git rev-parse --path-format=absolute --git-common-dir)
git_dir=$(git rev-parse --absolute-git-dir)
if [[ $git_dir != "$common" ]]; then ok 'worktree: linked'; else ok 'worktree: main'; fi
if [[ -z $base ]]; then
    ok "基点: HEAD=$head (--base 未指定)"
elif expected=$(git rev-parse --verify "${base}^{commit}" 2>/dev/null) && [[ $head == "$expected" ]]; then
    ok '基点: 一致'
else
    ng '基点: 不一致または不明な SHA — 依頼の基点と作業木を確認してください'
fi
# Read only a literal assignment; never source .env or print its values [D3].
cross=${CROSS_DIR:-}
if [[ -z $cross && -f .env ]]; then
    cross=$(python3 -B - <<'PY'
import re, shlex
from pathlib import Path
value = ''
for line in Path('.env').read_text().splitlines():
    match = re.match(r'^\s*(?:export\s+)?CROSS_DIR\s*=\s*(.*)$', line)
    if match:
        try:
            words = shlex.split(match[1], comments=True)
            value = words[0] if len(words) == 1 else ''
        except ValueError:
            value = ''
print(value)
PY
    ) 2>/dev/null
fi
if [[ -z $cross ]]; then
    ng 'CROSS_DIR: 未設定 — CROSS_DIR=/home/hight/opt/cross を付けてください'
elif "$cross/bin/i386-elf-gcc" --version >/dev/null 2>&1; then
    ok 'CROSS_DIR: 設定あり、i386-elf-gcc 実行可能'
else
    ng 'CROSS_DIR: コンパイラ実行不可 — 設定と bin/i386-elf-gcc を確認してください'
fi
tmp_ok=0 probe=
tmp_path=$(realpath -m -- "${TMPDIR:-/tmp}" 2>/dev/null)
if [[ -z ${TMPDIR:-} || $tmp_path == /tmp || $tmp_path == /tmp/* ]]; then
    ng 'TMPDIR: 未設定または /tmp 配下 — /home/hight/os32-tmp/run/<job> を設定してください'
elif [[ ! -d $tmp_path ]]; then
    ng 'TMPDIR: ディレクトリ無し — ディスク上に作成してください'
else
    fstype=$(df --output=fstype -- "$tmp_path" 2>/dev/null | tail -n 1 | tr -d ' ')
    available=$(df -B1 --output=avail -- "$tmp_path" 2>/dev/null | tail -n 1 | tr -d ' ')
    if [[ -z $fstype || $fstype == tmpfs || $fstype == ramfs ]]; then
        ng 'TMPDIR: ディスク上ではない — ディスク上のパスを設定してください'
    elif [[ ! $available =~ ^[0-9]+$ ]] || ((available < 2 * 1024 * 1024 * 1024)); then
        ng 'TMPDIR: 空き容量不足または取得不可 — 2GB 以上確保してください'
    elif probe=$(mktemp -d "$tmp_path/preflight.XXXXXXXX" 2>/dev/null); then
        tmp_ok=1
        ok "TMPDIR: $fstype、書込み可能、空き 2GB 以上"
    else
        ng 'TMPDIR: 書込み不可 — 権限を確認してください'
    fi
fi
trap 'if [[ -n $probe ]]; then rm -f -- "$probe/true32"; rmdir -- "$probe"; fi' EXIT
for font in ipaexg.ttf ipaexm.ttf; do
    if [[ -f assets/fonts/$font ]]; then
        ok "フォント: $font あり"
    elif ((!fix_fonts)); then
        ng "フォント: $font 無し — --fix-fonts または make fonts を実行してください"
    elif python3 -B - "$common" "$font" <<'PY'
import ast, hashlib, shutil, sys
from pathlib import Path
try:
    tree = ast.parse(Path('tools/fetch_fonts.py').read_text())
    members = next(ast.literal_eval(node.value) for node in tree.body
                   if isinstance(node, ast.Assign)
                   and any(isinstance(t, ast.Name) and t.id == 'MEMBERS' for t in node.targets))
    name = sys.argv[2]
    digest = next(d for n, d in members.values() if n == name)
    source = Path(sys.argv[1]).parent / 'assets/fonts' / name
    if hashlib.sha256(source.read_bytes()).hexdigest() != digest:
        sys.exit(1)
    dest = Path('assets/fonts') / name
    dest.parent.mkdir(parents=True, exist_ok=True)
    shutil.copyfile(source, dest)
except (OSError, ValueError, SyntaxError, StopIteration):
    sys.exit(1)
PY
    then
        ok "フォント: $font SHA-256 照合済み、コピー完了"
    else
        ng "フォント: $font 無し (コピー元無し・hash 不一致・コピー不可) — --fix-fonts または make fonts を実行してください"
    fi
done
runners=${HOST32_RUNNERS-'native qemu'}
[[ -n ${runners//[[:space:]]/} ]] || ng 'runner: 空 — HOST32_RUNNERS=qemu を設定してください'
for runner in $runners; do
    case $runner in
        qemu)
            if command -v qemu-i386 >/dev/null 2>&1; then ok 'runner qemu: qemu-i386 あり';
            else ng 'runner qemu: qemu-i386 無し — qemu-user をインストールしてください'; fi ;;
        native)
            if ((tmp_ok)) && printf '%s\n' '.global _start' '_start: mov $1,%eax; xor %ebx,%ebx; int $0x80' |
                gcc -m32 -nostdlib -static -x assembler -o "$probe/true32" - >/dev/null 2>&1; then
                if python3 -B - "$probe/true32" <<'PYCODE'
import resource, subprocess, sys
resource.setrlimit(resource.RLIMIT_CORE, (0, 0))
try:
    result = subprocess.run([sys.argv[1]], stdout=subprocess.DEVNULL,
                            stderr=subprocess.DEVNULL, timeout=5)
    sys.exit(0 if result.returncode == 0 else 1)
except (OSError, subprocess.TimeoutExpired):
    sys.exit(1)
PYCODE
                then ok 'runner native: 32bit 実行可能';
                else ng 'runner native: 実行不可 (sandbox では SIGSYS) — HOST32_RUNNERS=qemu を設定してください'; fi
            else
                ng 'runner native: 試行不可 — TMPDIR と gcc -m32 を確認、sandbox では HOST32_RUNNERS=qemu を設定してください'
            fi ;;
        *) ng 'runner: 未知の指定 — native または qemu を設定してください' ;;
    esac
done
head_time=$(git show -s --format=%ct HEAD)
for artifact in build/out/vmkernel.lz4 build/out/kernel.map; do
    if [[ ! -f $artifact ]]; then ng "成果物: $artifact 無し — make all が要る";
    elif (( $(stat -c %Y -- "$artifact") < head_time )); then ng "成果物: $artifact が HEAD より古い — make all が要る";
    else ok "成果物: $artifact あり、HEAD より古くない"; fi
done
exit "$rc"
