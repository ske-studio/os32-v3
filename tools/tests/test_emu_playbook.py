"""Host-only tests: no real helper imports, LLM, config, or emulator access."""
import ast
import contextlib
import copy
import io
import json
from pathlib import Path
import struct
import re
import os
import tempfile
import types
import unittest
from unittest.mock import Mock, patch

from tools.emu_agent import playbook as p

DONE = {"action": "done", "report": "OK"}
KEY = {"action": "key", "text": "abcd12345"}
CLICK = {"action": "click", "x": 30, "y": 468}
DRAG = {"action": "drag", "x0": 0, "y0": 0, "x1": 639, "y1": 479}


def plan(*steps, height=480):
    return {"width": 640, "height": height, "steps": copy.deepcopy(list(steps)) + [copy.deepcopy(DONE)]}


class FakeDriver:
    def __init__(self):
        self.calls = []
        self.geometry = (640, 480)
        self.failure = None

    def preflight(self):
        self.calls.append("preflight")
        return self.geometry

    def key(self, **kw):
        return self.hit("key", kw)

    def click(self, x, y):
        return self.hit("click", [x, y])

    def drag(self, x0, y0, x1, y1):
        return self.hit("drag", [x0, y0, x1, y1])

    def status(self):
        return self.hit("status", {})

    def tvram(self):
        return self.hit("tvram", {})

    def screenshot(self, step):
        return self.hit("screenshot", step)

    def wait(self, seconds):
        return self.hit("wait", seconds)

    def hit(self, name, args):
        self.calls.append((name, args))
        if isinstance(self.failure, Exception):
            raise self.failure
        return self.failure if self.failure is not None else "ok"


class LoopTests(unittest.TestCase):
    def session(self, approved, proposals):
        driver, records = FakeDriver(), []
        replies = iter(proposals)
        def chat(model, messages):
            value = next(replies)
            if isinstance(value, Exception):
                raise value
            if not isinstance(value, str):
                value = json.dumps(value)
            return {"content": value}, {}, 0
        llm = Mock(side_effect=chat)
        runner = p.Runner(approved, llm, driver, lambda **row: records.append(row))
        return runner, driver, records, llm

    def test_bad_proposals_zero_driver_calls_and_latched(self):
        bad = [
            {"action": "cmd_nowait", "cmd": "v86 -b /host/dos5hd.nhd"},
            {"action": "key", "seq": "CTRL+STOP"},
            {"action": "reset"}, {"action": "deploy"}, {"action": "make", "target": "all"},
            {"action": "click", "x": "30", "y": 468},
            {"action": "click", "x": 30.0, "y": 468},
            {"action": "click", "x": True, "y": 468},
            {"action": "click", "x": 30, "y": 469},
            {"action": "click", "x": 30}, dict(CLICK, btn=1), KEY, DONE,
            '```json\n' + json.dumps(CLICK) + '\n```',
            json.dumps(CLICK) + ' trailing', json.dumps(CLICK) + json.dumps(DONE),
            '{"action":"click","x":0,"x":30,"y":468}',
            '{"action":"click","x":NaN,"y":468}', '[]', 'null', '{',
        ]
        for proposal in bad:
            with self.subTest(proposal=proposal):
                runner, driver, rows, llm = self.session(plan(CLICK), [proposal, CLICK, DONE])
                self.assertFalse(runner.run("fake")["ok"])
                self.assertTrue(runner.stopped)
                self.assertEqual(driver.calls, [])
                self.assertFalse(runner.run("fake")["ok"])
                self.assertEqual(driver.calls, [])
                self.assertEqual(llm.call_count, 1)
                self.assertEqual(rows[-1]["event"], "rejection")
                self.assertEqual(rows[-1]["expected"], CLICK)
                self.assertIn("proposal", rows[-1])

    def test_failure_after_prefix_no_later_execution(self):
        for bad in (KEY, DONE, '{', {"action": "key", "seq": "CTRL+STOP"}):
            with self.subTest(bad=bad):
                runner, driver, _, llm = self.session(plan(KEY, CLICK), [KEY, bad, CLICK, DONE])
                self.assertFalse(runner.run("fake")["ok"])
                before = copy.deepcopy(driver.calls)
                self.assertEqual(runner.completed, 1)
                self.assertFalse(runner.run("fake")["ok"])
                self.assertEqual(driver.calls, before)
                self.assertEqual(llm.call_count, 2)

    def test_driver_failure_and_exception_stop_without_replay(self):
        for failure in ('error: rejected', '{"ok":false}', RuntimeError("broken"), False, None):
            runner, driver, _, llm = self.session(plan(KEY, CLICK), [KEY, CLICK, DONE])
            driver.failure = failure
            if failure is None:
                driver.key = Mock(return_value=None)
            self.assertFalse(runner.run("fake")["ok"])
            before = copy.deepcopy(driver.calls)
            self.assertFalse(runner.run("fake")["ok"])
            self.assertEqual(driver.calls, before)
            self.assertEqual(llm.call_count, 1)

    def test_geometry_mismatch_or_invalid_before_gui(self):
        for geometry in ((640, 400), (800, 480), (640, True), (640, 480.0), None):
            runner, driver, _, _ = self.session(plan(KEY), [KEY, DONE])
            driver.geometry = geometry
            self.assertFalse(runner.run("fake")["ok"])
            self.assertEqual(driver.calls, ["preflight"])

    def test_full_order_done_and_transcript(self):
        steps = [KEY, CLICK, DRAG, {"action": "status"}, {"action": "tvram"},
                 {"action": "screenshot"}, {"action": "wait", "seconds": 1}, DONE]
        runner, driver, rows, llm = self.session(plan(*steps[:-1]), steps)
        self.assertTrue(runner.run("fake")["ok"])
        self.assertEqual(runner.completed, len(steps))
        self.assertEqual([c[0] for c in driver.calls if isinstance(c, tuple)],
                         ['key', 'click', 'drag', 'status', 'tvram', 'screenshot', 'wait'])
        self.assertEqual(driver.calls.count("preflight"), 3)
        self.assertEqual([r['expected'] for r in rows if r['event'] == 'result'], steps)
        self.assertTrue(all('proposal' in r and 'result' in r for r in rows if r['event'] == 'result'))
        self.assertFalse(runner.run("fake")["ok"])
        self.assertEqual(llm.call_count, len(steps))

    def test_done_report_must_match(self):
        runner, driver, _, _ = self.session(plan(), [{"action": "done", "report": "success"}])
        self.assertFalse(runner.run("fake")["ok"])
        self.assertEqual(driver.calls, [])

    def test_llm_exception_and_tool_calls_abort(self):
        for msg in ({"content": json.dumps(KEY), "tool_calls": [{}]}, {"content": None}, []):
            runner, driver, _, llm = self.session(plan(KEY), [])
            llm.side_effect = None
            llm.return_value = (msg, {}, 0)
            self.assertFalse(runner.run("fake")["ok"])
            self.assertEqual(driver.calls, [])
        runner, driver, _, llm = self.session(plan(KEY), [RuntimeError('offline'), KEY])
        self.assertFalse(runner.run("fake")["ok"])
        self.assertEqual(driver.calls, [])
        self.assertEqual(llm.call_count, 1)

    def test_plan_and_messages_mutation_cannot_change_authorization(self):
        approved = plan(KEY, CLICK)
        runner, driver, _, llm = self.session(approved, [])
        approved['steps'][0]['text'] = 'CTRL+STOP'
        def chat(model, messages):
            messages.clear()
            return {'content': json.dumps({'action': 'key', 'text': 'CTRL+STOP'})}, {}, 0
        llm.side_effect = chat
        self.assertFalse(runner.run('fake')['ok'])
        self.assertEqual(driver.calls, [])

    def test_prompt_is_limited_to_plan(self):
        runner, _, _, llm = self.session(plan(KEY), [KEY, DONE])
        seen = []
        original = llm.side_effect
        def chat(model, messages):
            seen.append(copy.deepcopy(messages))
            return original(model, messages)
        llm.side_effect = chat
        runner.run('fake')
        prompt = seen[0][0]['content']
        self.assertIn('abcd12345', prompt)
        for forbidden in ('cmd_nowait', 'CTRL+STOP', 'hotdeploy', 'DOS/V86', 'SYSTEM_PROMPT'):
            self.assertNotIn(forbidden, prompt)


