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
import enum
import hashlib
import json
import re

import gripe
import gripe.db
import grumble.converter
import grumble.errors
import grumble.schema

logger = gripe.get_logger(__name__)


class Validator(object):
    def property(self, prop=None):
        if prop:
            self.prop = prop
            if hasattr(self, "updateProperty") and callable(self.updateProperty):
                self.updateProperty(prop)
        return prop


class RequiredValidator(Validator):
    def __call__(self, instance, value):
        if value is None:
            raise grumble.errors.PropertyRequired(self.prop.name)


class ChoicesValidator(Validator, set):
    def __init__(self, choices=None):
        super(ChoicesValidator, self).__init__()
        if choices:
            if isinstance(choices, (list, set, tuple, dict)):
                for c in choices:
                    self.add(c)
            else:
                self.add(choices)

    def __call__(self, instance, value):
        if isinstance(value, (list, tuple)):
            for v in value:
                self(instance, v)
        else:
            if value is not None and str(value) not in self:
                raise grumble.errors.InvalidChoice(self.prop.name, value)


class RangeValidator(Validator):
    def __init__(self, minval=None, maxval=None):
        self._minval = minval if minval is not None else -float("inf")
        self._maxval = maxval if maxval is not None else float("inf")
        assert self._minval < self._maxval, "Minimum value for RangeValidator must be less than maximum value"

    def __call__(self, instance, value):
        if value < self._minval or value > self._maxval:
            raise grumble.errors.OutOfRange(self.prop.name, value)


class RegExpValidator(Validator):
    def __init__(self, pat=None):
        self._pattern = None
        self.pattern(pat)

    def pattern(self, pat=None):
        if pat is not None:
            self._pattern = pat
        return self._pattern

    def updateProperty(self, prop):
        prop.config["regexp"] = self._pattern

    def __call__(self, instance, value):
        if value and not re.match(self.pattern(), value):
            raise grumble.errors.PatternNotMatched(self.prop.name, value)


class MetaProperty(type):
    _properties = {}

    def __new__(mcs, name, bases, dct, **kwargs):
        ret = type.__new__(mcs, name, bases, dct)
        datatype = kwargs.get("datatype", (hasattr(ret, "datatype") and ret.datatype) or None)
        if datatype:
            MetaProperty._properties[datatype] = ret

        ret._default_config = {}
        ret._default_config.update(kwargs)

        def _set_attr(attr, default):
            if attr in kwargs:
                setattr(ret, attr, kwargs[attr])
            if not hasattr(ret, attr):
                setattr(ret, attr, default)

        _set_attr("datatype", str)
        _set_attr("sqltype", "TEXT")
        _set_attr("readonly", False)
        _set_attr("default", None)
        _set_attr("transient", False)
        _set_attr("suffix", None)
        _set_attr("format", None)
        _set_attr("required", False)
        _set_attr("regexp", None)
        _set_attr("choices", None)
        _set_attr("minimum", None)
        _set_attr("maximum", None)
        _set_attr("validator", None)
        _set_attr("getvalue", None)
        _set_attr("setvalue", None)
        _set_attr("display", None)
        _set_attr("default_format", None)
        if "on_assign" in kwargs:
            ret.on_assign = kwargs["on_assign"]
        if "assigned" in kwargs:
            ret.assigned = kwargs["assigned"]

        _set_attr("column_name", None)
        _set_attr("private", False)
        _set_attr("is_label", False)
        _set_attr("is_key", False)
        if ret.is_key:
            _set_attr("scoped", False)
        else:
            ret.scoped = False
        _set_attr("indexed", False)

        return ret

    @staticmethod
    def wrap_value(value, *args, **kwargs):
        return WrapperProperty(value, *args, **kwargs)


class BaseProperty(metaclass=MetaProperty):
    _default_validators = []
    property_counter = 0
    
    def __new__(cls, *args, **kwargs):
        ret: BaseProperty = super(BaseProperty, cls).__new__(cls)
        ret.config = {}
        ret.config.update(cls._default_config)
        ret.config.update(kwargs)
        return ret

    def __init__(self, *args, **kwargs):
        self.validators = []

    def validator(self, v=None):
        if v is not None:
            if hasattr(v, "property") and callable(v.property):
                v.property(self)
            self.validators.append(v)
        return self


