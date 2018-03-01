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
import gripe.conversions
import gripe.db
import grumble.geopt
import sweattrails.config
import sweattrails.device.exceptions
import sweattrails.session

logger = gripe.get_logger(__name__)


class Logger(object):
    def status_message(self, msg, *args):
        logger.debug(msg.format(*args))

    def progress_init(self, msg, *args):
        self.progress_msg = msg.format(*args)
        logger.debug(self.progress_msg + " - Starting")

    def progress(self, new_progress):
        pass

    def progress_end(self):
        logger.debug(self.progress_msg + " - Ended")


class Record(object):
    def __init__(self, bridge):
        self._bridge = bridge
        self._grumble_obj = None
        self.container = None
        self.activity = None
        self.initialize()

    def initialize(self):
        pass

    def object(self, obj=None):
        if obj is not None:
            self._grumble_obj = obj
        return self._grumble_obj

    def __call__(self):
        return self.object()

    def get_data(self, key):
        return self._bridge.get_data(key)

    def set_data(self, key, data):
        return self._bridge.set_data(key, data)

    def status_message(self, msg, *args):
        if self.container is not None:
            self.container.status_message(msg, *args)

    def progress_init(self, msg, *args):
        if self.container is not None:
            self.container.progress_init(msg, *args)
    
    def progress(self, num):
        if self.container is not None:
            self.container.progress(num)
        
    def progress_end(self):
        if self.container is not None:
            self.container.progress_end()


class DictBridge(dict):
    def __self__(self, obj):
        super(DictBridge, self).__init__(obj)

    def get_data(self, key):
        return self.get(key)

    def set_data(self, key, data):
        self[key] = data

    @classmethod
    def create(cls, container, obj):
        ret = None
        bridge = cls(obj)
        t = obj["type"]
        if t == 'activity':
            ret = Activity(bridge)
            container.add_activity(ret)
        elif t == 'lap':
            ret = Lap(bridge)
            container.add_lap(ret)
        elif t == 'trackpoint':
            ret = Trackpoint(bridge)
            container.add_trackpoint(ret)
        if ret:
            ret.container = container
        return ret


class Lap(Record):
    def __init__(self, bridge):
        super(Lap, self).__init__(bridge)

    def convert_interval(self, interval):
        self.start_time = self.get_data("start_time")
        interval.interval_id = str(gripe.conversions.local_date_to_utc(self.start_time)) + "Z"
        ts = self.start_time - self.activity.start
        interval.timestamp = ts
        interval.distance = self.get_data("total_distance")
        interval.elapsed_time = self.get_data("total_elapsed_time")
        interval.duration = self.get_data("total_timer_time")
        interval.calories_burnt = self.get_data("total_calories")
        interval.put()
        return interval


class Trackpoint(Record):
    def __init__(self, bridge):
        super(Trackpoint, self).__init__(bridge)

    def convert(self, session, prev):
        d = self.get_data("distance")
        if d is None or (prev and prev.distance > d):
            return
        wp = sweattrails.session.Waypoint(parent=session)
        wp.timestamp = self.get_data("timestamp") - self.activity.start
        lat = self.get_data("position_lat")
        lon = self.get_data("position_long")
        if lat and lon:
            wp.location = grumble.geopt.GeoPt(
                gripe.conversions.semicircle_to_degrees(lat),
                gripe.conversions.semicircle_to_degrees(lon))
        wp.speed = self.get_data("speed")
        wp.elevation = self.get_data("altitude")
        wp.distance = self.get_data("distance")
        wp.cadence = self.get_data("cadence")
        wp.heartrate = self.get_data("heart_rate")
        wp.power = self.get_data("power")
        wp.torque = 0  # FIT doesn't seem to have torque.
        wp.temperature = self.get_data("temperature")
        wp.put()
        return wp


