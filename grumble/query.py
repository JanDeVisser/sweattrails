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

import gripe
import gripe.db
import grumble.key
import grumble.dbadapter

logger = gripe.get_logger(__name__)


def quotify(colname):
    if '.' in colname:
        alias, name = colname.split('.', 1)
        alias = alias.strip()
    else:
        alias = None
        name = colname
    name = name.strip()
    return alias, ('"%s"' if name[0] != '"' else '%s') % name


class Sort(object):
    def __init__(self, colname, ascending=True):
        self._alias, self.colname = quotify(colname)
        self.ascending = ascending

    def order(self):
        return "ASC" if self.ascending else "DESC"

    def alias(self, fallback):
        return self._alias if self._alias is not None else fallback

    def to_sql(self, fallback_alias="k"):
        return "%s.%s %s" % (self.alias(fallback_alias), self.colname, self.order())

    def __str__(self):
        return self.to_sql()


class Condition(object):
    def __init__(self, sql, values):
        self.sql = sql
        self.values = values

    def to_sql(self, vals=None, alias="k"):
        if vals is not None:
            if isinstance(self.values, (list, tuple)):
                vals.extend(self.values)
            elif self.values is not None:
                vals.append(self.values)
        return "(" + self.sql + ")"


class Filter(object):
    def __init__(self, *args):
        if len(args) == 2:
            split = args[0].rsplit(None, 1)
            self.colname = split[0]
            self.op = split[1] if len(split) > 1 else "="
            self._alias, self.colname = quotify(self.colname)
            self.value = args[1]
        elif len(args) == 3:
            self._alias, self.colname = quotify(str(args[0]))
            self.op = args[1]
            self.value = args[2]
        else:
            assert 0, "Could not interpret %s arguments to add_filter" % len(args)
        if self.op:
            self.op = self.op.strip().upper()
        if hasattr(self.value, "key") and callable(self.value.key):
            self.value = str(self.value.key())

    def alias(self, fallback):
        return self._alias if self._alias is not None else fallback

    def to_sql(self, vals=None, alias="k"):
        if self.value is None:
            if self.op == "!=":
                n = " IS NOT NULL"
            else:
                n = " IS NULL"
            return '(%s.%s%s)' % (self.alias(alias), self.colname, n)
        elif self.op and self.op.endswith("IN"):
            try:
                vals.extend(self.value)
                num = len(self.value)
            except TypeError:
                vs = str(self.value).split(",")
                if vals is not None:
                    vals.extend(vs)
                num = len(vs)
            if num > 1:
                return ' (%s.%s %s (' % (self.alias(alias), self.colname, self.op) + \
                       ', '.join(["%%s"] * num) + ')'
            elif num == 1:
                return '(%s.%s %s %%s)' % \
                       (self.alias(alias), self.colname, "=" if self.op == "IN" else "!=")
        else:
            if vals is not None:
                vals.append(self.value)
            return '(%s.%s %s %%s)' % (self.alias(alias), self.colname, self.op)

    def __str__(self):
        return self.to_sql()


class Join(object):
    def __init__(self, ix, kind, prop, alias=None, join_with="k"):
        if isinstance(kind.__class__, grumble.ModelMetaClass):
            self._kind = kind.__class__
        elif isinstance(kind, grumble.ModelMetaClass):
            self._kind = kind
        else:
            self._kind = grumble.Model.for_name(str(kind))
        self._property = prop
        self._alias = alias
        self._join_with = join_with
        self._ix = ix

    def tablename(self):
        return self._kind.modelmanager.tablename

    def columns(self):
        return self._kind.modelmanager.columns

    def key_column(self):
        return self._kind.modelmanager.key_col

    def key_column_name(self):
        return self.key_column().name

    def property(self):
        return self._property

    def alias(self):
        return self._alias if self._alias else "j" + str(self._ix)

    def join_sql(self):
        return ' INNER JOIN %s %s ON (%s."_key" = %s."%s")' % \
               (self.tablename(), self.alias(), self.alias(), self._join_with, self.property())

    def column_sql(self, query_columns):
        join_cols = [self.alias() + '."' + c.name + '"' for c in self.columns()]
        query_columns.extend(['+' + c for c in join_cols])
        return ', ' + ', '.join(join_cols)

    def key_column_sql(self, query_columns):
        query_columns.append(self.alias() + "." + self.key_column().name)
        return ', %s."%s"' % (self.alias(), self.key_column().name)

    def __str__(self):
        return self.join_sql()