class ModelProperty(BaseProperty):
    def __new__(cls, *args, **kwargs):
        if "template" in kwargs:
            prop: ModelProperty = kwargs["template"]
            ret: ModelProperty = super(ModelProperty, prop.__class__).__new__(prop.__class__)
            ret.name = prop.name
            ret.kind = None
            ret.column_name = prop.column_name
            ret.verbose_name = prop.verbose_name
            ret.readonly = prop.readonly
            ret.default = prop.default
            ret.private = prop.private
            ret.transient = prop.transient
            ret.required = prop.required
            ret.is_label = prop.is_label
            ret.is_key = prop.is_key
            ret.scoped = prop.scoped
            ret.indexed = prop.indexed
            ret.suffix = prop.suffix

            ret.converter = prop.converter
            ret.getvalue = prop.getvalue
            ret.setvalue = prop.setvalue
            ret.display = prop.display
            ret.format = prop.format
            ret.validators = []
            for v in prop.validators:
                ret.validators.append(v)
            if not hasattr(ret, "on_assign") and hasattr(prop, "on_assign"):
                ret.on_assign = prop.on_assign
            if not hasattr(ret, "assigned") and hasattr(prop, "assigned"):
                ret.assigned = prop.assigned

            ret.seq_nr = prop.seq_nr
            ret.config = dict(prop.config)
            ret.inherited_from = prop
        else:
            ret: ModelProperty = super(ModelProperty, cls).__new__(cls)
            ret.seq_nr = BaseProperty.property_counter
            BaseProperty.property_counter += 1
            ret.name = None
            ret.kind = None
            ret.inherited_from = None
            ret.validators = []
            ret.config = {}
            ret.config.update(kwargs)
            ret.converter = kwargs.get("converter",
                   ret.converter if hasattr(ret, "converter") else grumble.converter.Converters.get(cls.datatype, ret))
        return ret

    def __init__(self, *args, **kwargs):
        super(ModelProperty, self).__init__(*args, **kwargs)
        self.config.update(kwargs)
        self.validators = []

        def _update_attr(attr):
            if attr in kwargs:
                setattr(self, attr, kwargs[attr])

        _update_attr("name")
        _update_attr("verbose_name")
        if not hasattr(self, "verbose_name") or not self.verbose_name:
            self.verbose_name = self.name
        _update_attr("readonly")
        _update_attr("default")
        _update_attr("private")
        _update_attr("transient")
        _update_attr("is_label")
        _update_attr("is_key")
        if self.is_key:
            _update_attr("scoped")
        _update_attr("indexed")
        _update_attr("suffix")
        _update_attr("getvalue")
        _update_attr("setvalue")
        _update_attr("format")
        _update_attr("display")
        _update_attr("required")
        if self.required:
            self.validator(RequiredValidator())
        _update_attr("regexp")
        if self.regexp:
            self.validator(RegExpValidator(self.regexp))
        _update_attr("choices")
        if self.choices:
            self.validator(ChoicesValidator(self.choices))
        _update_attr("minimum")
        _update_attr("maximum")
        if self.minimum is not None or self.maximum is not None:
            self.validator(RangeValidator(self.minimum, self.maximum))
        v = kwargs.get("validator")
        if v is not None:
            self.validator(v)
        validators = kwargs.get("validators")
        if validators is not None:
            for v in validators:
                self.validator(v)
        if "on_assign" in kwargs:
            self.on_assign = kwargs["on_assign"]
        if "assigned" in kwargs:
            self.assigned = kwargs["assigned"]

    def set_name(self, name):
        self.name = name
        if not self.column_name:
            self.column_name = name
            self.config["column_name"] = self.column_name
        if not self.verbose_name:
            self.verbose_name = name.replace('_', ' ').title()
            self.config["verbose_name"] = self.verbose_name

    def set_kind(self, kind):
        self.kind = kind

    def get_coldef(self):
        ret = grumble.schema.ColumnDefinition(self.column_name, self.sqltype, self.required,
                                              self.to_sqlvalue(self.default), self.indexed)
        ret.is_key = self.is_key
        ret.scoped = self.scoped
        return [ret]

    def on_insert_(self, instance):
        value = self.__get__(instance)
        if not value and self.default:
            value = self.__set__(instance, self.default)
        return gripe.call_if_exists(self, "on_insert", value, instance, value)

    def initial_value_(self):
        return gripe.call_if_exists(self, "initial_value", self.default, self.default)

    def on_store_(self, value):
        return gripe.call_if_exists(self, "on_store", None, value)

    def after_store_(self, value):
        return gripe.call_if_exists(self, "after_store", None, value)

    def schema(self):
        ret = {
            "name": self.name, "type": self.__class__.__name__,
            "verbose_name": self.verbose_name,
            "default": self.default, "readonly": self.readonly,
            "is_key": self.is_key, "datatype": self.datatype.__name__
        }
        gripe.call_if_exists(self, "schema_customizer", None, ret)
        return ret

    def validate(self, instance, value):
        for v in self.__class__._default_validators + self.validators:
            v(instance, value) if callable(v) else v.validate(instance, value)

    def update_fromsql(self, instance, values):
        instance._values[self.name] = self.from_sqlvalue(values[self.column_name])

    def values_tosql(self, instance, values):
        if not self.transient:
            values[self.column_name] = self.to_sqlvalue(self.__get__(instance))

    def _get_storedvalue(self, instance):
        instance.load()
        return instance._values[self.name] if self.name in instance._values else None

    def __get__(self, instance, owner=None):
        try:
            if not instance:
                return self
            if self.transient and hasattr(self, "getvalue") and callable(self.getvalue):
                ret = self.getvalue(instance)
                instance.load()
                instance._values[self.name] = ret
                return ret
            else:
                return self._get_storedvalue(instance)
        except Exception:
            logger.exception("Exception getting property '%s'", self.name)
            raise

    def __set__(self, instance, value):
        try:
            if self.is_key and not hasattr(instance, "_brandnew"):
                pass
            if self.transient and hasattr(self, "setvalue") and callable(self.setvalue):
                self.setvalue(instance, value)
            else:
                instance.load()
                old = instance._values[self.name] if self.name in instance._values else None
                converted = self.convert(value) if value is not None else None
                if gripe.hascallable(self, "on_assign"):
                    self.on_assign(instance, old, converted)
                instance._values[self.name] = converted
                if self.is_key:
                    instance._key_name = converted
                if gripe.hascallable(self, "assigned"):
                    self.assigned(instance, old, converted)
            return self.__get__(instance, None)
        except Exception:
            logger.exception("Exception setting property '%s' to value '%s'", self.name, value)
            raise

    def __delete__(self, instance):
        return NotImplemented

    def _get_format(self, value):
        ret = None
        if self.format is not None:
            ret = self.format(value) if callable(self.format) else str(self.format)
            if ret == "$":
                ret = ".2f"
        return ret

    def to_display(self, value, instance=None):
        if self.display is not None:
            return self.display(value, instance)
        elif value is not None:
            fmt = self._get_format(value)
            return ("{:" + fmt + "}").format(value) if fmt is not None else str(value)
        else:
            return ''

    def convert(self, value):
        return self.converter.convert(value)

    def to_sqlvalue(self, value):
        return self.converter.to_sqlvalue(value)

    def from_sqlvalue(self, sqlvalue):
        return self.converter.from_sqlvalue(sqlvalue)

    def from_json_value(self, value):
        try:
            return self.datatype.from_dict(value)
        except Exception:
            try:
                return self.converter.from_jsonvalue(value)
            except Exception:
                logger.exception("ModelProperty<%s>.from_json_value(%s [%s])", self.__class__, value, type(value))
                return value

    def to_json_value(self, instance, value):
        try:
            return value.to_dict()
        except Exception:
            try:
                return self.converter.to_jsonvalue(value)
            except Exception:
                return value


