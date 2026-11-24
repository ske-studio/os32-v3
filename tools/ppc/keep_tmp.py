"""QEMU の受入の道具の一時の置き場。CI (tools/ci/gate.py の --env の回) が OS32_KEEP_TMP=1 を立てた時は
抜けても消さず、落ちた検査のゲストの証拠を gate.py が検査ごとの TMPDIR から拾えるようにする。"""
import os
import tempfile


def temporary_directory(**kwargs):
    """tempfile.TemporaryDirectory と同じ。OS32_KEEP_TMP=1 の時だけ抜けても消さない。"""
    return tempfile.TemporaryDirectory(delete=os.environ.get('OS32_KEEP_TMP') != '1', **kwargs)