class ValidationTests(unittest.TestCase):
    def test_recursive_type_sensitive_equality(self):
        for a, b in [({'a': [1]}, {'a': [True]}), ([1], [1.0]), (1, '1'),
                     ({'a': 1}, {'a': 1, 'b': 2}), ([1, 2], [2, 1])]:
            self.assertFalse(p.exact_equal(a, b))
        self.assertTrue(p.exact_equal({'b': [None, True], 'a': 1}, {'a': 1, 'b': [None, True]}))

    def test_strict_json(self):
        for raw in ('{} {}', '{}junk', '```{} ```', '[]', 'null', '{"x":1,"x":2}',
                    '{"x":{"a":1,"a":2}}', '{"x":NaN}', '{"x":Infinity}', '{"x":1e999}', '{}\x00'):
            with self.subTest(raw=raw), self.assertRaises(p.Rejected):
                p.strict_object(raw)
        self.assertEqual(p.strict_object(' \n{"x":1} \t'), {'x': 1})

    def test_entire_plan_invalid_tail_rejected_before_calls(self):
        bad_steps = [{'action': 'cmd', 'cmd': 'ver'}, {'action': 'deploy'},
                     dict(CLICK, x=-1), dict(CLICK, x=640), dict(CLICK, y=480),
                     dict(CLICK, x=True), dict(CLICK, x='30'), dict(CLICK, x=30.0),
                     dict(DRAG, y1=480), dict(DRAG, mid=[]),
                     {'action': 'key', 'seq': 'ENTER', 'text': 'x'}, {'action': 'key'},
                     {'action': 'key', 'text': ''}, {'action': 'key', 'seq': 1},
                     {'action': 'wait', 'seconds': True}, {'action': 'wait', 'seconds': 0},
                     {'action': 'wait', 'seconds': 61}, {'action': 'wait', 'seconds': '1'},
                     {'action': 'status', 'extra': 1}, {'action': 'done'}, {'action': []}]
        bad_plans = [plan(KEY, bad) for bad in bad_steps]
        bad_plans += [plan(DONE, KEY), {'width': 640, 'height': 480, 'steps': [KEY]},
                      dict(plan(), width=800), dict(plan(), height=True), dict(plan(), height=1),
                      dict(plan(), extra=1), dict(plan(), steps=[])]
        for approved in bad_plans:
            with self.subTest(approved=approved), self.assertRaises(p.Rejected):
                p.Runner(approved, Mock(), FakeDriver(), Mock())

    def test_400_bounds_and_immutable_snapshot(self):
        approved = plan({'action': 'click', 'x': 639, 'y': 399}, height=400)
        frozen = p.validate_plan(approved)
        approved['steps'][0]['y'] = 500
        with self.assertRaises(TypeError):
            frozen['steps'][0]['y'] = 0
        self.assertEqual(frozen['steps'][0]['y'], 399)
        with self.assertRaises(p.Rejected):
            p.validate_plan(plan(CLICK, height=400))


