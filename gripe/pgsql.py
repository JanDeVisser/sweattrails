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

import psycopg2
import gripe.db

logger = gripe.get_logger("gripe.db")


class Cursor(gripe.db.LoggedCursor, psycopg2.extensions.cursor):
    placeholder = '%s'
    
    def _interpolate(self, sql, args):
        return self.mogrify(sql, args)


class Connection(gripe.db.LoggedConnection, psycopg2.extensions.connection):
    def cursor(self):
        return super(Connection, self).cursor(cursor_factory = Cursor)


class DbAdapter(object):

    @classmethod
    def setup(cls, pgsql_conf):
        cls.config = pgsql_conf
        assert pgsql_conf.user, \
            "Config: No user role in postgresql section of conf/database.json"
        pgsql_user = pgsql_conf.user
        assert pgsql_conf.admin, \
            "Config: No admin role in postgresql section of conf/database.json"
        pgsql_admin = pgsql_conf.admin
        assert pgsql_user.user_id and pgsql_user.password, \
            "Config: user role is missing user_id or password in postgresql section of conf/database.json"
        assert pgsql_admin.user_id and pgsql_admin.password, \
            "Config: admin role is missing user_id or password in postgresql section of conf/database.json"

        if pgsql_conf.database:
            database = pgsql_conf.database
            if database != 'postgres':
                # We're assuming the postgres database exists and should
                # never be wiped:
                with gripe.db.Tx.begin("admin", "postgres", True) as tx:
                    cur = tx.get_cursor()
                    create_db = False
                    if isinstance(pgsql_conf.wipe_database, bool) \
                            and pgsql_conf.wipe_database:
                        cur.execute('DROP DATABASE IF EXISTS "%s"' %
                            (database,))
                        create_db = True
                    else:
                        cur.execute("SELECT COUNT(*) FROM pg_catalog.pg_database WHERE datname = %s", (database,))
                        create_db = (cur.fetchone()[0] == 0)
                    if create_db:
                        cur.execute('CREATE DATABASE "%s"' % (database,))

        if pgsql_conf.schema:
            with gripe.db.Tx.begin("admin") as tx:
                cur = tx.get_cursor()
                create_schema = False
                schema = pgsql_conf.schema
                if isinstance(pgsql_conf.wipe_schema, bool) \
                        and pgsql_conf.wipe_schema:
                    cur.execute('DROP SCHEMA IF EXISTS "%s" CASCADE' % schema)
                    create_schema = True
                else:
                    cur.execute("SELECT COUNT(*) FROM information_schema.schemata WHERE schema_name = %s", (schema,))
                    create_schema = (cur.fetchone()[0] == 0)
                if create_schema:
                    cur.execute(
                        'CREATE SCHEMA "%s" AUTHORIZATION %s' %
                        (schema, pgsql_conf["user"]["user_id"]))

    def initialize(self, role, database, autocommit):
        self.role = role
        self.database = database
        self.autocommit = autocommit

    def connect(self):
        dsn = "user=%s password=%s" % (
            self.config[self.role].user_id,
            self.config[self.role].password)
        if not self.database and "database" in self.config:
            self.database = self.config.database
        if not self.database:
            self.database = "postgres" \
                if self.role == "admin" \
                else self.config[self.role].user_id
        dsn += " dbname=%s" % self.database
        if "host" in self.config:
            dsn += " host=%s" % self.config.host
        if "port" in self.config:
            dsn += " port=%s" % self.config.port
        logger.debug("Connecting with role '%s' autocommit = %s",
            self.role, self.autocommit)
        conn = psycopg2.connect(dsn, connection_factory = Connection)
        conn.autocommit = self.autocommit
        return conn
