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
import json
import sys

import gripe

logger = gripe.get_logger(__name__)


class Converters(type):
    _converters = {}

    def __new__(mcs, name, bases, dct, **kwargs):
        ret = type.__new__(mcs, name, bases, dct)
        datatypes = kwargs.get("datatypes", hasattr(ret, "datatypes") and ret.datatypes)
        if datatypes:
            for datatype in datatypes:
                Converters._converters[datatype] = ret
        else:
            datatype = kwargs.get("datatype", (hasattr(ret, "datatype") and ret.datatype) or None)
            if datatype:
                Converters._converters[datatype] = ret
        return ret

    @classmethod
    def get(mcs, datatype, prop):
        return mcs._converters.get(datatype, PropertyConverter)(datatype, prop)


class PropertyConverter(metaclass=Converters):
    def __init__(self, datatype=None, prop=None):
        self.prop = prop
        if datatype:
            self.datatype = datatype

    def convert(self, value):
        """
            Convert value from to the canonical internal representation. Raise
            an exception if value cannot be converted.
        """
        try:
            return self.datatype(value) if not isinstance(value, self.datatype) else value
        except Exception:
            logger.exception("PropertyConverter<%s>.convert(%s [%s])", self.datatype, value, type(value))
            raise

    def to_sqlvalue(self, value):
        """
                Convert value, which is guaranteed to be produced by a call to
                convert(), to a value suitable for storing in persistant
                storage.
        """
        return value

    def from_sqlvalue(self, value):
        """
            Convert value, which was retrieved from persistant storage, to its
            canonical internal representation.
        """
        return value

    def to_jsonvalue(self, value):
        return value

    def from_jsonvalue(self, value):
        return value


class DictConverter(PropertyConverter, datatype=dict):
    def convert(self, value):
        if isinstance(value, dict):
            return dict(value)
        elif value is None:
            return {}
        else:
            return json.loads(str(value))

    def to_jsonvalue(self, value):
        assert value is not None, "DictConverter.to_jsonvalue(): value should not be None"
        assert isinstance(value, dict), "DictConverter.to_jsonvalue(): value must be a dict"
        return dict(value)

    def from_jsonvalue(self, value):
        assert (value is None) or isinstance(value, dict), "DictConverter.from_jsonvalue(): value must be a dict"
        return value or {}


class ListConverter(PropertyConverter, datatype=list):
    def convert(self, value):
        if isinstance(value, (list, tuple)):
            return list(value)
        elif isinstance(value, str):
            s = value.strip() if value or value == '' else None
            if s and s.startswith("[") and s.endswith("]"):
                return json.loads(s)
        return [] if value is None else [value]

    def to_jsonvalue(self, value):
        assert value is not None, "ListConverter.to_jsonvalue(): value should not be None"
        assert isinstance(value, list), "ListConverter.to_jsonvalue(): value must be a list"
        return list(value)

    def from_jsonvalue(self, value):
        assert (value is None) or isinstance(value, list), "ListConverter.from_jsonvalue(): value must be a list"
        return value or []


class BooleanConverter(PropertyConverter, datatype=bool):
    def convert(self, value):
        if isinstance(value, str) and self.prop.choices and len(self.prop.choices) == 2:
            value = self.prop.choices.index(value) == 1
        return super(BooleanConverter, self).convert(value)

    def to_jsonvalue(self, value):
        assert (value is None) or isinstance(value, bool), "BooleanConverter.to_jsonvalue: value must be bool"
        return self.prop.choices[1 if value else 0] \
            if self.prop.choices and len(self.prop.choices) == 2 \
            else value

    def from_jsonvalue(self, value):
        if isinstance(value, str) and self.prop.choices and len(self.prop.choices) == 2:
            value = self.prop.choices.index(value) == 1
        return super(BooleanConverter, self).from_jsonvalue(value)


class DateTimeConverter(PropertyConverter, datatype=datetime.datetime):
    def convert(self, value):
        if isinstance(value, (int, float)):
            value = datetime.datetime.utcfromtimestamp(value)
        return super(DateTimeConverter, self).convert(value)

    def to_jsonvalue(self, value):
        assert (value is None) or isinstance(value, datetime.datetime), \
            "DateTimeConverter.to_jsonvalue: value must be datetime"
        return gripe.json_util.datetime_to_dict(value)

    def from_jsonvalue(self, value):
        return gripe.json_util.dict_to_datetime(value) if isinstance(value, dict) else value


class DateConverter(PropertyConverter, datatype=datetime.date):
    def to_jsonvalue(self, value):
        assert (value is None) or isinstance(value, datetime.date), \
            "DateConverter.to_jsonvalue: value must be date"
        return gripe.json_util.date_to_dict(value)

    def from_jsonvalue(self, value):
        return gripe.json_util.dict_to_date(value) if isinstance(value, dict) else value


class TimeConverter(PropertyConverter, datatype=datetime.time):
    def to_jsonvalue(self, value):
        assert (value is None) or isinstance(value, datetime.time), \
            "TimeConverter.to_jsonvalue: value must be time"
        return gripe.json_util.time_to_dict(value)

    def from_jsonvalue(self, value):
        return gripe.json_util.dict_to_time(value) if isinstance(value, dict) else value


class TimeDeltaConverter(PropertyConverter, datatype=datetime.timedelta):
    def convert(self, value):
        if isinstance(value, (int, float)):
            value = datetime.timedelta(seconds=value)
        return super(TimeDeltaConverter, self).convert(value)

    def to_jsonvalue(self, value):
        assert (value is None) or isinstance(value, datetime.timedelta), \
            "TimeDeltaConverter.to_jsonvalue: value must be timedelta"
        return value.total_seconds()

    def from_jsonvalue(self, value):
        return datetime.timedelta(seconds=value)
