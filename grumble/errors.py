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


class PropertyRequired(gripe.Error):
    """Raised when no value is specified for a required property"""
    def __init__(self, propname):
        self.propname = propname

    def __str__(self):
        return "Property %s requires a value" % (self.propname,)


class InvalidChoice(gripe.Error):
    """Raised when a value is specified for a property that is not in the
    property's <tt>choices</tt> list"""
    def __init__(self, propname, value):
        self.propname = propname
        self.value = value

    def __str__(self):
        return "Value %s is invalid for property %s" % \
            (self.value, self.propname)


class OutOfRange(gripe.Error):
    """Raised when a value is out of range"""
    def __init__(self, propname, value):
        self.propname = propname
        self.value = value

    def __str__(self):
        return "Value %s out of range for property %s" % \
            (self.value, self.propname)


class ObjectDoesNotExist(gripe.Error):
    """Raised when an object is requested that does not exist"""
    def __init__(self, cls, objid):
        self.cls = cls
        self.id = objid

    def __str__(self):
        return "Model %s:%s does not exist" % (self.cls.__name__, self.id)


class KeyPropertyRequired(gripe.Error):
    """Raised when an object stored but the key property is not set (None)"""
    def __init__(self, cls, propname):
        self.cls = cls
        self.propname = propname

    def __str__(self):
        return "Key property '%s' not set when storing datamodel '%s'" % (self.propname, self.cls)


class PatternNotMatched(gripe.Error):
    """Raised when an object stored but the key property is not set (None)"""
    def __init__(self, propname, value):
        self.propname = propname
        self.value = value

    def __str__(self):
        return "String '%s' does match required pattern for property '%s'" % (self.value, self.propname)
