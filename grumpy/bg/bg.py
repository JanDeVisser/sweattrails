#
# Copyright (c) 2017 Jan de Visser (jan@sweattrails.com)
#
# This program is free software; you can redistribute it and/or modify it
# under the terms of the GNU General Public License as published by the Free
# Software Foundation; either version 2 of the License, or (at your option)
# any later version.
#
# This program is distributed in the hope that it will be useful, but WITHOUT
# ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
# FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
# more details.
#
# You should have received a copy of the GNU General Public License along
# with this program; if not, write to the Free Software Foundation, Inc., 51
# Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
#

import queue
import traceback

from PyQt5.QtCore import QCoreApplication
from PyQt5.QtCore import QObject
from PyQt5.QtCore import QThread
from PyQt5.QtCore import pyqtSignal

import gripe

logger = gripe.get_logger(__name__)


class LoggingThread(QThread):
    statusMessage = pyqtSignal(str)
    progressInit = pyqtSignal(str)
    progressUpdate = pyqtSignal(int)
    progressEnd = pyqtSignal()

    def __init__(self, *args):
        super(LoggingThread, self).__init__(*args)
        self._stopped = False
        QCoreApplication.instance().aboutToQuit.connect(self.quit)

    def quit(self):
        self.stop()
        self.wait()

    def stop(self):
        self._stopped = True

    def status_message(self, msg, *args):
        self.statusMessage.emit(msg.format(*args))

    def progress_init(self, msg, *args):
        self.progressInit.emit(msg.format(*args))

    def progress(self, percentage):
        self.progressUpdate.emit(percentage)

    def progress_end(self):
        self.progressEnd.emit()


class BackgroundThread(LoggingThread):
    jobStarted = pyqtSignal(QObject)
    jobFinished = pyqtSignal(QObject)
    jobError = pyqtSignal(QObject, str, Exception)
    queueEmpty = pyqtSignal()

    _singleton = None
    _plugins = []

    def __init__(self):
        super(BackgroundThread, self).__init__()
        self._queue = queue.Queue()
        if "grumpy" in gripe.Config.app and "plugins" in gripe.Config.app.grumpy:
            for plugin in gripe.Config.app.grumpy.plugins:
                self.add_plugin(plugin)

    def addjob(self, job):
        job.thread = self
        self._queue.put(job)

    def add_plugin(self, plugin_class):
        logger.debug("Initializing background plugin '%s'", plugin_class)
        plugin_class = gripe.resolve(plugin_class)
        self._plugins.append(plugin_class(self))

    def run(self):
        self._stopped = False

        while not self._stopped:
            for plugin in self._plugins:
                try:
                    plugin.run()
                except Exception as e:
                    traceback.print_exc()
                    self.status_message("Exception handling background task '%s': %s", str(plugin), str(e))
            try:
                while True:
                    job = self._queue.get(True, 1)
                    try:
                        job(self)
                    except Exception as e:
                        traceback.print_exc()
                        job.error("Unexpected exception handling task", e)
                    self._queue.task_done()
            except queue.Empty:
                self.queueEmpty.emit()
        logger.debug("BackgroundThread finished")

    def quit(self):
        super(BackgroundThread, self).quit()
        for plugin in self._plugins:
            try:
                plugin.quit()
            except Exception as e:
                traceback.print_exc()
                self.status_message("Exception stopping background task '%s': %s", str(plugin), str(e))

    @classmethod
    def get_thread(cls):
        if not cls._singleton:
            cls._singleton = BackgroundThread()
        return cls._singleton

    @classmethod
    def add_backgroundjob(cls, job):
        t = cls.get_thread()
        t.addjob(job)


class ThreadPlugin:
    def __init__(self, thread):
        self.thread = thread

    def __str__(self):
        return self.__class__.__name__

    def addjob(self, job):
        self.thread.addjob(job)

    def run(self):
        pass

    def quit(self):
        pass
