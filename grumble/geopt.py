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

from math import cos
from math import sin
from math import pow
from math import sqrt
from math import atan2
from math import radians
import re

import gripe.db
import grumble.property


class GeoPt(object):
    def __init__(self, *args):
        self.lat = None
        self.lon = None
        if args:
            self._build(*args)

    def _build(self, *args):
        if ((len(args) == 1) and args[0]) or ((len(args) == 2) and (args[1] is None)):
            a = args[0]
            assert not(isinstance(a, (float, int))), "GeoPt: cannot build GeoPt with only one number"
            if isinstance(a, GeoPt):
                self.lat = a.lat
                self.lon = a.lon
            elif isinstance(a, tuple) or isinstance(a, list):
                assert len(a) <= 2, "GeoPt: Cannot build GeoPt from a sequence longer than 2 elements"
                self._build(*a)
            elif isinstance(a, dict):
                self._build(a.get("lat", None), a.get("lon", None))
            elif isinstance(a, str):
                coords = self._parse(a)
                if coords:
                    self._assign(*coords)
            else:
                assert 0, "GeoPt: Cannot build GeoPt from %s, a %s" % (a, type(a))
        elif len(args) == 2:
            self._assign(args[0], args[1])

    @staticmethod
    def _float(coord, direction):
        if coord is None:
            return None
        elif isinstance(coord, (list, tuple)):
            assert coord and len(coord) == 3, "Cannot convert a coordinate tuple '%s' to a float" % coord
            deg = int(coord[0])
            frac = (-1 if deg < 0 else 1) * ((int(coord[1]) / 60.0) + (float(coord[2]) / 3600.0))
            return deg + frac
        else:
            try:
                return float(coord)
            except ValueError:
                return GeoPt._parse_coord(str(coord), direction)

    def _assign(self, latitude=0, longitude=0):
        lat = self._float(latitude, "NnSs")
        lon = self._float(longitude, "EeWw")
        if lat is None or lon is None:
            lat = lon = None
        else:
            assert (-90 <= lat <= 90), "GeoPt: Latitude must be between -90 and +90"
            if not (-180 < lon <= 180):
                """
                    If the absolute value of the longitude value is larger than 
                    180, it is converted as if it was wrapped around and a value 
                    between -180 and 180:
                """
                a = abs(lon)
                s = a / lon if lon else 1
                if abs(lon) == 180:
                    lon = 180
                elif a > 180:
                    lon = (lon + 360) % s * 360
                    lon = lon if lon <= 180 else (lon + (-s) * 360)
        self.lat = lat
        self.lon = lon

    @staticmethod
    def _parse(s):
        s = s.strip()
        if s:
            sw = s.startswith("(")
            ew = s.endswith(")")
            assert (sw and ew) or (not(sw or ew)), "Unbalanced parenthesis in GeoPt string '%s'" % s
            if sw and ew:
                s = s[1:-1].strip()
            if s:
                parts = s.split(",", 2)
                if len(parts) == 2:
                    return GeoPt._parse_coord(parts[0], "NnSs"), GeoPt._parse_coord(parts[1], "EeWw")
        return None

    @staticmethod
    def _parse_coord(g, direction):
        g = g.strip()
        if g:
            try:
                return float(g)
            except ValueError:
                m = re.match(r"(-?[\d]+)\*?\s*([\d]+)\'?\s*([\d.]+)\"?\s*([%s]?)" % direction, g)
                if m:
                    deg = int(m.group(1))
                    frac = (int(m.group(2)) / 60.0) + (float(m.group(3)) / 3600.0)
                    return (-1 if m.group(4) and m.group(4) in "WwSs" else 1) * (deg + frac)
        return None

    def __repr__(self):
        if self:
            return '(%s, %s)' % self.tuple()
        else:
            return ""

    def __str__(self):
        if self:
            return "(%s, %s)" % (self._string(self.lat, False), self._string(self.lon, True))
        else:
            return ""

    @staticmethod
    def _sign(num):
        return 1 if num >= 0 else -1

    @staticmethod
    def _string(coord, lon=False):
        (deg, minute, sec) = GeoPt._degrees(coord)
        return "%s* %s' %s\" %s" % (abs(deg), minute, sec, "WS NE"[(GeoPt._sign(deg) * (2 if lon else 1)) + 2]) \
            if coord is not None \
            else ""

    def __nonzero__(self):
        return self.lat is not None

    def __eq__(self, other):
        """
            Determines if this GeoPt is the same as another. Two points are
            assumed identical if their distance according to the great-circle
            algorithm is less than 10m.
        """
        if self:
            if not other:
                return False
            else:
                return self.distance(other) < 10
        else:
            return not other

    def tuple(self):
        return (self.lat, self.lon) if self else ()

    @staticmethod
    def _degrees(coord):
        """
            Convert the float coord value to degrees, minutes, and seconds. 
            Returns a 3-element tuple where the first element is the degrees
            value (which is the integer part of the coord value), the second
            element is the minute value (an integer 1/60th of a degree), and
            the third is the second value (1/60 of a minute, 1/3600 of a degree.
            This is a float value).
        """
        if coord is None:
            return ()
        else:
            c = abs(coord)
            deg = int(c)
            c = c - deg
            deg = -deg if coord < 0 else deg
            minute = int(c * 60)
            c = c - (minute / 60.0)
            sec = c * 3600
            return deg, minute, sec

    def degrees(self):
        return self._degrees(self.lat), self._degrees(self.lon)

    def distance(self, other):
        """
            Returns the distance in meters between this GeoPt object and another
            The distance is calculated as the great-circle distance using 
            the Vincenty Formula, with r = 6371km.
            see http://en.wikipedia.org/wiki/Great-circle_distance
        """
        if not(self or other):
            return None
        phi1 = radians(self.lat)
        phi2 = radians(other.lat)
        dlambda = radians(abs(self.lon - other.lon))
        y = sqrt(
            pow(cos(phi2) * sin(dlambda), 2) +
            pow(cos(phi1) * sin(phi2) - sin(phi1) * cos(phi2) * cos(dlambda), 2))
        x = sin(phi1) * sin(phi2) + cos(phi1) * cos(phi2) * cos(dlambda)
        earth_radius_m = 6371000
        return atan2(y, x) * earth_radius_m

    def to_dict(self):
        return {"lat": self.lat, "lon": self.lon} if self else {}

    @classmethod
    def from_dict(cls, d):
        return GeoPt(d)


