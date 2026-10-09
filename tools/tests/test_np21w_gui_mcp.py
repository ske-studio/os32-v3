"""Exercise stdio tools/list and every new tools/call with mock GUI I/O.

The fixture records all original schemas/descriptions at b469f8f. No HTTP.
"""
import contextlib
import io
import json
from pathlib import Path
import sys
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT))
from tools.np21w_mcp import server
from tools.np21w_mcp import gui


class McpTests(unittest.TestCase):
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
               ('emu_wait_text',{'regex':'PASS','source':'screen','timeout':15})]
        with patch.object(server,'GUI',g), patch('urllib.request.urlopen',side_effect=AssertionError('LIVE HTTP FORBIDDEN')):
            for name,args in tests:
                result=self.request('tools/call',{'name':name,'arguments':args})
                self.assertNotIn('isError',result,(name,result))
                decoded=json.loads(result['content'][0]['text'])
                if name=='emu_gui_launch':
                    self.assertEqual((decoded['owner'],decoded['slot']),(4,None))
        self.assertTrue(calls)

    def test_invalid_input_is_an_mcp_error(self):
        with patch('urllib.request.urlopen',side_effect=AssertionError('LIVE HTTP FORBIDDEN')):
            result=self.request('tools/call',{'name':'emu_gui_launch','arguments':{'path':'relative'}})
        self.assertTrue(result['isError'])
        self.assertIn('absolute',result['content'][0]['text'])


if __name__=='__main__':
    unittest.main()
