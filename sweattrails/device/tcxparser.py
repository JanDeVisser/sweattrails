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

import gripe
import gripe.conversions
import gripe.pyxslt
import sweattrails.device.parser

logger = gripe.get_logger(__name__)

tcx_parser = gripe.pyxslt.XMLProcessor()

@tcx_parser.for_start_of("Activities/Activity$")
def txc_initialize(context):
    context.activity = {"type": "activity", "total_distance": 0, 
                        "total_elapsed_time": 0, "total_timer_time": 0 }

@tcx_parser.for_end_of("Activities/Activity$")
def tcx_commit_activity(context):
    sweattrails.device.parser.DictBridge.create(context, context.activity)

@tcx_parser.for_text_of("Activity/@Sport")
def txc_set_sessiontype(context, sport):
    context.activity["sport"] = sport.lower()

@tcx_parser.for_text_of("Activity/Id")
def tcx_set_session_start(context, datestr):
    if not(datestr.endswith("Z")):
        datestr += "Z"
    context.activity["start_time"] = datetime.datetime.strptime(datestr, "%Y-%m-%dT%H:%M:%S.%fZ")

@tcx_parser.for_start_of("Activity/Lap$")
def tcx_start_interval(context):
    context.lap = {"type": "lap" }

@tcx_parser.for_end_of("Activity/Lap$")
def tcx_commit_interval(context):
    sweattrails.device.parser.DictBridge.create(context, context.lap)

@tcx_parser.for_text_of("Activity/Lap/@StartTime")
def tcx_set_lap_start_time(context, datestr):
    if not(datestr.endswith("Z")):
        datestr += "Z"
    context.lap["start_time"] = datetime.datetime.strptime(datestr, "%Y-%m-%dT%H:%M:%S.%fZ")
    
@tcx_parser.for_text_of("Activity/Lap/TotalTimeSeconds")
def tcx_set_lap_time(context, seconds):
    seconds = int(round(float(seconds)))
    context.lap["total_elapsed_time"] = seconds
    context.lap["total_timer_time"] = seconds
    context.activity["total_elapsed_time"] = context.activity["total_elapsed_time"] + distance
    context.activity["total_timer_time"] = context.activity["total_timer_time"] + distance

@tcx_parser.for_text_of("Activity/Lap/DistanceMeters")
def tcx_set_lap_distance(context, distance):
    distance = float(distance)
    context.lap["total_distance"] = distance
    context.activity["total_distance"] = context.activity["total_distance"] + distance

@tcx_parser.for_text_of("Activity/Lap/Calories")
def tcx_set_work(context, work):
    context.lap["total_calories"] = int(work)

@tcx_parser.for_start_of("Activity/Lap/Track/Trackpoint$")
def tcx_new_trackpoint(context, tstr):
    context.trackpoint = { "type": "trackpoint" }

@tcx_parser.for_end_of("Activity/Lap/Track/Trackpoint$")
def tcx_commit_trackpoint(context):
    sweattrails.device.parser.DictBridge.create(context, context.trackpoint)

@tcx_parser.for_text_of("Activity/Lap/Track/Trackpoint/Time")
def tcx_set_tp_time(context, tstr):
    if not(tstr.endswith("Z")):
        tstr += "Z"
    try:
        t = datetime.datetime.strptime(tstr, "%Y-%m-%dT%H:%M:%S.%fZ")
    except:
        t = datetime.datetime.strptime(tstr, "%Y-%m-%dT%H:%M:%SZ")
    context.trackpoint["timestamp"] = t
    if "timestamp" not in context.activity or t > context.activity["timestamp"]:
        context.activity["timestamp"] = t

@tcx_parser.for_text_of("Activity/Lap/Track/Trackpoint/AltitudeMeters")
def tcx_set_altitude(context, alt):
    context.trackpoint["altitude"] = float(alt)

@tcx_parser.for_text_of("Activity/Lap/Track/Trackpoint/DistanceMeters")
def tcx_set_tp_distance(context, distance):
    context.trackpoint["distance"] = float(distance)

@tcx_parser.for_text_of("Activity/Lap/Track/Trackpoint/Cadence")
def tcx_set_cadence(context, cadence):
    context.trackpoint["cadence"] = int(cadence)

@tcx_parser.for_text_of("Activity/Lap/Track/Trackpoint/HeartRateBpm/Value")
def tcx_set_heartrate(context, hr):
    context.trackpoint["heart_rate"] = int(hr)

@tcx_parser.for_text_of("Activity/Lap/Track/Trackpoint/Position/LatitudeDegrees")
def tcx_set_latitude(context, lat):
    context.trackpoint["position_lat"] = gripe.conversions.degrees_to_semicircles(float(lat))

@tcx_parser.for_text_of("Activity/Lap/Track/Trackpoint/Position/LongitudeDegrees")
def tcx_set_longitude(context, lon):
    context.trackpoint["position_long"] = gripe.conversions.degrees_to_semicircles(float(lat))

@tcx_parser.for_text_of("Activity/Lap/Track/Trackpoint/Extensions/ns3:TPX/ns3:Speed")
def tcx_set_speed(context, speed):
    context.trackpoint["speed"] = float(speed)

@tcx_parser.for_text_of("Activity/Lap/Track/Trackpoint/Extensions/ns3:TPX/ns3:Watts")
def tcx_set_power(context, power):
    context.trackpoint["power"] = int(power)

class TCXParser(sweattrails.device.parser.Parser):
    def parse_file(self):
        tcx_parser.process(self.filename, self)