class GeoBox(object):
    def __init__(self, *args):
        self._sw = None
        self._ne = None
        if args:
            self._assign(*args)

    def _assign(self, *args):
        assert (len(args) <= 4), "Illegal GeoBox constructor args '%s'" % args
        if args[0] is None:
            return
        elif isinstance(args[0], GeoPt):
            self._sw = GeoPt(args[0])
            self._ne = GeoPt(self._sw)
            state = 1
        elif isinstance(args[0], GeoBox):
            self._sw = GeoPt(args[0].sw())
            self._ne = GeoPt(args[0].ne())
            state = 0
        elif isinstance(args[0], dict):
            d = args[0]
            sw = d.get("sw", None)
            ne = d.get("ne", None)
            if sw is not None and ne is not None:
                self._sw = GeoPt(sw)
                self._ne = GeoPt(ne)
            state = 0
        elif isinstance(args[0], str):
            s = args[0].strip()
            if s:
                pts = self._parse(s)
                if pts:
                    self._assign(*pts)
                    state = 0
                else:
                    self._sw = GeoPt(args[0])
                    self._ne = GeoPt(self._sw)
                    state = 1
            else:
                state = 0
        else:
            assert len(args) > 1, "Illegal GeoBox constructor args '%s'" % args
            self._sw = GeoPt(args[0], args[1])
            self._ne = GeoPt(self._sw)
            state = 2
        if (len(args) > state) and (state > 0):
            if isinstance(args[state], (GeoPt, str)):
                ne = GeoPt(args[state])
            else:
                assert len(args) > state + 1, "Illegal GeoBox constructor args '%s'" % args
                ne = GeoPt(args[state], args[state + 1])
            self.extend(ne)

    @staticmethod
    def _parse(s):
        s = s.strip()
        if not s or GeoPt._parse(s):
            return None
        else:
            while s.startswith("("):
                s = s[1:].strip()
            while s.endswith(")"):
                s = s[:-1].strip()
            parts = s.split(",", 4)
            assert len(parts) == 4
            for ix in range(0, 3):
                c = parts[ix]
                c = c.strip()
                c = c[1:] if c.startswith("(") else c
                c = c[:-1] if c.endswith(")") else c
                parts[ix] = c
            return GeoPt(parts[0], parts[1]), GeoPt(parts[2], parts[3])

    def sw(self):
        return GeoPt(self._sw) if self else None

    def ne(self):
        return GeoPt(self._ne) if self else None

    def extend(self, *point):
        point = GeoPt(*point)
        if point:
            if self:
                if self._sw.lat > point.lat:
                    self._sw.lat = point.lat
                if self._sw.lon > point.lon:
                    self._sw.lon = point.lon
                if self._ne.lat < point.lat:
                    self._ne.lat = point.lat
                if self._ne.lon < point.lon:
                    self._ne.lon = point.lon
            else:
                self._sw = GeoPt(point)
                self._ne = GeoPt(point)
        return self

    def union(self, *other):
        other = GeoBox(*other)
        self.extend(other.sw())
        self.extend(other.ne())
        return self

    def contains(self, *point):
        point = GeoPt(*point)
        if not point:
            return False
        else:
            return (self.sw().lat <= point.lat <= self.ne().lat and
                    self.sw().lon <= point.lon <= self.ne().lon)

    def intersects(self, *other):
        """
            Check if two boxes collide.
        """
        other = GeoBox(*other)
        if not(self and other):
            return False
        else:
            return not(
                self.sw().lon > other.ne().lon or
                self.ne().lon < other.sw().lon or
                self.sw().lat > other.ne().lat or
                self.ne().lat < other.sw().lat)

    def span(self):
        return GeoPt(
            self.ne().lat - self.sw().lat,
            self.ne().lon - self.sw().lon) if self else None

    def __repr__(self):
        if self:
            return '(%r, %r)' % self.tuple()
        else:
            return ""

    def __str__(self):
        if self:
            return "(%s, %s)" % self.tuple()
        else:
            return ""

    def __eq__(self, *other):
        try:
            bx = GeoBox(*other)
            if self:
                return ((self.sw() == bx.sw()) and (self.ne() == bx.ne())
                        if other else False)
            else:
                return not(bool(bx))
        except ValueError:
            return False

    def __nonzero__(self):
        return bool(self._sw)

    def tuple(self):
        return (self._sw, self._ne) if self else ()

    def to_dict(self):
        if self:
            return {"sw": self._sw.to_dict(), "ne": self._ne.to_dict()}
        else:
            return {}

    @classmethod
    def from_dict(cls, d):
        return GeoBox(d)


