#
# Copyright (c) 2012-2014 Jan de Visser (jan@sweattrails.com)
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
import math
import time

def local_date_to_utc(d):
    """Local date to UTC"""
    return datetime.datetime.utcfromtimestamp(time.mktime(d.timetuple()))

def seconds_to_time(secs):
    t = int(secs)
    minutes = int(t // 60);
    seconds = t % 60;
    hours = minutes // 60;
    minutes %= 60;
    return datetime.time(hours, minutes, seconds)

def time_to_seconds(t):
    return t.hour * 3600 + t.minute * 60 + t.second if t else 0

def time_after_offset(t, offset):
    return seconds_to_time(time_to_seconds(t) - offset)

def timedelta_to_string(td):
    h = int(math.floor(td.seconds / 3600))
    r = td.seconds - (h * 3600)
    m = int(math.floor(r / 60))
    s = r % 60
    if h > 0:
        return "%dh %02d'%02d\"" % (h, m, s)
    else:
        return "%d'%02d\"" % (m, s)


def semicircle_to_degrees(semicircles):
    """Convert a number in semicircles to degrees"""
    return semicircles * (180.0 / 2.0 ** 31)

def degrees_to_semicircles(degrees):
    """Convert a number in degrees to semicircles"""
    return degrees * (2.0 ** 31 / 180.0)

def ms_to_kmh(ms):
    """Convert a speed in m/s (meters per second) to km/h (kilometers per hour)"""
    return (ms if ms else 0) * 3.6

def ms_to_mph(ms):
    """Convert a speed in m/s (meters per second) to mph (miles per hour)"""
    return (ms if ms else 0) * 2.23693632

def ms_to_minkm(ms):
    """Convert a speed in m/s (meters per second) to a pace in minutes per km"""
    return _pace(ms_to_kmh(ms))

def ms_to_minmile(ms):
    """Convert a speed in m/s (meters per second) to a pace in minutes per mile"""
    return _pace(ms_to_mph(ms))

def _pace(speed):
    if speed > 0:
        p = 60 / speed
        pmin = math.floor(p)
        psec = math.floor((p - pmin) * 60)
        return "%d'%02d\"" % (pmin, psec)
    else:
        return ""

def km_to_mile(km):
    """Convert a distance in km (kilometers) to miles"""
    return km * 0.621371192

def m_to_ft(m):
    """Convert a measurement in m (meters) to ft (feet)"""
    return m * 3.2808399
