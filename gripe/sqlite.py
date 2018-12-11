'''
Created on May 23, 2014

@author: jan
'''

import datetime
import os
import sqlite3
import shutil
import gripe
import gripe.db

logger = gripe.get_logger(__name__)

class Cursor(gripe.db.LoggedCursor, sqlite3.Cursor):
    closed = False

class Connection(gripe.db.LoggedConnection, sqlite3.Connection):
    def cursor(self):
        return super(Connection, self).cursor(Cursor)

class DbAdapter(object):
    
    @classmethod
    def setup(cls, sqlite_conf):
        cls.config = sqlite_conf
        cls._dbdir = sqlite_conf.dbdir if "dbdir" in sqlite_conf else "db"
        if isinstance(sqlite_conf.wipe, bool) and sqlite_conf.wipe:
            shutil.rmtree(os.path.join(gripe.root_dir(), cls._dbdir))
        gripe.mkdir(cls._dbdir)
            
    @classmethod
    def dbdir(cls):
        return os.path.join(gripe.root_dir(), cls._dbdir)

    def initialize(self, role, database, autocommit):
        self.role = role
        self.database = database
        if not self.database and "database" in self.config:
            self.database = self.config["database"]
        self.database = self.database or "grumble.db"
        self.autocommit = autocommit
        self.dbfile = os.path.join(self.dbdir(), "%s.db" % self.database)


    def connect(self):
        logger.debug("Opening database '%s'", self.dbfile)
        conn = sqlite3.connect(self.dbfile, 
           detect_types = sqlite3.PARSE_DECLTYPES,
           isolation_level = None if self.autocommit else 'DEFERRED',
           factory = Connection)
        conn.autocommit = self.autocommit
        return conn

# Map bool to the 'bool' type:
def adapt_bool(b):
    return 'True' if b else ''

def convert_bool(b):
    return b == 'True'

sqlite3.register_adapter(bool, adapt_bool)
sqlite3.register_converter("boolean", convert_bool)

# Map datetime.time to the 'time' type:
def adapt_time(time):
    return time.strftime("%H:%M:%S")

def convert_time(t):
    return datetime.datetime.strptime("%H:%M:%S").time()

sqlite3.register_adapter(datetime.time, adapt_time)
sqlite3.register_converter("time", convert_time)

# Map datetime.timedelta to the 'interval' type:
def adapt_timedelta(delta):
    return str(delta.seconds)

def convert_timedelta(s):
    return datetime.timedelta(seconds = int(s))

sqlite3.register_adapter(datetime.timedelta, adapt_timedelta)
sqlite3.register_converter("interval", convert_timedelta)