class GeoPtProperty(grumble.property.ModelProperty):
    datatype = GeoPt
    sqltype = "point"


class GeoBoxProperty(grumble.property.ModelProperty):
    datatype = GeoBox
    sqltype = "box"


if gripe.db.Tx.database_type == "postgresql":
    import psycopg2.extensions
    
    #
    # psycopg2 machinery to cast pgsql datatypes to ours and vice versa.
    #

    def adapt_point(geopt):
        return psycopg2.extensions.AsIs("'%r'" % geopt)
    
    def adapt_box(geobox):
        return psycopg2.extensions.AsIs("'%r'" % geobox)
    
    psycopg2.extensions.register_adapter(GeoPt, adapt_point)
    psycopg2.extensions.register_adapter(GeoBox, adapt_box)
    
    def cast_point(value, cursor):
        try:
            return GeoPt(value) if value is not None else None
        except ValueError:
            raise psycopg2.InterfaceError("bad point representation: %r" % value)
    
    def cast_box(value, cursor):
        try:
            return GeoBox(value) if value is not None else None
        except ValueError:
            raise psycopg2.InterfaceError("bad box representation: %r" % value)
    
    with gripe.db.Tx.begin() as tx:
        cur = tx.get_cursor()
        cur.execute("SELECT NULL::point, NULL::box")
        point_oid = cur.description[0][1]
        box_oid = cur.description[1][1]
    
    POINT = psycopg2.extensions.new_type((point_oid,), "POINT", cast_point)
    BOX = psycopg2.extensions.new_type((box_oid,), "BOX", cast_box)
    psycopg2.extensions.register_type(POINT, None)
    psycopg2.extensions.register_type(BOX, None)
    
    
elif gripe.db.Tx.database_type == "sqlite3":
    import sqlite3
    
    def adapt_point(geopt):
        return repr(geopt)
    
    def adapt_box(geobox):
        return repr(geobox)
    
    def convert_point(value):
        return GeoPt(value) if value is not None else None
    
    def convert_box(value):
        return GeoBox(value) if value is not None else None
    
    sqlite3.register_adapter(GeoPt, adapt_point)
    sqlite3.register_converter("point", convert_point)
    sqlite3.register_adapter(GeoBox, adapt_box)
    sqlite3.register_converter("box", convert_box)


