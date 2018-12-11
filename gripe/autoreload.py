# autoreloading launcher
# stolen a lot from Ian Bicking's WSGIKit (www.wsgikit.org)

import os
import os.path
import sys
import signal
import platform
import threading
import atexit
import queue

_interval = 1.0
_times = {}
_files = []

_running = False
_queue = queue.Queue()
_lock = threading.Lock()


def _restart(path):
    _queue.put(True)
    prefix = 'monitor (pid=%d):' % os.getpid()
    print('%s Change detected to \'%s\'.' % (prefix, path), file=sys.stderr)
    print('%s Triggering process restart.' % prefix, file=sys.stderr)
    if platform.system() == 'Windows':
        # Windows embedded mode
        import ctypes
        print('OS is Windows. Using ctypes.windll', file=sys.stderr)
        ctypes.windll.libhttpd.ap_signal_parent(1)
    else:
        # Linux. Assuming daemon mode.
        os.kill(os.getpid(), signal.SIGINT)


def _modified(path):
    try:
        # If path doesn't denote a file and were previously
        # tracking it, then it has been removed or the file type
        # has changed so force a restart. If not previously
        # tracking the file then we can ignore it as probably
        # pseudo reference such as when file extracted from a
        # collection of modules contained in a zip file.

        if not os.path.isfile(path):
            return path in _times

        # Check for when file last modified.

        mtime = os.stat(path).st_mtime
        if path not in _times:
            _times[path] = mtime

        # Force restart when modification time has changed, even
        # if time now older, as that could indicate older file
        # has been restored.

        if mtime != _times[path]:
            return True
    except IOError:
        # If any exception occured, likely that file has been
        # been removed just before stat(), so force a restart.

        return True

    return False


def _monitor():
    while 1:
        # Check modification times on all files in sys.modules.
        for module in sys.modules.values():
            if not hasattr(module, '__file__'):
                continue
            path = getattr(module, '__file__')
            if not path:
                continue
            if os.path.splitext(path)[1] in ['.pyc', '.pyo', '.pyd']:
                path = path[:-1]
            if _modified(path):
                return _restart(path)

        # Check modification times on files which have
        # specifically been registered for monitoring.
        for path in _files:
            if _modified(path):
                return _restart(path)

        # Go to sleep for specified interval.

        try:
            return _queue.get(timeout=_interval)
        except queue.Empty:
            pass


_thread = threading.Thread(target=_monitor)
_thread.setDaemon(True)


def _exiting():
    try:
        _queue.put(True)
    except queue.Full:
        pass
    _thread.join()


atexit.register(_exiting)


def _track(paths):
    if not isinstance(paths, str):
        map(lambda path: _track(path), paths)
    else:
        if paths not in _files:
            _files.append(paths)


def track(path):
    print("Change monitor tracks file(s) %s" % path, file=sys.stderr)
    _track(path)


def trackdir(directory):
    print("Change monitor tracks directory %s" % dir, file=sys.stderr)
    _track([os.path.join(directory, entry) for entry in os.listdir(directory)])


def start(interval=1.0):
    global _interval
    if interval < _interval:
        _interval = interval

    global _running
    _lock.acquire()
    if not _running:
        prefix = 'monitor (pid=%d):' % os.getpid()
        print('%s Starting change monitor.' % prefix, file=sys.stderr)
        _running = True
        _thread.start()
    _lock.release()
