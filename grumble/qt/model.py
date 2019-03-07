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

import sys
import traceback

from PyQt5.QtCore import QAbstractListModel
from PyQt5.QtCore import QAbstractTableModel
from PyQt5.QtCore import QAbstractItemModel
from PyQt5.QtCore import QModelIndex
from PyQt5.QtCore import Qt

import gripe
import gripe.db
import grumble

logger = gripe.get_logger(__name__)


class TableColumn(object):
    def __init__(self, name, **kwargs):
        self.name = name
        self._join = name[0] == '+'
        name = name.replace('+', '').replace('"', '')
        self._sanitized = name
        self.path = self.name.split(".")
        self.propname = self.path[-1] if self.path else None
        assert not self._join or len(self.path) == 2, "Invalid join column path '%s'" % name
        self.prop = None
        self.kind = None
        self.display = None
        for (n, v) in kwargs.items():
            if n != 'kind':
                setattr(self, n, v)
            else:
                self.set_kind(v)

    def set_kind(self, kind):
        if not self._join:
            k = kind
            for n in self.path[:-1]:
                if k and hasattr(k, n):
                    prop: grumble.ReferenceProperty = getattr(k, n)
                    if prop and isinstance(prop, grumble.ReferenceProperty):
                        k = prop.reference_class
            self.kind = k
        else:
            join, cls = self.path[0].split(':')
            self.kind = grumble.meta.Registry.get(cls)
            self._sanitized = join + "." + self.propname
        self.prop = getattr(self.kind, self.propname) if self.propname else None
        return self

    def get_header(self):
        if hasattr(self, "header"):
            return self.header(self) if callable(self.header) else self.header
        elif self.prop:
            return self.prop.verbose_name
        else:
            return self.name

    def get_value(self, instance):
        if callable(self):
            val = self(instance)
        elif hasattr(self, "value"):
            val = self.value(instance) if callable(self.value) else self.value
        else:
            val = self._get_value(instance)
        if val is not None:
            val = self.prop.to_display(val, instance) if self.prop else str(val)
        else:
            val = ''
        return val

    def _get_value(self, instance):
        if self.path and not self._join:
            v = instance
            for n in self.path:
                if v:
                    v = getattr(v, n)
            return v
        else:
            return instance.joined_value(self._sanitized)


class TableModel(QAbstractTableModel):
    def __init__(self, query, *args):
        super(TableModel, self).__init__()
        self._query = query
        self._kind = query.get_kind()
        self._columns = self._get_column_defs(args)
        self._data = None
        self._count = None

    def _get_column_defs(self, *args):
        ret = []
        for arg in args:
            if isinstance(arg, (list, tuple)):
                ret.extend(self._get_column_defs(*arg))
            else:
                if isinstance(arg, TableColumn):
                    col = arg
                else:
                    col = TableColumn(str(arg))
                    col.set_kind(self._kind)
                ret.append(col)
        return ret

    def add_columns(self, *args):
        self._columns.extend(self._get_column_defs(args))

    def rowCount(self, parent=QModelIndex()):
        if self._count is None:
            self._count = len(self._data) if self._data is not None else self._query.count()
        logger.debug("rowCount(query = {0}): {1}".format(str(self._query), self._count))
        return self._count

    def columnCount(self, parent=QModelIndex()):
        # logger.debug("TableModel.columnCount()")
        return len(self._columns)

    def headerData(self, col, orientation, role):
        # logger.debug("TableModel.headerData(%s,%s,%s)", col, orientation, role)
        return self._columns[col].get_header() \
            if orientation == Qt.Horizontal and role == Qt.DisplayRole \
            else None

    def _get_data(self, ix):
        if self._data is None:
            # logger.debug("TableModel._get_data(%s) -> query", ix)
            with gripe.db.Tx.begin():
                self._data = [o for o in self._query]
        return self._data[ix]

    def data(self, index, role=Qt.DisplayRole):
        # logger.debug("data({0}, {1}, {2}, query = {3}".
        #              format(index.row(), index.column(),
        #                     (lambda r: {Qt.DisplayRole: "Display", Qt.UserRole: "User"}.get(r, r))(role),
        #                     str(self._query)))
        if index.row() < 0:
            return None
        if role == Qt.DisplayRole:
            instance = self._get_data(index.row())
            col = self._columns[index.column()]
            ret = col.get_value(instance)
            return ret
        elif role == Qt.UserRole:
            r = self._get_data(index.row())
            return r.key()
        else:
            return None

    def flush(self):
        self.layoutAboutToBeChanged.emit()
        self.beginResetModel()
        self._data = None
        self._count = None
        self.endResetModel()
        self.layoutChanged.emit()

    def sort(self, colnum, order):
        """
            Sort table by given column number.
        """
        # logger.debug("TableModel.sort(%s)", colnum)
        self.layoutAboutToBeChanged.emit()
        self._data = None
        self._count = None
        self._query.clear_sort()
        self._query.add_sort(self._columns[colnum].name, order == Qt.AscendingOrder)
        self.layoutChanged.emit()