if __name__ == "__main__":
    dunbar = (43.452601, -80.521909)
    dunbar_deg = ((43, 27, 9.3636), (-80, 31, 18.8724))
    avondale = (43.452614, -80.520625)
    thornhill = (43.659435, -79.489647)
    greenwich = (51.5032432, 0)

    print("GeoPt()", GeoPt())
    dunbar_pt = GeoPt(dunbar[0], dunbar[1])
    print("tuple", dunbar)
    print("repr", repr(dunbar_pt))
    print("str", str(dunbar_pt))
    print("GeoPt(float, float)", dunbar_pt)
    print("repr(GeoPt(float, float))", repr(dunbar_pt))
    print("GeoPt(tuple)", GeoPt(dunbar))
    print("GeoPt(*tuple)", GeoPt(*dunbar))
    print("GeoPt(degrees)", GeoPt(dunbar_deg))
    print("GeoPt(*degrees)", GeoPt(*dunbar_deg))
    print("GeoPt(float, degrees)", GeoPt(dunbar[0], dunbar_deg[1]))
    print("GeoPt(degrees, float)", GeoPt(dunbar_deg[0], dunbar[1]))

    print("GeoPt(str, str)", GeoPt(str(dunbar[0]), str(dunbar[1])))
    print("GeoPt(str, float)", GeoPt(str(dunbar[0]), dunbar[1]))
    print("GeoPt(float, str)", GeoPt(dunbar[0], str(dunbar[1])))
    print("GeoPt(GeoPt)", GeoPt(dunbar_pt))
    print("GeoPt(repr())", GeoPt(repr(dunbar_pt)))
    print("GeoPt(str())", GeoPt(str(dunbar_pt)))
    print("GeoPt(' %s, %s ')", GeoPt("  %s   , %s   " % (dunbar_pt.lat, dunbar_pt.lon)))

    avondale_pt = GeoPt(avondale)
    print("Dunbar -> Avondale", dunbar_pt.distance(avondale_pt))
    thornhill_pt = GeoPt(thornhill)
    print("Dunbar -> Thornhill", dunbar_pt.distance(thornhill_pt) / 1000)
    print("Dunbar -> Greenwich", dunbar_pt.distance(GeoPt(greenwich)) / 1000)

    print(GeoBox())
    box = GeoBox(avondale_pt, dunbar_pt)
    print(box)
    print(GeoBox(avondale[0], avondale[1], dunbar_pt))
    print(GeoBox(avondale_pt, dunbar[0], dunbar[1]))
    print(GeoBox(avondale[0], avondale[1], dunbar[0], dunbar[1]))
    print(GeoBox(""))
    print(GeoBox("( %r , %r )" % (avondale_pt, dunbar_pt)))
    print(GeoBox("( %s , %s )" % (avondale_pt, dunbar_pt)))
    print(GeoBox("%r , %r" % (avondale_pt, dunbar_pt)))
    print(GeoBox("%s , %s" % (avondale_pt, dunbar_pt)))
    print(GeoBox(avondale_pt, repr(dunbar_pt)))
    print(GeoBox(repr(avondale_pt), dunbar_pt))
    print(GeoBox(repr(avondale_pt), repr(dunbar_pt)))
    print(GeoBox(avondale_pt, str(dunbar_pt)))
    print(GeoBox(str(avondale_pt), dunbar_pt))
    print(GeoBox(str(avondale_pt), str(dunbar_pt)))
    print(GeoBox(box))

    with gripe.db.Tx.begin():
        class Test(grumble.Model):
            _flat = True
            label_prop = "loc_label"
            loc_label = grumble.TextProperty(required=True)
            loc = GeoPtProperty()
            box = GeoBoxProperty()

    with gripe.db.Tx.begin():
        jan = Test(loc_label="Jan", loc=dunbar_pt, box=box)
        print("++", jan.loc_label, jan.loc, jan.box)
        jan.put()
        print("+++", jan.id(), jan.keyname(), jan.label(), jan.loc, jan.box)
        k = jan.key()

    with gripe.db.Tx.begin():
        jan = Test.get(k)
        print("++++", jan.id(), jan.keyname(), jan.label(), jan.loc, jan.box)
