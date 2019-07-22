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


class TableColumn:
    def __init__(self, attr, **kwargs):
        self.debug = False
        self._attr: str = attr
        self._join = attr[0] == '+'
        self.name = None
        name = attr.replace('+', '').replace('"', '')
        self._sanitized = name
        self.path = name.split(".")
        self.propname = self.path[-1] if self.path else None
        assert not self._join or len(self.path) == 2, "Invalid join column path '%s'" % name
        self.kind = None
        self.property = None
        if not hasattr(self, "display"):
            self.display = None
        if not hasattr(self, "header"):
            self.header = None
        if not hasattr(self, "display"):
            self.name = None
        if not hasattr(self, "value"):
            self.value = None
        for (n, v) in kwargs.items():
            if n not in ('kind',):
                setattr(self, n, v)
        if "kind" in kwargs:
            self.set_kind(kwargs["kind"])
        if self.name is None:
            self.name: str = self.property and self.property.name \
                if hasattr(self, "property") and self.property and self.property.name \
                else attr

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
            p0 = self.path[0].replace('+', '')
            if ':' in p0:
                join, cls = p0.split(':', 2)
            else:
                join = cls = p0
            self.kind = grumble.meta.Registry.get(cls)
            self._sanitized = join + "." + self.propname
        self.property = getattr(self.kind, self.propname) if self.propname else None
        return self

    def get_value(self, data):
        if callable(self):
            v = self(data)
        elif hasattr(self, "value") and self.value is not None:
            v = self.value(data) if callable(self.value) else self.value
        elif data and self._attr in data:
            v = data[self._attr]
        else:
            v = None
        if v is not None:
            if self.display is not None:
                return self.display(v) if callable(self.display) else self.display
            elif self.property is not None:
                return self.property.to_display(v, data if isinstance(data, grumble.Model) else None)
            else:
                return str(v)
        else:
            return ''

    def get_header(self):
        if self.header is not None:
            return self.header() if callable(self.header) else self.header
        elif self.property:
            return self.property.verbose_name if self.property.verbose_name else self.name
        else:
            return self.name.replace('_', ' ').title()

    def _get_value(self, instance):
        if self.path and not self._join:
            v = instance
            for n in self.path:
                if v:
                    v = v()
                    v = v[n] if n in v else None
            return v
        else:
            return instance.joined_value(self._sanitized)


class TableModel(QAbstractTableModel):
    def __init__(self, query, *args, **kwargs):
        super(TableModel, self).__init__()
        self.debug = False
        self._query = query
        self._kind = kwargs.get("kind", query.get_kind())
        self._columns = self._get_column_defs(*args)
        self._data = None
        self._count = None
        for (k, v) in kwargs.items():
            setattr(self, k, v)

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
                    col.debug = self.debug
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
            logger.debug("TableModel._get_data(%s) -> query", ix)
            with gripe.db.Tx.begin():
                self._data = [o for o in self._query]
        return self._data[ix]

    def data(self, index, role=Qt.DisplayRole):
        if index.row() < 0:
            return None
        if role == Qt.DisplayRole:
            instance = self._get_data(index.row())
            col = self._columns[index.column()]
            ret = col.get_value(instance)
        elif role == Qt.UserRole:
            r = self._get_data(index.row())
            ret = r.key()
        else:
            ret = None
        if role in (Qt.DisplayRole, Qt.UserRole):
            logger.debug("data(({0}, {1}), {2}) = {3}".
                         format(index.row(), index.column(),
                                (lambda r: {Qt.DisplayRole: "Display", Qt.UserRole: "User"}.get(r, r))(role), ret))
            logger.debug("Query: %s", str(self._query))
        return ret

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
    def __init__(self, parent: 'TreeItem', obj, columns=[]) -> None:
        self._parent_item = parent
        self.set_object(obj)
        self._columns = columns
        self._child_items = []
        self._row = 0

    def appendChild(self, item: 'TreeItem') -> None:
        if hasattr(self, "on_append"):
            self.on_append(item)
        item._row = len(self._child_items)
        self._child_items.append(item)

    def child(self, row: int) -> 'TreeItem':
        return self._child_items[row] if row < self.childCount() else None

    def childCount(self) -> int:
        return len(self._child_items)

    def columnCount(self) -> int:
        return (len(self._columns) if self._columns else 0) + 1

    def data(self, column: int) -> str:
        if self._obj:
            if column == 0:
                return self._obj.label()
            elif column < len(self._columns) + 1:
                col = self._columns[column-1]
                return col.get_value(self._obj)
            else:
                return None
        else:
            return self._kind.verbose_name()

    def parentItem(self) -> 'TreeItem':
        return self._parent_item

    def row(self) -> int:
        return self._row

    def get_key(self) -> grumble.key:
        return self._obj.key() if self._obj else None

    def set_object(self, obj):
        if hasattr(self, "on_assign"):
            self.on_assign(obj)
        if isinstance(obj, grumble.meta.ModelMetaClass):
            self._obj = None
            self._kind = obj
        elif obj is not None:
            self._obj = obj
            self._kind = grumble.meta.Registry.get(self._obj.kind())
        else:
            self._obj = self._kind = None

    def get_object(self):
        return self._obj


