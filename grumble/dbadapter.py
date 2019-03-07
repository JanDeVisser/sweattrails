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
    def __init__(self, kind, query=None):
        self._query = query
        if isinstance(kind, grumble.schema.ModelManager):
            self._manager = kind
        else:
            kind = grumble.meta.Registry.get(kind)
            self._manager = kind.modelmanager

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

    def execute(self, query_type, new_values=None):
        logger.debug("Executing query for model '%s'", self._manager.name)
        self._manager.seal()
        assert self._query is not None, "Must set a Query prior to executing a ModelQueryRenderer"
        with gripe.db.Tx.begin() as tx:
            key_ix = -1
            cols = ()
            vals = []
            if query_type == QueryType.Delete:
                sql = "DELETE FROM %s k" % self.tablename()
            elif query_type == QueryType.Count:
                sql = "SELECT COUNT(*) AS COUNT FROM %s k" % self.tablename()
                cols = ('COUNT',)
                for j in self.joins():
                    sql += j.join_sql()
            elif query_type == QueryType.Aggregate:
                assert self._query.sum()
                sql = "SELECT SUM(k.%s) AS SUM FROM %s k" % (self._query.sum(), self.tablename())
                cols = ('SUM',)
                for j in self.joins():
                    sql += j.join_sql()
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
            elif query_type in (QueryType.Columns, QueryType.KeyName):
                if query_type == QueryType.Columns:
                    cols = [c.name for c in self.columns()]
                    collist = 'k."' + '", k."'.join(cols) + '"'
                    key_name = self.key_column().name
                    key_ix = cols.index(key_name)

                    for j in self.joins():
                        collist += j.column_sql(cols)
                else:
                    assert query_type == QueryType.KeyName
                    key_name = self.key_column().name
                    cols = [key_name]
                    collist = 'k."%s"' % key_name
                    key_ix = 0

                    for j in self.joins():
                        collist += j.key_column_sql(cols)

                sql = "SELECT %s \n\t\tFROM %s k " % (collist, self.tablename())
                for j in self.joins():
                    sql += "\n\t\t" + j.join_sql()
            else:
                assert 0, "Huh? Unrecognized query query_type %s in query for table '%s'" % (query_type, self.name())

            if query_type != QueryType.Insert:
                glue = '\n\t\tWHERE '
                and_glue = '\n\t\t\tAND '
                if self.has_key():
                    sql += glue
                    glue = and_glue
                    sql += '(k."%s" = %%s)' % self.key_column().name
                    vals.append(str(self.key().name))
                if self.has_ancestor() and self.ancestor():
                    assert not self.flat(), "Cannot perform ancestor queries on flat table '%s'" % self.name()
                    sql += glue
                    glue = and_glue
                    sql += '(k."_ancestors" LIKE %s)'
                    vals.append(str(self.ancestor().get().path()) + "%")
                if self.has_parent():
                    assert not self.flat(), "Cannot perform parent queries on flat table '%s'" % self.name()
                    sql += glue + '(k."_parent" '
                    glue = and_glue
                    p = self.parent()
                    if p:
                        sql += " = %s"
                        vals.append(str(p))
                    else:
                        sql += " IS NULL"
                    sql += ")"
                if self.owner():
                    sql += glue + '(k."_ownerid" = %s)'
                    glue = and_glue
                    vals.append(self.owner())
                for f in self.conditions():
                    s = f.to_sql(vals)
                    if s:
                        sql += glue + s
                        glue = and_glue
            if query_type == QueryType.Columns and self.sortorder():
                sql += '\n\t\tORDER BY ' + \
                       ', '.join([so.to_sql() for so in self.sortorder()])
            if self.limit():
                sql += '\n\t\tLIMIT ' + self.limit()
            logger.debug("Rendered query: %s [%s]", sql, vals)
            cur = tx.get_cursor()
            self._query.sql = sql
            cur.execute(sql, vals, columns=cols, key_index=key_ix)
            return cur