class Activity(Lap):
    def __init__(self, bridge):
        super(Activity, self).__init__(bridge)
        self.start = self.get_data("start_time")
        self.end = self.get_data("timestamp")
        self.laps = []
        self.trackpoints = []
        self.activity = self
        self.index = 0

    def contains(self, obj):
        return self.start < obj.get_data("timestamp") <= self.end \
            if obj.get_data("timestamp") \
            else False

    def add_lap(self, lap):
        self.laps.append(lap)
        lap.activity = self

    def add_trackpoint(self, trackpoint):
        self.trackpoints.append(trackpoint)
        trackpoint.activity = self

    def convert(self, athlete):
        assert athlete, "Activity.convert(): athlete is None"
        self.start_time = self.get_data("start_time")
        
        q = sweattrails.session.Session.query()
        q.add_filter("start_time = ", self.start_time)
        q.add_filter("athlete = ", athlete)
        session = q.get()
        if session:
            raise sweattrails.device.exceptions.SessionExistsError(session)
            
        self.session = sweattrails.session.Session()
        self.session.athlete = athlete
        self.session.start_time = self.start_time
        self.session.inprogress = False
        profile = sweattrails.config.ActivityProfile.get_profile(athlete)
        assert profile, "Activity.convert(): User %s has no profile" % athlete.uid()
        sessiontype = profile.get_default_SessionType(self.get_data("sport"))
        assert sessiontype, "Activity.convert(): User %s has no default session type for sport %s" % \
                            (athlete.uid(), self.get_data("sport"))
        self.session.sessiontype = sessiontype
        self.status_message("Converting session {}/{} ({:s})",
                            self.index, len(self.container.activities), sessiontype.name)
        self.convert_interval(self.session)

        num = len(self.laps)
        intervals = []
        if num > 1:
            self.progress_init("Session {}/{}: Converting {} intervals",
                               self.index, len(self.container.activities), num)
            for ix in range(num):
                lap = self.laps[ix]
                self.progress(int((float(ix) / float(num)) * 100.0))
                interval = sweattrails.session.Interval(parent=self.session)
                lap.convert_interval(interval)
                intervals.append(interval)
            intervals.sort(key=lambda ival: ival.timestamp)
            self.progress_end()

        num = len(self.trackpoints)
        self.progress_init("Session {}/{}: Converting {} waypoints", self.index, len(self.container.activities), num)
        prev = None
        interval_ix = 0
        interval = intervals[interval_ix] if interval_ix < len(intervals) else None
        for ix in range(num):
            trackpoint = self.trackpoints[ix]
            self.progress(int((float(ix) / float(num)) * 100.0))
            prev = trackpoint.convert(self.session, prev) or prev
            if interval and prev and prev.timestamp >= interval.timestamp:
                interval.offset = prev.distance
                interval.put()
                interval_ix += 1
                interval = intervals[interval_ix] if interval_ix < len(intervals) else None
        self.progress_end()

        self.progress_init("Analyzing session {}/{}", self.index, len(self.container.activities))
        self.session.analyze(self)
        self.progress_end()
        return self.session


class Parser(object):
    def __init__(self, filename):
        self.filename = filename
        self.name = self.filename
        self.buffer = None
        self.user = None
        self.logger = None
        self.activities = []
        self.laps = []
        self.trackpoints = []
        self.set_logger(Logger())

    def set_athlete(self, athlete):
        self.user = athlete
        
    def set_logger(self, log):
        self.logger = log

    def status_message(self, msg, *args):
        if self.logger and hasattr(self.logger, "status_message"):
            self.logger.status_message(msg, *args)

    def progress_init(self, msg, *args):
        if self.logger and hasattr(self.logger, "progress_init"):
            self.logger.progress_init(msg, *args)

    def progress(self, num):
        if self.logger and hasattr(self.logger, "progress"):
            self.logger.progress(num)

    def progress_end(self):
        if self.logger and hasattr(self.logger, "progress_end"):
            self.logger.progress_end()

    def find_activity_for_obj(self, obj):
        for s in self.activities:
            if s.contains(obj):
                return s
        return None

    def add_activity(self, activity):
        self.activities.append(activity)
        activity.index = len(self.activities)

    def add_lap(self, lap):
        self.laps.append(lap)

    def add_trackpoint(self, trackpoint):
        self.trackpoints.append(trackpoint)

    def parse_file(self, buf=None):
        assert False, "Abstract method parse_file called"

    def parse(self, buf=None):
        assert self.user, "No user set on parser"
        if buf is None:
            assert self.filename, "No filename set on parser"
            assert gripe.exists(self.filename), "parser: file '%s' does not exist" % self.filename

        try:
            self.status_message("Reading file {}", self.filename)
            self.parse_file(buf)
            self.status_message("Processing file {}", self.filename)
            ret = self._process()
            self.status_message("File {} converted", self.filename)
            return ret
        except sweattrails.device.exceptions.SessionExistsError:
            raise
        except Exception as exception:
            logger.exception("Exception parsing FIT file")
            raise sweattrails.device.exceptions.FileImportError(exception)

    def _process(self):
        # Collect all laps and records with the sessions they
        # belong with:
        for l in self.laps:
            s = self.find_activity_for_obj(l)
            if s:
                s.add_lap(l)
        for r in self.trackpoints:
            s = self.find_activity_for_obj(r)
            if s:
                s.add_trackpoint(r)

        # Create ST sessions and convert everything:
        ret = []
        for s in self.activities:
            with gripe.db.Tx.begin():
                ret.append(s.convert(self.user))
        return ret

# ---------------------------------------------------------------------------------------------------
