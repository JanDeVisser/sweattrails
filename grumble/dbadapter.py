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

import datetime
import enum
import sys

import gripe
import gripe.db
import grumble.meta

logger = gripe.get_logger(__name__)


class AutoName(enum.Enum):
    def _generate_next_value_(name, start, count, last_values):
        return name


class QueryType(AutoName):
    Columns = enum.auto()
    KeyName = enum.auto()
    Update = enum.auto()
    Insert = enum.auto()
    Delete = enum.auto()
    Count = enum.auto()
    Aggregate = enum.auto()


class DbAdapter(object):
    def __init__(self, modelmanager):
        self._mm = modelmanager

    def __getattr__(self, name):
        return getattr(self._mm, name)

    def getModelQueryRenderer(self, query):
        return ModelQueryRenderer(self._mm, query)


class ModelQueryRenderer(object):
    def __init__(self, manager, query=None):
        self._query = query
        self._kind = None
        self._kinds = None
        self._manager = manager

    def query(self, q=None):
        if q:
            self._query = q
        return self._query

    def flat(self):
        return self._manager.flat

    def audit(self):
        return self._manager.audit

    def name(self):
        return self._manager.name

    def tablename(self):
        return self._manager.tablename

    def columns(self):
        return self._manager.columns

    def key_column(self):
        return self._manager.key_col

    def has_key(self):
        return self._query.has_keyvalue()

    def key(self):
        return self._query.key()

    def has_ancestor(self):
        return self._query.has_ancestor()

    def ancestor(self):
        return self._query.ancestor()

    def has_parent(self):
        return self._query.has_parent()

    def parent(self):
        return self._query.parent()

    def owner(self):
        return self._query.owner()

    def filters(self):
        return self.conditions()

    def joins(self):
        return self._query.joins()

    def sortorder(self):
        return self._query.sortorder()

    def limit(self):
        return self._query.limit()

    def conditions(self):
        return self._query.conditions()

    @staticmethod
    def _update_audit_info(new_values, insert):
        # Set update audit info:
        new_values["_updated"] = datetime.datetime.now()
        new_values["_updatedby"] = gripe.sessionbridge.get_sessionbridge().userid()
        if insert:
            # Set creation audit info:
            new_values["_created"] = new_values["_updated"]
            new_values["_createdby"] = new_values["_updatedby"]
            # If not specified, set owner to creator:
            if not new_values.get("_ownerid"):
                new_values["_ownerid"] = new_values["_createdby"]
        else:  # Update, don't clobber creation audit info:
            if "_created" in new_values:
                new_values.pop("_created")
            if "_createdby" in new_values:
                new_values.pop("_createdby")

    @staticmethod
    def _scrub_audit_info(new_values):
        for c in ("_updated", "_updatedby", "_created", "_createdby", "_ownerid", "_acl"):
            if c in new_values:
                del new_values[c]

    def execute(self, query_type, kind, new_values=None, subclasses=False):
        self._kind = grumble.meta.Registry.get(kind)
        self._manager = self._kind.modelmanager
        self._kinds = [self._kind]
        if subclasses:
            for sub in self._kind.subclasses():
                if not sub.abstract():
                    self._kinds.append(sub)
        logger.debug("Executing query for model '%s'", self._manager.name)
        self._manager.seal()
        assert self._query is not None, "Must set a Query prior to executing a ModelQueryRenderer"
        with gripe.db.Tx.begin() as tx:
            key_ix = -1
            cols = []
            vals = []

            if query_type == QueryType.Delete:
                sql = "DELETE FROM %s k" % self.tablename()
            elif query_type in (QueryType.Update, QueryType.Insert):
                assert new_values, "ModelQuery.execute: QueryType %s requires new values" % QueryType[query_type]
                if self.audit():
                    self._update_audit_info(new_values, query_type == QueryType.Insert)
                else:
                    self._scrub_audit_info(new_values)
                if query_type == QueryType.Update:
                    sql = 'UPDATE %s k SET %s ' % (self.tablename(), ", ".join(['"%s" = %%s' % c for c in new_values]))
                else:  # Insert
                    sql = 'INSERT INTO %s ( "%s" ) VALUES ( %s )' % \
                            (self.tablename(), '", "'.join(new_values), ', '.join(['%s'] * len(new_values)))
                vals.extend(new_values.values())
            elif query_type in (QueryType.Columns, QueryType.KeyName, QueryType.Aggregate, QueryType.Count):
                for k in self._kinds:
                    k.seal()
                columns = {c.name for k in self._kinds for c in k.modelmanager.columns}

                gb = None
                sql = ''
                glue = "\nWITH objects AS ("
                for k in self._kinds:
                    sql += glue
                    sql += '\n\tSELECT \'%s\' "_kind"' % k.kind()
                    my_columns = {c.name for c in k.modelmanager.columns}
                    for c in columns:
                        sql += ', '
                        sql += ('"%s"' if c in my_columns else 'NULL "%s"') % c
                    for c in self._query.synthetic_columns():
                        sql += ', '
                        sql += ('%s "%s"') % (c.formula(), c.name())
                    sql += '\n\t\tFROM %s' % k.modelmanager.tablename
                    glue = '\n\tUNION ALL'
                sql += '\n)\n'

                if self._query.has_aggregates():
                    for a in self._query.aggregates():
                        if a.groupby() and gb is None:
                            gb = a.groupby()
                            break
                    cols = ["_kind"]
                    collist = ("'%s'" + ' "_kind"') % \
                              (self._kinds[0].kind()
                               if gb is None or not isinstance(gb, grumble.meta.ModelMetaClass)
                               else gb.kind())
                    for a in self._query.aggregates():
                        cols.append(a.name())
                        collist += ', %s(%s) "%s"' % (a.func(), a.column(), a.name())
                    if gb is not None and isinstance(gb, grumble.meta.ModelMetaClass):
                        gb_cols = ['%s."%s"' % (gb.basekind(), col.name) for col in gb.modelmanager.columns]
                        if gb_cols:
                            collist += ', ' + ', '.join(gb_cols)
                            cols.extend(gb_cols)
                            key_name = '%s."%s"' % (gb.basekind(), gb.modelmanager.key_col.name)
                            key_ix = cols.index(key_name)
                elif query_type == QueryType.Columns:
                    cols = ["_kind"]
                    cols.extend(columns)
                    collist = 'k."' + '", k."'.join(cols) + '"'
                    key_name = self.key_column().name
                    key_ix = cols.index(key_name)

                    for j in self.joins():
                        collist += j.column_sql(cols)
                else:
                    cols = ["_kind", "_parent"]
                    key_name = self.key_column().name
                    cols.append(key_name)
                    collist = 'k."_kind", k."_parent", k."%s"' % key_name
                    key_ix = 2

                    for j in self.joins():
                        collist += j.key_column_sql(cols)

                sql += 'SELECT %s \n\t\tFROM objects k ' % collist
                for j in self.joins():
                    sql += "\n\t\t" + j.join_sql(vals)
            else:
                assert 0, "Huh? Unrecognized query query_type %s in query for table '%s'" % (query_type, self.name())

            if query_type != QueryType.Insert:
                clauses = []
                if self.has_key():
                    if self.key().scope():
                        clauses.append('(k."_parent" = %s)')
                        vals.append(str(self.key().scope()))
                    else:
                        clauses.append('(k."_parent" IS NULL)')
                    clauses.append('(k."%s" = %%s)' % self.key_column().name)
                    vals.append(str(self.key().name))
                if self.has_ancestor() and self.ancestor():
                    assert not self.flat(), "Cannot perform ancestor queries on flat table '%s'" % self.name()
                    clauses.append('(k."_parent" LIKE %s)')
                    vals.append(str(grumble.key.to_key(self.ancestor())) + "%")
                if self.has_parent():
                    assert not self.flat(), "Cannot perform parent queries on flat table '%s'" % self.name()
                    p = self.parent()
                    if p:
                        clauses.append('(k."_parent" = %s)')
                        vals.append(str(grumble.key.to_key(p)))
                    else:
                        clauses.append('(k."_parent" IS NULL)')
                if self.owner():
                    vals.append(self.owner())
                    clauses.append('(k."_ownerid" = %s)')
                for f in self.conditions():
                    s = f.to_sql(vals)
                    if s:
                        clauses.append(s)
                if clauses:
                    sql += '\n\t\tWHERE ' + '\n\t\t\tAND '.join(clauses)
                if self._query.has_aggregates() and gb is not None:
                    sql += "\n\t\tGROUP BY "
                    if isinstance(gb, grumble.meta.ModelMetaClass):
                        sql += ', '.join(['%s."%s"' % (gb.basekind(), col.name) for col in gb.modelmanager.columns])
                    else:
                        sql += '\n\t\tGROUP BY k."%s"' % self._query.aggregate().groupby()
            if query_type == QueryType.Columns and self.sortorder():
                sql += '\n\t\tORDER BY ' + \
                       ', '.join([so.to_sql() for so in self.sortorder()])
            if self.limit():
                sql += '\n\t\tLIMIT ' + self.limit()
            if query_type == QueryType.Count:
                sql = 'SELECT COUNT(*) "_count" FROM (%s) results' % sql
                cols = ['_count']
            logger.debug("Rendered query: %s [%s]", sql, vals)
            cur = tx.get_cursor()
            self._query.sql = sql
            cur.execute(sql, vals, columns=cols, key_index=key_ix)
            return cur