class CLITests(unittest.TestCase):
    def test_dry_run_default_no_live_factory(self):
        with tempfile.TemporaryDirectory() as d:
            path = Path(d) / 'plan.json'
            path.write_text(json.dumps(plan(KEY)))
            factory = Mock(side_effect=AssertionError('live access'))
            out = io.StringIO()
            with contextlib.redirect_stdout(out):
                self.assertEqual(p.main([str(path)], live_factory=factory), 0)
            factory.assert_not_called()
            self.assertIn('dry_run', out.getvalue())
            self.assertIn('abcd12345', out.getvalue())

    def test_execute_production_loop_and_invalid_file_no_factory(self):
        with tempfile.TemporaryDirectory() as d:
            path, log = Path(d) / 'plan.json', Path(d) / 'log.jsonl'
            path.write_text(json.dumps(plan(KEY)))
            replies = iter([KEY, DONE])
            driver = FakeDriver()
            factory = Mock(return_value=(lambda m, msgs: ({'content': json.dumps(next(replies))}, {}, 0), driver, lambda s: s))
            with contextlib.redirect_stdout(io.StringIO()):
                self.assertEqual(p.main([str(path), '--execute', '--transcript', str(log)], live_factory=factory), 0)
            self.assertIn(('key', {'text': 'abcd12345'}), driver.calls)
            rows = [json.loads(line) for line in log.read_text().splitlines()]
            self.assertEqual(rows[-1]['expected'], DONE)
            factory.reset_mock()
            path.write_text(json.dumps(plan(KEY, {'action': 'cmd_nowait', 'cmd': 'v86'})))
            with contextlib.redirect_stdout(io.StringIO()):
                self.assertEqual(p.main([str(path), '--execute'], live_factory=factory), 1)
            factory.assert_not_called()


class LiveAdapterTests(unittest.TestCase):
    def setup_adapter(self):
        # gui_gate has no import-time I/O. Its real paced key / Mouse methods
        # run against these fake HTTP and sleep boundaries only.
        from tools import gui_gate
        gui = types.SimpleNamespace(**vars(gui_gate))
        self.posts, self.sleeps = [], []
        self.response = b'{"ok":true}'
        deepcopy = copy.deepcopy
        def post(path, data, timeout=20):
            self.posts.append((path, deepcopy(data)))
            if isinstance(self.response, Exception):
                raise self.response
            return self.response
        transport = patch.object(p, '_single_post',
                                 side_effect=lambda base, path, data, timeout=20:
                                 (200, post(path, data, timeout)))
        transport.start()
        self.addCleanup(transport.stop)
        gui.time = types.SimpleNamespace(sleep=lambda n: self.sleeps.append(n))
        agent = types.SimpleNamespace(
            emu=types.SimpleNamespace(BASE='http://fake.invalid'),
            CTX={}, obs_failed=lambda s: '"ok":false' in s or s.startswith('error:'),
            act_status=Mock(return_value=json.dumps(SAFE_STATE)),
            act_tvram=Mock(return_value='desktop'),
            act_screenshot=Mock(return_value='screenshot saved: fake.bmp'),
            act_wait=Mock(return_value='waited 1s'))
        return p.LiveDriver(agent, gui, 'test-session'), agent, gui

    def test_real_helpers_pacing_and_mouse_no_extra_release(self):
        driver, agent, gui = self.setup_adapter()
        before = gui.post
        self.assertEqual(driver.preflight(), (640, 480))
        driver.key(text='abcdefghij')
        self.assertEqual(self.posts, [('/api/key', {'text': 'abcd'}),
                                      ('/api/key', {'text': 'efgh'}),
                                      ('/api/key', {'text': 'ij'})])
        self.assertEqual(self.sleeps, [0.35] * 3)
        self.posts.clear()
        driver.key(seq='SHIFT+SPACE')
        self.assertEqual(self.posts, [('/api/key', {'seq': 'SHIFT+SPACE'})])
        self.posts.clear()
        driver.click(639, 479)
        self.assertEqual(self.posts, [('/api/mouse', {'ax': 65535, 'ay': 65535}),
                                     ('/api/mouse', {'btn': 1}), ('/api/mouse', {'btn': 0})])
        self.posts.clear()
        driver.drag(0, 0, 639, 479)
        self.assertEqual(len(self.posts), 4)
        self.assertEqual(self.posts[-1], ('/api/mouse', {'btn': 0}))
        self.assertIs(gui.post, before)

    def test_ack_failure_stops_inside_paced_key_or_gesture(self):
        for response in (b'{"ok":false}', b'error: bad key', b'{}', b'{"ok":1}',
                         b'{"ok":true,"ok":false}', b'{"ok":true,"error":"failed"}', b'{"ok":true} junk', RuntimeError('transport')):
            for kind in ('key', 'click', 'drag'):
                with self.subTest(response=response, kind=kind):
                    driver, _, _ = self.setup_adapter()
                    driver.preflight()
                    self.response = response
                    with self.assertRaises((p.Rejected, RuntimeError)):
                        if kind == 'key':
                            driver.key(text='abcdefghijk')
                        elif kind == 'click':
                            driver.click(0, 0)
                        else:
                            driver.drag(0, 0, 639, 479)
                    self.assertEqual(len(self.posts), 1)
                    self.assertEqual(self.sleeps, [])

    def test_live_geometry_and_observations_use_existing_helpers(self):
        driver, agent, _ = self.setup_adapter()
        agent.act_status.return_value = json.dumps(dict(SAFE_STATE, scrn_ymax=400))
        self.assertEqual(driver.preflight(), (640, 400))
        driver.click(639, 399)
        self.assertEqual(self.posts[0][1], {'ax': 65535, 'ay': 65535})
        self.assertEqual(driver.status(), agent.act_status.return_value)
        self.assertEqual(driver.tvram(), 'desktop')
        self.assertEqual(driver.screenshot(3), 'screenshot saved: fake.bmp')
        self.assertEqual(agent.CTX, {})
        self.assertEqual(driver.wait(1), 'waited 1s')
        agent.act_wait.assert_called_once_with({'seconds': 1})
        for text in ('{}', '{"scrn_xmax":640,"scrn_ymax":true}',
                     '{"scrn_xmax":640.0,"scrn_ymax":480}', '{"ok":false}'):
            agent.act_status.return_value = text
            with self.assertRaises(p.Rejected):
                driver.preflight()

    def test_production_loop_with_real_helper_partial_failure(self):
        driver, _, _ = self.setup_adapter()
        replies = iter([KEY, CLICK, DONE])
        calls = []
        def chat(model, messages):
            calls.append(model)
            return {'content': json.dumps(next(replies))}, {}, 0
        # Fail the second chunk after one successful four-character prefix.
        # The existing helper must never send the third chunk or cleanup keys.
        original = driver.key
        def key(**kw):
            original_sleep = driver._gui['time'].sleep
            def fail_next(n):
                original_sleep(n)
                self.response = b'{"ok":false}'
            driver._gui['time'] = types.SimpleNamespace(sleep=fail_next)
            return original(**kw)
        driver.key = key
        rows = []
        runner = p.Runner(plan(KEY, CLICK), chat, driver, lambda **r: rows.append(r))
        self.assertFalse(runner.run('fake')['ok'])
        self.assertEqual(len(self.posts), 2)
        self.assertEqual(calls, ['fake'])
        self.assertFalse(runner.run('fake')['ok'])
        self.assertEqual(len(self.posts), 2)
        self.assertEqual(rows[-1]['event'], 'rejection')


