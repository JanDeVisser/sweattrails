import logging
from datetime import datetime
from datetime import time

import Util
from Athlete import CriticalPowerDef
from Model.Config import SessionType
from Session import GeoData
from Session import Interval
from Session import SessionFile
from Session import Waypoint
from xml_processor import XMLProcessor

import gripe
import grumble.geopt
import sweattrails.session

logger = gripe.get_logger("sweattrails.model.session")

class Policy():
    DONT_UPDATE, UPDATE_WITH_OFFSET, UPDATE_ABSOLUTE = range(3)

    def __init__(self, distance, duration):
        self.distance = distance
        self.duration = duration

class CritPower():
    def __init__(self, waypoints, cpdef):
        self.waypoints = waypoints
        self.cpdef = cpdef
        self.duration = cpdef.duration
        self.start = 0
        self.power = None
        self.cur = 0

    def analyze(self, i):
        wp = self.waypoints[i]
        while (self.cur < i) and (wp.seconds - self.waypoints[self.cur].seconds) > self.duration:
            self.cur += 1
        if (wp.seconds - self.waypoints[self.cur].seconds) < self.duration - 1:
            return
        sum_pwr = 0
        for wp in self.waypoints[self.cur+1:i+1]:
            # fixme: sum += wp.power * (timediff with last wp)
            sum_pwr += wp.power if wp.power else 0
        avg = int(round(sum_pwr / self.duration))
        if avg > self.power:
            self.power = avg
            self.start = self.cur

    def persist(self, interval):
        cp = sweattrails.session.CriticalPower(parent = interval)
        cp.interval = interval
        cp.cpdef = self.cpdef
        cp.start_time = self.waypoints[self.start].timestamp
        cp.power = self.power
        cp.put()

