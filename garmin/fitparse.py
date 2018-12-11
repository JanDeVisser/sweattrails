#!/usr/bin/python

import datetime
import sys
import unitconvert

from fitparse import Activity, FitParseError

class RecordWrapper(object):
    def __init__(self, rec):
        self.data = rec.as_dict(True)
        self.initialize()

    def initialize(self):
        pass

class Session(RecordWrapper):
    def initialize(self):
        self.start = self.data["start_time"]
        self.end = self.data["timestamp"]
        self.laps = []
        self.records = []

    def contains(self, obj):
        return self.start < obj.data["timestamp"] <= self.end

    def add_lap(self, lap):
        self.laps.append(lap)

    def add_record(self, record):
        self.records.append(record)

    @classmethod
    def create(cls, rec):
        assert rec.type.name == 'session'
        ret = Session(rec)
        return ret

class Lap(RecordWrapper):
    @classmethod
    def create(cls, rec):
        assert rec.type.name == 'lap'
        ret = Lap(rec)
        return ret

class Record(RecordWrapper):
    @classmethod
    def create(cls, rec):
        assert rec.type.name == 'record'
        ret = Record(rec)
        return ret


class ActivityWrapper(object):
    def __init__(self, activity):
        self.activity = activity
        self.sessions = []
        self.laps = []
        self.records = []

    def find_session_for_obj(self, obj):
        for s in self.sessions:
            if s.contains(obj):
                return s
        return None

    def process(self):
        print >> sys.stderr, "Processing FIT file"
        for r in self.activity.records:
            if r.type.name == 'session':
                self.sessions.append(Session.create(r))
            elif r.type.name == 'lap':
                self.laps.append(Lap.create(r))
            elif r.type.name == 'record':
                self.records.append(Record.create(r))
        print >> sys.stderr, "#sessions ", len(self.sessions)
        print >> sys.stderr, "#laps ", len(self.laps)
        print >> sys.stderr, "#records ", len(self.records)
        for l in self.laps:
            print >> sys.stderr, "lap", l.data["timestamp"], l.data["timestamp"].microsecond
            s = self.find_session_for_obj(l)
            if s:
                s.add_lap(l)
        for r in self.records:
            s = self.find_session_for_obj(r)
            if s:
                s.add_record(r)

        for s in self.sessions:
            print >> sys.stderr, "Session", s.start, s.start.microsecond, " -- ", s.end, s.end.microsecond
            print >> sys.stderr, "  #laps", len(s.laps)
            print >> sys.stderr, "  #records", len(s.records)

def convert(filename):
    activity = Activity(filename)
    activity.parse()
    wrapper = ActivityWrapper(activity)
    wrapper.process()
    return None


def printhelp():
    print "usage: python" + sys.argv[0] + " FILE"

def main():

    if len(sys.argv) == 1:
        printhelp()
        return 0

    try:
        convert(sys.argv[1])
        return 0
    except FitParseError, exception:
        sys.stderr.write(str(exception) + "\n")
        return 1

if __name__ == "__main__":
    sys.exit(main())

