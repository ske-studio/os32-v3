#!/usr/bin/env python3
import os
from pathlib import Path
import subprocess
import tempfile
import time
import unittest

SCRIPT = Path(__file__).resolve().with_name("queue_run.sh")


class QueueTest(unittest.TestCase):
    def test_dispatch(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            run = root / "run"
            run.mkdir()
            bin_dir = root / "bin"
            bin_dir.mkdir()
            (run / "PACK.md").touch()
            queue = run / "QUEUE.tsv"
            rows = [
                ("done_now", "build", "", "running"),
                ("failed_now", "read", "", "running"),
                ("local", "read", "", "running"),
                ("waiting", "build", "failed_now", "queued"),
                ("missing_pack", "build", "", "queued"),
                ("first", "build", "done_now", "queued"),
                ("over_build", "build", "", "queued"),
                ("over_read", "read", "", "queued"),
            ]
            original = "# 注記\n壊れた行\n" + "".join(
                f"{name}\t{kind}\tmodel\t.\tPACK.md\t{deps}\t{state}\t備考\n"
                for name, kind, deps, state in rows)
            original = original.replace("missing_pack\tbuild\tmodel\t.\tPACK.md",
                                        "missing_pack\tbuild\tmodel\t.\tmissing.md")
            queue.write_text(original)
            (run / "QUEUE.conf").write_text("build=2\nread=8\n")
            for stage, log in {"done_now": "output\nrc=0\n",
                               "failed_now": "rc=0\noutput\nrc=17",
                               "local": "rc=0\nstill running\n"}.items():
                (run / stage).mkdir()
                (run / stage / "impl.log").write_text(log)
            self.executable(bin_dir / "ps", f"""#!/bin/sh
cat <<'EOF'
100 1 /usr/bin/codex exec --add-dir {run}/local -s workspace-write
101 1 /usr/bin/codex exec -s read-only review
102 1 node /somewhere/codex.js exec -s workspace-write implement
103 102 /somewhere/codex exec -s workspace-write implement
104 1 timeout 7000 codex exec -s workspace-write wrapper
EOF
""")
            self.executable(bin_dir / "codex_run.sh", '#!/bin/sh\necho "$1" >> "$LAUNCHED"\n')
            launched = root / "launched"
            env = dict(os.environ, PATH=f"{bin_dir}:{os.environ['PATH']}",
                       QUEUE_RUN_DIR=str(run), QUEUE_READ_LIMIT="2", LAUNCHED=str(launched))
            env.pop("QUEUE_BUILD_LIMIT", None)

            def dispatch(*args):
                return subprocess.run([str(SCRIPT), *args], env=env, check=True,
                                      capture_output=True, text=True)

            dry = dispatch("--dry-run")
            self.assertEqual(queue.read_text(), original)
            self.assertFalse(launched.exists())
            self.assertIn("形式が不正", dry.stderr)
            self.assertIn("missing_pack: 依頼ファイル", dry.stderr)
            self.assertIn("[dry-run] first: queued -> running", dry.stdout)
            self.assertIn("build 1/2, read 2/2", dry.stdout)
            applied = dispatch()
            self.assertEqual(launched.read_text(), "first\n")
            self.assertEqual(applied.stdout, dry.stdout.replace("[dry-run] ", ""))
            changed = queue.read_text()
            self.assertTrue(changed.startswith("# 注記\n壊れた行\n"))
            states = {row.split("\t")[0]: row.split("\t")[6]
                      for row in changed.splitlines()[2:]}
            self.assertEqual(states, dict(done_now="done", failed_now="failed", local="running",
                                          waiting="queued", missing_pack="queued", first="running",
                                          over_build="queued",
                                          over_read="queued"))
            dispatch()
            self.assertEqual(launched.read_text(), "first\n")

    def test_read_launcher(self):
        with tempfile.TemporaryDirectory() as tmp:
            run = Path(tmp)
            (run / "PACK with space.md").touch()
            (run / "review").mkdir()
            (run / "review/impl.log").write_text("rc=0\n")
            (run / "watch.seen").write_text(
                "review/impl.log\nstall:review/impl.log:12\nalert:review/impl.log:13\nother/impl.log\n")
            (run / "QUEUE.tsv").write_text(
                "review\tread\tmodel\t.\tPACK with space.md\t\tqueued\t\n")
            self.executable(run / "ps", "#!/bin/sh\n")
            self.executable(run / "codex", """#!/bin/sh
case " $* " in
    *" -s read-only "*) echo "$@"; exit 7 ;;
    *) exit 1 ;;
esac
""")
            env = dict(os.environ, PATH=f"{run}:{os.environ['PATH']}", QUEUE_RUN_DIR=tmp,
                       QUEUE_BUILD_LIMIT="0", QUEUE_READ_LIMIT="1")
            subprocess.run([str(SCRIPT)], env=env, check=True, capture_output=True)
            log = run / "review/impl.log"
            deadline = time.monotonic() + 5
            while not log.read_text().endswith("rc=7\n") and time.monotonic() < deadline:
                time.sleep(0.01)
            self.assertTrue(log.read_text().endswith("rc=7\n"))
            self.assertIn("PACK with space.md", log.read_text())
            self.assertEqual(len(list((run / "review").glob("impl.*.log"))), 1)
            self.assertEqual((run / "watch.seen").read_text(), "other/impl.log\n")
            subprocess.run([str(SCRIPT)], env=env, check=True, capture_output=True)
            self.assertIn("\tfailed\t", (run / "QUEUE.tsv").read_text())

    @staticmethod
    def executable(path, source):
        path.write_text(source)
        path.chmod(0o755)


if __name__ == "__main__":
    unittest.main()
