"""Ordered TU workers; OS32_AST_JOBS defaults to min(cpu_count, 4).

A make recipe owns one slot. Borrow only immediately available jobserver tokens
for extra workers, returning them even on failure. If descriptors were closed
(or make has -j without a usable jobserver), keep the recipe's single slot.
"""
from concurrent.futures import ProcessPoolExecutor
from contextlib import contextmanager
import importlib
import multiprocessing
import os
import re


def worker_limit(jobs=None):
    value = jobs if jobs is not None else os.environ.get('OS32_AST_JOBS')
    if value is None:
        return min(os.cpu_count() or 1, 4)
    try:
        count = int(value)
    except (TypeError, ValueError):
        raise ValueError('OS32_AST_JOBS must be a positive integer') from None
    if count < 1:
        raise ValueError('OS32_AST_JOBS must be a positive integer')
    return count


@contextmanager
def worker_slots(limit):
    flags = os.environ.get('MAKEFLAGS', '').partition(' -- ')[0]
    match = re.search(r'--jobserver-(?:auth|fds)=([^\s]+)', flags)
    tokens = b''
    reader = writer = None
    try:
        if match and limit > 1:
            auth = match[1]
            try:
                if auth.startswith('fifo:'):
                    reader = os.open(auth[5:], os.O_RDWR | os.O_NONBLOCK)
                    writer = reader
                else:
                    read_fd, write_fd = map(int, auth.split(','))
                    # Reopen rather than dup: O_NONBLOCK must not change make's
                    # shared open file description. Closed recipe fds fall back.
                    os.fstat(read_fd)
                    writer = os.dup(write_fd)
                    reader = os.open('/proc/self/fd/%d' % read_fd,
                                     os.O_RDONLY | os.O_NONBLOCK)
                while len(tokens) < limit - 1:
                    token = os.read(reader, 1)
                    if not token:
                        break
                    tokens += token
            except (OSError, ValueError):
                pass
        parallel_make = match or re.search(r'(?:^|\s)(?:-j|--jobs)(?:\d|=|\s|$)', flags)
        yield 1 + len(tokens) if parallel_make else limit
    finally:
        if tokens:
            os.write(writer, tokens)
        if reader is not None:
            os.close(reader)
        if writer is not None and writer != reader:
            os.close(writer)


def _invoke(module, name, item):
    # The new interpreter imports the checker and loads libclang in the child.
    import clang_ast as ast
    notices = []
    cross_notice, cross_free_notice = ast.cross_notice, ast.cross_free_notice
    ast.cross_notice = lambda *args: notices.append(('cross_notice', args))
    ast.cross_free_notice = lambda: notices.append(('cross_free_notice', ()))
    try:
        try:
            return True, getattr(importlib.import_module(module), name)(item), notices
        except Exception as error:
            return False, error, notices
    finally:
        ast.cross_notice, ast.cross_free_notice = cross_notice, cross_free_notice


def ordered_units(worker, items, jobs=None, module=None):
    """Yield results/exceptions in input order, including the serial fast path.

    Only plain Python inputs/results cross the process boundary, never clang
    cursors. module supports checkers loaded under an alias by fixture tests.
    """
    items = list(items)
    with worker_slots(min(worker_limit(jobs), max(1, len(items)))) as count:
        if count == 1:
            yield from map(worker, items)
        else:
            with ProcessPoolExecutor(max_workers=count,
                                     mp_context=multiprocessing.get_context('spawn')) as pool:
                futures = [pool.submit(_invoke, module or worker.__module__, worker.__name__, item)
                           for item in items]
                for future in futures:
                    success, result, notices = future.result()
                    # Replay through the parent's cached notice functions: one
                    # announcement per mode, just like a serial scan.
                    import clang_ast as ast
                    for name, args in notices:
                        getattr(ast, name)(*args)
                    if not success:
                        raise result
                    yield result