class TodoItem:
    def __init__(self, obj, count, current_list):
        self.obj = obj
        self.count = count
        self.current_list = list(current_list)

    def list_same(self, todo):
        c = {ti.obj.cat_name for ti in self.current_list}
        t = {ti.obj.cat_name for ti in todo}
        return c == t


class TreeModel(QAbstractItemModel):
    def __init__(self, parent, kind=None, query=None, root=None, columns=[], **kwargs) -> None:
        super(TreeModel, self).__init__(parent)
        self._root: grumble.key.Key = root.key() if root else None
        self._kind = kind if isinstance(kind, grumble.meta.ModelMetaClass) else grumble.meta.Registry.get(kind)
        if query is not None:
            self._query = query
            if self._kind is None:
                self._kind = query.get_kind(0)
        else:
            assert self._kind
            self._query = self._kind.query(keys_only=False)
            if self._root:
                self._query.set_ancestor(self._root)
        for (n, v) in kwargs.items():
            setattr(self, n, v)
        self._root_item: TreeItem = None
        self._columns = columns

    def columnCount(self, parent: QModelIndex = None) -> int:
        self._load()
        return len(self._columns) + 1
        # if parent is not None and parent.isValid():
        #     # return parent.internalPointer().columnCount()
        #     return len(self._columns) + 1
        # elif self._root_item:
        #     # return self._root_item.columnCount()
        #     return 1
        # else:
        #     return 0

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
            return item.data(index.column()) if item.parentItem() is not None else '<<ROOT>>'
        elif role == Qt.UserRole:
            item = index.internalPointer()
            return item.get_key() if item else None
        else:
            return None

    def flags(self, index: QModelIndex):
        self._load()
        return super(TreeModel, self).flags(index)

    def headerData(self, section, orientation, role):
        if orientation == Qt.Horizontal and role == Qt.DisplayRole:
            if section == 0:
                return self._kind.verbose_name()
            else:
                return self._columns[section-1].get_header()
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

    def _treeitem(self, parent, obj):
        cls = self.itemclass if hasattr(self, "itemclass") else TreeItem
        return cls(parent, obj, self._columns)

    def _load(self):
        if self._root_item is not None:
            return
        self._root_item = TreeItem(None, None)
        objects = {}
        todo = []

        def handle(todo_item):
            obj = todo_item.obj
            debug = False # "category" in obj.kind().lower()
            if not obj.parent_key() or (self._root and obj.parent_key() == self._root.key()):
                if debug:
                    print("ROOT ", obj.cat_name, str(obj.key()))
                item = self._treeitem(self._root_item, obj)
                self._root_item.appendChild(item)
                objects[obj.key()] = item
            else:
                parent = objects.get(obj.parent_key())
                if parent:
                    if debug:
                        print("CHLD ", obj.cat_name, str(obj.key()))
                    item = self._treeitem(parent, obj)
                    objects[obj.key()] = item
                    parent.appendChild(item)
                else:
                    if todo_item.count == 0 or not todo_item.list_same(todo):
                        if debug:
                            print(" WAIT", obj.cat_name, ", ".join(ti.obj.cat_name for ti in todo))
                        todo.append(TodoItem(obj, todo_item.count + 1, todo))
                    else:
                        if debug:
                            print("LOST ", obj.cat_name, str(obj.parent_key()), obj.parent().cat_name)
                        assert 0

        if self._root:
            self._query.set_ancestor(self._root)
        with gripe.db.Tx.begin():
            for o in self._query:
                handle(TodoItem(o, 0, todo))
        while todo:
            handle(todo.pop(0))
