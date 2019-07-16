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

import base64
import binascii
import urllib.parse

import gripe
import grumble.meta

logger = gripe.get_logger(__name__)


def to_key(k):
    ret = None
    if k is not None:
        ret = gripe.call_if_exists(k, "key", None)
        if ret is None:
            ret = Key(urllib.parse.unquote_plus(str(k)))
    return ret


class Key(object):
    def __new__(cls, *args):
        if (len(args) == 1) and hasattr(args[0], "key") and callable(args[0].key):
            return args[0].key()
        else:
            ret = super(Key, cls).__new__(cls)
            ret.id = None
            ret.name = '<<>>'
            ret._scope = None
            ret._kind = '<<>>'
            return ret

    def __init__(self, *args):
        assert args, "Cannot construct void Key"
        if self.id is not None:
            return
        if len(args) == 1:
            value = args[0]
            assert value is not None, "Cannot initialize Key from None"
            if isinstance(value, str):
                self._assign_id(value)
            elif isinstance(value, dict):
                if "id" in value:
                    self.__init__(value["id"])
                elif "key" in value:
                    self.__init__(value["key"])
                elif "kind" in value and "name" in value and "scope" in value:
                    self.__init__(value["kind"], value.get("scope"), value["name"])
                elif "kind" in value and "name" in value:
                    self.__init__(value["kind"], value["name"])
                else:
                    assert 0, "Cannot create Key object from dict %s" % value
            elif gripe.hascallable(value, "key"):
                k = value.key()
                self._kind = k.kind()
                self.id = k.id
                self.name = k.name
                self._scope = k._scope
            else:
                assert 0, "Cannot initialize Key from %s, type %s" % (value, type(value))
        else:
            kind = args[0]
            if not isinstance(kind, str):
                assert isinstance(kind, str) or gripe.hascallable(kind, "kind"), \
                    "First argument of Key(kind, name) must be string or model class, not %s" % type(kind)
                kind = gripe.call_if_exists(kind, "kind", kind)
            assert kind, "Kind must not be empty"
            name = args[-1]
            assert isinstance(name, str), \
                "Last argument of Key(%s, ..., name) must be string, not %s" % (kind, type(name))
            assert name, "Key name argument must not be empty"
            name = urllib.parse.quote_plus(str(args[-1]))
            if len(args) == 3 and args[1] is not None:
                p = args[1]
                assert isinstance(p, str) or gripe.hascallable(p, "key"), \
                    "Scope must be Key, Model or string"
                p = to_key(p)
            else:
                p = None
            self._assign(p, kind, name)
        if not (hasattr(self, "id") and self.id):
            self.id = base64.urlsafe_b64encode(bytearray(str(self), 'UTF-8'))

    def _assign_id(self, ident):
        try:
            value = str(base64.urlsafe_b64decode(bytearray(ident, 'UTF-8')), 'ASCII')
        except binascii.Error:
            value = ident
        key_path = value.split('/')
        scope = '/'.join(key_path[:-1]) if len(key_path) > 1 else None
        (kind, name) = key_path[-1].split(":")
        self._assign(scope, kind, name)

    def _assign(self, scope, kind, name):
        self._kind = grumble.meta.Registry.get(kind).kind()
        # assert self._kind, "Cannot parse key %s: unknown kind %s" % (value, arr[0])
        self.name = urllib.parse.unquote_plus(name)
        if scope:
            self._scope = to_key(scope)
        else:
            self._scope = None
        self._id = "{:s}{:s}:{:s}".format(
            "{:s}/".format(str(self._scope)) if self._scope else '',
            self._kind,
            urllib.parse.quote_plus(self.name))
        self.id = str(base64.urlsafe_b64encode(bytearray(self._id, 'UTF-8')))

    def __str__(self):
        if "_id" in self.__dict__:
            return self._id
        else:
            return super(Key, self).__str__()

    def __call__(self):
        return self.get()
    
    def __getattr__(self, name):
        return getattr(self.get(), name)

    def key(self):
        return self

    def deref(self):
        return self.get()

    def kind(self):
        return self._kind

    def basekind(self):
        (_, _, k) = self.kind().rpartition(".")
        return k

    def samekind(self, model, sub=False):
        return self.modelclass().samekind(model, sub)

    def modelclass(self):
        return grumble.meta.Registry.get(self.kind())

    def scope(self):
        return self._scope

    def ancestors(self):
        ret = []
        p = self.scope()
        while p:
            ret.insert(0, p)
            p = p.scope()
        return ret

    def root(self):
        r = self
        p = self.scope()
        while p:
            r = p
            p = r.scope()
        return r

    def path(self):
        return str(self)

    def __eq__(self, other):
        if not(isinstance(other, Key)) and hasattr(other, "key") and callable(other.key):
            return self == other.key()
        else:
            if not(other or isinstance(other, Key)):
                return False
            else:
                return (self.kind() == other.kind()) and (self.scope() == other.scope()) and (self.name == other.name)

    def __hash__(self):
        return hash(str(self))

    def get(self):
        cls = grumble.meta.Registry.get(self.kind())
        return cls.get(self)


if __name__ == "__main__":
    class ParentModel(grumble.Model):
        pass

    class ChildModel(grumble.Model):
        pass

    kind = 'ParentModel'
    p_name = 'Parent'
    p = Key(kind, p_name)
    print(p.key())

    c = Key(ChildModel, p, "Child")
    print(c)

    cc = Key(c)
    print(cc)

    p_none = Key(kind, None, p_name)
    print(p_none.key())
    print(p_none == p)

    gc = Key(ChildModel, c, "GrandChild")
    print(gc)

    gc_clone = to_key(str(gc))
    assert gc_clone == gc
    print(gc_clone.name)