class ListModel(QAbstractListModel):
    def __init__(self, query, display_column):
        QAbstractListModel.__init__(self)
        self._query = query
        self._query.add_sort(display_column, True)
        self._column_name = display_column
        self._data = None
        self._count = None

    def flush(self):
        self.layoutAboutToBeChanged.emit()
        self.beginResetModel()
        self._data = None
        self._count = None
        self.endResetModel()
        self.layoutChanged.emit()

    def rowCount(self, parent=QModelIndex()):
        if self._count is None:
            self._count = self._query.count()
        return self._count

    def headerData(self, section, orientation, role):
        return self._display_column.verbose_name \
            if orientation == Qt.Horizontal and role == Qt.DisplayRole \
            else None

    def _get_data(self, ix):
        if not self._data:
            self._data = []
            with gripe.db.Tx.begin():
                self._data = [o for o in self._query]
            self._count = len(self._data)
        return self._data[ix]

    def data(self, index, role=Qt.DisplayRole):
        if (role in (Qt.DisplayRole, Qt.EditRole)) and (index.column() == 0):
            r = self._get_data(index.row())
            return getattr(r, self._column_name) if self._column_name else r.label()
        elif role == Qt.UserRole:
            r = self._get_data(index.row())
            return r.key()
        else:
            return None

    def text_for_key(self, key):
        # return self.match(QModelIndex(0, 0), Qt.UserRole, key, 1, )
        instance = key()
        return (getattr(instance, self._column_name) if self._column_name else instance.label()) if instance else ''


class TreeItem:
    def __init__(self, parent: 'TreeItem', obj) -> None:
        self._parent_item = parent
        if isinstance(obj, (grumble.Model, grumble.Key)):
            self._obj = obj()
            self._kind = self._obj.__class__
        else:
            self._obj = None
            self._kind = obj
        self._child_items = []
        self._row = 0

    def appendChild(self, item: 'TreeItem') -> None:
        item._row = len(self._child_items)
        self._child_items.append(item)

    def child(self, row: int) -> 'TreeItem':
        return self._child_items[row] if row < self.childCount() else None

    def childCount(self) -> int:
        return len(self._child_items)

    def columnCount(self) -> int:
        return 1

    def data(self, column: int) -> str:
        if self._obj:
            return self._obj.label() if column == 0 else None
        else:
            return self._kind.verbose_name()

    def parentItem(self) -> 'TreeItem':
        return self._parent_item

    def row(self) -> int:
        return self._row

    def get_key(self) -> grumble.key:
        return self._obj.key() if self._obj else None


class TreeModel(QAbstractItemModel):
    def __init__(self, parent: TreeItem, kind: grumble.meta.ModelMetaClass, root=None) -> None:
        super(TreeModel, self).__init__(parent)
        self._root: grumble.key.Key = root.key() if root else None
        self._kind: grumble.meta.ModelMetaClass = kind
        self._root_item: TreeItem = None

    def columnCount(self, parent: QModelIndex = None) -> int:
        self._load()
        if parent is not None and parent.isValid():
            return parent.internalPointer().columnCount()
        elif self._root_item:
            return self._root_item.columnCount()
        else:
            return 0

    def flush(self) -> None:
        self.layoutAboutToBeChanged.emit()
        self.beginResetModel()
        self._root_item = None
        self.endResetModel()
        self.layoutChanged.emit()

    def data(self, index: QModelIndex, role):
        if not index.isValid():
            return None
        self._load()
        item: TreeItem = index.internalPointer()
        if role == Qt.DisplayRole:
            return item.data(index.column())
        elif role == Qt.UserRole:
            item = index.internalPointer()
            return item.get_key()
        else:
            return None

    def flags(self, index: QModelIndex):
        self._load()
        return super(TreeModel, self).flags(index)

    def headerData(self, section, orientation, role):
        if orientation == Qt.Horizontal and role == Qt.DisplayRole:
            return self._kind.verbose_name()
        else:
            return None

    def hasIndex(self, row: int, col: int, parent: QModelIndex):
        self._load()
        if parent and parent.isValid():
            parent_item: TreeItem = parent.internalPointer()
            child_item: TreeItem = parent_item.child(row)
            return child_item is not None and col < child_item.columnCount()
        else:
            return row < self._root_item.childCount() and col < self._root_item.columnCount()

    def index(self, row: int, column: int, parent: QModelIndex) -> QModelIndex:
        self._load()
        if not self.hasIndex(row, column, parent):
            return QModelIndex()

        if parent and parent.isValid():
            parent_item: TreeItem = parent.internalPointer()
            child_item: TreeItem = parent_item.child(row)
        elif row < self._root_item.childCount():
            child_item: TreeItem = self._root_item.child(row)
        else:
            child_item = None
        return self.createIndex(row, column, child_item) if child_item else QModelIndex()

    def parent(self, index: QModelIndex) -> QModelIndex:
        if not index.isValid():
            return QModelIndex()

        self._load()
        child_item: TreeItem = index.internalPointer()
        parent_item: TreeItem = child_item.parentItem()
        if not parent_item:
            return QModelIndex()
        else:
            assert isinstance(parent_item, TreeItem)
            return self.createIndex(parent_item.row(), 0, parent_item)

    def rowCount(self,  parent: QModelIndex = None) -> int:
        self._load()
        if parent is None or not parent.isValid():
            return self._root_item.childCount()
        elif parent.column() > 0:
            return 0
        else:
            parent_item = parent.internalPointer()
            return parent_item.childCount()

    def _load(self):
        if self._root_item is not None:
            return
        self._root_item = TreeItem(None, None)
        objects = {}
        todo = []

        def handle(obj):
            if not obj.parent_key() or (self._root and obj.parent_key() == self._root.key()):
                item = TreeItem(None, obj)
                self._root_item.appendChild(item)
                objects[obj.key()] = item
            else:
                parent = objects.get(obj.parent_key())
                if parent:
                    item = TreeItem(parent, obj)
                    objects[obj.key()] = item
                    parent.appendChild(item)
                else:
                    todo.append(obj)

        q = self._kind.query(keys_only=False)
        if self._root:
            q.set_ancestor(self._root)
        with gripe.db.Tx.begin():
            for o in q:
                handle(o)
        while todo:
            handle(todo.pop(0))
