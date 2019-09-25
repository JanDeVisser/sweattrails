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

import sys
import uuid

import gripe.acl
import gripe.db
import gripe.sessionbridge
import grumble.errors
import grumble.key
import grumble.meta
import grumble.property
import grumble.query
import grumble.schema

logger = gripe.get_logger(__name__)


class Model(metaclass=grumble.meta.ModelMetaClass):
    classes = {}
    acl = {"admin": "RUDQC", "owner": "RUDQ"}

    def __new__(cls, **kwargs):
        cls.seal()
        return super(Model, cls).__new__(cls)

    def __init__(self, *args, **kwargs):
        self._brandnew = True
        self._acl = gripe.acl.ACL(kwargs.pop("acl", None))
        self._values = None
        self._instance_properties = dict(self._allproperties)
        if "key" in kwargs:
            self._key = kwargs.pop("key")
            p = self._key.scope()
            self._set_ancestors_from_parent(p)
            self._key_name = self._key.name
            self._id = self._key.id
        else:
            if "parent" in kwargs:
                self._set_ancestors_from_parent(kwargs.pop("parent"))
            else:
                self._ancestors = None
                self._parent = None
            self._key_name = kwargs.pop("key_name",
                                        gripe.call_if_exists(self, "get_new_key", None, *args, **kwargs))
            self._id = kwargs.pop("id", None)
            for (prop_name, prop) in self._instance_properties.items():
                setattr(self, prop_name, prop.initial_value_())
        for (prop_name, prop_value) in kwargs.items():
            if prop_name not in self._instance_properties:
                self.add_adhoc_property(prop_name, prop_value)
            else:
                setattr(self, prop_name, prop_value)

    @classmethod
    def schema(cls):
        cls.seal()
        ret = {"kind": cls.kind(),
               "flat": cls._flat,
               "audit": cls._audit,
               "properties": [prop.schema() for prop in cls._properties_by_seqnr if not prop.private]}
        return ret

    @classmethod
    def seal(cls):
        if not hasattr(cls, "_sealed") or not (cls._sealed or hasattr(cls, "_sealing")):
            logger.info("Sealing class %s", cls.kind())
            setattr(cls, "_sealing", True)
            if cls.customizer:
                c = gripe.resolve(cls.customizer)
                if c:
                    c(cls)
            if hasattr(cls, "key_prop"):
                cls._key_propdef = getattr(cls, cls.key_prop)
                cls._key_scoped = cls._key_propdef.scoped
            else:
                cls._key_propdef = None
                cls._key_scoped = False
            cls._properties_by_seqnr = [p for p in cls._allproperties.values()]
            cls._properties_by_seqnr.sort(key=lambda p: p.seq_nr)
            if not cls._abstract:
                cls.modelmanager.reconcile()
            logger.info("Class %s SEALED", cls.kind())
            cls._sealed = True
            delattr(cls, "_sealing")
            
    def __getattribute__(self, name):
        try:
            return super(Model, self).__getattribute__(name)
        except AttributeError:
            ip = super(Model, self).__getattribute__("_instance_properties")
            ap = super(Model, self).__getattribute__("_allproperties")
            if name in ip and name not in ap:
                prop = ip[name]
                return prop.__get__(self, self)
            else:
                raise

    def __setattr__(self, name, value):
        try:
            if name in self._instance_properties:
                prop = self._instance_properties[name]
                prop.__set__(self, value)
                return
        except AttributeError:
            pass
        super(Model, self).__setattr__(name, value)

    def __repr__(self):
        return str(self.key())

    def __str__(self):
        self.load()
        label = self.label_prop if hasattr(self.__class__, "label_prop") else None
        if self.keyname():
            s = self.keyname()
            if label:
                s += " (%s)" % label
            return "<%s: %s>" % (self.kind(), s)
        else:
            super(self.__class__, self).__repr__()

    def __hash__(self):
        return hash(self.id())

    def __eq__(self, other):
        if not other or not(hasattr(other, "key") and callable(other.key)):
            return False
        else:
            return self.key() == other.key()

    def __call__(self):
        return self

    def __getitem__(self, item):
        if isinstance(item, grumble.property.ModelProperty):
            if item.name not in self._instance_properties:
                raise KeyError("'%s' is not a property of kind '%s'" % (item.name, self.kind()))
            return getattr(self, item.name)
        elif item == '_kind':
            return self.kind()
        elif item == '_key':
            return self.key()
        elif item == '_parent':
            return self.parent()
        elif item == '_key_name':
            return self.keyname()
        elif item.startswith('+'):
            try:
                return self.joined_value(item)
            except ValueError as ve:
                raise KeyError(str(ve))
        elif not item:
            raise KeyError('Empty key')
        else:
            path = item.replace('"', '').split(".", 2)
            attr = path[0]
            if attr not in self._instance_properties:
                raise KeyError("'%s' is not a property of kind '%s'" % (attr, self.kind()))
            ret = getattr(self, attr) if item in self._instance_properties else None
            if len(path) > 1:
                if isinstance(ret, Model):
                    ret = ret[path[1]]
                else:
                    raise KeyError("'%s' is not a reference property of kind '%s'" % (attr, self.kind()))
            return ret

    def __contains__(self, item):
        if isinstance(item, grumble.property.ModelProperty):
            return item.name in self._instance_properties
        elif item.startswith('+'):
            item = item[1:]
            return hasattr(self, "_joins") and item in self._joins
        elif item in ('_kind', '_key', '_parent', '_key_name'):
            return True
        else:
            path = item.replace('"', '').split(".", 2)
            return path[0] in self._instance_properties

    def _set_ancestors_from_parent(self, parent):
        if not self._flat:
            if parent and hasattr(parent, "key") and callable(parent.key):
                p = parent.key()
            elif isinstance(parent, str):
                p = grumble.key.Key(parent)
            else:
                p = None
            assert parent is None or isinstance(p, grumble.key.Key)
            self._parent = p
            if p:
                self._ancestors = p.path()
            else:
                self._ancestors = "/"
        else:
            self._parent = None
            self._ancestors = "/"

    def _set_ancestors(self, parent):
        if not self._flat:
            parent = grumble.key.to_key(parent)
            if parent is None:
                self._parent = None
                self._ancestors = "/"
            else:
                self._ancestors = str(parent)
                # print("parent: '%s' (%s)" % (parent, type(parent)), file=sys.stderr)
                self._parent = parent
        else:
            self._parent = None
            self._ancestors = "/"

    def _populate(self, values, prefix=None):
        if values is not None:
            self._values = {}
            self._joins = {}
            if isinstance(values, (list, tuple)):
                v = {k: v for (k, v) in values}
            else:
                assert isinstance(values, dict)
                v = values
            if prefix:
                vv = {k[len(prefix)+1:].replace('"', ''): v for (k, v) in v.items() if k.startswith(prefix + ".")}
                for (k, val) in v.items():
                    if '.' not in k and k[0] != '_' and k not in vv:
                        vv[k] = val
                v = vv
            parent = v.get("_parent")
            self._key_name = v.get("_key_name")
            self._ownerid = v.get("_ownerid")
            self._acl = gripe.acl.ACL(v.get("_acl"))
            for prop in [p for p in self._properties.values() if not p.transient]:
                prop.update_fromsql(self, v)
            for (k, val) in [(k, val) for (k, val) in v.items() if k not in self._instance_properties and k[0] != '_']:
                self.add_adhoc_property(k, val)
            self._set_ancestors(parent)
            if (self._key_name is None) and hasattr(self, "key_prop"):
                self._key_name = getattr(self, self.key_prop)
            self._key = grumble.key.Key(self.kind(), parent, self._key_name)
            self._exists = True
            if hasattr(self, "_brandnew"):
                del self._brandnew
            if hasattr(self, "after_load") and callable(self.after_load):
                self.after_load()
        else:
            self._exists = False

    def _populate_joins(self, values):
        # logger.debug("_populate_joins for %s: %s", self.key(), values)
        if values is not None:
            self._joins = {k[1:].replace('"', ''): values[k] for k in values if k[0] == '+'}
            if self._joins:
                logger.debug("self._joins for %s: %s", self.key(), self._joins)

    def joined_value(self, join):
        if join[0] == '+':
            join = join[1:]
        logger.debug("%s.joined_value(%s) %s %s", self.key(), join,
                     join in self._joins if hasattr(self, "_joins") else "??",
                     self._joins.get(join, "???") if hasattr(self, "_joins") else "??")
        if hasattr(self, "_joins") and join in self._joins:
            return self._joins[join]
        else:
            raise ValueError("'%s' does no have join '%s'" % (str(self), join))

    def load(self):
        # logger.debug("_load -> kind: %s, key: %s", self.kind(), str(self.key()))
        if (not hasattr(self, "_values") or self._values is None) and (self._id or self._key_name):
            self._populate(grumble.query.ModelQuery.get(self.key()))
        else:
            # If self._values is None, and self._id and self._key_name are both None as well,
            # this is a new Model and we need to initialize _values with an empty dict:
            if not hasattr(self, "_values") or self._values is None:
                self._values = {}
            assert hasattr(self, "_parent"), "Object of kind %s doesn't have _parent" % self.kind()
            assert hasattr(self, "_ancestors"), "Object of kind %s doesn't have _ancestors" % self.kind()

    def _store(self):
        self.load()
        if hasattr(self, "_brandnew"):
            for prop in self._properties.values():
                prop.on_insert_(self)
            if hasattr(self, "initialize") and callable(self.initialize):
                self.initialize()
        include_key_name = not self._key_propdef
        if self._key_propdef:
            key = getattr(self, self.key_prop)
            if key is None:
                raise grumble.errors.KeyPropertyRequired(self.kind(), self.key_prop)
            self._key_name = key
        elif not self._key_name:
            self._key_name = uuid.uuid1().hex
        self._id = None
        self._storing = 1
        while self._storing:
            for prop in self._properties.values():
                prop.on_store_(self)
            if hasattr(self, "on_store") and callable(self.on_store):
                self.on_store()
            self._validate()
            values = {}
            for prop in self._properties.values():
                prop.values_tosql(self, values)
            if include_key_name:
                values['_key_name'] = self._key_name
            if not self._flat:
                p = self.parent_key()
                values['_parent'] = str(p) if p else None
            values["_acl"] = self._acl.to_json()
            values["_ownerid"] = self._ownerid if hasattr(self, "_ownerid") else None
            grumble.query.ModelQuery.set(hasattr(self, "_brandnew"), self.key(), values)
            if hasattr(self, "_brandnew"):
                if hasattr(self, "after_insert") and callable(self.after_insert):
                    self.after_insert()
                del self._brandnew
            for prop in self._properties.values():
                prop.after_store_(self)
            if hasattr(self, "after_store") and callable(self.after_store):
                self.after_store()
            self._exists = True
            gripe.db.Tx.put_in_cache(self)
            self._storing -= 1
        del self._storing

    def _on_delete(self):
        return gripe.call_if_exists(self, "on_delete", True)

    def _validate(self):
        for prop in self._properties.values():
            prop.validate(self, prop.__get__(self, None))
        gripe.call_if_exists(self, "validate", None)

    def is_new(self):
        return hasattr(self, "_brandnew")

    def id(self):
        if not self._id and self._key_name:
            self._id = self.key().id
        return self._id

    def keyname(self):
        return self._key_name

    def label(self):
        return self.label_prop if hasattr(self, "label_prop") else str(self)

    def parent_key(self):
        """
            Returns the parent Model of this Model, as a Key, or None if this
            Model does not have a parent.
        """
        if not hasattr(self, "_parent") and self._id is not None:
            self.load()
        return self._parent

    def parent(self):
        """
            Returns the parent Model of this Model, or None if this
            Model does not have a parent.
        """
        k = self.parent_key()
        return k() if k else None

    def set_parent(self, parent):
        assert not self._flat, "Cannot set parent of flat Model %s" % self.kind()
        if parent:
            parent = grumble.key.Key(parent)()
            assert str(self.key()) not in parent.path(), \
                "Cyclical datamodel: attempting to set %s as parent of %s" % (parent, self)
        self.load()
        self._set_ancestors_from_parent(parent)

    def ancestors(self):
        """
            Returns the ancestor path of this Model object. This is the path
            string of the parent object or empty if this object has no parent.
        """
        if not hasattr(self, "_parent") and (self._id is not None or self._key_name is not None):
            self.load()
        return self._ancestors if self._ancestors != "/" else ""

    def key(self):
        """
            Returns the Key object representing this Model. A Key consists of
            the Model's kind and its key name. These two identifying properties
            are combined into the Model's id, which is also part of a Key
            object.
        """
        if not hasattr(self, "_key"):
            if self._id:
                self._key = grumble.key.Key(self._id)
            elif hasattr(self, "_parent") and self._key_name:
                self._key = grumble.key.Key(self.kind(), self.parent_key(), self._key_name)
            else:
                assert 0, "Cannot construct key. Need either _id or _key_name and _parent"
        return self._key

    def path(self):
        return self.key().path()

    def pathlist(self):
        """
            Returns a list containing the ancestors of this Model, the root
            Model first. If this object has no parent, the returned list is
            empty.
        """
        return [Model.get(k) for k in self.key().ancestors()[:-1]]

    def root(self):
        root_key = self.key().root()
        return root_key.get() if root_key != self.key() else self

    def ownerid(self, oid=None):
        self.load()
        if oid is not None:
            self._ownerid = oid
        return self._ownerid

    def put(self):
        if hasattr(self, "_storing"):
            self._storing += 1
        else:
            self._store()

    def exists(self):
        if hasattr(self, "_brandnew"):
            return True
        else:
            self.load()
            return self._exists

    def to_dict(self, **flags):
        with gripe.LoopDetector.begin(self.id()) as detector:
            if detector.loop:
                logger.info("to_dict: Loop detected. %s is already serialized", self)
                return {"key": self.id()}
            p = self.parent_key()
            ret = {"key": self.id(), 'parent': p.id if p else None}
            detector.add(self.id())
            for b in self.__class__.__bases__:
                if hasattr(b, "_to_dict") and callable(b._to_dict):
                    b._to_dict(self, ret, **flags)

            def serialize(serialized, name, prop):
                if prop.private:
                    return serialized
                if gripe.hascallable(self, "to_dict_" + name):
                    return getattr(self, "to_dict_" + name)(serialized, **flags)
                else:
                    try:
                        serialized[name] = prop.to_json_value(self, getattr(self, name))
                    except gripe.NotSerializableError:
                        pass
                    return serialized

            for (n, p) in self._allproperties.items():
                ret = serialize(ret, n, p)
            for (n, p) in self._query_properties.items():
                ret = serialize(ret, n, p)
            ret = gripe.call_if_exists(self, "sub_to_dict", ret, ret, **flags)
            return ret

    def _update(self, d):
        pass

    @classmethod
    def _deserialize(cls, descriptor):
        for name, prop in filter(lambda n_p: ((not n_p[1].private) and (n_p[0] in descriptor)),
                                 cls._allproperties.items()):
            value = descriptor[name]
            try:
                descriptor[name] = prop.from_json_value(value)
            except Exception:
                logger.exception("Could not deserialize value '%s' for property '%s'", value, name)
                del descriptor[name]
        return descriptor

    def _update_deserialized(self, descriptor, **flags):
        self.load()
        try:
            for b in self.__class__.__bases__:
                if hasattr(b, "_update") and callable(b._update):
                    b._update(self, descriptor)
            for prop in filter(lambda p: not p.private and not p.readonly and (p.name in descriptor),
                               self.properties().values()):
                name = prop.name
                try:
                    value = descriptor[name]
                    if hasattr(self, "update_" + name) and callable(getattr(self, "update_" + name)):
                        getattr(self, "update_" + name)(descriptor)
                    else:
                        setattr(self, name, value)
                except Exception:
                    raise
            self.put()
            if hasattr(self, "on_update") and callable(self.on_update):
                self.on_update(descriptor, **flags)
        except Exception:
            logger.exception("Could not update datamodel %s.%s using descriptor %s",
                             self.kind(), self.keyname(), descriptor)
            raise
        return self.to_dict(**flags)

    def update(self, descriptor, **flags):
        return self._update_deserialized(self._deserialize(descriptor), **flags)

    @classmethod
    def create(cls, descriptor=None, parent=None, **flags):
        if descriptor is None:
            descriptor = {}
        try:
            kwargs = {"parent": parent}
            descriptor = cls._deserialize(descriptor)
            kwargs.update(descriptor)
            obj = cls(**kwargs)
            obj._update_deserialized(descriptor, **flags)
        except Exception:
            logger.exception("Could not create new %s datamodel from descriptor %s", cls.__name__, descriptor)
            raise
        if hasattr(obj, "on_create") and callable(obj.on_create):
            obj.on_create(descriptor, **flags) and obj.put()
        return obj

    def invoke(self, method, args, kwargs):
        self.load()
        args = args or []
        kwargs = kwargs or {}
        assert hasattr(self, method) and callable(getattr(self, method)), \
            "%s.%s has not method %s. Can't invoke" % (self.kind(), self.key(), method)
        logger.info("Invoking %s on %s.%s using arguments *%s, **%s",
                    method, self.kind(), self.key(), args, kwargs)
        return getattr(self, method)(*args, **kwargs)

    def get_user_permissions(self):
        roles = set(gripe.sessionbridge.get_sessionbridge().roles())
        if gripe.sessionbridge.get_sessionbridge().userid() == self.ownerid():
            roles.add("owner")
        roles.add("world")
        perms = set()
        for role in roles:
            perms |= self.get_all_permissions(role)
        return perms

    @classmethod
    def get_user_classpermissions(cls):
        roles = set(gripe.sessionbridge.get_sessionbridge().roles())
        roles.add("world")
        perms = set()
        for role in roles:
            perms |= cls.get_class_permissions(role) | Model.get_global_permissions(role)
        return perms

    def get_object_permissions(self, role):
        return self._acl.get_ace(role)

    @classmethod
    def get_class_permissions(cls, role):
        return cls._acl.get_ace(role)

    @staticmethod
    def get_global_permissions(role):
        return Model._acl.get_ace(role)

    def get_all_permissions(self, role):
        return self.get_object_permissions(role) | self.get_class_permissions(role) | self.get_global_permissions(role)

    def set_permissions(self, role, perms):
        self._acl.set_ace(role, perms)

    def can_read(self):
        return "R" in self.get_user_permissions()

    def can_update(self):
        return "U" in self.get_user_permissions()

    def can_delete(self):
        return "D" in self.get_user_permissions()

    @classmethod
    def can_query(cls):
        return "Q" in cls.get_user_classpermissions()

    @classmethod
    def can_create(cls):
        return "C" in cls.get_user_classpermissions()

    @classmethod
    def add_property(cls, propname, propdef):
        if not isinstance(propdef, (grumble.property.ModelProperty, grumble.property.CompoundProperty)):
            return
        assert not cls._sealed or propdef.transient, "Model %s is sealed. No more properties can be added" % cls.__name__
        if not hasattr(cls, propname):
            setattr(cls, propname, propdef)
        propdef.set_name(propname)
        propdef.set_kind(cls.__name__)
        if not cls._sealed:
            mm = grumble.schema.ModelManager.for_name(cls._kind)
            if not propdef.transient:
                mm.add_column(propdef.get_coldef())
            if hasattr(propdef, "is_label") and propdef.is_label:
                assert not propdef.transient, "Label property cannot be transient"
                cls.label_prop = propdef
            if hasattr(propdef, "is_key") and propdef.is_key:
                assert not propdef.transient, "Key property cannot be transient"
                cls.key_prop = propname
            cls._properties[propname] = propdef
            if isinstance(propdef, grumble.property.CompoundProperty):
                for p in propdef.compound:
                    setattr(cls, p.name, p)
                    cls._allproperties[p.name] = p
            else:
                cls._allproperties[propname] = propdef

    def add_adhoc_property(self, propname, prop, value=None):
        assert not (hasattr(self, propname) or propname in self._instance_properties)
        is_model_prop = isinstance(prop, grumble.property.ModelProperty) and \
                        not isinstance(prop, grumble.property.WrapperProperty)
        if not isinstance(prop, grumble.property.ModelProperty):
            prop = grumble.property.WrapperProperty(prop)
        prop.set_name(propname)
        prop.set_kind(self.kind())
        self._instance_properties[propname] = prop
        if is_model_prop:
            setattr(self, propname, value if value is not None else prop.initial_value_())


    @classmethod
    def _import_properties(cls, from_cls):
        for (propname, propdef) in from_cls.properties().items():
            cls.add_property(propname, grumble.property.ModelProperty(template=propdef))

    @classmethod
    def samekind(cls, model, sub=False):
        kinds = [cls.kind()]
        if sub:
            kinds += cls.subclasses()
        return model.kind() in kinds

    @classmethod
    def kind(cls):
        return cls._kind

    @classmethod
    def basekind(cls):
        (_, _, k) = cls.kind().rpartition(".")
        return k

    @classmethod
    def abstract(cls):
        return cls._abstract

    @classmethod
    def flat(cls):
        return cls._flat

    @classmethod
    def audit(cls):
        return cls._audit

    @classmethod
    def properties(cls):
        return cls._properties

    def instance_properties(self):
        return self._instance_properties

    @classmethod
    def keyproperty(cls):
        return cls.key_prop if hasattr(cls, "key_prop") else None

    @classmethod
    def for_name(cls, name):
        return grumble.meta.Registry.get(name)

    @classmethod
    def subclasses(cls):
        return grumble.meta.Registry.subclasses(cls)

    @classmethod
    def get(cls, ident, values=None, prefix=None):
        if cls != Model:
            if isinstance(ident, str) and ':' not in ident:
                ident = "{:s}:{:s}".format(cls.kind(), ident)
            k = grumble.key.Key(ident)
            with gripe.db.Tx.begin():
                cls.seal()
                ret = None
                if hasattr(cls, "_cache") and k in cls._cache:
                    ret = cls._cache.get(k)
                    return ret
                if not ret:
                    ret = gripe.db.Tx.get_from_cache(k)
                if not ret:
                    assert (cls.kind().endswith(k.kind())) or not k.kind(), \
                        "%s.get(%s.%s) -> wrong key kind" % (cls.kind(), k.kind(), k.name)
                    ret = cls(key=k)
                    gripe.db.Tx.put_in_cache(ret)
                    if hasattr(cls, "_cache"):
                        cls._cache[ret.key()] = ret
                if values:
                    ret._populate(values, prefix)
                    ret._populate_joins(values)
        else:
            k = grumble.key.Key(ident)
            return k.modelclass().get(k, values, prefix)
        return ret

    @classmethod
    def get_by_key(cls, key):
        assert cls != Model, "Cannot use get_by_key on unconstrained Models"
        k = grumble.key.Key(cls, key)
        return cls.get(k)

    @classmethod
    def get_by_key_and_parent(cls, key, parent):
        cls.seal()
        assert cls != Model, "Cannot use get_by_key_and_parent on unconstrained Models"
        assert cls.key_prop, "Cannot use get_by_key_and_parent Models without explicit keys"
        q = cls.query(parent=parent)
        q.add_filter(cls.key_prop, "=", key)
        return q.get()

    @classmethod
    def by(cls, prop, value, **kwargs):
        cls.seal()
        assert cls != Model, "Cannot use by() on unconstrained Models"
        kwargs["keys_only"] = False
        q = cls.query('"%s" = ' % prop, value, **kwargs)
        return q.get()

    def children(self, cls=None):
        cls = cls or self
        q = cls.query(parent=self)
        return q

    def descendents(self, cls=None):
        cls = cls or self
        q = cls.query(ancestor=self)
        return q

    @classmethod
    def _declarative_query(cls, *args, **kwargs):
        q = Query(cls, kwargs.get("keys_only", True), kwargs.get("include_subclasses", True),
                  kwargs.get("raw", False), kwargs.get("alias", None))
        for (k, v) in kwargs.items():
            if k == "ancestor" and not cls._flat:
                q.set_ancestor(v)
            elif k == "parent" and "ancestor" not in kwargs and not cls._flat:
                q.set_parent(v)
            elif k == "ownerid":
                q.owner(v)
            elif k == "_sortorder":
                def _add_sortorder(qry, order):
                    if isinstance(order, (list, tuple)):
                        for s in order:
                            _add_sortorder(qry, s)
                    elif isinstance(order, dict):
                        qry.add_sort(order["column"], order.get("ascending", True))
                    else:
                        qry.add_sort(str(order), True)
                _add_sortorder(q, v)
            elif isinstance(v, (list, tuple)):
                q.add_filter(k, *v)
            elif k in ("keys_only", "include_subclasses", "raw", "alias"):
                pass
            else:
                q.add_filter(k, v)
        ix = 0
        while ix < len(args):
            arg = args[ix]
            if isinstance(arg, (list, tuple)):
                q.add_filter(*arg)
                ix += 1
            else:
                assert len(args) > ix + 1
                expr = args[ix + 1]
                q.add_filter(arg, expr)
                ix += 2
        return q

    @classmethod
    def _named_search(cls, named_search, *args, **kwargs):
        factory = getattr(cls, "named_search_" + named_search)
        return factory(*args, **kwargs) \
            if factory and callable(factory) \
            else cls._declarative_query(*args, **kwargs)

    @classmethod
    def query(cls, *args, **kwargs):
        cls.seal()
        logger.debug("%s.query: args %s kwargs %s", cls.__name__, args, kwargs)
        assert cls != Model, "Cannot query on unconstrained Model class"
        named_search = kwargs.pop("named_search", None)
        return cls._named_search(named_search, *args, **kwargs) \
            if named_search \
            else cls._declarative_query(*args, **kwargs)

    @classmethod
    def all(cls, **kwargs):
        cls.seal()
        return Query(cls, **kwargs)

    @classmethod
    def count(cls, **kwargs):
        cls.seal()
        return Query(cls, **kwargs).count()

    @classmethod
    def _import_template_data(cls, data):
        cname = cls.__name__.lower()
        for cdata in data:
            clazz = grumble.meta.Registry.get(cdata.model)
            if clazz:
                with gripe.db.Tx.begin():
                    if clazz.all(keys_only=True).count() == 0:
                        logger.info("_import_template_data(%s): Loading template datamodel data for datamodel %s",
                                    cname, cdata.model)
                        for d in cdata["data"]:
                            logger.debug("_import_template_data(%s): datamodel %s object %s", cname, cdata.model, d)
                            clazz.create(d)

    @classmethod
    def load_template_data(cls):
        cname = cls.__name__.lower()
        dirname = cls._template_dir \
            if hasattr(cls, "_template_dir") and cls._template_dir is not None \
            else "data/template"
        fname = "%s/%s.json" % (dirname, cname)
        data = gripe.json_util.JSON.file_read(fname)
        if data and "data" in data:
            d = data["data"]
            logger.info("Importing data file %s", fname)
            if hasattr(cls, "import_template_data") and callable(cls.import_template_data):
                cls.import_template_data(d)
            else:
                cls._import_template_data(d)