class Analysis:
    def __init__(self, container):
        self.container = container
        self.waypoints = container.waypoints
        self.athlete = container.interval.get_athlete()
        self.max_power = 0
        self.avg_power = 0
        self.normalized_power = 0
        self.max_torque = 0.0
        self.avg_torque = 0.0
        self.max_cadence = 0
        self.avg_cadence = 0
        self.max_speed = 0.0
        self.avg_speed = 0.0
        self.max_hr = 0
        self.avg_hr = 0
        self.max_lat = None
        self.min_lat = None
        self.max_lon = None
        self.min_lon = None
        self.cur_altitude = None
        self.min_elev = None
        self.max_elev = None
        self.elev_gain = None
        self.elev_loss = None
        self.critical_power = []
        if container.type == 'bike':
            for cpdef in CriticalPowerDef.query(ancestor = self.athlete):
                if cpdef.duration <= Util.time_to_seconds(container.interval.duration):
                    self.critical_power.append(CritPower(self.waypoints, cpdef))

    def analyze(self):
        sum_power = 0
        sum_torque = 0.0
        sum_cadence = 0
        sum_hr = 0
        sum_norm = 0
        start_30 = 0
        l = len(self.waypoints)
        rolling30_count = 0
        for i in range(l):
            for cp in self.critical_power:
                cp.analyze(i)
            wp = self.waypoints[i]
            # fixme: sum_power += wp.power * (timediff with last wp)
            sum_power += wp.power if wp.power else 0
            self.max_power = max(self.max_power, wp.power)
            sum_torque += wp.torque if wp.torque else 0.0
            self.max_torque = max(self.max_torque, wp.torque)
            sum_cadence += wp.cadence if wp.cadence else 0
            self.max_cadence = max(self.max_cadence, wp.cadence)
            sum_hr += wp.heartrate if wp.heartrate else 0
            self.max_hr = max(self.max_hr, wp.heartrate)
            self.max_speed = max(self.max_speed, wp.speed)
            self.geo_analysis(wp)
            if (self.container.type == 'bike') and (wp.seconds >= 30):
                while (wp.seconds - self.waypoints[start_30].seconds) > 30:
                    start_30 += 1
                    wp_start30 = self.waypoints[start_30]
                    diff_30 = wp.seconds - wp_start30.seconds
                    if diff_30 >= 29:
                        sum_30 = 0
                        prev_wp = None
                        for wp30 in self.waypoints[start_30+1:i+1]:
                            td = (wp30.seconds - prev_wp.seconds) if prev_wp else 1
                            sum_30 += (wp30.power * td) if wp30.power else 0
                            prev_wp = wp30
                            sum_norm += (round(sum_30/diff_30))**4
                            rolling30_count += 1
        timediff = self.waypoints[l-1].seconds - self.waypoints[0].seconds
        if self.container.type == 'bike':
            self.avg_power = int(round(sum_power / timediff))
            self.normalized_power = int(round((sum_norm/rolling30_count)**(0.25)))
            self.avg_torque = round(sum_torque / timediff, 3)
            ftp = self.container.get_ftp()
            if self.normalized_power > 0:
                self.vi = round(self.normalized_power / self.avg_power, 2)
                self.intensity_factor = round(self.normalized_power / ftp, 2) if ftp > 0 else 0
                self.tss = (timediff * self.intensity_factor**2)/36 if ftp > 0 else 0
        self.avg_cadence = int(round(sum_cadence / timediff))
        self.avg_hr = int(round(sum_hr / timediff))
        self.avg_speed = wp.distance / timediff

    def geo_analysis(self, wp):
        alt = wp.elevation
        if alt:
            if self.cur_altitude is not None:
                if alt > self.cur_altitude:
                    self.elev_gain += (alt - self.cur_altitude)
                else:
                    self.elev_loss += (self.cur_altitude - alt)
            else:
                self.elev_gain = 0
                self.elev_loss = 0
            self.min_elev = min(self.min_elev, alt)
            self.max_elev = min(self.max_elev, alt)
            self.cur_altitude = alt
        if wp.location:
            lat = wp.location.lat
            lon = wp.location.lon
            self.max_lat = max(self.max_lat, lat)
            self.min_lat = min(self.min_lat if self.min_lat else 200, lat)
            self.max_lon = max(self.max_lon, lon)
            self.min_lon = min(self.min_lon if self.min_lon else 200, lon)

    def persist(self):
        interval = self.container.interval
        interval.type = self.container.type
        if self.container.type == 'bike':
            interval.average_power = self.avg_power
            interval.normalized_power = self.normalized_power
            interval.max_power = self.max_power
            interval.average_torque = self.avg_torque
            interval.max_torque = self.max_torque
            interval.vi = self.vi
            interval.intensity_factor = self.intensity_factor
            interval.tss = self.tss
        interval.average_cadence = self.avg_cadence
        interval.max_cadence = self.max_cadence
        interval.max_speed = self.max_speed
        interval.put()
        for c in self.critical_power:
            c.persist(interval)
        if self.max_lat or (self.elev_gain and self.elev_gain > 0):
            geodata = GeoData(parent=interval)
            geodata.interval = interval
            if self.elev_gain:
                geodata.max_elev = self.max_elev
                geodata.min_elev = self.min_elev
                geodata.elev_gain = self.elev_gain
                geodata.elev_loss = self.elev_loss
            if self.max_lat:
                geodata.bounding_box = grumble.geopt.GeoBox(
                    self.min_lat, self.min_lon, self.max_lat, self.max_lon)
            geodata.put()

