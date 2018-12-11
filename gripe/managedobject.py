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

logger = gripe.get_logger("gripe")

class ObjectExists(gripe.AuthException):
    def __init__(self, cls, idval):
        self._cls = cls
        self._idval = idval
        logger.debug(str(self))

    def __str__(self):
        return "%s with ID %s already exists" % (self._cls.__name__, self._idval)

_temp_mo_meta = None
_temp_mo_map = None

class ManagedObjectMetaClass(type):
    def __init__(cls, name, bases, dct):
        global _temp_mo_meta, _temp_mo_map
        cls._meta = _temp_mo_meta if _temp_mo_meta is not None else { }
        cls._map = _temp_mo_map if _temp_mo_map is not None else { }
        _temp_mo_meta = None
        _temp_mo_map = None
        cls._objects = {}
        cls._accessors = {}
        cls._idattr = None

        def get(cls, val):
            if isinstance(val, dict):
                idval = val.get(cls._map.get("idval", "id"))
            elif isinstance(val, cls):
                idval = val.__id__()
            else:
                idval = str(val)
            logger.debug("%s.get(%s) registry %s", cls.__name__, idval, cls._objects)
            return cls._objects.get(idval)
        cls.get = classmethod(get)

        def _set(cls, oldid, newid, obj):
            if oldid and (oldid in cls._objects):
                del cls._objects[oldid]
            cls._objects[newid] = obj
        cls._set = classmethod(_set)

        def _add(clazz, idval, **kwargs):
            if idval in clazz._objects:
                e = clazz._meta.get("exists", ObjectExists)
                raise e(clazz, idval)
            obj = object.__new__(clazz)
            obj._mo_init(idval, **kwargs)
            return obj
        cls._add = classmethod(_add)

        def add(clazz, idval, **kwargs):
            obj = clazz._add(idval, **kwargs)
            obj.put()
            return obj
        cls.add = classmethod(add)

        def put(cls):
            objtag = cls.__name__.lower() + "s"
            configtag = cls._meta.get("configtag", "app")
            objects = {}
            for idval in cls._objects:
                objects[idval] = cls._objects[idval].to_dict()
            section = gripe.Config[configtag] if configtag in gripe.Config else {}
            section[objtag] = objects
            gripe.Config.set(configtag, section)
        cls.put = classmethod(put)

        def objectmanager():
            if not(hasattr(cls, "_initialized")):
                cls._initialized = True
                objects = cls.__name__.lower() + "s"
                configtag = cls._meta.get("configtag", "app")
                if configtag in gripe.Config and objects in gripe.Config[configtag]:
                    for idval in gripe.Config[configtag][objects]:
                        cls._add(idval, **gripe.Config[configtag][objects][idval])
                else:
                    logger.warn("No %s defined in %s configuration" % (objects, configtag))
            return cls
        mod = __import__(cls.__module__)
        setattr(mod, cls.__name__ + "Manager", objectmanager)

class map_attribute(object):
    def __init__(self, maps_to = None):
        if isinstance(maps_to, (list, tuple)):
            self.attr = maps_to[0]
            self.maps_to = maps_to[1]
        else:
            if not hasattr(self, "attr"):
                self.attr = self.__class__.__name__
            self.maps_to = maps_to

    def __call__(self, obj):
        if isinstance(obj, type):
            obj._map[self.attr] = self.maps_to
            return obj
        else:
            return self._attr(obj)

    def _attr(self, obj):
        global _temp_mo_map
        if _temp_mo_map is None:
            _temp_mo_map = { }
        _temp_mo_map[self.attr] = self.maps_to
        return obj

class meta_value(object):
    def __init__(self, value = None):
        if isinstance(value, (list, tuple)):
            self.attr = value[0]
            self.value = value[1]
        else:
            if not hasattr(self, "attr"):
                self.attr = self.__class__.__name__
            self.value = value

    def __call__(self, obj):
        if isinstance(obj, type):
            obj._meta[self.attr] = self.value
            return obj
        else:
            return self._attr(obj)

    def _attr(self, obj):
        global _temp_mo_meta
        if _temp_mo_meta is None:
            _temp_mo_meta = { }
        _temp_mo_meta[self.attr] = self.value
        return obj


class idattr(map_attribute):
    attr = "idval"

class labelattr(map_attribute):
    attr = "label"

class objectexists(meta_value):
    attr = "exists"

class configtag(meta_value):
    pass


class ManagedObject(object, metaclass=ManagedObjectMetaClass):
    def __str__(self):
        return self.__id__()

    def __repr__(self):
        return self.__id__() or "<unassigned>"

    def __eq__(self, other):
        return self.__id__() == other.__id__() if self.__class__ == other.__class__ else False

    def __hash__(self):
        return self.__id__().__hash__()

    def _mo_init(self, idval, **attrs):
        self._attribs = {}
        self.objid(idval)
        if hasattr(self, "__initialize__"):
            processed_attrs = self.__initialize__(**attrs)
            if processed_attrs is not None:
                attrs = processed_attrs
        for attr in attrs:
            setattr(self, attr, attrs[attr])
        return self

    @classmethod
    def _get_accessor(cls, attr):
        accessors = cls._accessors.get(attr)
        if accessors is None:
            mapped_to = cls._map.get(attr, attr)
            a = attr
            if isinstance(mapped_to, str):
                a = hasattr(cls, mapped_to) and getattr(cls, mapped_to)
                a = callable(a) and a
            accessors = (mapped_to, a)
            cls._accessors[attr] = accessors
        return accessors

    @classmethod
    def _get_idattr(cls):
        if cls._idattr is None:
            cls._idattr = cls._map.get("idval", "idval")
        return cls._idattr

    def __id__(self, idval = None):
        if idval is not None:
            self.idval = idval
        return self.idval

    objid = __id__

    def objectlabel(self):
        attr = self._map.get("label", "label")
        return hasattr(self, attr) and getattr(self, attr) or self.objid()

    def __getattr__(self, name):
        attr, a = self._get_accessor(name)
        if a:
            return a()
        else:
            if attr not in self._attribs:
                raise AttributeError(attr)
            else:
                return self._attribs[attr]

    def __setattr__(self, name, value):
        if name.startswith("_"):
            object.__setattr__(self, name, value)
        else:
            attr, a = self._get_accessor(name)
            if attr == self._get_idattr():
                oldid = hasattr(self, attr) and getattr(self, attr)
                self.__class__._set(oldid, value, self)
            if a:
                a(self, value)
            else:
                self._attribs[attr] = value

    def to_dict(self):
        ret = {}
        idattr = self._get_idattr()
        attrs = {}
        attrs.update(self._attribs)
        if idattr != "id":
            ret["id"] = getattr(self, idattr)
            if idattr in attrs:
                del attrs[idattr]
        ret.update(attrs)
        return ret