class RawObservationTests(unittest.TestCase):
    def real_helpers(self, directory, raw):
        # Compile actual helper definitions only: never execute import-time env reads.
        source = Path(p.__file__).with_name('agent.py').read_text()
        tree = ast.parse(source)
        names = {'act_status', 'act_tvram', 'act_screenshot', 'act_wait', 'clip', 'obs_failed'}
        nodes = [n for n in tree.body if isinstance(n, ast.FunctionDef) and n.name in names]
        nodes += [n for n in tree.body if isinstance(n, ast.Assign) and
                  any(isinstance(t, ast.Name) and t.id == '_OBS_FAIL' for t in n.targets)]
        def get_to_file(endpoint, path):
            Path(path).write_bytes(raw)
            return path
        emu = types.SimpleNamespace(BASE='http://fake.invalid', get=Mock(return_value=raw),
                                    get_to_file=Mock(side_effect=get_to_file))
        namespace = dict(json=json, os=os, re=re, time=types.SimpleNamespace(sleep=Mock()),
                         LOGS=directory, CTX={'session': 'unchanged', 'step': 0},
                         OBS_LIMIT=1600, WAIT_MAX=60, emu=emu)
        exec(compile(ast.Module(body=nodes, type_ignores=[]), '<actual agent helper definitions>', 'exec'), namespace)
        return types.SimpleNamespace(**namespace)

    @patch.object(p, '_single_get')
    def test_raw_observation_failure_cannot_be_hidden_by_helper(self, single_get):
        from tools import gui_gate
        for action, raw in (({'action': 'tvram'}, b'{"ok":false}'),
                            ({'action': 'tvram'}, b'{"lines":[],"error":"broken"}'),
                            ({'action': 'tvram'}, b'{}'),
                            ({'action': 'screenshot'}, b'{"ok":false}'),
                            ({'action': 'status'}, b'{"error":"broken"}')):
            with self.subTest(action=action, raw=raw), tempfile.TemporaryDirectory() as d:
                agent = self.real_helpers(d, raw)
                single_get.side_effect = lambda base, path: agent.emu.get(path)
                driver = p.LiveDriver(agent, gui_gate, 'test')
                replies = iter([action, DONE])
                llm = Mock(side_effect=lambda m, msgs: ({'content': json.dumps(next(replies))}, {}, 0))
                runner = p.Runner(plan(action), llm, driver, Mock(), agent.clip)
                self.assertFalse(runner.run('fake')['ok'])
                self.assertEqual(llm.call_count, 1)
                self.assertEqual(agent.CTX, {'session': 'unchanged', 'step': 0})

    @patch.object(p, '_single_get')
    def test_actual_observation_helpers_success_and_private_context(self, single_get):
        from tools import gui_gate
        with tempfile.TemporaryDirectory() as d:
            bmp = bytearray(54)
            bmp[:2] = b'BM'
            struct.pack_into('<I', bmp, 14, 40)
            struct.pack_into('<ii', bmp, 18, 640, 480)
            agent = self.real_helpers(d, bytes(bmp))
            single_get.side_effect = lambda base, path: agent.emu.get(path)
            driver = p.LiveDriver(agent, gui_gate, 'test')
            self.assertIn('step03.bmp', driver.screenshot(3))
            self.assertTrue((Path(d) / 'test' / 'shots' / 'step03.bmp').is_file())
            self.assertEqual(agent.CTX, {'session': 'unchanged', 'step': 0})
            agent.emu.get.return_value = b'{"lines":["desktop   ","ready"]}'
            self.assertEqual(driver.tvram(), 'desktop\nready')


