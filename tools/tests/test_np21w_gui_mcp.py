"""Exercise stdio tools/list and every new tools/call with mock GUI I/O.

The fixture records all original schemas/descriptions at b469f8f. No HTTP.
"""
import contextlib
import io
import json
import os
import subprocess
import tempfile
from pathlib import Path
import sys
import unittest
from unittest.mock import patch
from urllib.parse import parse_qs

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT))
from tools.np21w_mcp import server
from tools.np21w_mcp import gui


class McpTests(unittest.TestCase):
    def test_key_form_encoding_preserves_chords_and_text(self):
        for args in ({'seq': 'CTRL+STOP'},
                     {'text': 'a+b &c=1% 日本語'},
                     {'seq': 'CTRL+STOP', 'text': '+&='}):
            with self.subTest(args=args), patch.object(server.emu, 'post',
                    return_value=b'{"ok":true}') as post:
                result = self.request('tools/call', {'name': 'emu_key', 'arguments': args})
                self.assertNotIn('isError', result)
                path, body = post.call_args.args
                self.assertEqual(path, '/api/key')
                self.assertEqual(parse_qs(body), {k: [v] for k, v in args.items()})
                if 'seq' in args:
                    self.assertIn('CTRL%2BSTOP', body)

    def test_key_encoding_survives_direct_and_curl_client(self):
        for direct in (True, False):
            with self.subTest(direct=direct), \
                 patch.object(server.emu, '_direct_available', return_value=direct), \
                 patch.object(server.emu, '_http', return_value=b'{"ok":true}') as http, \
                 patch.object(server.emu, '_run', return_value=b'{"ok":true}') as curl:
                self.request('tools/call', {'name': 'emu_key',
                    'arguments': {'seq': 'CTRL+STOP', 'text': '+ &='}})
                if direct:
                    body = http.call_args.args[1]
                    curl.assert_not_called()
                else:
                    args = curl.call_args.args[0]
                    body = args[args.index('--data-binary') + 1]
                    http.assert_not_called()
                self.assertEqual(parse_qs(body), {'seq': ['CTRL+STOP'], 'text': ['+ &=']})

    def request(self, method, params=None):
        stream = io.StringIO()
        with contextlib.redirect_stdout(stream):
            server._handle({'jsonrpc':'2.0','id':1,'method':method,'params':params or {}})
        return json.loads(stream.getvalue())['result']

    def test_existing_schemas_unchanged_and_tools_list(self):
        before=json.loads((ROOT/'tools/tests/fixtures/np21w_schema_before.json').read_text())
        listed={t['name']:t for t in self.request('tools/list')['tools']}
        for name, expected in before.items():
            self.assertEqual({k:listed[name][k] for k in expected},expected)
        new={'emu_mouse','emu_type','emu_gui_launch','emu_gui_state','emu_screen_text',
             'emu_consink','emu_wait_mem','emu_wait_text'}
        self.assertEqual(set(listed)-set(before),new)
        self.assertEqual(listed['emu_wait_mem']['inputSchema']['required'],['symbol','op','value'])
        self.assertEqual(listed['emu_wait_text']['inputSchema']['required'],['regex'])
        for name in ('emu_wait_mem','emu_wait_text','emu_gui_launch'):
            self.assertEqual(listed[name]['inputSchema']['properties']['timeout'],
                             {'type':'number','minimum':15,'default':60})
        self.assertNotIn('ax',listed['emu_mouse']['inputSchema']['properties'])
        self.assertNotIn('args',listed['emu_gui_launch']['inputSchema']['properties'])
        self.assertEqual(listed['emu_wait_text']['inputSchema']['properties']['since'],
                         {'type':'integer','minimum':0})
        print('MCP schema: %d existing unchanged + %d GUI tools' % (len(before),len(new)))

    def test_each_new_tool_calls_real_gui_with_mock_observation(self):
        calls=[]
        class Clock:
            def sleep(self,n):pass
            def monotonic(self):return 0
        def get(path):
            self.assertEqual(path,'/api/status')
            return b'{"grph_disp":1,"scrn_ymax":480}',{}
        g=gui.Gui(gui.Transport(post=lambda p,d:calls.append((p,d)),get=get,clock=Clock()),
                  descriptor=gui.NO_DESCRIPTOR)
        state={'kernel':[{'owner':i,'state':0} for i in range(2,6)],'slots':[],
               'version':1,'windows':[],'front':0}
        sequence=[state,dict(state,kernel=[{'owner':i,'state':2 if i==4 else 0} for i in range(2,6)])]
        g.wm_state=lambda: sequence.pop(0) if sequence else state
        g.screen_text=lambda **kw: {'lines':['PASS'],'unknown_count':0,'path':kw.get('path')}
        g.consink=lambda **kw: {'text':'PASS','lines':['PASS'],'since':10}
        g.symbols=lambda: ({'probe':(0x1234,4)}, {})
        g.word=lambda a: self.assertEqual(a,0x1234) or 7
        tests=[('emu_mouse',{'action':'click','x':639,'y':479}),('emu_type',{'text':'abcd'}),
               ('emu_gui_launch',{'path':'/demo.bin','timeout':15}),('emu_gui_state',{}),
               ('emu_screen_text',{'path':'proof.png','region':[0,0,8,16]}),
               ('emu_consink',{'since':0,'grep':'PASS'}),
               ('emu_wait_mem',{'symbol':'probe','op':'eq','value':7,'timeout':15}),
               ('emu_wait_text',{'regex':'PASS','source':'screen','timeout':15}),
               ('emu_wait_text',{'regex':'PASS','since':0,'timeout':15})]
        with patch.object(server,'GUI',g), patch('urllib.request.urlopen',side_effect=AssertionError('LIVE HTTP FORBIDDEN')):
            for name,args in tests:
                result=self.request('tools/call',{'name':name,'arguments':args})
                self.assertNotIn('isError',result,(name,result))
                decoded=json.loads(result['content'][0]['text'])
                if name=='emu_gui_launch':
                    self.assertEqual((decoded['owner'],decoded['slot']),(4,None))
        self.assertTrue(calls)

    def test_type_hold_schema_and_forwarding(self):
        listed = {t['name']: t for t in self.request('tools/list')['tools']}
        self.assertEqual(listed['emu_type']['inputSchema']['properties']['hold'],
                         {'type': 'integer', 'minimum': 0, 'maximum': 5000})
        calls = []
        g = gui.Gui(gui.Transport(post=lambda p,d: calls.append((p,d)),
            get=lambda p: (b'{"grph_disp":1,"scrn_ymax":480}', {})))
        with patch.object(server, 'GUI', g), patch('urllib.request.urlopen',
                side_effect=AssertionError('LIVE HTTP FORBIDDEN')):
            result = self.request('tools/call', {'name': 'emu_type',
                'arguments': {'seq': 'T', 'hold': 3000}})
            self.assertNotIn('isError', result)
            self.assertEqual(calls, [('/api/key', {'seq': 'T', 'hold': 3000})])
            result = self.request('tools/call', {'name': 'emu_type',
                'arguments': {'text': 't', 'hold': 3000}})
            self.assertTrue(result['isError'])
            self.assertIn('hold', result['content'][0]['text'])

    def test_server_script_tools_list_and_fallback_with_mock_transport(self):
        # Run the production script from outside the repo: no tools package on
        # sys.path. sitecustomize supplies offline I/O before server imports gui.
        bootstrap = """
import sys, types, urllib.request
sys.path.insert(0, %r)
import gui
urllib.request.urlopen = lambda *a, **k: (_ for _ in ()).throw(AssertionError('LIVE HTTP FORBIDDEN'))
original_init = gui.Gui.__init__
def init(self, *args, **kwargs):
    original_init(self, *args, **kwargs)
    self.posts = []
    self.transport = gui.Transport(post=lambda p,d:self.posts.append((p,d)),
        get=lambda p:(b'{"grph_disp":1,"scrn_ymax":480}',{}),
        clock=types.SimpleNamespace(sleep=lambda n:None, monotonic=lambda:0))
    state = {'kernel':[{'owner':i,'state':0} for i in range(2,6)],
             'slots':[], 'windows':[], 'front':0, 'version':1}
    ready = dict(state,kernel=[{'owner':i,'state':2 if i==4 else 0} for i in range(2,6)])
    sequence = [state,ready]
    self.wm_state = lambda: sequence.pop(0) if sequence else ready
    self.consink = lambda **kw: {'since': 44}
    self.mouse_posts = lambda: sum(p=='/api/mouse' for p,d in self.posts)
gui.Gui.__init__ = init
original_launch = gui.Gui.launch
def launch(self, *args, **kwargs):
    result = original_launch(self, *args, **kwargs)
    assert self.mouse_posts() == 6
    assert 'tools' not in sys.modules
    return result
gui.Gui.launch = launch
""" % str(ROOT/'tools/np21w_mcp')
        requests = [{'jsonrpc':'2.0','id':1,'method':'tools/list'},
                    {'jsonrpc':'2.0','id':2,'method':'tools/call','params':{
                        'name':'emu_gui_launch','arguments':{'path':'/x','fallback':True,'timeout':15}}}]
        with tempfile.TemporaryDirectory() as tmp:
            (Path(tmp)/'sitecustomize.py').write_text(bootstrap)
            env = dict(os.environ, PYTHONPATH=tmp)
            proc = subprocess.run([sys.executable,'-B',str(ROOT/'tools/np21w_mcp/server.py')],
                cwd=tmp, env=env, input=''.join(json.dumps(r)+'\n' for r in requests),
                capture_output=True, text=True, timeout=10)
        self.assertEqual(proc.returncode,0,proc.stderr)
        self.assertNotIn('Error in sitecustomize',proc.stderr)
        responses = [json.loads(line)['result'] for line in proc.stdout.splitlines()]
        self.assertIn('emu_gui_launch',[t['name'] for t in responses[0]['tools']])
        self.assertNotIn('isError',responses[1],responses[1])
        result=json.loads(responses[1]['content'][0]['text'])
        self.assertEqual((result['ok'],result['owner'],result['slot']),(True,4,None))

    def test_invalid_input_is_an_mcp_error(self):
        with patch('urllib.request.urlopen',side_effect=AssertionError('LIVE HTTP FORBIDDEN')):
            result=self.request('tools/call',{'name':'emu_gui_launch','arguments':{'path':'relative'}})
        self.assertTrue(result['isError'])
        self.assertIn('absolute',result['content'][0]['text'])


if __name__=='__main__':
    unittest.main()
