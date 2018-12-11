#
# Copyright (c) 2014 Jan de Visser (jan@sweattrails.com)
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


from PyQt5.QtCore import Qt
from PyQt5.QtCore import pyqtSignal

from PyQt5.QtWidgets import QTableView

import grumble.qt.model


class TableView(QTableView):
    objectSelected = pyqtSignal(grumble.key.Key)
    
    def __init__(self, query = None, columns = None, parent = None):
        super(TableView, self).__init__(parent)
        self._query = None
        self._columns = None
        if query is not None or columns is not None:
            self.setQueryAndColumns(query, *columns)
        self.setSelectionBehavior(self.SelectRows)
        self.setShowGrid(False)
        vh = self.verticalHeader()
        vh.setVisible(False)
        hh = self.horizontalHeader()
        hh.setStretchLastSection(True)
        self.resizeColumnsToContents()
        self.setSortingEnabled(True)
        self.doubleClicked.connect(self.rowSelected)

    def refresh(self):
        self.model().beginResetModel()
        self.resetQuery()
        self.model().flush()
        self.model().endResetModel()
        self.resizeColumnsToContents()

    def setQueryAndColumns(self, query, *columns):
        self._query = query
        self._columns = columns
        if self._query is not None:
            tm = grumble.qt.model.TableModel(self._query, self._columns)
            self.setModel(tm)
            hh = self.horizontalHeader()
            fm = hh.fontMetrics()
            for ix in range(len(self._columns)):
                c = self._columns[ix]
                if hasattr(c, "template"):
                    hh.resizeSection(ix, len(c.template) * fm.maxWidth() + 11)

    def query(self):
        return self._query

    def columns(self):
        return self._columns

    def resetQuery(self):
        pass

    def rowSelected(self, ix):
        key = self.model().data(ix, Qt.UserRole)
        if key:
            self.objectSelected.emit(key)