class ExtraBoundaryTests(unittest.TestCase):
    def test_local_factory_rejects_nonlocal_llm_before_calls(self):
        fake = types.ModuleType('tools.emu_agent.agent')
        fake.FLM_BASE = 'https://remote.invalid'
        fake.llm_chat = Mock()
        with patch.dict('sys.modules', {'tools.emu_agent.agent': fake}):
            with self.assertRaises(p.Rejected):
                p._live_factory('test')
        fake.llm_chat.assert_not_called()

    def test_changed_geometry_after_valid_prefix_stops(self):
        case = LoopTests()
        runner, driver, _, llm = case.session(plan(KEY, CLICK), [KEY, CLICK, DONE])
        driver.preflight = Mock(side_effect=[(640, 480), (640, 400)])
        self.assertFalse(runner.run('fake')['ok'])
        self.assertEqual(driver.calls, [('key', {'text': 'abcd12345'})])
        self.assertEqual(llm.call_count, 2)

    def test_session_latch_and_progress_are_read_only(self):
        case = LoopTests()
        runner, driver, _, _ = case.session(plan(CLICK), [dict(CLICK, x=99)])
        # Public result state must not allow clearing the latch or advancing steps.
        with self.assertRaises(AttributeError):
            runner.stopped = False
        with self.assertRaises(AttributeError):
            runner.completed = 100
        self.assertFalse(runner.run('fake')['ok'])
        self.assertEqual(driver.calls, [])

    def test_cli_bad_json_and_existing_transcript_no_live_calls(self):
        with tempfile.TemporaryDirectory() as d:
            path, log = Path(d) / 'plan.json', Path(d) / 'existing.jsonl'
            factory = Mock(side_effect=AssertionError('live access'))
            for raw in ('{"width":640,"width":640,"height":480,"steps":[]}',
                        json.dumps(plan(KEY)) + '{}'):
                path.write_text(raw)
                with contextlib.redirect_stdout(io.StringIO()):
                    self.assertEqual(p.main([str(path), '--execute'], live_factory=factory), 1)
            path.write_text(json.dumps(plan(KEY)))
            log.write_text('keep')
            with contextlib.redirect_stdout(io.StringIO()):
                self.assertEqual(p.main([str(path), '--execute', '--transcript', str(log)], live_factory=factory), 1)
            self.assertEqual(log.read_text(), 'keep')
            factory.assert_not_called()


class FinalBoundaryTests(unittest.TestCase):
    def test_rejection_retains_failed_driver_result(self):
        case = LoopTests()
        runner, driver, rows, _ = case.session(plan(KEY), [KEY, DONE])
        driver.failure = 'error: input refused'
        self.assertFalse(runner.run('fake')['ok'])
        self.assertEqual(rows[-1]['result'], 'error: input refused')
        self.assertEqual(rows[-1]['expected'], KEY)
        self.assertEqual(json.loads(rows[-1]['proposal']), KEY)

    def test_startup_failure_returns_failure_and_transcript(self):
        with tempfile.TemporaryDirectory() as d:
            path, log = Path(d) / 'plan.json', Path(d) / 'log.jsonl'
            path.write_text(json.dumps(plan(KEY)))
            with contextlib.redirect_stdout(io.StringIO()):
                result = p.main([str(path), '--execute', '--transcript', str(log)],
                                live_factory=Mock(side_effect=RuntimeError('startup failed')))
            self.assertEqual(result, 1)
            rows = [json.loads(line) for line in log.read_text().splitlines()]
            self.assertEqual(rows[-1]['event'], 'rejection')
            self.assertEqual(rows[-1]['expected'], KEY)
            self.assertIsNone(rows[-1]['proposal'])

    def test_driver_exception_after_valid_prefix_blocks_replay(self):
        case = LoopTests()
        runner, driver, _, llm = case.session(plan(KEY, CLICK), [KEY, CLICK, DONE])
        driver.click = Mock(side_effect=RuntimeError('partial click'))
        self.assertFalse(runner.run('fake')['ok'])
        self.assertEqual(runner.completed, 1)
        self.assertEqual(llm.call_count, 2)
        self.assertFalse(runner.run('fake')['ok'])
        driver.click.assert_called_once_with(30, 468)

    def test_no_final_done_never_success_and_exception_preflight(self):
        case = LoopTests()
        runner, driver, _, llm = case.session(plan(KEY), [KEY])
        self.assertFalse(runner.run('fake')['ok'])
        self.assertEqual(runner.completed, 1)
        self.assertEqual(llm.call_count, 2)
        runner, driver, _, llm = case.session(plan(KEY), [KEY, DONE])
        driver.preflight = Mock(side_effect=OSError('unavailable'))
        self.assertFalse(runner.run('fake')['ok'])
        self.assertEqual(driver.calls, [])
        self.assertEqual(llm.call_count, 1)

    @patch.object(p, '_single_get')
    def test_live_factory_and_agent_actions_table_are_not_dispatcher(self, single_get):
        with tempfile.TemporaryDirectory() as d:
            helper = RawObservationTests().real_helpers(d, b'{"lines":["ready"]}')
            single_get.side_effect = lambda base, path: helper.emu.get(path)
            agent = types.ModuleType('tools.emu_agent.agent')
            agent.__dict__.update(vars(helper))
            agent.FLM_BASE = 'http://127.0.0.1:52625'
            agent.llm_chat = Mock(side_effect=[({'content': '{"action":"tvram"}'}, {}, 0),
                                               ({'content': json.dumps(DONE)}, {}, 0)])
            trap = Mock(side_effect=AssertionError('bypass'))
            agent.ACTIONS = {'tvram': trap, 'cmd_nowait': trap}
            agent.parse_action = trap
            agent.SYSTEM_PROMPT = 'cmd_nowait CTRL+STOP'
            with patch.dict('sys.modules', {'tools.emu_agent.agent': agent}):
                llm, driver, clip = p._live_factory('test')
                runner = p.Runner(plan({'action': 'tvram'}), llm, driver, Mock(), clip)
                self.assertTrue(runner.run('fake')['ok'])
            self.assertIs(llm, agent.llm_chat)
            trap.assert_not_called()