class IntervalContainer():
    def __init__(self, interval, parent = None):
        self.parent = parent
        self.waypoints = []
        self.interval = interval
        self.min_lat = 200
        self.max_lat = -200
        self.min_lon = 100
        self.max_lon = -100
        self.cur_altitude = None
        self.policy = Policy(Policy.UPDATE_ABSOLUTE, Policy.UPDATE_ABSOLUTE)
        self.distance_offset = 0
        self.start_time = None
        self.ftp = 0
        if parent is None:
            self.seqnr = 0
            self.root = self
            self.session = self.interval.parent()
        else:
            self.seqnr = self.parent.seqnr
            self.type = parent.type
            self.root = parent.root
            self.session = parent.session
        self.wp = None
        self.current = None

    def get_ftp(self):
        if self.ftp:
            return self.ftp
        elif self.parent:
            return self.parent.get_ftp()
        else:
            self.ftp = self.session.get_athlete().get_ftp(self.session.session_start)
            return self.ftp

    def close(self):
        if self.wp is not None:
            self.wp.put()
        if self.interval is not None:
            self.interval.put()
            a = Analysis(container = self)
            a.analyze()
            a.persist()
        if self.parent is not None:
            self.parent.current = None
            self.parent.seqnr = self.seqnr
        return self.parent

    def timestamp(self, value):
        self.ts = value
        if self.policy.duration == Policy.DONT_UPDATE:
            pass
        elif self.policy.duration == Policy.UPDATE_WITH_OFFSET:
            self.interval.duration = Util.time_after_offset(value, self.start_time)
        elif self.policy.duration == Policy.UPDATE_ABSOLUTE:
            self.interval.duration = value
        if self.parent:
            self.parent.timestamp(value)

    def distance(self, value):
        v = int(round(float(value)))
        if self.policy.distance == Policy.DONT_UPDATE:
            pass
        elif self.policy.distance == Policy.UPDATE_WITH_OFFSET:
            self.interval.distance = self.distance_offset + v
        elif self.policy.distance == Policy.UPDATE_ABSOLUTE:
            self.interval.distance = v
        if not self.current:
            self.wpt().distance = self.interval.distance
        if self.parent:
            self.parent.distance(value)

    def work(self, value):
        self.interval.work = int(round(float(value)))

    def speed(self, value):
        self.wpt().speed = float(value)

    def power(self, value):
        self.wpt().power = int(round(float(value)))

    def torque(self, v):
        self.wpt().torque = float(v)

    def heartrate(self, v):
        self.wpt().heartrate = int(round(float(v)))

    def cadence(self, v):
        self.wpt().cadence = int(round(float(v)))

    def elevation(self, value):
        self.wpt().elevation = int(round(float(value)))

    def location(self, latval, lonval):
        lat = float(latval)
        lon = float(lonval)
        if lat != 0.0 and lon != 0.0:
            self.wpt().location = GeoPt(lat, lon)

    def new_wp(self):
        if self.wp is not None:
            self.wp.put()
        self.wp = None

    def wpt(self):
        if self.wp is None:
            self.wp = Waypoint(parent=self.interval)
            self.wp.seqnr = self.seqnr
            self.wp.interval = self.interval
            self.seqnr += 1
            self.interval.num_waypoints = self.seqnr
            if self.ts is not None:
                self.wp.timestamp = self.ts
                self.wp.seconds = Util.time_to_seconds(self.ts)
        return self.wp

    def close_wp(self):
        if self.wp is not None:
            self.wp.put()
            self.wp = None

class FileParser():
    def __init__(self):
        self.current = None

    def initialize(self, datafile):
        self.datafile = datafile
        self.athlete = datafile.athlete
        self.session = sweattrails.session.Session(parent=self.athlete)
        self.session.athlete = self.athlete.user
        self.session.description = self.datafile.description
        self.session.session_start = self.datafile.session_start
        self.initialize_session()
        self.session.put()
        self.new_interval()
        self.root = self.current
        self.sessionkey = self.session.key()
        if self.athlete.uploads is None:
            self.athlete.uploads = 1
        else:
            self.athlete.uploads += 1
        self.athlete.last_upload = datetime.now()
        self.athlete.put()
        self.data = ""
        df = datafile
        while df:
            self.data += df.data
            df = df.next

    def initialize_session(self):
        pass

    def prepare(self):
        pass

    def process(self):
        self.process_data()
        while self.current is not None:
            self.close_interval()
        self.session.put()
        return self.session.key()

    def new_interval(self, sub = False, policy = None, offset = None, start_time = None, interval_id = None):
        if self.current is None:
            p = self.session
            policy = Policy(Policy.UPDATE_ABSOLUTE, Policy.UPDATE_ABSOLUTE)
            offset = 0
            lap = 0
            start_time = time.min
            part_of = None
        else:
            if policy is None:
                policy = self.current.policy
            if offset is None:
                offset = self.current.interval.distance
            if start_time is None:
                start_time = self.current.interval.duration
            if sub:
                p = self.current.interval
                lap = 0
            else:
                p = self.current.interval.parent()
                lap = self.current.interval.interval_id + 1
                self.close_interval()
                part_of = p
                p.num_intervals += 1
        if interval_id is not None:
            interval_id = lap
        interval = Interval(parent=p)
        interval.interval_id = interval_id
        interval.part_of = part_of
            interval.duration = Util.seconds_to_time(0)
            interval.distance = 0
        interval.put()
        if p == self.session:
            self.session.interval = interval
            self.session.put()
        # self.current points to parent, even if sub, because of close_interval
            container = IntervalContainer(interval, self.current)
            container.policy = policy
            container.distance_offset = offset
        container.start_time = Util.time_to_seconds(start_time)

        # self.current points to parent, even if sub, because of close_interval
        if self.current is not None:
            self.current.current = container
        self.current = container

    def close_interval(self):
        self.current = self.current.close()

    def new_wp(self, ts = None):
        self.current.timestamp(ts)
        return self.current.new_wp()

    def distance(self, value):
        self.current.distance(value)

    def speed(self, value):
        self.current.speed(value)

    def work(self, value):
        self.current.work(value)

    def power(self, value):
        self.current.power(value)

    def torque(self, value):
        self.current.torque(value)

    def heartrate(self, value):
        self.current.heartrate(value)

    def cadence(self, value):
        self.current.cadence(value)

    def elevation(self, value):
        self.current.elevation(value)

    def location(self, latval, lonval):
        self.current.location(latval, lonval)

    def process_data(self):
        logging.info(" --- FileParser process_data() ---")
        return None

    def lap(self):
        return self.current.interval.interval_id

    def set_sessiontype(self, sessiontype):
        self.session.sessiontype = SessionType.get_sessiontype(self.athlete, sessiontype)
        self.current.type = self.session.sessiontype.get_basetype()

