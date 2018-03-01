#!/usr/bin/python
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

import StringIO

import gripe
import sweattrails.device.exceptions
import sweattrails.device.fitparse
import sweattrails.device.parser

logger = gripe.get_logger(__name__)


class FITRecord(object):
    def __init__(self, rec):
        self._fitrec = rec
        self._data = rec.as_dict(False)
    
    def fitrecord(self):
        return self._fitrec

    def get_data(self, key):
        assert self.fitrecord(), "FITRecord.get_data(%s): no FIT record set in class %s" % (key, self.__class__)
        return self.fitrecord().get_data(key)

    def set_data(self, key, data):
        assert False, "set_data not supported on FIT records"

    @classmethod
    def create(cls, container, rec):
        ret = None
        bridge = cls(rec)
        if rec.type.name == 'session':
            ret = sweattrails.device.parser.Activity(bridge)
            container.add_activity(ret)
        elif rec.type.name == 'lap':
            ret = sweattrails.device.parser.Lap(bridge)
            container.add_lap(ret)
        elif rec.type.name == 'record':
            ret = sweattrails.device.parser.Trackpoint(bridge)
            container.add_trackpoint(ret)
        if ret:
            ret.container = container
        return ret


class FITParser(sweattrails.device.parser.Parser):
    def __init__(self, filename):
        super(FITParser, self).__init__(filename)
        self.fitactivity = None
        
    def parse_file(self, buffer = None):
        self.fitactivity = sweattrails.device.fitparse.Activity(self.filename)
        self.status_message("Parsing FIT file {}", self.filename)
        self.fitactivity.parse(buffer=buffer)
        
        # Walk all records and wrap them in our types:
        for r in self.fitactivity.records:
            rec = FITRecord.create(self, r)
            if rec is not None:
                rec.container = self

if __name__ == "__main__":
    import sys
    import traceback

    import gripe.db
    import grizzle

    class Logger(object):
        def status_message(self, msg, *args):
            print >> sys.stderr, msg.format(*args)
            
        def progress_init(self, msg, *args):
            self.curr_progress = 0
            sys.stdout.write((msg + " [").format(*args))
            sys.stdout.flush()
            
        def progress(self, new_progress):
            diff = new_progress/10 - self.curr_progress 
            sys.stderr.write("." * diff)
            sys.stdout.flush()
            self.curr_progress = new_progress/10
            
        def progress_end(self):
            sys.stdout.write("]\n")
            sys.stdout.flush()
            

    def printhelp():
        print "usage: python" + sys.argv[0] + " <uid> <password> <fit file>"

    def main():
        if len(sys.argv) != 4:
            printhelp()
            return 0

        uid = sys.argv[1]
        password = sys.argv[2]
        user = None
        with gripe.db.Tx.begin():
            u = grizzle.UserManager().get(uid)
            if u.authenticate(password = password):
                user = u
        if not user:
            print >> sys.stderr, "Authentication error"
            printhelp()
            return 0

        try:
            parser = FITParser(sys.argv[3])
            parser.set_athlete(user)
            parser.set_logger(Logger())
            parser.parse()
            return 0
        except:
            traceback.print_exc()
            return 1

    sys.exit(main())

