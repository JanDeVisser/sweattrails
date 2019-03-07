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

from PyQt5.QtCore import QCoreApplication

import gripe.db
import sweattrails.qt.app.core
import grumpy.bg.bg
import sweattrails.qt.imports
import sweattrails.qt.mainwindow
import sweattrails.withings

logger = gripe.get_logger(__name__)


class SweatTrailsCmdLine(QCoreApplication,
                         sweattrails.qt.app.core.SweatTrailsCore,
                         sweattrails.qt.imports.DownloadManager):
    def __init__(self, argv):
        super(SweatTrailsCmdLine, self).__init__(argv)
        self.curr_progress = 0

    def start(self, args):
        super(SweatTrailsCmdLine, self).start(args)
        if not self.is_authenticated():
            raise sweattrails.qt.app.core.NotAuthenticatedException()
        t = grumpy.bg.bg.BackgroundThread.get_thread()
        t.queueEmpty.connect(self.quit)
        self.after_download.connect(self._after_download)

    def file_import(self, filenames):
        self.import_files(*filenames)

    def _after_download(self, job):
        self.quit()

    def get_download_manager(self):
        return self

    def selectActivities(self, antfiles):
        return antfiles