def transient(prop):
    prop.transient = True
    return prop


class CompoundProperty(BaseProperty):
    def __init__(self, *args, **kwargs):
        super(CompoundProperty, self).__init__(*args, **kwargs)
        self.seq_nr = BaseProperty.property_counter
        BaseProperty.property_counter += 1
        self.compound = []
        for p in args:
            self.compound.append(p)
        if "name" in kwargs:
            self.set_name(kwargs["name"])
        else:
            self.name = None
        cls = self.__class__
        self.verbose_name = kwargs.get("verbose_name",
                                       cls.verbose_name
                                       if hasattr(cls, "verbose_name")
                                       else self.name)
        self.transient = kwargs.get("transient", cls.transient if hasattr(cls, "transient") else False)
        self.private = kwargs.get("private", cls.private if hasattr(cls, "private") else False)
        self.readonly = kwargs.get("readonly", cls.readonly if hasattr(cls, "readonly") else False)
        self.validators = []
        v = kwargs.get("validator")
        if v is not None:
            self.validator(v)
        validators = kwargs.get("validators")
        if validators is not None:
            for v in validators:
                self.validator(v)

    def set_name(self, name):
        self.name = name
        if not self.verbose_name:
            self.verbose_name = name
        for p in self.compound:
            if p.suffix:
                p.set_name(name + p.suffix)

    def set_kind(self, kind):
        self.kind = kind
        for prop in self.compound:
            prop.set_kind(kind)

    def schema(self):
        ret = {
            "name": self.name, "type": self.__class__.__name__,
            "verbose_name": self.verbose_name,
            "readonly": self.readonly,
            "is_key": False,
            "components": [
                prop.schema() for prop in self.compound
            ]
        }
        self._schema(ret)
        return ret

    def _schema(self, schema):
        return schema

    def get_coldef(self):
        ret = []
        for prop in self.compound:
            ret += prop.get_coldef()
        return ret

    def on_insert_(self, instance):
        for p in self.compound:
            p.on_insert_(instance)

    def on_store_(self, instance):
        for p in self.compound:
            p.on_store_(instance)

    def after_store_(self, value):
        for p in self.compound:
            p.after_store_(value)

    def validate(self, instance, value):
        for (p, v) in zip(self.compound, value):
            p.validate(instance, v)
        for v in self.__class__._default_validators + self.validators:
            v(instance, value) if callable(v) else v.validate(instance, value)

    def initial_value_(self):
        return tuple(p.initial_value_() for p in self.compound)

    def update_fromsql(self, instance, values):
        for p in self.compound:
            p.update_fromsql(instance, values)

    def values_tosql(self, instance, values):
        for p in self.compound:
            p.values_tosql(instance, values)

    def __get__(self, instance, owner):
        if not instance:
            return self
        instance.load()
        return tuple(p.__get__(instance, owner) for p in self.compound)

    def __set__(self, instance, value):
        instance.load()
        for (p, v) in zip(self.compound, value):
            p.__set__(instance, v)

    def __delete__(self, instance):
        return NotImplemented

    def convert(self, value):
        return tuple(p.convert(v) for (p, v) in zip(self.compound, value))

    def _from_json_value(self, value):
        raise gripe.NotSerializableError(self.name)

    def _to_json_value(self, instance, value):
        raise gripe.NotSerializableError(self.name)


