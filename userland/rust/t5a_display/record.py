"""Run from repository root; append actual command/output/exit to TDD_LOG."""
import pathlib
import subprocess
import sys

root = pathlib.Path(__file__).resolve().parent
command = sys.argv[2:]
result = subprocess.run(command, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
with (root / "TDD_LOG.md").open("a") as log:
    log.write("\n## " + sys.argv[1] + "\n\n```text\n$ " + " ".join(command) + "\n")
    log.write(result.stdout)
    log.write("\nexit=" + str(result.returncode) + "\n```\n")
print(result.stdout)
print("exit=" + str(result.returncode))
sys.exit(result.returncode)
