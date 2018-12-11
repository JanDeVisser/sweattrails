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


import operator
import gripe

logger = gripe.get_logger("gripe")


class UrlCollectionElem(object):

    def _initialize(self, objid, label, level):
        self._objid = self._label = self._level = None
        self._ownerobj = None
        self._collection = None
        self._objid = objid
        self.objectlabel(label)
        self.level(level)
        
    def _owner(self, owner):
        assert not owner or (hasattr(owner, "objid") and hasattr(owner, "objectlabel"))
        self._ownerobj = owner
        self._objid = self._label = self._level = None
        self._collection = None
        
    def __copy__(self, other):
        self._ownerobj = other._ownerobj
        self._collection = other._collection
        self._objid = other._objid
        self._label = other._label
        self._level = other._level

    def objid(self):
        if self._ownerobj:
            return self._ownerobj.objid() 
        else:
            return self._objid

    def objectlabel(self, l = None):
        if l is not None:
            self._label = str(l)
        if self._ownerobj:
            return self._ownerobj.objectlabel()
        else:
            return self._label or self.objid()

    def label(self):
        return self.objectlabel()

    def level(self, lvl = None):
        if lvl is not None:
            self._level = int(lvl)
        return self._level

    def collection(self, c = None):
        if c is not None:
            assert isinstance(c, UrlCollection), "UrlCollectionElem.collection argument must be UrlCollection"
            self._collection = c
        return self._collection


class Url(UrlCollectionElem):
    def __init__(self, *args):
        self._url = None
        if len(args) == 1:
            if isinstance(args[0], dict):
                d = args[0]
                self._initialize(d.get("id"), d.get("label"), d.get("level"))
                self.url(d.get("url"))
            elif isinstance(args[0], Url):
                self.url(args[0])
            elif hasattr(args[0], "objid") and hasattr(args[0], "objectlabel"):
                self._owner(args[0])
            elif isinstance(args[0], str):
                self._initialize(args[0], None, 10)
            else:
                assert 0, "Cannot initialize Url with this argument: %s" % args[0]
        else:
            assert 1 < len(args) < 4, "Cannot initialize Url with these args: %s" % args
            self._initialize(args[0], args[1] if len(args) > 1 else None, int(args[3]) if (len(args) > 3) and args[3] else 10)
            self.url(args[2] if len(args) > 2 else None)

    def url(self, u = None):
        if u is not None:
            if isinstance(u, Url):
                self.__copy__(u)
                u = u.url()
            self._url = u
        if self._url is None and self.collection() is not None:
            self._url = self.collection().uri_for(self.objid())
            assert self._url is not None, "No url found for id %s" % self.objid()
        return self._url

    def __repr__(self):
        return '<url id="%s" href="%s" level="%s">%s</url>' % (self.objid(), self.url(), self.level(), self.objectlabel())


class UrlCollection(UrlCollectionElem, dict):
    def __init__(self, *args):
        self._factory = None
        if len(args) == 1:
            if isinstance(args[0], UrlCollection):
                self.copy(args[0])
            elif isinstance(args[0], dict):
                d = args[0]
                self._initialize(d.get("id"), d.get("label"), d.get("level"))
                self.append(d.get("urls"))
            elif hasattr(args[0], "objid") and hasattr(args[0], "objectlabel"):
                self._owner(args[0])
            elif isinstance(args[0], str):
                self._initialize(args[0], None, 10)
            else:
                assert 0, "Cannot initialize UrlCategory with %s <%s>" % (args[0], type(args[0]))
        else:
            assert len(args) > 1, "Cannot initialize UrlCategory with these arguments: %s" % args
            self._initialize(args[0], args[1], int(args[2]) if (len(args) > 2) and args[2] else 10)
            if (len(args) > 3) and args[3]:
                self.append(*args[3:])

    def copy(self, other):
        if isinstance(other, UrlCollection):
            self.__copy__(other)
        for u in other.urls():
            self.append(Url(u))
        for c in other.collections():
            self.append(UrlCollection(c))        
    
    def append(self, *urls):
        for u in urls:
            if isinstance(u, (list, tuple)):
                self.append(*u)
            elif isinstance(u, UrlCollection):
                c = self.get(u.objid())
                if c is not None and isinstance(c, UrlCollection):
                    c.append(u.urls())
                else:
                    c = u
                    self[c.objid()] = c
                c.collection(self)
            elif u is not None:
                u = Url(u)
                u.collection(self)
                self[u.objid()] = u

    def urls(self):
        return sorted([url for url in list(self.values()) if isinstance(url, Url)], key = operator.attrgetter("_level", "_objid"))

    def collections(self):
        return sorted([c for c in list(self.values()) if isinstance(c, UrlCollection)], key = operator.attrgetter("_level", "_objid"))

    def elements(self):
        return sorted(list(self.values()), key = operator.attrgetter("_level", "_objid"))

    def uri_factory(self, factory = None):
        if factory is not None:
            self._factory = factory
        return self._factory if self._factory or (self.collection() is None) else self.collection().uri_factory()

    def uri_for(self, objid):
        f = self.uri_factory()
        return f.uri_for(objid) if f is not None else None

    def __repr__(self):
        return '<collection id="%s" label="%s" level="%s">%s</collection>' % (self.objid(), self.objectlabel(), self.level(), "".join([str(e) for e in self.elements()]))

