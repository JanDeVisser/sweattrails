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
from PyQt5.QtCore import QObject
from PyQt5.QtCore import QTimer
from PyQt5.QtCore import pyqtSignal

from PyQt5.QtCore import QModelIndex
from PyQt5.QtWidgets import QTableView
from PyQt5.QtWidgets import QTreeView

import gripe
import grumble.qt.model


class View:
    objectSelected = pyqtSignal(grumble.key.Key)

    def refresh(self, *args):
        self.model().beginResetModel()
        self.resetQuery()
        self.model().flush()
        self.model().endResetModel()
        gripe.call_if_exists(self, "on_refresh", None, *args)

    def row_selected(self, new, old=None):
        key = self.model().data(new, Qt.UserRole)
        if key:
            self.objectSelected.emit(key)

    def select_first(self):
        if self.model().rowCount() > 0:
            ix = self.model().index(0, 0, QModelIndex())
            self.setCurrentIndex(ix)
            key = self.model().data(ix, Qt.UserRole)
            self.objectSelected.emit(key)


class TableView(QTableView, View):
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
        self.activated.connect(self.row_selected)
        self.pressed.connect(self.row_selected)
        # QTimer.singleShot(0, self.select_first)

    def on_refresh(self, *args, **kwargs):
        self.resizeColumnsToContents()
        self.reset()

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

    def query(self) -> grumble.model.Query:
        return self._query

    def columns(self):
        return self._columns

    def resetQuery(self):
        pass


class TreeView(QTreeView, View):
    def __init__(self, parent, kind, root=None):
        super(TreeView, self).__init__(parent)
        self._model = grumble.qt.model.TreeModel(self, kind, root)
        self.setModel(self._model)
        self.activated.connect(self.row_selected)
        self.pressed.connect(self.row_selected)
        QTimer.singleShot(0, self.select_first)

    def resetQuery(self):
        pass
