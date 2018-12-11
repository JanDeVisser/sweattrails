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


class Sqlite3Adapter(grumble.dbadapter.DbAdapter):
    def __init__(self, modelmanager):
        super(Sqlite3Adapter, self).__init__(modelmanager)
        self._mm.tableprefix = ""

    def table_exists(self, cur):
        sql = "SELECT name FROM sqlite_master WHERE name = %s"
        v = [self.table]
        cur.execute(sql, v)
        return cur.fetchone() is not None

    def __str__(self):
        return self._mm.__str__()

    def update_table(self, cursor, table_existed=False):
        del cursor
        if table_existed and self._recon != "all":
            logger.info("%s: reconcile() _recon is '%s' and table existed. Leaving table alone", self, self._recon)
            return
        if False:
            for c in self.columns:
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
                    cur.execute('CREATE INDEX "%s_%s" ON %s ( "%s" )' % (self.table, c.name, self.tablename, c.name))
                if c.is_key and c.scoped:
                    cur.execute('CREATE UNIQUE INDEX "%s_%s" ON %s ( "_parent", "%s" )' % (
                    self.table, c.name, self.tablename, c.name))