SAFE_STATE = dict(ok=True, running=1, user_pause=0, trap_pause=0,
                  fault_generation=0, scrn_xmax=640, scrn_ymax=480)


class ReviewTests(unittest.TestCase):
    def integration(self, directory, state=None, get_error=None, post_failure=None):
        from tools import gui_gate
        agent = RawObservationTests().real_helpers(directory, b'{}')
        self.requests = []
        self.posts = []
        state = SAFE_STATE if state is None else state
        def response(method, path, body=None):
            self.requests.append((method, path))
            if method == 'POST':
                self.posts.append((path, p.urllib.parse.parse_qs(body.decode())))
                if post_failure is not None and len(self.posts) == 3:
                    if isinstance(post_failure, Exception):
                        raise post_failure
                    return post_failure
                return b'{"ok":true}'
            if get_error:
                raise get_error
            if path == '/api/status':
                return json.dumps(state).encode()
            if path == '/api/tvram':
                return b'{"lines":["desktop   ","ready"]}'
            bmp = bytearray(54)
            bmp[:2] = b'BM'
            struct.pack_into('<Iii', bmp, 14, 40, 640, 480)
            return bytes(bmp)

        # Actual shared-client functions, excluding imports and env-reading BASE.
        # Old adapter therefore really probes and falls back, with fake I/O only.
        import urllib.request
        import urllib.error
        def urlopen(req, timeout):
            path = p.urllib.parse.urlsplit(req if isinstance(req, str) else req.full_url).path
            method = 'GET' if isinstance(req, str) else req.get_method()
            raw = response(method, path, None if isinstance(req, str) else req.data)
            return contextlib.nullcontext(types.SimpleNamespace(read=lambda: raw)) if not isinstance(req, str) else types.SimpleNamespace(read=lambda: raw)
        tree = ast.parse(Path('tools/np21w_mcp/np21w_client.py').read_text())
        nodes = [n for n in tree.body if isinstance(n, (ast.FunctionDef, ast.ClassDef))]
        fallback = Mock(side_effect=AssertionError('unexpected Windows curl fallback'))
        ns = dict(BASE='http://fake.invalid', CURL='fake-curl', _direct=None,
                  urllib=types.SimpleNamespace(request=types.SimpleNamespace(
                      Request=urllib.request.Request, urlopen=urlopen), error=urllib.error),
                  subprocess=types.SimpleNamespace(run=fallback))
        exec(compile(ast.Module(body=nodes, type_ignores=[]), '<actual client definitions>', 'exec'), ns)
        agent.emu = types.SimpleNamespace(**ns)

        class Connection:
            def __init__(self, *args, **kwargs):
                pass
            def request(self, method, path, body=None, headers=None):
                self.raw = response(method, path, body)
            def getresponse(self):
                return types.SimpleNamespace(status=200, read=lambda: self.raw)
            def close(self):
                pass
        # Patch stdlib boundaries before constructing the real adapter.
        patches = [patch('urllib.request.urlopen', side_effect=urlopen),
                   patch('http.client.HTTPConnection', Connection)]
        for patched in patches:
            patched.start()
            self.addCleanup(patched.stop)
        gui = types.SimpleNamespace(**vars(gui_gate))
        gui.time = types.SimpleNamespace(sleep=Mock())
        driver = p.LiveDriver(agent, gui, 'review')
        return driver, fallback

    def run_steps(self, driver, *steps):
        replies = iter([*steps, DONE])
        llm = Mock(side_effect=lambda m, msgs: ({'content': json.dumps(next(replies))}, {}, 0))
        rows = []
        runner = p.Runner(plan(*steps), llm, driver, lambda **r: rows.append(r))
        result = runner.run('fake')
        return runner, result, rows, llm

    def test_safe_state_contract_before_all_input(self):
        for field, safe in [('running', 1), ('user_pause', 0), ('trap_pause', 0), ('fault_generation', 0)]:
            for bad in ('missing', None, True, False, float(safe), str(safe), 1-safe, -1):
                for action in (KEY, CLICK, DRAG):
                    with self.subTest(field=field, bad=bad, action=action), tempfile.TemporaryDirectory() as d:
                        state = dict(SAFE_STATE)
                        if bad == 'missing':
                            del state[field]
                        else:
                            state[field] = bad
                        driver, _ = self.integration(d, state)
                        runner, result, rows, llm = self.run_steps(driver, action)
                        self.assertFalse(result['ok'])
                        self.assertEqual(self.posts, [])
                        self.assertEqual(runner.completed, 0)
                        self.assertEqual(llm.call_count, 1)

    def test_safe_state_success_counts_steps_separately_from_posts(self):
        with tempfile.TemporaryDirectory() as d:
            driver, fallback = self.integration(d)
            runner, result, rows, _ = self.run_steps(driver, KEY)
            self.assertTrue(result['ok'])
            self.assertEqual(runner.completed, 2)  # key step + final done
            self.assertEqual(self.requests, [('GET', '/api/status')] + [('POST', '/api/key')] * 3)
            detail = next(r['side_effects'] for r in rows if r['event'] == 'result')
            self.assertEqual(detail['attempted_side_effects'], 3)
            self.assertEqual(len(detail['completed_suboperations']), 3)
            self.assertIsNone(detail['attempted_operation'])
            self.assertFalse(detail['partial_execution'])
            fallback.assert_not_called()

    def test_status_ack_and_geometry_required_before_input(self):
        cases = [dict(SAFE_STATE, ok=v) for v in (None, False, 1, 'true')]
        cases += [dict(SAFE_STATE, **{field: v})
                  for field in ('scrn_xmax', 'scrn_ymax')
                  for v in (None, True, 640.0, '640', 0, 800)]
        for state in cases:
            with self.subTest(state=state), tempfile.TemporaryDirectory() as d:
                driver, _ = self.integration(d, state)
                _, result, _, _ = self.run_steps(driver, CLICK)
                self.assertFalse(result['ok'])
                self.assertEqual(self.posts, [])

    def test_diagnostic_status_allows_unsafe_state(self):
        with tempfile.TemporaryDirectory() as d:
            driver, _ = self.integration(d, dict(SAFE_STATE, running=0, trap_pause=1, fault_generation=2))
            _, result, _, _ = self.run_steps(driver, {'action': 'status'}, {'action': 'tvram'}, {'action': 'screenshot'})
            self.assertTrue(result['ok'])
            self.assertEqual(self.posts, [])
            self.assertEqual(self.requests, [('GET', '/api/status'), ('GET', '/api/tvram'),
                                             ('GET', '/api/screenshot')])
            self.assertEqual(driver.tvram(), 'desktop\nready')
            self.assertEqual((Path(d)/'review/shots/step03.bmp').read_bytes()[:2], b'BM')

    def test_first_transport_failure_is_single_attempt(self):
        for action in (KEY, CLICK, DRAG, {'action': 'status'}, {'action': 'tvram'}, {'action': 'screenshot'}):
            with self.subTest(action=action), tempfile.TemporaryDirectory() as d:
                driver, fallback = self.integration(d, get_error=OSError('fake transport failure'))
                runner, result, rows, llm = self.run_steps(driver, action, KEY)
                self.assertFalse(result['ok'])
                fallback.assert_not_called()
                self.assertEqual(len(self.requests), 1)
                self.assertEqual(self.posts, [])
                self.assertEqual(llm.call_count, 1)
                self.assertFalse(runner.run('fake')['ok'])

    def test_drag_failure_records_completed_prefix_and_uncertainty(self):
        for failure in (b'{"ok":false,"error":"token=FAKE_SECRET\\nrefused"}',
                        b'not JSON FAKE_SECRET', OSError('FAKE_SECRET transport')):
            with self.subTest(failure=type(failure).__name__), tempfile.TemporaryDirectory() as d:
                driver, _ = self.integration(d, post_failure=failure)
                runner, result, rows, llm = self.run_steps(driver, {'action': 'status'}, DRAG, KEY)
                self.assertFalse(result['ok'])
                self.assertEqual(runner.completed, 1)
                self.assertEqual(len(self.posts), 3)
                self.assertEqual(self.posts[1][1], {'btn': ['1']})
                detail = rows[-1]['result']
                self.assertIsInstance(detail, dict)
                self.assertEqual(len(detail['completed_suboperations']), 2)
                self.assertEqual(detail['attempted_operation']['data'], {'ax': 65535, 'ay': 65535})
                for operation, data in zip(detail['completed_suboperations'],
                                           ({'ax': 0, 'ay': 0}, {'btn': 1})):
                    self.assertEqual(operation, dict(path='/api/mouse', data=data,
                                     ack={'ok': True, 'error_present': False}, error=None))
                attempted = detail['attempted_operation']
                if isinstance(failure, OSError):
                    self.assertEqual(attempted['error'], 'transport_failure')
                    self.assertIsNone(attempted['ack'])
                elif failure.startswith(b'{'):
                    self.assertEqual(attempted['error'], 'negative_ack')
                    self.assertEqual(attempted['ack'], {'ok': False, 'error_present': True})
                else:
                    self.assertEqual(attempted['error'], 'invalid_ack')
                    self.assertIsNone(attempted['ack'])
                self.assertTrue(detail['partial_execution'])
                self.assertTrue(detail['button_state_uncertain'])
                self.assertEqual(detail['last_acknowledged_buttons'], 1)
                self.assertEqual(detail['attempted_side_effects'], 3)
                self.assertNotIn('FAKE_SECRET', json.dumps(rows))
                self.assertEqual(llm.call_count, 2)
                self.assertFalse(runner.run('fake')['ok'])
                self.assertEqual(len(self.posts), 3)