def delete(model):
    ret = 0
    if model is not None and not hasattr(model, "_brandnew") and model.exists():
        if model._on_delete():
            logger.info("Deleting datamodel %s.%s", model.kind(), model.key())
            ret = grumble.query.ModelQuery.delete_one(model.key())
        else:
            logger.info("on_delete trigger prevented deletion of datamodel %s.%s", model.kind(), model.key())
    return ret


def query(kind, *args, **kwargs):
    kind = grumble.meta.Registry.get(kind)
    q = kind.query(*args, **kwargs)
    return q.fetchall()


def abstract(cls):
    cls._abstract = True
    return cls


def flat(cls):
    cls._flat = True
    return cls


def unaudited(cls):
    cls._audit = False
    return cls


def cached(cls):
    cls._cache = {}
    return cls


class Query(grumble.query.ModelQuery):
    def __init__(self, kind, keys_only=True, include_subclasses=True, raw=False, alias=None, **kwargs):
        super(Query, self).__init__()
        self.kinds = []
        self._kindlist = []
        self._include_subclasses = include_subclasses
        self.keys_only = keys_only
        if isinstance(kind, str):
            self.kinds = [grumble.meta.Registry.get(kind)]
        else:
            try:
                self.kinds = [grumble.meta.Registry.get(k) for k in kind]
            except TypeError:
                self.kinds = [grumble.meta.Registry.get(kind)]
        if "ancestor" in kwargs:
            self.set_ancestor(kwargs["ancestor"])
        parent = kwargs.get("parent")
        if parent:
            self.set_parent(parent)
        self._raw = raw
        self._alias = alias

    def __str__(self):
        return "Query({0}{1}{2}{3}{4})".format(
            ";".join([k.kind() for k in self.kinds]),
            (" (w/ subclasses)" if self._include_subclasses else ""),
            (", filters=" + " AND ".join([str(f) for f in self._conditions])) if self._conditions else '',
            (", parent=" + str(self._parent)) if hasattr(self, "_parent") else '',
            (", ancestor=" + str(self._ancestor)) if hasattr(self, "_ancestor") else '')

    def raw(self):
        return self._raw

    def _reset_state(self):
        self._cur_kind = None
        self._results = None
        self._iter = None

    def set_includesubclasses(self, include_subclasses):
        self._include_subclasses = include_subclasses

    def set_keysonly(self, keys_only):
        self.keys_only = keys_only

    def set_ancestor(self, ancestor):
        for k in self.kinds:
            if grumble.meta.Registry.get(k)._flat:
                logger.debug("Cannot do ancestor queries on flat datamodel %s. Ignoring request", self.kinds)
                return
        logger.debug("%s: setting ancestor to %s", str(self), type(ancestor) if ancestor else "<None>")
        return super(Query, self).set_ancestor(ancestor)

    def set_parent(self, parent):
        for k in self.kindlist():
            if grumble.meta.Registry.get(k)._flat:
                logger.debug("Cannot do ancestor queries on flat datamodel %s. Ignoring request", self.kinds)
                return
        logger.debug("%s: setting parent to %s", str(self), parent)
        return super(Query, self).set_parent(parent)

    def get_kind(self, ix=0):
        return grumble.meta.Registry.get(self.kinds[ix]) if self.kinds and ix < len(self.kinds) else None

    def kindlist(self):
        if not self._kindlist:
            assert self.kinds
            for k in self.kinds:
                if not k.abstract():
                    self._kindlist.append(grumble.meta.Registry.get(k.kind()))
            assert self._kindlist
        return self._kindlist

    def __iter__(self):
        self._iter = iter(self.kindlist())
        self._cur_kind = None
        self._results = None
        if hasattr(self, "initialize_iter"):
            self.initialize_iter()
        return self

    def filter(self, model):
        return model

    def __next__(self):
        ret = None
        cur = None
        while ret is None:
            if self._results:
                cur = next(self._results, None)
            while cur is None:
                self._cur_kind = grumble.meta.Registry.get(next(self._iter))
                self._results = iter(self.execute(self._cur_kind, self.keys_only, subclasses=self._include_subclasses))
                cur = next(self._results, None)
            if cur:
                if self._raw:
                    ret = {k: v for (k, v) in zip(self._results.columns(), cur)}
                else:
                    # print(self, cur, file=sys.stderr)
                    k = grumble.meta.Registry.get(cur[0])
                    model = k.get(
                        grumble.key.Key(k,
                                        cur[self._results.parent_index()] if not k.flat() else None,
                                        cur[self._results.key_index()]),
                        None if self.keys_only else {k: v for (k, v) in zip(self._results.columns(), cur)},
                        self._alias
                    )
                    ret = self.filter(model)
        return ret

    def __len__(self):
        return self.count()

    def count(self):
        ret = 0
        for k in self.kindlist():
            ret += self._count(k, subclasses=self._include_subclasses)
        return ret

    def singleton(self):
        k = self.kindlist()[0]
        r = self.execute(k, self.keys_only, subclasses=self._include_subclasses).single_row()
        return r[1]
        # return self.execute(k, self.keys_only, subclasses=self._include_subclasses).singleton()

    def has(self):
        return self.count() > 0

    def delete(self):
        res = 0
        for k in self.kindlist():
            cls = grumble.meta.Registry.get(k)
            if hasattr(cls, "on_delete") and callable(cls.on_delete):
                for m in self:
                    if m.on_delete():
                        res += self.delete_one(m)
            else:
                res += self._delete(k)
        return res

    def run(self):
        return self.__iter__()

    def get(self):
        i = iter(self)
        try:
            return next(i)
        except StopIteration:
            return None

    def fetchall(self):
        with gripe.db.Tx.begin():
            results = [r for r in self]
            logger.debug("%s.fetchall(): len = %s", str(self), len(results))
            return results
