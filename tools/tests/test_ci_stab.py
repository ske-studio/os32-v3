"""Regression checks for scheduling, controls, fixture reuse and cleanup."""
import os
import pathlib
import subprocess
import tempfile
import unittest
from unittest.mock import patch

import host32
import mutpar


def _process_timeout_fixture(item):
    import multiprocessing
    import sys
    sys.modules['ci_stab_retry_poison'] = object()
    assert multiprocessing.current_process().name != 'MainProcess'
    if mutpar._phase.get() != 'retry':
        raise subprocess.TimeoutExpired('fixture', 1)
    return item


class InfrastructureTests(unittest.TestCase):
    def test_make_clean_removes_control_receipts(self):
        import re
        root = pathlib.Path(__file__).resolve().parents[2]
        rule = re.search(r'^clean:.*\n(?:\t.*\n)+', (root / 'Makefile').read_text(), re.M)[0]
        with tempfile.TemporaryDirectory() as directory:
            tmp = pathlib.Path(directory)
            cache = tmp / 'build/out/host32-controls'
            cache.mkdir(parents=True)
            (cache / 'control.json').write_text('{}')
            (tmp / 'keep').touch()
            (tmp / 'Makefile').write_text(rule + '\n' + rule.splitlines()[0].split(':')[1] + ':\n')
            subprocess.run(['make', 'clean'], cwd=tmp, check=True, capture_output=True,
                           env={k: v for k, v in os.environ.items() if k not in ('MAKEFLAGS', 'MFLAGS')})
            self.assertFalse(cache.exists())
            self.assertTrue((tmp / 'keep').exists())

    def test_control_prunes_old_sessions_on_every_runner(self):
        import time
        with tempfile.TemporaryDirectory() as directory, \
             patch.object(host32.subprocess, 'check_output', return_value=b''):
            cache = pathlib.Path(directory) / 'build/out/host32-controls'
            for mutate in (True, False):
                for i in range(3):
                    if cache.exists():
                        for path in cache.iterdir():
                            old = time.time() - 7 * 3600
                            os.utime(path, (old, old))
                    with patch.dict(os.environ, OS32_CONTROL_SESSION=f'{mutate}-{i}'):
                        for runner in ('native', 'qemu'):
                            with host32.control(False, runner, directory):
                                pass
                        if mutate:
                            with host32.control(True, 'native', directory):
                                pass
                    self.assertEqual(len(list(cache.glob('*.json'))), 1 if mutate else 2)

    def test_control_preserves_old_current_and_unrelated_receipts(self):
        with tempfile.TemporaryDirectory() as directory, \
             patch.object(host32.subprocess, 'check_output', return_value=b''):
            with host32.control(False, 'native', directory):
                pass
            receipt = host32._receipt(directory, 'native')
            other = receipt.with_name('other-suite-native-old.json')
            other.write_text('{}')
            for path in (receipt, other):
                os.utime(path, (1, 1))
            with host32.control(True, 'native', directory) as normal:
                self.assertFalse(normal)
            self.assertTrue(other.exists())

    def test_build_compiles_outside_lock_and_keeps_pending(self):
        import fcntl
        with tempfile.TemporaryDirectory() as directory:
            tmp = pathlib.Path(directory)
            src, out = tmp / 'main.c', tmp / 'out'
            src.write_text('int main(void) { return 0; }\n'
                           + 'const char identity[] = "' + str(tmp) + '";\n')
            real_run = subprocess.run
            def run(args, **kwargs):
                if '-o' in args and '-E' not in args:
                    pending = pathlib.Path(args[args.index('-o') + 1])
                    with (pending.parent / '.lock').open('a') as lock:
                        fcntl.flock(lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
                        pending.touch()
                        host32.prune_fixtures(pending.parent)
                        self.assertTrue(pending.exists())
                return real_run(args, **kwargs)
            with patch.object(host32.subprocess, 'run', side_effect=run):
                host32.build(['gcc', str(src), '-o', str(out)], check=True)
            self.assertEqual(subprocess.run([str(out)]).returncode, 0)

    def test_build_relative_link_inputs_use_cwd(self):
        with tempfile.TemporaryDirectory() as directory:
            tmp = pathlib.Path(directory)
            (tmp / 'main.c').write_text('int value(void); int main(void) { return value(); }\n'
                                       + 'const char identity[] = "' + str(tmp) + '";\n')
            for kind in ('o', 'a'):
                for value in (3, 7):
                    (tmp / 'value.c').write_text(f'int value(void) {{ return {value}; }}\n')
                    subprocess.run(['gcc', '-c', 'value.c', '-o', 'value.o'], cwd=tmp, check=True)
                    if kind == 'a':
                        subprocess.run(['ar', 'rcs', 'value.a', 'value.o'], cwd=tmp, check=True)
                    host32.build(['gcc', 'main.c', f'value.{kind}', '-o', str(tmp / 'out')],
                                 cwd=tmp, check=True, capture_output=True)
                    self.assertEqual(subprocess.run([str(tmp / 'out')]).returncode, value)

    def test_process_pool_timeout_retries_isolated(self):
        with patch.dict(os.environ, OS32_MUT_JOBS='2'):
            self.assertEqual(list(mutpar.run_ordered(_process_timeout_fixture,
                                                    [1, 2], processes=True)), [1, 2])
        import sys
        self.assertNotIn('ci_stab_retry_poison', sys.modules)

    def test_timeout_retry_after_workers_finish(self):
        calls = []
        def one(item):
            calls.append(item)
            if calls.count(item) == 1:
                raise subprocess.TimeoutExpired('fixture', 1)
            return item
        with patch.dict(os.environ, OS32_MUT_JOBS='2'):
            self.assertEqual(list(mutpar.run_ordered(one, [1, 2])), [1, 2])
        self.assertEqual(sorted(calls[:2]), [1, 2])
        self.assertEqual(calls[2:], [1, 2])

    def test_reported_timeout_retry_once(self):
        calls = []
        def one(item):
            calls.append(item)
            return ('TIMEOUT', item, 90, 'deadline')
        with patch.dict(os.environ, OS32_MUT_JOBS='1'):
            self.assertEqual(list(mutpar.run_ordered(one, [1]))[0][0], 'TIMEOUT')
        self.assertEqual(calls, [1, 1])

    def test_second_timeout_is_error(self):
        calls = []
        def one(item):
            calls.append(item)
            raise subprocess.TimeoutExpired('fixture', 1)
        with self.assertRaises(subprocess.TimeoutExpired):
            list(mutpar.run_ordered(one, [1]))
        self.assertEqual(calls, [1, 1])

    def test_timeout_red_deferred_until_serial_retry(self):
        calls = []
        def one(item):
            calls.append(item)
            if calls.count(item) == 1:
                return mutpar.timeout_red(('RED', item))
            return ('SURVIVED', item)
        with patch.dict(os.environ, OS32_MUT_JOBS='2'):
            self.assertEqual(list(mutpar.run_ordered(one, [1, 2])),
                             [('SURVIVED', 1), ('SURVIVED', 2)])
        self.assertEqual(calls[2:], [1, 2])
        with patch.dict(os.environ, OS32_MUT_JOBS='1'):
            self.assertEqual(list(mutpar.run_ordered(
                lambda x: mutpar.timeout_red(('RED', x)), [1])), [('RED', 1)])

    def test_raw_subprocess_retry(self):
        ok = subprocess.CompletedProcess(['probe'], 0)
        with patch.object(mutpar.subprocess, 'run',
                          side_effect=[subprocess.TimeoutExpired('probe', 20), ok]) as run:
            self.assertIs(mutpar.run_timeout(['probe'], timeout=20), ok)
            self.assertEqual(run.call_count, 2)

    def test_explicit_host_timeout_preserved(self):
        with patch.object(host32, 'is_ilp32', return_value=True), \
             patch.object(host32, 'command', return_value=['probe']), \
             patch.object(host32.subprocess, 'run', return_value=subprocess.CompletedProcess([], 0)) as run:
            host32.run(['probe'], timeout=20)
            self.assertEqual(run.call_args.kwargs['timeout'], 20)

    def test_cache_bounds_and_single_lock(self):
        with tempfile.TemporaryDirectory() as directory:
            cache = pathlib.Path(directory)
            for i in range(8):
                path = cache / str(i)
                path.write_bytes(b'0123456789')
                os.utime(path, ns=(i + 1, i + 1))
            (cache / 'legacy.lock').touch()
            (cache / '.lock').touch()
            with patch.object(host32, 'CACHE_BYTES', 30), \
                 patch.object(host32, 'CACHE_ENTRIES', 2):
                host32.prune_fixtures(cache)
            self.assertEqual({p.name for p in cache.iterdir()}, {'6', '7', '.lock'})

    def test_compile_only_key_differs_from_link(self):
        with tempfile.TemporaryDirectory() as directory:
            tmp = pathlib.Path(directory)
            src, out = tmp / 'main.c', tmp / 'out'
            src.write_text('int main(void) { return 0; }\n')
            host32.build(['gcc', '-c', str(src), '-o', str(out)], check=True)
            host32.build(['gcc', str(src), '-o', str(out)], check=True)
            self.assertEqual(subprocess.run([str(out)]).returncode, 0)

    def test_control_sessions_do_not_consume_each_other(self):
        with tempfile.TemporaryDirectory() as directory, \
             patch.object(host32.subprocess, 'check_output', return_value=b''):
            for session in ('make-one', 'make-two'):
                with patch.dict(os.environ, OS32_CONTROL_SESSION=session):
                    with host32.control(False, 'qemu', directory):
                        pass
            for session in ('make-one', 'make-two'):
                with patch.dict(os.environ, OS32_CONTROL_SESSION=session):
                    with host32.control(True, 'qemu', directory) as normal:
                        self.assertFalse(normal)

    def test_control_untracked_header_and_path_compiler(self):
        import shutil
        with tempfile.TemporaryDirectory() as directory:
            root = pathlib.Path(directory)
            subprocess.run(['git', 'init', '-q', str(root)], check=True)
            (root / 'include').mkdir()
            header = root / 'include/new.h'
            header.write_text('before')
            with host32.control(False, 'qemu', root):
                pass
            header.write_text('after')
            with self.assertRaises(RuntimeError):
                with host32.control(True, 'qemu', root):
                    pass
            with host32.control(False, 'qemu', root):
                pass
            compiler = root / 'gcc'
            compiler.write_text('#!/bin/sh\necho test-compiler\n')
            compiler.chmod(0o755)
            with patch.object(shutil, 'which', return_value=str(compiler)), \
                 self.assertRaises(RuntimeError):
                with host32.control(True, 'qemu', root):
                    pass

    def test_guest_cleanup(self):
        import test_guest_tests as guest
        with tempfile.TemporaryDirectory() as tmp, patch.dict(os.environ, TMPDIR=tmp):
            old = tempfile.tempdir
            tempfile.tempdir = tmp
            try:
                guest.drive([])
                self.assertEqual(list(pathlib.Path(tmp).iterdir()), [])
            finally:
                tempfile.tempdir = old


    def test_wall_time_is_record_only(self):
        import contextlib
        import io
        import itertools
        from types import SimpleNamespace
        import test_appmem
        import test_appmem_map
        for module in (test_appmem, test_appmem_map):
            with self.subTest(suite=module.__name__):
                def run(args, **kw):
                    key = pathlib.Path(args[0]).stem
                    index = int(key[3:]) if key.startswith('mut') else None
                    output = 'PASS\n' if index is None else (
                        'FAIL: ' + module.MUTANTS[index][-1] + '\n')
                    return subprocess.CompletedProcess(args, int(index is not None), output, '')
                ticks = iter(itertools.chain.from_iterable((i * 32, i * 32 + 31)
                                                          for i in range(len(module.MUTANTS))))
                with patch.object(module.argparse.ArgumentParser, 'parse_args',
                                  return_value=SimpleNamespace(runner='qemu', mutate=True)), \
                     patch.object(module.host32, 'control',
                                  return_value=contextlib.nullcontext(False), create=True), \
                     patch.object(module.host32, 'begin_control', create=True), \
                     patch.object(module.host32, 'build', create=True), \
                     patch.object(module.subprocess, 'run'), \
                     patch.object(module.host32, 'run', side_effect=run), \
                     patch.object(module, 'run_ordered', side_effect=lambda fn, items: map(fn, items)), \
                     patch.object(module.time, 'monotonic', side_effect=lambda: next(ticks)), \
                     contextlib.redirect_stdout(io.StringIO()) as output:
                    module.main()
                self.assertIn('max=31.00s', output.getvalue())

    def test_lan_partial_start_cleanup(self):
        from unittest.mock import Mock
        import test_lan_bridge as lan
        for phase in ('agent', 'bridge'):
            agent = Mock()
            agent.poll.return_value = None
            with tempfile.TemporaryDirectory() as tmp:
                start = patch.object(lan.subprocess, 'Popen',
                    side_effect=[agent, RuntimeError('bridge startup')])
                wait = patch.object(lan.Rig, '_wait_for',
                    side_effect=RuntimeError('agent startup') if phase == 'agent' else None)
                with start, wait, self.assertRaises(RuntimeError):
                    lan.Rig(tmp)
                agent.terminate.assert_called_once()
                agent.wait.assert_called_once()

    def test_lan_delayed_address_and_dead_agent(self):
        from unittest.mock import Mock
        import test_lan_bridge as lan
        rig = lan.Rig.__new__(lan.Rig)
        rig.agent = Mock()
        rig.agent.poll.return_value = None
        with patch.object(lan.time, 'monotonic', side_effect=[0, 0, 11]), \
             patch.object(lan.time, 'sleep'), \
             patch.object(lan.os.path, 'exists', side_effect=[False, True]):
            rig._wait_for('socket')
        rig.agent.poll.return_value = 1
        with patch.object(lan.os.path, 'exists', return_value=True), \
             self.assertRaises(AssertionError):
            rig._wait_for('socket')

    def test_freshness_missing_stale_and_current(self):
        import sys
        sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1]))
        from check_artifacts import require_fresh
        with tempfile.TemporaryDirectory() as tmp:
            root = pathlib.Path(tmp)
            with self.assertRaisesRegex(RuntimeError, '先に make all'):
                require_fresh(root, ['image'])
            (root / 'image').write_text('output')
            (root / 'source').write_text('input')
            os.utime(root / 'image', ns=(1, 1))
            with self.assertRaisesRegex(RuntimeError, '先に make all'):
                require_fresh(root, ['image'], ['source'])
            os.utime(root / 'image', ns=(10**19, 10**19))
            require_fresh(root, ['image'], ['source'])

    def test_freshness_transitive_phony_and_side_effect(self):
        import sys
        sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1]))
        import check_artifacts as artifacts
        with tempfile.TemporaryDirectory() as tmp:
            root = pathlib.Path(tmp)
            for name in ('Makefile', 'map', 'elf', 'source'):
                (root / name).write_text(name)
            os.utime(root / 'map', ns=(10, 10))
            os.utime(root / 'elf', ns=(20, 20))
            os.utime(root / 'source', ns=(1, 1))
            with patch.object(artifacts, '_dependencies',
                              return_value={'elf': {'phony'}, 'phony': {'source'}}):
                artifacts.require_fresh(root, ['map'], targets={'map': 'elf'})
                os.utime(root / 'source', ns=(30, 30))
                with self.assertRaisesRegex(RuntimeError, '先に make all'):
                    artifacts.require_fresh(root, ['map'], targets={'map': 'elf'})

    def test_control_runner_inputs_and_consumption(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = pathlib.Path(tmp)
            (root / 'source').write_text('before')
            with patch.object(host32.subprocess, 'check_output', return_value=b'source\0'):
                with self.assertRaises(RuntimeError):
                    with host32.control(True, 'qemu', root):
                        pass

                with host32.control(False, 'qemu', root) as normal:
                    self.assertTrue(normal)
                with self.assertRaises(RuntimeError):
                    with host32.control(True, 'native', root):
                        pass
                with host32.control(True, 'qemu', root) as normal:
                    self.assertFalse(normal)
                with self.assertRaises(RuntimeError):
                    with host32.control(True, 'qemu', root):
                        pass
                with host32.control(False, 'qemu', root):
                    pass
                (root / 'source').write_text('after')
                with self.assertRaises(RuntimeError):
                    with host32.control(True, 'qemu', root):
                        pass


    def test_later_failure_invalidates_control(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = pathlib.Path(tmp)
            with patch.object(host32.subprocess, 'check_output', return_value=b''):
                @host32.control_session
                def normal():
                    host32.begin_control(False, 'qemu', root)
                    with host32.control(False, 'qemu', root):
                        pass
                    raise AssertionError('later source-hash check')
                with self.assertRaises(AssertionError):
                    normal()
                with self.assertRaises(RuntimeError):
                    with host32.control(True, 'qemu', root):
                        pass
    def test_fixture_compile_once_and_header_invalidation(self):
        with tempfile.TemporaryDirectory() as directory:
            tmp = pathlib.Path(directory)
            src, header, obj = tmp / 'fixture.c', tmp / 'fixture.h', tmp / 'fixture.o'
            src.write_text('#include "fixture.h"\nint value(void) { return VALUE; }\n'
                           + 'const char identity[] = "' + str(tmp) + '";\n')
            header.write_text('#define VALUE 1\n')
            cmd = ['gcc', '-c', str(src), '-o', str(obj)]
            real_run = subprocess.run
            with patch.object(host32.subprocess, 'run', wraps=real_run) as run:
                host32.build(cmd, check=True)
                obj.unlink()
                host32.build(cmd, check=True)
                compiles = [c for c in run.call_args_list if '-c' in c.args[0] and '-E' not in c.args[0]]
                self.assertEqual(len(compiles), 1)
                header.write_text('#define VALUE 2\n')
                host32.build(cmd, check=True)
                compiles = [c for c in run.call_args_list if '-c' in c.args[0] and '-E' not in c.args[0]]
                self.assertEqual(len(compiles), 2)


    def test_net_agent_wait_and_tmpdir(self):
        import test_net_link as net
        body = r'''
#include <unistd.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/wait.h>
static int connects, delayed, mode, kills, waits;
static pid_t fake_fork(void) { return 4242; }
static pid_t fake_waitpid(pid_t p, int *s, int f) { (void)s; if (!f) { waits++; return p; } return mode == 2 ? p : 0; }
static int fake_kill(pid_t p, int sig) { (void)p; (void)sig; kills++; return 0; }
static int fake_usleep(useconds_t u) { (void)u; return 0; }
static int fake_connect(int f, const struct sockaddr *s, socklen_t n)
{ (void)f; (void)s; (void)n; return mode == 1 ? -1 : ++connects >= (delayed ? 201 : 1) ? 0 : -1; }
#define kill fake_kill
#define fork fake_fork
#define waitpid fake_waitpid
#define usleep fake_usleep
#define connect fake_connect
#define main fixture_main
#include "HOST_SOURCE"
#undef main
int main(int argc, char **argv)
{
    int rc, wrong_path;
    const char *dir = getenv("TMPDIR");
    (void)argc;
    delayed = !strcmp(argv[1], "wait");
    mode = !strcmp(argv[1], "timeout") ? 1 : (!strcmp(argv[1], "dead") ? 2 : 0);
    rc = agent_start();
    if (!strcmp(argv[1], "long")) return rc == -1 ? 0 : 4;
    if (mode) return rc == -1 && agent_pid == -1 && !agent_state[0] &&
        (mode == 2 || (kills == 1 && waits == 1)) ? 0 : 3;
    wrong_path = strncmp(agent_sock, dir, strlen(dir));
    agent_pid = -1;
    agent_stop();
    return rc ? 1 : (!delayed && wrong_path ? 2 : 0);
}
'''.replace('HOST_SOURCE', str(net.HOST_SRC))
        with tempfile.TemporaryDirectory() as directory:
            tmp = pathlib.Path(directory)
            src, exe = tmp / 'probe.c', tmp / 'probe'
            src.write_text(body)
            compiled = subprocess.run(['gcc', *net.FLAGS, *net.argptr_defines(), *net.INC,
                            str(src), '-o', str(exe)], capture_output=True)
            self.assertEqual(compiled.returncode, 0, compiled.stderr.decode())
            for mode in ('wait', 'tmp', 'timeout', 'dead'):
                with self.subTest(mode=mode):
                    result = subprocess.run([str(exe), mode], env={**os.environ, 'TMPDIR': directory})
                    self.assertEqual(result.returncode, 0, mode)
                    self.assertFalse(list(tmp.glob("os32-n1-*")))
            result = subprocess.run([str(exe), 'long'], capture_output=True, text=True,
                                    env={**os.environ, 'TMPDIR': directory + '/' + 'x' * 108})
            self.assertEqual(result.returncode, 0)
            self.assertIn('TMPDIR path is too long', result.stderr)

    def test_net_abnormal_exit_and_outer_timeout_cleanup(self):
        import signal
        import sys
        import time
        import test_net_link as net
        with tempfile.TemporaryDirectory() as directory:
            tmp = pathlib.Path(directory)
            script = tmp / 'child.py'
            report = tmp / 'pids'
            script.write_text(
                'import os, pathlib, subprocess, sys, time\n'
                'p = subprocess.Popen([sys.executable, "-c", "import time; time.sleep(60)"])\n'
                'pathlib.Path(sys.argv[1]).write_text(str(p.pid) + " " + os.environ["TMPDIR"])\n'
                'if sys.argv[2] == "exit": os._exit(9)\n'
                'time.sleep(60)\n')
            for mode in ('exit', 'timeout', 'sigterm'):
                report.unlink(missing_ok=True)
                args = [sys.executable, str(script), str(report), mode]
                if mode == 'exit':
                    self.assertEqual(net.run_fixture(args), 9)
                elif mode == 'timeout':
                    with self.assertRaises(subprocess.TimeoutExpired):
                        net.run_fixture(args, timeout=2)
                else:
                    wrapper = subprocess.Popen([sys.executable, '-c',
                        'import sys; sys.path.insert(0, sys.argv[1]); '
                        'import test_net_link as net; net.run_fixture(sys.argv[2:])',
                        str(pathlib.Path(net.__file__).parent), *args])
                    try:
                        deadline = time.monotonic() + 10
                        while not report.exists() and time.monotonic() < deadline:
                            time.sleep(.01)
                        self.assertTrue(report.exists())
                        wrapper.terminate()
                        self.assertEqual(wrapper.wait(timeout=10), 128 + signal.SIGTERM)
                    finally:
                        if wrapper.poll() is None:
                            wrapper.kill()
                        wrapper.wait()
                pid, state = report.read_text().split()
                self.assertFalse(pathlib.Path(state).exists())
                # An orphan can briefly be a zombie until init reaps it, but
                # no executable descendant may remain after killpg.
                for _ in range(100):
                    status = pathlib.Path('/proc', pid, 'stat')
                    if not status.exists() or status.read_text().split()[2] == 'Z':
                        break
                    time.sleep(.01)
                else:
                    os.kill(int(pid), signal.SIGKILL)
                    self.fail('live descendant leaked')

    def test_real_artifacts_fail_before_work(self):
        import sys
        import test_vmkernel_lz4
        import test_vk32_crc
        import test_packages
        sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1]))
        import check_artifacts
        import check_manifests
        import gen_memmap
        for module in (test_vmkernel_lz4, test_vk32_crc):
            with patch.object(check_artifacts, 'require_fresh',
                              side_effect=RuntimeError('先に make all')), \
                 patch.object(module.subprocess, 'run',
                              side_effect=AssertionError('work before preflight')), \
                 self.assertRaisesRegex(RuntimeError, '先に make all'):
                module.main(['--real'])
        for module, work in ((test_packages, 'case_real_plan'),
                             (check_manifests, 'check_missing_hosts'),
                             (gen_memmap, 'load')):
            with patch.object(check_artifacts, 'require_fresh',
                              side_effect=RuntimeError('先に make all')), \
                 patch.object(module, work,
                              side_effect=AssertionError('work before preflight')), \
                 patch.object(sys, 'argv', [str(module.__file__)]), \
                 self.assertRaisesRegex(RuntimeError, '先に make all'):
                module.main()
        import runpy
        with patch.object(check_artifacts, 'require_fresh',
                          side_effect=RuntimeError('先に make all')), \
             patch.object(subprocess, 'run',
                          side_effect=AssertionError('work before preflight')), \
             patch.object(sys, 'argv', ['test_fdc_track.py', '--require-image']), \
             self.assertRaisesRegex(RuntimeError, '先に make all'):
            runpy.run_path(str(pathlib.Path(__file__).with_name('test_fdc_track.py')),
                           run_name='__main__')


if __name__ == '__main__':
    unittest.main()