class ModelQuery(object):
    def __init__(self):
        self._owner = None
        self._limit = None
        self._filters = []
        self._conditions = []
        self._sortorder = []
        self._joins = []

    def _reset_state(self):
        pass

    def set_key(self, key, kind=None):
        self._reset_state()
        assert not (self.has_parent() or self.has_ancestor()), \
            "Cannot query for ancestor or parent and key at the same time"
        assert ((key is None) or isinstance(key, (str, grumble.key.Key))), \
            "Must specify an string, Key, or None in ModelQuery.set_key"
        if isinstance(key, str):
            try:
                key = grumble.key.Key(key)
            except TypeError:
                key = grumble.key.Key(kind, key)
        if key is None:
            self.unset_key()
        else:
            self._key = key
        return self

    def unset_key(self):
        self._reset_state()
        if hasattr(self, "_key"):
            del self._key
        return self

    def has_keyvalue(self):
        return hasattr(self, "_key")

    def key(self):
        assert self.has_keyvalue(), "Cannot call key() on ModelQuery with no key set"
        return self._key

    def set_ancestor(self, ancestor):
        self._reset_state()
        assert not (self.has_parent() or self.has_keyvalue()), \
            "Cannot query for ancestor and key or parent at the same time"
        if isinstance(ancestor, str):
            ancestor = grumble.key.Key(ancestor) if ancestor != "/" else None
        elif hasattr(ancestor, "key") and callable(ancestor.key):
            ancestor = ancestor.key()
            assert ancestor, "ModelQuery.set_ancestor: not-None ancestor with key() == None. Is the Model stored"
        assert (ancestor is None) or isinstance(ancestor, grumble.key.Key), \
            "Must specify an ancestor object or None in ModelQuery.set_ancestor"
        logger.debug("Q: Setting ancestor to %s", ancestor)
        self._ancestor = ancestor
        return self

    def unset_ancestor(self):
        self._reset_state()
        if hasattr(self, "_ancestor"):
            del self._ancestor
        return self

    def has_ancestor(self):
        return hasattr(self, "_ancestor")

    def ancestor(self):
        assert self.has_ancestor(), \
            "Cannot call ancestor() on ModelQuery with no ancestor set"
        return self._ancestor

    def set_parent(self, parent):
        self._reset_state()
        assert not (self.has_ancestor() or self.has_keyvalue()), \
            "Cannot query for ancestor or keyname and parent at the same time"
        if isinstance(parent, str):
            parent = grumble.key.Key(parent) if parent else None
        elif hasattr(parent, "key") and callable(parent.key):
            parent = parent.key()
            assert parent, "ModelQuery.set_parent: not-None ancestor with key() == None. Is the Model stored"
        assert (parent is None) or isinstance(parent, grumble.key.Key), \
            "Must specify a parent object or None in ModelQuery.set_parent"
        self._parent = parent
        return self

    def unset_parent(self):
        self._reset_state()
        if hasattr(self, "_parent"):
            del self._parent
        return self

    def has_parent(self):
        return hasattr(self, "_parent") or (self.has_keyvalue() and self.key() and self.key().scope())

    def parent(self):
        assert self.has_parent(), "Cannot call parent() on ModelQuery with no parent set"
        if hasattr(self, "_parent"):
            return self._parent
        else:
            return self.key().scope()

    def owner(self, o=None):
        if o is not None:
            self._reset_state()
            self._owner = o
        return self._owner

    def clear_filters(self):
        self.clear_conditions()

    def add_filter(self, *args):
        self._reset_state()
        self._conditions.append(Filter(*args))
        return self

    def filters(self):
        return self._conditions

    def clear_conditions(self):
        self._conditions = []

    def add_condition(self, cond, values):
        self._reset_state()
        self._conditions.append(Condition(cond, values))

    def conditions(self):
        return self._conditions

    def clear_joins(self):
        self._joins = []

    def add_join(self, kind, prop, alias=None, join_with="k"):
        self._joins.append(Join(len(self._joins), kind, prop, alias, join_with))

    def joins(self):
        return self._joins

    def add_parent_join(self, parent_kind, alias="p"):
        self.add_join(parent_kind, "_parent", alias)

    def clear_sort(self):
        self._sortorder = []

    def add_sort(self, colname, ascending=True):
        self._reset_state()
        logger.debug("Adding sort order on %s", colname)
        self._sortorder.append(Sort(colname, ascending))
        return self

    def sortorder(self):
        return self._sortorder

    def set_limit(self, limit):
        self._limit = limit
        return self

    def clear_limit(self):
        self._limit = None
        return self

    def limit(self):
        return self._limit

    def execute(self, kind, t):
        if isinstance(t, bool):
            t = grumble.dbadapter.QueryType.KeyName if t else grumble.dbadapter.QueryType.Columns
        with gripe.db.Tx.begin():
            mm = grumble.schema.ModelManager.for_name(kind)
            r = mm.getModelQueryRenderer(self)
            return r.execute(t)

    def _count(self, kind):
        """
            Executes this query and returns the number of matching rows. Note
            that the actual results of the query are not available; these need to
            be obtained by executing the query again
        """
        with gripe.db.Tx.begin():
            mm = grumble.schema.ModelManager.for_name(kind)
            r = mm.getModelQueryRenderer(self)
            return r.execute(grumble.dbadapter.QueryType.Count).singleton()

    def _delete(self, kind):
        with gripe.db.Tx.begin():
            mm = grumble.schema.ModelManager.for_name(kind)
            r = mm.getModelQueryRenderer(self)
            return r.execute(grumble.dbadapter.QueryType.Delete).rowcount

    @classmethod
    def get(cls, key):
        with gripe.db.Tx.begin():
            if isinstance(key, str):
                key = grumble.key.Key(key)
            else:
                assert isinstance(key, grumble.key.Key), "ModelQuery.get requires a valid key object"
            q = ModelQuery().set_key(key)
            mm = grumble.schema.ModelManager.for_name(key.kind())
            r = mm.getModelQueryRenderer(q)
            return r.execute(grumble.dbadapter.QueryType.Columns).single_row_bycolumns()

    @classmethod
    def set(cls, insert, key, values):
        with gripe.db.Tx.begin():
            if isinstance(key, str):
                key = grumble.key.Key(key)
            elif key is None and insert:
                pass
            elif hasattr(key, "key") and callable(key.key):
                key = key.key()
            else:
                assert isinstance(key, grumble.key.Key), \
                    "ModelQuery.get requires a valid key object, not a %s" % type(key)
            q = ModelQuery().set_key(key)
            mm = grumble.schema.ModelManager.for_name(key.kind())
            r = mm.getModelQueryRenderer(q)
            r.execute(grumble.dbadapter.QueryType.Insert if insert else grumble.dbadapter.QueryType.Update, values)

    @classmethod
    def delete_one(cls, key):
        if isinstance(key, str):
            key = grumble.key.Key(key)
        elif hasattr(key, "key") and callable(key.key):
            key = key.key()
        assert isinstance(key, grumble.key.Key), "ModelQuery.delete_one requires a valid key object"
        return ModelQuery().set_key(key)._delete(key.kind())