class CSVParser(FileParser):
    def process_data(self):
        self.seqnr = 0
        self.duration = 0
        self.mode = 0

        self.set_sessiontype("Road ride")
        self.new_interval(True, Policy(Policy.UPDATE_WITH_OFFSET, Policy.UPDATE_ABSOLUTE), 0, time.min, 0)
        state = 0
        self.lines = self.data.splitlines()
        self.data = None
        for line in self.lines:
            if line.strip() == '':
            continue
            if state == 0:
            if line.startswith("Version,"):
                logging.info("Detected extended CSV file")
                state = 1
            elif line.startswith("User Name,"):
                state = 2
            elif line.startswith("Minutes"):
                state = 3
                self.headers = line.split(",")
                if len(self.headers) == 9:
                self.mode = 1
                elif len(self.headers) == 18:
                self.mode = 2
            elif state == 1:
            # Version,Date/Time,Km,Minutes,RPE,Tags,"Weight, kg","Work, kJ",FTP,"Sample Rate, s",Device Type,Firmware Version,Last Updated,Category 1,Category 2
            # 6,2011-11-08 11:13:06,13.947,31.23572,0,,72.575,373,230,1,PowerTap+ (ANT+),7.6,2011-11-08 11:56:25,0,0
            logging.info("Parsing header line")
            data = line.split(",")
            self.session.session_start = datetime.strptime(data[1], "%Y-%m-%d %H:%M:%S")
            self.work(int(data[7]))
            self.session.device = data[10]
            logging.info("Header line parsed")
            state = 0
            elif state == 2:
            # User Name,Power Zone 1,Power Zone 2,Power Zone 3,Power Zone 4,Power Zone 5,Power Zone 6,HR Zone 1,HR Zone 2,HR Zone 3,HR Zone 4,HR Zone 5,Calc Power A,Calc Power B,Calc Power C
            # < user name >,0,0,0,0,0,0,150,160,170,180,250,0,0,0
            # Ignored for now
            state = 0
            elif state == 3:
            self.importLine(line)

    def importLine(self, line):
        # Mode = 0: Minutes, Torq (N-m),Km/h,Watts,Km,Cadence,Hrate,ID
        # Mode = 1: Minutes, Torq (N-m),Km/h,Watts,Km,Cadence,Hrate,ID,Altitude
        # Mode = 2: Minutes, Torq (N-m),Km/h,Watts,Km,Cadence,Hrate,ID,Altitude (m),
        #        Temperature (C),"Grade, %",Latitude,Longitude,Power Calc'd,
        #        Calc Power,Right Pedal,Pedal Power %,Cad. Smooth
        logging.info("Line ------> " + line)
        data = line.split(",")

        # Disregard datapoints where we're standing still and not pedaling:
        #if ((int(data[3]) == 0) and (float(data[2]) < 1) and (int(data[5]) == 0)):
        #    return

        lap = int(data[7])
        if  lap > self.lap():
            self.new_interval(False,
            Policy(Policy.UPDATE_WITH_OFFSET, Policy.UPDATE_WITH_OFFSET),
            - self.root.interval.distance, self.root.interval.duration, lap)
        seconds = int(round(float(data[0]) * 60))
        ts = Util.seconds_to_time(seconds)
        self.new_wp(ts)

        self.speed(float(data[2]) / 3.600)
        self.distance(int(round(float(data[4]) * 1000)))
        self.power(data[3])
        self.torque(data[1])
        self.heartrate(data[6])
        self.cadence(data[5])
        if self.mode > 0:
            self.elevation(data[8])
        if self.mode == 2:
            self.location(data[11], data[12])


