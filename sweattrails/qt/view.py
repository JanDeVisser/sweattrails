'''
Created on Sep 3, 2014

@author: jan
'''

import math

from PyQt5.QtCore import QCoreApplication

import gripe
import grumble.qt.model
import grumble.qt.bridge
import sweattrails.config

logger = gripe.get_logger(__name__)


class TimebasedColumn(object):
    
    @classmethod
    def _seconds_to_string(cls, seconds):
        h = int(math.floor(seconds / 3600))
        r = seconds - (h * 3600)
        m = int(math.floor(r / 60))
        s = r % 60
        if h > 0:
            return "%dh %02d'%02d\"" % (h, m, s)
        else:
            return "%d'%02d\"" % (m, s)


class TimestampColumn(TimebasedColumn, grumble.qt.model.TableColumn):
    def __init__(self, property = "timestamp", **kwargs):
        super(TimestampColumn, self).__init__(property, **kwargs)

    def __call__(self, instance):
        value = getattr(instance, self.name)
        return self._seconds_to_string(value.seconds if value else 0)


class SecondsColumn(TimebasedColumn, grumble.qt.model.TableColumn):
    def __init__(self, property, **kwargs):
        super(SecondsColumn, self).__init__(property, **kwargs)

    def __call__(self, instance):
        return self._seconds_to_string(getattr(instance, self.name))


class PaceSpeedColumn(grumble.qt.model.TableColumn):
    def __init__(self, prop="speed", **kwargs):
        super(PaceSpeedColumn, self).__init__(prop, **kwargs)
        self.what = kwargs.get("what")
        if not self.what:
            interval = kwargs.get("interval")
            session = interval.get_session()
            self.what = session.sessiontype.speedPace
        self.units = kwargs.get("units", QCoreApplication.instance().user.get_part("userprofile").units)
        
    def get_header(self):
        header = super(PaceSpeedColumn, self).get_header()
        if self.what == "Speed":
            suffix = "km/h" if self.units == "metric" else "mph"
        elif self.what == "Pace":
            suffix = "min/km" if self.units == "metric" else "min/mile"
        else:
            suffix = "min/100m" if self.units == "metric" else "min/100yd"
        return "{} ({})".format(header, suffix)
    
    def __call__(self, instance):
        value = self._get_value(instance)
        if self.what == "Speed":
            val = gripe.conversions.ms_to_kmh(value) \
                if self.units == "metric" \
                else gripe.conversions.ms_to_mph(value)
            return "{:.1f}".format(val)
        elif self.what == "Pace":
            return gripe.conversions.ms_to_minkm(value) \
                if self.units == "metric" \
                else gripe.conversions.ms_to_minmile(value)
        else:
            return "0"


class DistanceColumn(grumble.qt.model.TableColumn):
    def __init__(self, property = "distance", **kwargs):
        super(DistanceColumn, self).__init__(property, **kwargs)
        self.units = kwargs.get("units",
            QCoreApplication.instance().user.get_part("userprofile").units)
        
    def get_header(self):
        header = super(DistanceColumn, self).get_header()
        suffix = "km" if self.units == "metric" else "mile"
        return "{} ({})".format(header, suffix)
    
    def __call__(self, instance):
        value = self._get_value(instance)
        d = float(value if value else 0) / 1000.0
        if self.units != "metric":
            d = gripe.conversions.km_to_mile(d)
        if d < 1:
            return "{:.3f}".format(d)
        elif d < 10:
            return "{:.2f}".format(d)
        elif d < 100:
            return "{:.1f}".format(d)
        else:
            return "{:.0f}".format(d)


#----------------------------------------------------------------------------
#  D I S P L A Y  C O N V E R T E R S
#----------------------------------------------------------------------------

class SessionTypeIcon(grumble.qt.bridge.DisplayConverter):
    def __init__(self, bridge):
        super(SessionTypeIcon, self).__init__(bridge)

    def to_display(self, sessiontype, interval):
        icon = sessiontype.icon
        logger.debug("SessionTypeIcon: sessiontype: %s icon %s", sessiontype.name, icon)
        if not icon:
            profile = interval.get_activityprofile()
            node = profile.get_node(sweattrails.config.SessionType, sessiontype.name)
            icon = node.get_root_property("icon")
        if not icon:
            return "image/other.png"
        return icon


class PaceSpeed(grumble.qt.bridge.DisplayConverter):
    def __init__(self, bridge):
        super(PaceSpeed, self).__init__(bridge)
        self.labelprefixes = bridge.config.get("labelprefixes")
        
    def label(self, instance):
        if not instance:
            return True
        else:
            session = instance.get_session()
            what = session.sessiontype.speedPace
            prefix = self.labelprefixes.get(what, self.labelprefixes.get(None, "")) \
                if isinstance(self.labelprefixes, dict) \
                else str(self.labelprefixes)
            return "{prefix} {what}".format(prefix=prefix, what=what)

    def suffix(self, instance):
        if not instance:
            return True
        else:
            session = instance.get_session()
            what = session.sessiontype.speedPace
            units = session.athlete.get_part("userprofile").units
            if what == "Speed":
                return "km/h" if units == "metric" else "mph"
            elif what == "Pace":
                return "min/km" if units == "metric" else "min/mile"
            else:
                return "min/100m" if units == "metric" else "min/100yd"

    def to_display(self, value, instance):
        session = instance.get_session()
        what = session.sessiontype.speedPace
        units = session.athlete.get_part("userprofile").units
        if what == "Speed":
            return "%.1f" % gripe.conversions.ms_to_kmh(value) \
                if units == "metric" \
                else "%.1f" % gripe.conversions.ms_to_mph(value)
        elif what == "Pace":
            return gripe.conversions.ms_to_minkm(value) \
                if units == "metric" \
                else gripe.conversions.ms_to_minmile(value)
        else:
            return "0"


class Distance(grumble.qt.bridge.DisplayConverter):
    def __init__(self, bridge):
        super(Distance, self).__init__(bridge)
        
    def suffix(self, instance):
        if not instance:
            return True
        else:
            session = instance.get_session()
            what = session.sessiontype.speedPace
            units = session.athlete.get_part("userprofile").units
            if what in ("Speed", "Pace"):
                return "km" if units == "metric" else "miles"
            else:
                return "m" if units == "metric" else "yds"

    def to_display(self, value, instance):
        session = instance.get_session()
        what = session.sessiontype.speedPace
        units = session.athlete.get_part("userprofile").units
        if what in ("Speed", "Pace"):
            d = (value if value else 0) / 1000
            if units != "metric":
                d = gripe.conversions.km_to_mile(d)
            if d < 10:
                return "%.2f" % d
            elif d < 100:
                return "%.1f" % d
            else:
                return "%d" % d
        else:
            return str(value) if value else 0


class MeterFeet(grumble.qt.bridge.DisplayConverter):
    def __init__(self, bridge):
        super(MeterFeet, self).__init__(bridge)

    def suffix(self, instance):
        if not instance:
            return True
        else:
            session = instance.get_session()
            units = session.athlete.get_part("userprofile").units
            return "m" if units == "metric" else "ft"

    def to_display(self, value, instance):
        session = instance.get_session()
        units = session.athlete.get_part("userprofile").units
        m = value if value else 0
        m = m if units == "metric" else gripe.conversions.m_to_ft(m)
        return int(round(m))

