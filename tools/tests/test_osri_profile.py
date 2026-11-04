"""S5C shared policy, CLI routing, and default one-file integration."""
CHECK = dict(name='osri-profile', tier=1, needs=['make:osri-enc2-core'], budget=90,
             covers=['gfx/osri_profile.h', 'gfx/gfx_osri.h', 'tools/osri_profile.py',
                     'tools/img2osri.py', 'tools/osri_enc2_native.py',
                     'tools/osri_enc2_spec.py', 'tools/osri_enc2_decode.py',
                     'tools/osri_enc2_master.py', 'tools/osri_enc2_order.py',
                     'tools/osri_enc2_pack.py', 'tools/osri_enc2_plan.py',
                     'tools/osri_enc2_row.py', 'tools/osri_enc2_timing.py',
                     'tools/osri_enc2_tree.py'])

import argparse
import contextlib
import io
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, os.environ.get('OSRI_PROFILE_TOOLS', str(ROOT / 'tools')))
import img2osri as cli
import osri_enc2_native as native
import osri_profile as profile


def check():
    with tempfile.TemporaryDirectory(prefix='osri-profile-') as name:
        directory = Path(name)
        header = directory / 'profile.h'
        for value in (15, 23, 31, 47):
            header.write_text(f'#define OSRI_DEFAULT_OUT_BUDGET {value}u\n')
            assert profile.read_out_budget(header) == value
        for text in ('', '#define OTHER 31u',
                     '#define OSRI_DEFAULT_OUT_BUDGET 31u\n' * 2,
                     '#define OSRI_DEFAULT_OUT_BUDGET (31u)',
                     '#define OSRI_DEFAULT_OUT_BUDGET 31u + 1',
                     '#define OSRI_DEFAULT_OUT_BUDGET 031u',
                     '#define OSRI_DEFAULT_OUT_BUDGET 3/**/1u',
                     '#define OSRI_DEFAULT_OUT_BUDGET nope',
                     '#define OSRI_DEFAULT_OUT_BUDGET -1',
                     '#define OSRI_DEFAULT_OUT_BUDGET 0',
                     '#define OSRI_DEFAULT_OUT_BUDGET 48',
                     '/* #define OSRI_DEFAULT_OUT_BUDGET 31u */'):
            header.write_text(text)
            try:
                profile.read_out_budget(header)
            except ValueError:
                pass
            else:
                raise AssertionError(('invalid header accepted', text))
        try:
            profile.read_out_budget(directory / 'missing')
        except FileNotFoundError:
            pass
        else:
            raise AssertionError('missing header accepted')

        def invoke(options):
            with patch.object(sys, 'argv', ['img2osri', 'input.png', '-o', str(directory/'out'), *options]), contextlib.redirect_stdout(io.StringIO()):
                cli.main()

        for options, budget, threads in (([], 31, None),
                (['--encoder', 'enc2', '--outs', '31', '--band', '1', '--threads', '4'], 31, 4),
                (['--outs', '15'], 15, None), (['--outs', '23'], 23, None),
                (['--outs', '47'], 47, None), (['--obs-outs', '47'], 47, None)):
            with patch.object(native, 'encode_image', return_value={'CRC': 'test'}) as encode, \
                 patch.object(cli, 'resize_image', return_value='source'), \
                 patch.object(cli, 'convert', return_value=(bytes(128096), 0)):
                invoke(options)
                encode.assert_called_once_with('input.png', str(directory/'out'), budget=budget, threads=threads)
        for options, budget, colors in ((['--encoder', 'pl-b'], 5, None),
                (['--hblank-outs', '5'], 5, None), (['--hblank-outs', '6'], 6, None),
                (['--obs-colors', '2'], 5, 2)):
            with patch.object(cli, 'resize_image', return_value='source'), \
                 patch.object(cli, 'convert', return_value=(bytes(128096), 0)) as convert, \
                 patch.object(cli, 'obs_statistics', return_value={}):
                invoke(options)
                convert.assert_called_once_with('source', budget, 1, obs_colors=colors)
        for options in (['--outs', '31', '--obs-outs', '31'],
                        ['--encoder', 'enc2', '--hblank-outs', '5'],
                        ['--encoder', 'enc2', '--obs-colors', '2'],
                        ['--band', '2'], ['--threads', '0'],
                        ['--encoder', 'pl-b', '--outs', '31'],
                        ['--encoder', 'pl-b', '--obs-outs', '31'],
                        ['--encoder', 'pl-b', '--threads', '2'],
                        ['--outs', '32'], ['--encoder', 'greedy-order']):
            with contextlib.redirect_stderr(io.StringIO()), \
                 patch.object(native, 'encode_image', return_value={'CRC': 'test'}):
                try:
                    invoke(options)
                except SystemExit as error:
                    assert error.code == 2
                else:
                    raise AssertionError(('invalid CLI accepted', options))
        # Alter the sole policy source in a copied tree: Python follows it and
        # the guest's existing B31 assertion rejects the unsupported C profile.
        tree = directory / 'tree'
        (tree/'gfx').mkdir(parents=True)
        (tree/'tools').mkdir()
        shutil.copy(profile.__file__, tree/'tools/osri_profile.py')
        gfx = ROOT/'gfx/gfx_osri.h'
        shutil.copy(gfx, tree/'gfx/gfx_osri.h')
        policy = tree/'gfx/osri_profile.h'
        original = (ROOT/'gfx/osri_profile.h').read_text()
        for value in (31, 23):
            policy.write_text(original.replace('31u', f'{value}u'))
            result = subprocess.run([sys.executable, '-B', '-c',
                'import osri_profile as p; print(p.OSRI_DEFAULT_OUT_BUDGET)'],
                cwd=tree/'tools', capture_output=True, text=True, check=True)
            assert int(result.stdout) == value
            result = subprocess.run(['gcc', '-std=gnu11', '-m32', '-ffreestanding',
                '-I'+str(ROOT), '-I'+str(ROOT/'include'), '-I'+str(ROOT/'gfx'),
                '-I'+str(ROOT/'sdk/include/os32'), '-x', 'c', '-c',
                '-o', str(directory/'profile.o'), '-'],
                input=f'#include "{tree}/gfx/gfx_osri.h"\n', capture_output=True, text=True)
            assert (result.returncode == 0) == (value == 31), result.stderr
            if value != 31:
                assert 'osri_default_budget_b31' in result.stderr
        # The common native entry passes the policy budget through planning and
        # packing, including the worker count; no comparison driver is involved.
        import numpy as np
        with patch.object(cli, 'resize_image', return_value=np.zeros((400,640,3), dtype=np.uint8)), \
             patch.object(native, 'plan', return_value=('p', 'o', {})) as plan, \
             patch.object(native, 'quantize', return_value='ix') as quantize, \
             patch('osri_enc2_pack.pack', return_value=bytes(128096)) as pack:
            native.encode_image('input', directory/'out', threads=2)
            assert plan.call_args.args[1] == 31
            assert plan.call_args.kwargs == {'threads': 2}
            quantize.assert_called_once()
            pack.assert_called_once_with('p', 'o', 'ix', 31)
    print('S5C profile/CLI: GREEN', flush=True)