tcx_parser = XMLProcessor()

@tcx_parser.for_start_of("Activities/Activity$")
def txc_initialize(context):
    context.lap = 0

@tcx_parser.for_text_of("Activity/@Sport")
def txc_set_sessiontype(context, sport):
    if sport == 'Running':
        sessiontype = "Run"
    elif sport == 'Biking':
        sessiontype = "Road ride"
    context.set_sessiontype(sessiontype)

@tcx_parser.for_text_of("Activity/Id")
def tcx_set_session_start(context, datestr):
    if not(datestr.endswith("Z")):
        datestr += "Z"
    context.session.session_start = datetime.strptime(datestr, "%Y-%m-%dT%H:%M:%S.%fZ")

@tcx_parser.for_start_of("Activity/Lap$")
def tcx_start_interval(context):
    context.new_interval(context.lap == 0,
        Policy(Policy.UPDATE_WITH_OFFSET, Policy.UPDATE_WITH_OFFSET),
        - context.root.interval.distance, context.root.interval.duration, context.lap)
    context.lap += 1

@tcx_parser.for_text_of("Activity/Lap/Calories")
def tcx_set_work(context, work):
    context.work(work)

@tcx_parser.for_text_of("Activity/Lap/Track/Trackpoint/Time")
def tcx_set_time(context, tstr):
    if not(tstr.endswith("Z")):
        tstr += "Z"
    try:
        t = datetime.strptime(tstr, "%Y-%m-%dT%H:%M:%S.%fZ")
    except:
        t = datetime.strptime(tstr, "%Y-%m-%dT%H:%M:%SZ")
    delta = t - context.session.session_start
    context.new_wp(Util.seconds_to_time(delta.total_seconds()))

@tcx_parser.for_text_of("Activity/Lap/Track/Trackpoint/AltitudeMeters")
def tcx_set_altitude(context, alt):
    context.elevation(alt)

@tcx_parser.for_text_of("Activity/Lap/Track/Trackpoint/DistanceMeters")
def tcx_set_distance(context, distance):
    context.distance(distance)

@tcx_parser.for_text_of("Activity/Lap/Track/Trackpoint/Cadence")
def tcx_set_cadence(context, cadence):
    context.cadence(cadence)

@tcx_parser.for_text_of("Activity/Lap/Track/Trackpoint/HeartRateBpm/Value")
def tcx_set_heartrate(context, hr):
    context.heartrate(hr)

@tcx_parser.for_text_of("Activity/Lap/Track/Trackpoint/Position/LatitudeDegrees")
def tcx_set_latitude(context, lat):
    context.lat = float(lat)

@tcx_parser.for_text_of("Activity/Lap/Track/Trackpoint/Position/LongitudeDegrees")
def tcx_set_longitude(context, lon):
    context.lon = float(lon)

@tcx_parser.for_end_of("Activity/Lap/Track/Trackpoint/Position$")
def tcx_set_location(context):
    context.location(context.lat, context.lon)

@tcx_parser.for_text_of("Activity/Lap/Track/Trackpoint/Extensions/ns3:TPX/ns3:Speed")
def tcx_set_speed(context, speed):
    context.speed(speed)

@tcx_parser.for_text_of("Activity/Lap/Track/Trackpoint/Extensions/ns3:TPX/ns3:Watts")
def tcx_set_power(context, power):
    context.power(power)

class TCXParser(FileParser):
    def process_data(self):
        tcx_parser.process(self.data, self)
        return self.sessionkey

def parse(key):
    datafile = SessionFile.get(key)
    if (datafile is None):
        logging.info("No SessionFile found for key %s", key)
        return None
    else:
    parser = None
    if datafile.filetype == 'CSV':
        parser = CSVParser()
    elif datafile.filetype == 'TCX':
        parser = TCXParser()
    if parser is not None:
        parser.initialize(datafile)
        parser.prepare()
        ret = parser.process()
        if ret is not None:
                while datafile:
                    next = datafile.next
                    datafile.delete()
                    datafile = next
        return ret
        else:
        return None
