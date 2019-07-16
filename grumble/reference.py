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
import grumble.converter
import grumble.model

logger = gripe.get_logger(__name__)


class ReferenceConverter(grumble.converter.PropertyConverter):
    def __init__(self, prop):
        super(ReferenceConverter, self).__init__(None, prop)

    def convert(self, value):
        return grumble.model.Model.get(value)

    def to_sqlvalue(self, value):
        if value is None:
            return None
        else:
            assert isinstance(value, grumble.model.Model)
            k = value.key()

            return str(k)

    def from_sqlvalue(self, sqlvalue):
        return grumble.model.Model.get(grumble.key.Key(sqlvalue)) if sqlvalue else None


class QueryProperty(object):
    def __init__(self, name, foreign_kind, foreign_key, private=True, serialize=False, verbose_name=None):
        self.name = name
        self.fk_kind = foreign_kind \
            if isinstance(foreign_kind, grumble.meta.ModelMetaClass) \
            else grumble.model.Model.for_name(foreign_kind)
        self.fk = foreign_key
        self.verbose_name = verbose_name if verbose_name else name
        self.serialize = serialize
        self.private = private

    def _get_query(self, instance):
        q = self.fk_kind.query()
        q.add_filter(self.fk, "=", instance)
        return q

    def __get__(self, instance, owner):
        if not instance:
            return self
        return self._get_query(instance)

    def __set__(self, instance, value):
        raise AttributeError("Cannot set Query Property")

    def __delete__(self, instance):
        return NotImplemented

    def _from_json_value(self, value):
        return gripe.NotSerializableError(self.name)

    def _to_json_value(self, instance, value):
        return [obj.to_dict() if self.serialize else obj.id() for obj in self._get_query(instance)]


class ReferenceProperty(grumble.property.ModelProperty):
    datatype = grumble.model.Model
    sqltype = "TEXT"

    def __init__(self, *args, **kwargs):
        super(ReferenceProperty, self).__init__(*args, **kwargs)
        if args and isinstance(args[0], ReferenceProperty):
            prop = args[0]
            self.reference_class = prop.reference_class
            self.collection_name = prop.collection_name
            self.collection_verbose_name = prop.collection_verbose_name
            self.serialize = prop.serialize
            self.collection_serialize = prop.collection_serialize
            self.collection_private = prop.collection_private
        else:
            self.reference_class = args[0] \
                if args \
                else kwargs.get("reference_class")
            if self.reference_class and isinstance(self.reference_class, str):
                self.reference_class = grumble.Model.for_name(self.reference_class)
            assert not self.reference_class or isinstance(self.reference_class, grumble.meta.ModelMetaClass)
            self.collection_name = kwargs.get("collection_name")
            self.collection_verbose_name = kwargs.get("collection_verbose_name")
            self.serialize = kwargs.get("serialize", True)
            self.collection_serialize = kwargs.get("collection_serialize", False)
            self.collection_private = kwargs.get("collection_private", True)
        self.converter = ReferenceConverter(self)

    def set_kind(self, kind):
        super(ReferenceProperty, self).set_kind(kind)
        if not self.collection_name:
            self.collection_name = kind.lower() + "_set"
        if not self.collection_verbose_name:
            self.collection_verbose_name = kind.title()
        if self.reference_class:
            k = grumble.model.Model.for_name(kind)
            qp = QueryProperty(self.collection_name, k, self.name, self.collection_private, self.collection_serialize, self.collection_verbose_name)
            setattr(self.reference_class, self.collection_name, qp)
            self.reference_class._query_properties[self.collection_name] = qp

    def to_json_value(self, instance, value):
        return (value.to_dict() if self.serialize else value.id()) if value else None

    def from_json_value(self, value):
        clazz = self.reference_class
        if isinstance(value, str):
            value = clazz.get(value) if clazz else grumble.model.Model.get(value)
        elif isinstance(value, dict) and ("key" in value):
            value = clazz.get(value["key"])
        elif isinstance(value, dict) and clazz.keyproperty() is not None and (clazz.keyproperty().name in value):
            value = clazz.by(clazz.keyproperty().name, value[clazz.keyproperty().name])
        elif not isinstance(value, clazz):
            assert 0, "Cannot update ReferenceProperty to %s (wrong type %s)" % (value, str(type(value)))
        return value

    def display(self, value, instance=None):
        return value().label() \
            if isinstance(value, (grumble.Key, grumble.Model)) \
            else str(value) if value is not None else ''


class SelfReferenceProperty(ReferenceProperty):
    def set_kind(self, kind):
        self.reference_class = grumble.model.Model.for_name(kind)
        super(SelfReferenceProperty, self).set_kind(kind)
        self.converter = ReferenceConverter(self)