class WrapperProperty(ModelProperty, transient=True):
    def __init__(self, value, *args, **kwargs):
        super(WrapperProperty, self).__init__(*args, **kwargs)
        self.transient = True
        self._value = None
        self.converter = grumble.converter.Converters.get(type(value), self)
        self.setvalue(None, value)
        
    def getvalue(self, instance):
        return self._value

    def setvalue(self, instance, value):
        self._value = self.convert(value) if value is not None else None


class StringProperty(ModelProperty, datatype=str, sqltype="TEXT"):
    pass


TextProperty = StringProperty
StrProperty = StringProperty


class LinkProperty(StringProperty):
    _default_validators = [
        RegExpValidator("(|https?:\/\/[\w\-_]+(\.[\w\-_]+)+([\w\-\.,@?^=%&amp;:/~\+#]*[\w\-\@?^=%&amp;/~\+#])?)")
    ]


class PasswordProperty(StringProperty, private=True):
    def on_store_(self, instance):
        value = self.__get__(instance, instance.__class__)
        self.__set__(instance, self.hash(value))

    @classmethod
    def hash(cls, password):
        return password \
            if password and password.startswith("sha://") \
            else "sha://%s" % hashlib.sha1(bytes(password, "UTF-8") if password else "").hexdigest()


class JSONProperty(ModelProperty, datatype=dict, sqltype="JSONB"):
    def initial_value_(self):
        return {}


