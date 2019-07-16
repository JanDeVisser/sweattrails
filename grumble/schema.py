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

logger = gripe.get_logger(__name__)


class ColumnDefinition(object):
    def __init__(self, name, data_type, required, defval, indexed):
        self.name = name
        self.data_type = data_type
        self.required = required
        self.defval = defval
        self.indexed = indexed
        self.is_key = False
        self.scoped = False


class ModelManager(object):
    modelconfig = gripe.Config.model
    models = modelconfig.get("model", {})
    def_recon_policy = modelconfig.get("reconcile", "none")
    _adapter_names = { 
        "sqlite3": "grumble.sqlite3.Sqlite3Adapter", 
        "postgresql": "grumble.postgresql.PostgresqlAdapter"
    }
    _adapter_factory = gripe.resolve(_adapter_names[gripe.db.Tx.database_type])
    assert _adapter_factory, "Misconfiguration: No Grumble database adapter for db type %s" % gripe.db.Tx.database_type

    def __init__(self, name):
        logger.debug("ModelManager.__init__(%s)", name)
        self.my_config = self.models.get(name, {})
        self.name = name
        self._adapter = self._adapter_factory(self) 
        self.table = name
        self.tablename = self.tableprefix + '"' + name + '"'
        self.columns = None
        self._prep_columns = []
        self.kind = None
        self.key_col = None
        self.flat = False
        self.audit = True

    def __str__(self):
        return "ModelManager <%s>" % self.name

    def set_tablename(self, tablename):
        self.table = tablename
        self.tablename = self.tableprefix + '"' + tablename + '"'

    def _set_columns(self):
        self.key_col = None
        for c in self._prep_columns:
            if c.is_key:
                self.key_col = c
                c.required = True
        self.columns = []
        if not self.key_col:
            kc = ColumnDefinition("_key_name", "TEXT", True, None, False)
            kc.is_key = True
            kc.scoped = False
            self.key_col = kc
            self.columns.append(kc)
        if not self.flat:
            self.columns += (ColumnDefinition("_parent", "TEXT", False, None, True),)
        self.columns += self._prep_columns
        if self.audit:
            self.columns += (ColumnDefinition("_ownerid", "TEXT", False, None, True),
                             ColumnDefinition("_acl", "TEXT", False, None, False),
                             ColumnDefinition("_createdby", "TEXT", False, None, False),
                             ColumnDefinition("_created", "TIMESTAMP", False, None, False),
                             ColumnDefinition("_updatedby", "TEXT", False, None, False),
                             ColumnDefinition("_updated", "TIMESTAMP", False, None, False))
        self.column_names = [c.name for c in self.columns]

    def add_column(self, column):
        assert self.kind, "ModelManager for %s without kind set??" % self.name
        assert not self.kind._sealed, "Kind %s is sealed" % self.name
        if isinstance(column, (tuple, list)):
            for c in column:
                self.add_column(c)
        else:
            self._prep_columns.append(column)

    def reconcile(self):
        self._set_columns()
        self._recon = self.my_config.get("reconcile", self.def_recon_policy)
        if self._recon != "none":
            with gripe.db.Tx.begin() as tx:
                cur = tx.get_cursor()
                if self._recon == "drop":
                    logger.info("%s: reconcile() drops table", self)
                    cur.execute('DROP TABLE IF EXISTS ' + self.tablename)
                    self._create_table(cur)
                else:  # _recon is 'all' or 'add'
                    if not self._table_exists(cur):
                        self._create_table(cur)
                    else:
                        self._update_table(cur)

    def _table_exists(self, cur):
        return self._adapter.table_exists(cur)

    def _create_table(self, cur):
        logger.info("%s: reconcile() creates table", self)
        sql = 'CREATE TABLE %s (' % self.tablename
        v = []
        cols = []
        for c in self.columns:
            csql = '\n"%s" %s' % (c.name, c.data_type)
            if c.required:
                csql += " NOT NULL"
            if c.defval:
                csql += " DEFAULT ( %s )"
                v.append(c.defval)
            if c.is_key and not c.scoped:
                csql += " PRIMARY KEY"
            cols.append(csql)
        sql += ",".join(cols) + "\n)"
        cur.execute(sql, v)
        for c in self.columns:
            if c.indexed and not c.is_key:
                cur.execute('CREATE INDEX "%s_%s" ON %s ( "%s" )' % (self.table, c.name, self.tablename, c.name))
            # if c.is_key and c.scoped:
            #     cur.execute('CREATE UNIQUE INDEX "%s_%s" ON %s ( "_parent", "%s" )' % (self.table, c.name, self.tablename, c.name))

    def _update_table(self, cur, table_existed=False):
        self._adapter.update_table(cur, table_existed)

    def seal(self):
        return self.kind.seal()
        
    def getModelQueryRenderer(self, query):
        return self._adapter.getModelQueryRenderer(query)

    modelmanagers_byname = {}

    @classmethod
    def for_name(cls, obj):
        set_mm = False
        manager = None
        name = None
        if hasattr(obj, "modelmanager"):
            if obj.modelmanager:
                manager = obj.modelmanager
            else:
                set_mm = True
        if manager is None and hasattr(obj, "kind"):
            if callable(obj.kind):
                name = obj.kind()
            else:
                name = obj.kind
        if isinstance(obj, str):
            name = obj

        if manager is not None:
            if name and name not in cls.modelmanagers_byname:
                cls.modelmanagers_byname[name] = manager
            return manager

        assert name, "Cannot get modelmanager for '%s' since I cannot determine the name to use" % obj
            
        manager = cls.modelmanagers_byname.get(name)
        if not manager:
            manager = ModelManager(name)
            cls.modelmanagers_byname[name] = manager
            if set_mm:
                obj.modelmanager = manager
        return manager