MUTATIONS = [
    ('default-legacy', 'img2osri.py', "else 'enc2')", "else 'pl-b')"),
    ('default-b23', 'img2osri.py', 'budget = OSRI_DEFAULT_OUT_BUDGET', 'budget = 23'),
    ('threads-lost', 'img2osri.py', 'budget=budget, threads=args.threads', 'budget=budget, threads=None'),
    ('double-budget', 'img2osri.py', 'args.outs is not None and args.obs_outs is not None', 'False'),
    ('native-b23', 'osri_enc2_native.py', 'plan(pixels, budget, threads=threads)', 'plan(pixels, 23, threads=threads)'),
    ('profile-fixed', 'osri_profile.py', 'return int(value[1])', 'return 31'),
    ('profile-duplicate', 'osri_profile.py', 'len(definitions) != 1', 'len(definitions) < 1'),
    ('profile-fallback', 'osri_profile.py', "text = Path(path).read_text()", "text = Path(path).read_text() if Path(path).exists() else '#define OSRI_DEFAULT_OUT_BUDGET 31u'"),
]


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--mutate', action='store_true')
    args = parser.parse_args()
    check()
    if args.mutate:
        with tempfile.TemporaryDirectory(prefix='osri-profile-mutate-') as name:
            tree = Path(name)
            shutil.copytree(ROOT/'tools', tree/'tools', ignore=shutil.ignore_patterns('__pycache__', 'tests'))
            (tree/'gfx').mkdir()
            shutil.copy(ROOT/'gfx/osri_profile.h', tree/'gfx/osri_profile.h')
            env = dict(os.environ, OSRI_PROFILE_TOOLS=str(tree/'tools'))
            for label, module, old, new in MUTATIONS:
                path = tree/'tools'/module
                source = path.read_text()
                assert source.count(old) == 1, label
                path.write_text(source.replace(old, new))
                result = subprocess.run([sys.executable, '-B', __file__], env=env, capture_output=True, text=True)
                path.write_text(source)
                assert result.returncode and 'AssertionError' in result.stderr, (label, result.stderr)
                print(label, 'runtime RED', flush=True)
            subprocess.run([sys.executable, '-B', __file__], env=env, check=True)


if __name__ == '__main__':
    main()


def run(ctx):
    return subprocess.call([sys.executable, '-B', 'tools/tests/test_osri_profile.py'] +
                           (['--mutate'] if ctx.mutate else []), cwd=ctx.root)
