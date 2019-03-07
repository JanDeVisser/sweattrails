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
        self._enum = None
        if choices:
            if isinstance(choices, (list, set, tuple, dict)):
                for c in choices:
                    self.add(c)
            elif isinstance(choices, enum.EnumMeta):
                self._enum = choices
                for v in choices:
                    (_, _, val) = str(v).rpartition('.')
                    self.add(val)
            else:
                self.add(choices)

    def __call__(self, instance, value):
        if isinstance(value, (list, tuple)):
            for v in value:
                self(instance, v)
        else:
            if value is not None and \
                    (self._enum is None or not isinstance(value, self._enum)) and \
                    (str(value) not in self):
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
    def __new__(mcs, name, bases, dct, **kwargs):
        ret = type.__new__(mcs, name, bases, dct)
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
        ret = grumble.schema.ColumnDefinition(self.column_name, self.sqltype, self.required, self.default, self.indexed)
        ret.is_key = self.is_key
        ret.scoped = self.scoped
        return [ret]

    def _on_insert(self, instance):
        value = self.__get__(instance)
        if not value and self.default:
            return self.__set__(instance, self.default)

    def _initial_value(self):
        return self.default

    def _on_store(self, value):
        pass

    def _after_store(self, value):
        pass

    def schema(self):
        ret = {
            "name": self.name, "type": self.__class__.__name__,
            "verbose_name": self.verbose_name,
            "default": self.default, "readonly": self.readonly,
            "is_key": self.is_key, "datatype": self.datatype.__name__
        }
        self._schema(ret)
        return ret

    def _schema(self, schema):
        return schema

    def _validate(self, instance, value):
        for v in self.__class__._default_validators + self.validators:
            v(instance, value) if callable(v) else v.validate(instance, value)

    def _update_fromsql(self, instance, values):
        instance._values[self.name] = self._from_sqlvalue(values[self.column_name])

    def _values_tosql(self, instance, values):
        if not self.transient:
            values[self.column_name] = self._to_sqlvalue(self.__get__(instance))

    def _get_storedvalue(self, instance):
        instance._load()
        return instance._values[self.name] if self.name in instance._values else None

    def __get__(self, instance, owner=None):
        try:
            if not instance:
                return self
            if self.transient and hasattr(self, "getvalue") and callable(self.getvalue):
                ret = self.getvalue(instance)
                if ret:
                    instance._load()
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
                return
            if self.transient and hasattr(self, "setvalue") and callable(self.setvalue):
                return self.setvalue(instance, value)
            else:
                instance._load()
                old = instance._values[self.name] if self.name in instance._values else None
                converted = self.convert(value) if value is not None else None
                if gripe.hascallable(self, "on_assign"):
                    self.on_assign(instance, old, converted)
                instance._values[self.name] = converted
                if self.is_key:
                    instance._key_name = converted
                if gripe.hascallable(self, "assigned"):
                    self.assigned(instance, old, converted)
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

    def _to_sqlvalue(self, value):
        return self.converter.to_sqlvalue(value)

    def _from_sqlvalue(self, sqlvalue):
        return self.converter.from_sqlvalue(sqlvalue)

    def _from_json_value(self, value):
        try:
            return self.datatype.from_dict(value)
        except Exception:
            try:
                return self.converter.from_jsonvalue(value)
            except Exception:
                logger.exception("ModelProperty<%s>.from_json_value(%s [%s])", self.__class__, value, type(value))
                return value

    def _to_json_value(self, instance, value):
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

    def _on_insert(self, instance):
        for p in self.compound:
            p._on_insert(instance)

    def _on_store(self, instance):
        for p in self.compound:
            p._on_store(instance)

    def _after_store(self, value):
        for p in self.compound:
            p._after_store(value)

    def _validate(self, instance, value):
        for (p, v) in zip(self.compound, value):
            p._validate(instance, v)
        for v in self.__class__._default_validators + self.validators:
            v(instance, value) if callable(v) else v.validate(instance, value)

    def _initial_value(self):
        return tuple(p._initial_value() for p in self.compound)

    def _update_fromsql(self, instance, values):
        for p in self.compound:
            p._update_fromsql(instance, values)

    def _values_tosql(self, instance, values):
        for p in self.compound:
            p._values_tosql(instance, values)

    def __get__(self, instance, owner):
        if not instance:
            return self
        instance._load()
        return tuple(p.__get__(instance, owner) for p in self.compound)

    def __set__(self, instance, value):
        instance._load()
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


class StringProperty(ModelProperty, datatype=str, sqltype="TEXT"):
    pass


TextProperty = StringProperty
StrProperty = StringProperty


class LinkProperty(StringProperty):
    _default_validators = [
        RegExpValidator("(|https?:\/\/[\w\-_]+(\.[\w\-_]+)+([\w\-\.,@?^=%&amp;:/~\+#]*[\w\-\@?^=%&amp;/~\+#])?)")
    ]


class PasswordProperty(StringProperty, private=True):
    def _on_store(self, instance):
        value = self.__get__(instance, instance.__class__)
        self.__set__(instance, self.hash(value))

    @classmethod
    def hash(cls, password):
        return password \
            if password and password.startswith("sha://") \
            else "sha://%s" % hashlib.sha1(bytes(password, "UTF-8") if password else "").hexdigest()


class JSONProperty(ModelProperty, datatype=dict, sqltype="JSONB"):
    def _initial_value(self):
        return {}


class ListProperty(ModelProperty, datatype=list, sqltype="JSONB"):
    def _initial_value(self):
        return []


class IntegerProperty(ModelProperty, datatype=int, sqltype="INTEGER"):
    pass


IntProperty = IntegerProperty


class FloatProperty(ModelProperty, datatype=float, sqltype="REAL"):
    pass


class BooleanProperty(ModelProperty, datatype=bool, sqltype="BOOLEAN"):
    pass


class DateTimeProperty(ModelProperty, datatype=datetime.datetime, sqltype="TIMESTAMP WITHOUT TIME ZONE",
                       default_format="%c"):
    def __init__(self, *args, **kwargs):
        super(DateTimeProperty, self).__init__(*args, **kwargs)
        self.auto_now = kwargs.get("auto_now", self.config.get("auto_now", False))
        self.auto_now_add = kwargs.get("auto_now_add", self.config.get("auto_now_add", False))

    def display(self, value, instance):
        return value.strftime(self.config.get("format", self.default_format))

    def _on_insert(self, instance):
        if self.auto_now_add and (self.__get__(instance, instance.__class__) is None):
            self.__set__(instance, self.now())

    def _on_store(self, instance):
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

    psycopg2.extensions.register_adapter(dict, adapt_json)
    psycopg2.extensions.register_adapter(list, adapt_json)
    psycopg2.extensions.register_adapter(type, adapt_type)

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
