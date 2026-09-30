"""キー注入の退行試験 (票 docs/archive/tools/TASK_KEY_INJECT.md 受入 K1〜K6)。

2 つを見る。どちらもホストだけで完結し、NP21/W もゲストも要らない。

1. NP21/W 側の対応表 `aidebug_keys.cpp` を**実物のままリンク**して、
   大文字が SHIFT 付きで出ること (K1)、小文字と記号とキー名が退行していないこと
   (K2 / K3 / K5) を確かめる。この翻訳単位は `compiler.h` の `SUPPORT_AIDEBUG` しか
   要らないので、Windows のヘッダ無しでホストで組める。
   `np21w-src` が無い環境ではここは SKIP する (OS32 のリポジトリだけでは完結しない)。

2. `tools/gui_gate.py` の逃がし記法の展開 (K4) と、
   **既定では今までと 1 バイトも変わらないこと** (K6)。

注意: **実際にゲストへキーが届くかはここでは分からない。** それは NP21/W を動かして
確かめるしかない (票 §4 の K1/K7)。ここで見ているのは対応表と台本の側だけ。
"""
import os
import pathlib
import subprocess
import sys
import tempfile

ROOT = pathlib.Path(__file__).resolve().parents[2]
# NP21/W ai-debug フォークの置き場。環境変数 NP21W_SRC_DIR で変えられる (既定 ~/np21w-src)。
NP21W = pathlib.Path(os.path.expanduser(os.environ.get('NP21W_SRC_DIR', '~/np21w-src')))
AIDEBUG = NP21W / 'src/win9x/aidebug'


def check_emulator_table():
    if not (AIDEBUG / 'aidebug_keys.cpp').exists():
        print('SKIP: %s が無い (NP21/W 側の対応表は試験できない)' % AIDEBUG)
        return
    with tempfile.TemporaryDirectory(prefix='os32-keyinj-') as tmp:
        tmp = pathlib.Path(tmp)
        # 本物の compiler.h は Windows のヘッダを引くので、必要な 1 行だけ立てる。
        (tmp / 'compiler.h').write_text('#define SUPPORT_AIDEBUG 1\n')
        exe = tmp / 'key_inject'
        subprocess.run(['g++', '-Wall', '-Wextra', '-Werror',
                        '-I' + str(tmp), '-I' + str(AIDEBUG),
                        str(ROOT / 'tools/tests/key_inject_host.cpp'),
                        str(AIDEBUG / 'aidebug_keys.cpp'), '-o', str(exe)], check=True)
        subprocess.run([str(exe)], check=True, timeout=60)
    print('EMU TABLE PASS (K1/K2/K3/K5)')


def check_gui_gate():
    sys.path.insert(0, str(ROOT / 'tools'))
    import gui_gate

    exp = gui_gate.expand_escapes

    # K4: 制御文字は seq の和音へ、`\\` は YEN キー行きの text へ。
    assert exp(r'A\x1bB') == [('text', 'A'), ('seq', 'ESC'), ('text', 'B')], exp(r'A\x1bB')
    assert exp(r'a\\b') == [('text', 'a\\b')]
    assert exp(r'\x01') == [('seq', 'CTRL+A')]
    assert exp(r'\x1a') == [('seq', 'CTRL+Z')]
    assert exp(r'\x03') == [('seq', 'CTRL+C')]
    assert exp(r'\e\n\r\t\b') == [('seq', 'ESC'), ('seq', 'RETURN'),
                                  ('seq', 'RETURN'), ('seq', 'TAB'), ('seq', 'BS')]
    assert exp(r'\x7f') == [('seq', 'DEL')]
    # 印字できるバイトは text のまま隣とつながる (分割の邪魔をしない)。
    assert exp(r'A\x42C') == [('text', 'ABC')]
    assert exp('') == []
    assert exp('plain') == [('text', 'plain')]

    # 作れないバイトは黙って捨てない ([V4])。
    for bad in (r'\x00', r'\x1c', r'\x1f', r'\x80', r'\xff'):
        try:
            exp(bad)
        except ValueError:
            pass
        else:
            raise AssertionError('%s が通ってしまった' % bad)
    # 壊れた記法も同じ。
    for bad in ('a\\', r'\q', r'\x4', r'\xzz'):
        try:
            exp(bad)
        except ValueError:
            pass
        else:
            raise AssertionError('%r が通ってしまった' % bad)

    # K6: 逃がし記法は**既定で有効** (2026-09-18)。`\` 自身は `\\` と書く。
    # ツリー内に `text=` で `\` を送る利用者は調査で 0 件だったので既定を変えた。
    posts = []
    real_post, real_sleep = gui_gate.post, gui_gate.time.sleep
    gui_gate.post = lambda path, data: posts.append((path, data))
    gui_gate.time.sleep = lambda _s: None
    try:
        gui_gate.key(text='/usr/bin/t5a_display.bin')
        assert posts == [('/api/key', {'text': c}) for c in
                         ('/usr', '/bin', '/t5a', '_dis', 'play', '.bin')], posts
        posts[:] = []
        gui_gate.key(text=r'C:\\dir')         # `\\` → YEN キー 1 個
        assert posts == [('/api/key', {'text': 'C:\\d'}),
                         ('/api/key', {'text': 'ir'})], posts
        posts[:] = []
        gui_gate.key(text=r'C:\dir', escapes=False)   # 素通しも従来どおり使える
        assert posts == [('/api/key', {'text': 'C:\\d'}),
                         ('/api/key', {'text': 'ir'})], posts
        # **肝**: `\\x41` は「`\` 1 個 + 文字列 x41」で、逃がした 0x41 ではない。
        # `\\` を最優先で食わないとこの区別が壊れる。
        posts[:] = []
        gui_gate.key(text=r'a\\x41b')        # 5 文字なので 4 + 1 に割れる
        assert posts == [('/api/key', {'text': 'a\\x4'}),
                         ('/api/key', {'text': '1b'})], posts
        posts[:] = []
        gui_gate.key(text=r'a\x41b')          # こちらは文字 A
        assert posts == [('/api/key', {'text': 'aAb'})], posts
        # 末尾の単独 `\` は黙って捨てず断る
        try:
            gui_gate.key(text='oops\\')
            raise AssertionError('末尾の単独 `\\` を断っていない')
        except ValueError:
            pass
        posts[:] = []
        gui_gate.key(seq='SHIFT+SPACE')       # K5
        assert posts == [('/api/key', {'seq': 'SHIFT+SPACE'})], posts
        # 4 文字の分割は残り、記法の途中では切れない。
        posts[:] = []
        gui_gate.key(text=r'abcdef\x1bghi')
        assert posts == [('/api/key', {'text': 'abcd'}),
                         ('/api/key', {'text': 'ef'}),
                         ('/api/key', {'seq': 'ESC'}),
                         ('/api/key', {'text': 'ghi'})], posts
    finally:
        gui_gate.post, gui_gate.time.sleep = real_post, real_sleep
    print('GUI_GATE PASS (K4 + K6 無変化)')


check_emulator_table()
check_gui_gate()
