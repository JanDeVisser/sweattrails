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
import gripe.acl
import grumble.schema

logger = gripe.get_logger(__name__)


class ModelMetaClass(type):
    def __new__(mcs, name, bases, dct, **kwargs):
        cls: ModelMetaClass = type.__new__(mcs, name, bases, dct)
        if name != 'Model':
            logger.debug("Creating new model class %s [%s]", name, bases)
            cls._sealed = False
            cls._kind = Registry.register(cls)
            cls._tablename = kwargs.get("table_name", dct.get("table_name", name))
            cls._abstract = kwargs.get("abstract", dct.get("_abstract", False))
            cls._flat = kwargs.get("flat", dct.get("_flat", False))
            cls._audit = kwargs.get("audit", dct.get("_audit", True))
            if kwargs.get("cached", dct.get("_cached", False)):
                cls._cache = {}
            cls._sealed = False
            acl = gripe.Config.model["model"][name]["acl"] \
                if "model" in gripe.Config.model and \
                   name in gripe.Config.model["model"] and \
                   "acl" in gripe.Config.model["model"][name] \
                else dct.get("acl", None)
            cls._acl = gripe.acl.ACL(acl)
            cls._properties = {}
            cls._allproperties = {}
            cls._query_properties = {}
            mm = grumble.schema.ModelManager.for_name(cls._kind)
            mm.flat = cls._flat
            mm.audit = cls._audit
            mm.set_tablename(cls._tablename)
            mm.kind = cls
            cls.modelmanager = mm
            for base in bases:
                if isinstance(base, ModelMetaClass) and base.__name__ != "Model":
                    cls._import_properties(base)
            for (propname, value) in dct.items():
                cls.add_property(propname, value)
            cls.customizer = gripe.Config.model["model"][name]["customizer"] \
                if "model" in gripe.Config.model and \
                   name in gripe.Config.model["model"] and \
                   "customizer" in gripe.Config.model["model"][name] \
                else kwargs.get("customizer", dct.get("_customizer"))
            cls.load_template_data()
        else:
            cls._acl = gripe.acl.ACL(gripe.Config.model.get("global_acl", dct["acl"]))
        return cls

    def __init__(cls, name, bases, dct, **kwargs):
        super(ModelMetaClass, cls).__init__(name, bases, dct, **kwargs)


class Registry(dict):
    def __init__(self):
        assert not hasattr(self.__class__, "_registry"), "grumble.meta.Registry is a singleton"
        super(Registry, self).__init__()

    @classmethod
    def _get_registry(cls):
        if not hasattr(cls, "_registry"):
            cls._registry = Registry() 
        return cls._registry
    
    @classmethod
    def register(cls, modelclass):
        assert modelclass, "Registry.register(): empty class name"
        reg = cls._get_registry()
        fullname = cls.fullname_for_class(modelclass)
        assert fullname not in cls._get_registry(), "Registry.register(%s): Already registered" % fullname
        reg[fullname] = modelclass
        return fullname

    @classmethod
    def fullname(cls, qualname):
        hierarchy = qualname.lower().split(".")
        while hierarchy and hierarchy[0] in ('model', '__main__'):
            hierarchy.pop(0)
        return ".".join(hierarchy)

    @classmethod
    def fullname_for_class(cls, modelclass):
        return Registry.fullname(modelclass.__qualname__)

    @classmethod
    def get(cls, name):
        reg = cls._get_registry()
        # if empty - whatever we want it ain't there:
        assert reg, "Looking for kind %s but registry empty" % name
        if isinstance(name, ModelMetaClass):
            n = Registry.fullname_for_class(name)
            assert n in reg, "Registry.get(model class '%s'): not in registry" % n
            return name
        elif isinstance(name.__class__, ModelMetaClass):
            n = Registry.fullname_for_class(name.__class__)
            assert n in reg, "Registry.get(instance of '%s'): not in registry" % n
            return name.__class__
        else:
            name = name.replace('/', '.').lower()
            if name.startswith("."):
                (empty, dot, name) = name.partition(".")
            if name.startswith("__main__."):
                (main, dot, name) = name.partition(".")
            ret = reg[name] if name in reg else None   # dict.get is shadowed.
            if not ret and "." not in name:
                e = ".%s" % name
                for n in reg:
                    if n.endswith(e):
                        c = reg[n]
                        assert not ret, "Registry.get(%s): Already found match %s but there's a second one %s" % \
                            (name, ret.kind(), c.kind())
                        ret = c
            if ret:
                return ret
            else:
                print("Going to fail for Registry.get(%s)" % name)
                print("Current registry: %s" % reg)
                raise NameError("kind(%s)" % name)

    @classmethod
    def subclasses(cls, rootclass):
        reg = cls._get_registry()
        ret = []
        for m in reg.values():
            if m != rootclass and issubclass(m, rootclass):
                ret.append(m)
        return ret
