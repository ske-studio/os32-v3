import subprocess
import sys

CHECK = dict(name="restructure-p0-8", tier=1, needs=["make:all"], budget=2,
             covers=["tools/restructure/external_paths.py", "tools/restructure/test_external_paths.py",
                     ".mcp.json", "tools/**/*.py", ".claude/skills/run-os32/driver.py",
                     ".github/workflows/build.yml", "build/core.yaml", "userland/deploy.yaml",
                     "docs/08_build.md", "docs/decisions/realhw/RA266.md",
                     "docs/tasks/restructure/NOW.md", "docs/tasks/restructure/RULES.md",
                     "docs/tasks/restructure/TASK_RESTRUCTURE.md",
                     ".claude/skills/os32-emu-remote/SKILL.md", ".claude/skills/os32-realhw-update/SKILL.md",
                     "tools/ci_fetch.sh", "tools/deploy_manifests.py", "tools/deploy_protect.py",
                     "tools/deploy_source_check.py", "tools/gen_deploy_set.py", "tools/host_agent_config.py",
                     "tools/host_agent_service.py", "tools/systemd/README.md", "tools/systemd/os32-host-agent@.service"],
             basis=["docs/tasks/restructure/P0.md P0-8"])


def run(ctx):
    argv = [sys.executable, "-B", "tools/restructure/test_external_paths.py"]
    if ctx.mutate:
        argv.append("--mutate")
    return subprocess.call(argv, cwd=ctx.root)
