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
import grumble.dbadapter

logger = gripe.get_logger(__name__)


class PostgresqlAdapter(grumble.dbadapter.DbAdapter):
    def __init__(self, modelmanager):
        super(PostgresqlAdapter, self).__init__(modelmanager)
        self._mm.schema = gripe.Config.database.postgresql.schema
        self._mm.tableprefix = '"%s".' % gripe.Config.database.postgresql.schema \
            if gripe.Config.database.postgresql.schema else ""

    def table_exists(self, cur):
        sql = "SELECT table_name FROM information_schema.tables WHERE table_name = %s"
        v = [self.table]
        if self.schema:
            sql += ' AND table_schema = %s'
            v.append(self.schema)
        cur.execute(sql, v)
        return cur.fetchone() is not None

    def update_table(self, cur, table_existed=False):
        if table_existed and self._recon != "all":
            logger.info("%s: reconcile() _recon is '%s' and table existed. Leaving table alone",
                        self, self._recon)
            return
        sql = "SELECT column_name, column_default, is_nullable, data_type " + \
              "FROM information_schema.columns WHERE table_name = %s"
        v = [self.table]
        if self.schema:
            sql += ' AND table_schema = %s'
            v.append(self.schema)
        cur.execute(sql, v)
        coldescs = []
        for coldesc in cur:
            coldescs.append(coldesc)
        for c in self.columns:
            c._exists = False
        for (colname, defval, is_nullable, data_type) in coldescs:
            column = None
            for c in self.columns:
                if c.name == colname:
                    column = c
                    break
            if column:
                column._exists = True
                if data_type.lower() != column.data_type.lower() and self._recon == "all":
                    logger.info("Data type change: %s.%s %s -> %s",
                                self.tablename, colname, data_type.lower(),
                                column.data_type.lower())
                    cur.execute('ALTER TABLE %s DROP COLUMN "%s"' % (self.tablename, colname))
                    column._exists = False
                else:
                    column._exists = True
                    alter = ""
                    v = []
                    if column.required != (is_nullable == 'NO'):
                        logger.info("NULL change: %s.%s required %s -> is_nullable %s",
                                    self.tablename, colname,
                                    column.required, is_nullable)
                        alter = " SET NOT NULL" if column.required else " DROP NOT NULL"
                    if column.defval != defval:
                        alter += " SET DEFAULT %s"
                        v.append(column.defval)
                    if alter != "":
                        cur.execute('ALTER TABLE %s ALTER COLUMN "%s" %s' %
                                    (self.tablename, colname, alter), v)
            else:
                # Column not found. Drop it:
                cur.execute('ALTER TABLE %s DROP COLUMN "%s"' % (self.tablename, colname))
        for c in filter(lambda col: not col._exists, self.columns):
            v = []
            sql = 'ALTER TABLE %s ADD COLUMN "%s" %s' % (self.tablename, c.name, c.data_type)
            if c.required:
                sql += " NOT NULL"
            if c.defval:
                sql += " DEFAULT %s"
                v.append(c.defval)
            if c.is_key and not c.scoped:
                sql += " PRIMARY KEY"
            cur.execute(sql, v)
            if c.indexed and not c.is_key:
                cur.execute('CREATE INDEX "%s_%s" ON %s ( "%s" )' %
                            (self.table, c.name, self.tablename, c.name))
            if c.is_key and c.scoped:
                cur.execute('CREATE UNIQUE INDEX "%s_%s" ON %s ( "_parent", "%s" )' %
                            (self.table, c.name, self.tablename, c.name))
