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


from PyQt5.QtCore import pyqtSignal
from PyQt5.QtCore import QCoreApplication
from PyQt5.QtWidgets import QCheckBox
from PyQt5.QtWidgets import QDialog
from PyQt5.QtWidgets import QDialogButtonBox
from PyQt5.QtWidgets import QTableWidget
from PyQt5.QtWidgets import QTableWidgetItem
from PyQt5.QtWidgets import QVBoxLayout

import gripe
import gripe.db
import sweattrails.qt.app.core
import sweattrails.qt.imports
import sweattrails.qt.mainwindow
import sweattrails.websync.garminconnect

logger = gripe.get_logger(__name__)


class SelectActivities(QDialog):
    select = pyqtSignal()
    download = pyqtSignal()

    def __init__(self, parent=None):
        super(SelectActivities, self).__init__(parent)
        layout = QVBoxLayout(self)
        self.table = QTableWidget(None)
        self.table.setColumnCount(3)
        self.table.setHorizontalHeaderLabels(["", "Date", "Title"])
        self.table.setColumnWidth(0, 20)
        self.table.setColumnWidth(1, 200)
        self.table.setColumnWidth(3, 400)
        layout.addWidget(self.table)
        self.buttonbox = QDialogButtonBox(QDialogButtonBox.Ok | QDialogButtonBox.Cancel)
        self.buttonbox.accepted.connect(self.accept)
        self.buttonbox.rejected.connect(self.reject)
        self.select.connect(self._select)
        self.download.connect(self._download)
        layout.addWidget(self.buttonbox)
        self.setMinimumSize(650, 300)
        self._activities = []
        self._selected = []
        self._gc = None

    def select_activities(self):
        logger.debug("gc.SelectActivities.select_activities")
        if self._gc is None:
            self._gc = sweattrails.websync.garminconnect.GarminConnect(QCoreApplication.instance().user)
            self._gc.credentials(gripe.Config.garmin.connect.username, gripe.Config.garmin.connect.password)
            self._gc.login()
        self._activities = self._gc.list(100)
        self.select.emit()
        return self._selected

    def _select(self):
        logger.debug("gc.SelectActivities._select")
        self.table.clear()

        self.table.setRowCount(len(self._activities))
        for row in range(len(self._activities)):
            activity = self._activities[row]
            self.table.setCellWidget(row, 0, QCheckBox(self))
            self.table.setItem(row, 1, QTableWidgetItem(activity["startTimeLocal"]))
            self.table.setItem(row, 2, QTableWidgetItem(activity["activityName"]))
        result = self.exec_()
        self._selected = []
        if result == QDialog.Accepted:
            for i in range(len(self._activities)):
                cb = self.table.cellWidget(i, 0)
                if cb.isChecked():
                    activity = self._activities[i]
                    self._selected.append(activity)
            if self._selected:
                self.download.emit()

    def _download(self):
        logger.debug("gc.SelectActivities._download")
        for activity in self._selected:
            self._gc.download(activity["activityId"])