class RedirectTransportTests(unittest.TestCase):
    @contextlib.contextmanager
    def transport(self, status, fail_at=1, observation=False):
        # Keep urllib's real HTTPErrorProcessor / HTTPRedirectHandler active.
        # Both old urllib and strict http.client end at this in-memory boundary.
        import urllib.request
        import urllib.response
        from email.message import Message
        self.attempts, self.closed = [], []
        post_count = 0

        def response(method, path, body=None, headers=None):
            nonlocal post_count
            self.attempts.append((method, path, body, headers))
            if method == 'POST':
                post_count += 1
            failed = observation or (method == 'POST' and post_count == fail_at)
            code = status if failed else 200
            raw = (b'{"ok":true}' if failed or method == 'POST'
                   else json.dumps(SAFE_STATE).encode())
            fields = Message()
            fields['Location'] = '/api/status'
            result = urllib.response.addinfourl(io.BytesIO(raw), fields,
                                               'http://fake.invalid' + path, code)
            result.msg = 'synthetic HTTP response'
            return result

        def http_open(handler, request):
            return response(request.get_method(), request.selector,
                            request.data, dict(request.header_items()))

        closed = self.closed
        class Connection(p.http.client.HTTPConnection):
            def __init__(self, *args, **kwargs):
                pass
            def request(self, method, path, body=None, headers=None):
                self.response = response(method, path, body, headers)
            def getresponse(self):
                return self.response
            def close(self):
                closed.append(True)
                self.response.close()

        with patch.object(urllib.request.HTTPHandler, 'http_open', http_open):
            opener = urllib.request.build_opener(urllib.request.ProxyHandler({}))
            with patch('urllib.request._opener', opener), \
                    patch('http.client.HTTPConnection', Connection), \
                    patch('http.client.HTTPSConnection', Connection):
                yield

    def input_case(self, status, action, fail_at=1):
        from tools import gui_gate
        with tempfile.TemporaryDirectory() as directory, self.transport(status, fail_at):
            agent = RawObservationTests().real_helpers(directory, b'{}')
            gui = types.SimpleNamespace(**vars(gui_gate))
            gui.time = types.SimpleNamespace(sleep=Mock())
            driver = p.LiveDriver(agent, gui, 'redirect')
            runner, result, rows, llm = ReviewTests().run_steps(driver, action, KEY)
            # Check attempt count first: legacy 301/302/303 follows Location to GET.
            self.assertEqual([(m, path) for m, path, _, _ in self.attempts],
                             [('GET', '/api/status')] +
                             [('POST', '/api/key' if action['action'] == 'key'
                               else '/api/mouse')] * fail_at)
            self.assertFalse(result['ok'])
            self.assertEqual(result['steps'], 0)
            self.assertFalse(any(row['event'] == 'result' for row in rows))
            detail = rows[-1]['result']
            self.assertEqual(len(detail['completed_suboperations']), fail_at - 1)
            self.assertEqual(detail['attempted_side_effects'], fail_at)
            self.assertEqual(detail['attempted_operation']['http_status'], status)
            self.assertTrue(detail['partial_execution'])
            self.assertTrue(detail['button_state_uncertain'])
            if fail_at == 1:
                self.assertEqual(detail['completed_suboperations'], [])
                gui.time.sleep.assert_not_called()
            else:
                self.assertEqual(gui.time.sleep.call_count, fail_at - 1)
                if action == DRAG:
                    self.assertEqual(detail['last_acknowledged_buttons'], 1)
                    self.assertEqual(detail['attempted_operation']['data'],
                                     {'ax': 65535, 'ay': 65535})
                else:
                    self.assertEqual(detail['completed_suboperations'][0]['data'],
                                     {'text': 'abcd'})
                    self.assertEqual(detail['attempted_operation']['data'],
                                     {'text': '1234'})
            self.assertEqual(llm.call_count, 1)
            self.assertFalse(runner.run('fake')['ok'])
            self.assertEqual(len(self.attempts), fail_at + 1)

    def test_post_redirects_stop_before_success_or_further_input(self):
        for status in (302, 301, 303, 307, 308):
            for action in (KEY, CLICK, DRAG):
                with self.subTest(status=status, action=action):
                    self.input_case(status, action)

    def test_post_redirect_retains_only_actual_acknowledged_prefix(self):
        for status in (302, 301, 303, 307, 308):
            for action, fail_at in ((KEY, 2), (DRAG, 3)):
                with self.subTest(status=status, action=action):
                    self.input_case(status, action, fail_at)

    def test_post_non_success_status_cannot_be_positive_ack(self):
        for status in (199, 300, 304, 400, 500):
            with self.subTest(status=status):
                self.input_case(status, KEY)

    def test_post_success_requires_http_success_and_preserves_form_encoding(self):
        from tools import gui_gate
        action = {'action': 'key', 'seq': 'SHIFT+SPACE'}
        for status in (200, 201, 299):
            with self.subTest(status=status), tempfile.TemporaryDirectory() as directory, \
                    self.transport(status):
                agent = RawObservationTests().real_helpers(directory, b'{}')
                driver = p.LiveDriver(agent, gui_gate, 'success')
                _, result, rows, _ = ReviewTests().run_steps(driver, action)
                self.assertTrue(result['ok'])
                self.assertEqual(result['steps'], 2)
                self.assertEqual(self.attempts[1],
                                 ('POST', '/api/key', b'seq=SHIFT%2BSPACE',
                                  {'Content-Type': 'application/x-www-form-urlencoded'}))
                self.assertEqual(len(self.attempts), 2)
                self.assertEqual(self.closed, [True, True])
                detail = next(row['side_effects'] for row in rows if row['event'] == 'result')
                self.assertEqual(len(detail['completed_suboperations']), 1)
                self.assertIsNone(detail['attempted_operation'])

    def test_get_redirect_is_one_attempt_and_blocks_input(self):
        from tools import gui_gate
        for status in (302, 301, 303, 307, 308):
            for action in (KEY, {'action': 'status'}, {'action': 'tvram'},
                           {'action': 'screenshot'}):
                with self.subTest(status=status, action=action), \
                        tempfile.TemporaryDirectory() as directory, \
                        self.transport(status, observation=True):
                    agent = RawObservationTests().real_helpers(directory, b'{}')
                    driver = p.LiveDriver(agent, gui_gate, 'redirect')
                    runner, result, rows, llm = ReviewTests().run_steps(driver, action, KEY)
                    self.assertFalse(result['ok'])
                    self.assertEqual(result['steps'], 0)
                    path = '/api/status' if action == KEY else '/api/' + action['action']
                    self.assertEqual([(m, p) for m, p, _, _ in self.attempts], [('GET', path)])
                    self.assertEqual(self.closed, [True])
                    self.assertEqual(llm.call_count, 1)
                    self.assertFalse(any(row['event'] == 'result' for row in rows))
                    self.assertFalse(list(Path(directory).rglob('*.bmp')))
                    self.assertFalse(runner.run('fake')['ok'])
                    self.assertEqual(len(self.attempts), 1)


if __name__ == '__main__':
    unittest.main()