class ListProperty(ModelProperty, datatype=list, sqltype="JSONB"):
    def initial_value_(self):
        return []


class IntegerProperty(ModelProperty, datatype=int, sqltype="INTEGER"):
    pass


IntProperty = IntegerProperty


class FloatProperty(ModelProperty, datatype=float, sqltype="REAL"):
    pass


class BooleanProperty(ModelProperty, datatype=bool, sqltype="BOOLEAN"):
    pass


class SQLTypes(enum.Enum):
    INTEGER = int
    TEXT = str
    REAL = float
    BOOLEAN = bool


class EnumProperty(ModelProperty, datatype=enum.Enum, sqltype=None):
    def __init__(self, *args, **kwargs):
        super(EnumProperty, self).__init__(*args, **kwargs)
        self._enum = gripe.resolve(self.config.get("enum"))
        assert self._enum and isinstance(self._enum, enum.EnumMeta) and list(self._enum)
        self.sqltype = SQLTypes(type(list(self._enum)[0].value)).name


class DateTimeProperty(ModelProperty, datatype=datetime.datetime, sqltype="TIMESTAMP WITHOUT TIME ZONE",
                       default_format="%c"):
    def __init__(self, *args, **kwargs):
        super(DateTimeProperty, self).__init__(*args, **kwargs)
        self.auto_now = kwargs.get("auto_now", self.config.get("auto_now", False))
        self.auto_now_add = kwargs.get("auto_now_add", self.config.get("auto_now_add", False))

    def display(self, value, instance):
        return value.strftime(self.config.get("format", self.default_format))

    def on_insert(self, instance, value):
        if self.auto_now_add and (self.__get__(instance, instance.__class__) is None):
            self.__set__(instance, self.now())

    def on_store(self, instance):
        if self.auto_now:
            self.__set__(instance, self.now())

    def now(self):
        return datetime.datetime.now()


class DateProperty(DateTimeProperty, datatype=datetime.date, sqltype="DATE", default_format="%x"):
    def now(self):
        return datetime.date.today()


class TimeProperty(DateTimeProperty, datatype=datetime.time, sqltype="TIME", default_format="%X"):
    def now(self):
        dt = datetime.datetime.now()
        return datetime.time(dt.hour, dt.minute, dt.second, dt.microsecond)


class TimeDeltaProperty(ModelProperty, datatype=datetime.timedelta, sqltype="INTERVAL"):
    pass


class PythonProperty(ModelProperty, datatype=type, sqltype="TEXT"):
    pass


if gripe.db.Tx.database_type == "postgresql":
    import psycopg2.extensions

    def adapt_json(d):
        return psycopg2.extensions.AsIs("'%s'" % json.dumps(d))

    def adapt_type(t):
        return psycopg2.extensions.AsIs("'%s'" % t.__name__)

    def adapt_key(k):
        return psycopg2.extensions.AsIs("'%s'" % str(k))

    psycopg2.extensions.register_adapter(dict, adapt_json)
    psycopg2.extensions.register_adapter(list, adapt_json)
    psycopg2.extensions.register_adapter(type, adapt_type)
    psycopg2.extensions.register_adapter(grumble.key.Key, adapt_key)

    def cast_jsonb(value, cursor):
        try:
            return json.loads(value) if value is not None else None
        except Exception:
            raise psycopg2.InterfaceError("bad JSON representation: %r" % value)

    with gripe.db.Tx.begin() as tx:
        cur = tx.get_cursor()
        cur.execute("SELECT NULL::jsonb")
        jsonb_oid = cur.description[0][1]

    JSONB = psycopg2.extensions.new_type((jsonb_oid,), "JSONB", cast_jsonb)
    psycopg2.extensions.register_type(JSONB, None)


elif gripe.db.Tx.database_type == "sqlite3":
    import sqlite3

    def adapt_json(d):
        return json.dumps(d)

    def convert_json(value):
        return json.loads(value) if value is not None else None

    sqlite3.register_adapter(dict, adapt_json)
    sqlite3.register_converter("JSONB", convert_json)

    def adapt_key(k):
        return str(k)

    sqlite3.register_adapter(grumble.key.Key, adapt_key)
