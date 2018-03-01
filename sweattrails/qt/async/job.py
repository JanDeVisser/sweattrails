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

import traceback

from PyQt5.QtCore import QCoreApplication
from PyQt5.QtCore import QObject
from PyQt5.QtCore import pyqtSignal

import gripe
import sweattrails.qt.async.bg

logger = gripe.get_logger(__name__)


class Job(QObject):
    jobStarted = pyqtSignal(QObject)
    jobFinished = pyqtSignal(QObject)
    jobError = pyqtSignal(QObject, str, Exception)

    def __init__(self):
        super(Job, self).__init__()
        self.user = QCoreApplication.instance().user

    def __str__(self):
        return self.__class__.__name__

    def sync(self):
        self._handle(None)

    def _handle(self, thread):
        self.thread = thread
        logger.debug("Handling job %s", self)
        try:
            self.started()
            with gripe.db.Tx.begin():
                self.handle()
            self.finished()
        except Exception as e:
            traceback.print_exc()
            self.error("Exception handling task", e)

    def started(self):
        logger.debug("Job '%s' started", str(self))
        self.jobStarted.emit(self)
        if self.thread is not None:
            self.thread.jobStarted.emit(self)

    def finished(self,):
        logger.debug("Job '%s' finished", str(self))
        self.jobFinished.emit(self)
        if self.thread is not None:
            self.thread.jobFinished.emit(self)

    def error(self, msg, error):
        logger.debug("Error handling job '%s': %s [%s]", str(self), msg, str(error))
        self.jobError.emit(self, msg, error)
        if self.thread is not None:
            self.thread.jobError.emit(self, msg, error)

    def status_message(self, msg, *args):
        if self.thread is not None:
            self.thread.status_message(msg, *args)

    def progress_init(self, msg, *args):
        if self.thread is not None:
            self.thread.progress_init(msg, *args)

    def progress(self, percentage):
        if self.thread is not None:
            self.thread.progress(percentage)

    def progress_end(self):
        if self.thread is not None:
            self.thread.progress_end()

    def submit(self):
        sweattrails.qt.async.bg.BackgroundThread.add_backgroundjob(self)