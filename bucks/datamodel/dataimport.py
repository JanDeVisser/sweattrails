#
#   Copyright (c) 2019 Jan de Visser (jan@sweattrails.com)
#
#   This program is free software; you can redistribute it and/or modify it
#   under the terms of the GNU General Public License as published by the Free
#   Software Foundation; either version 2 of the License, or (at your option)
#   any later version.
#
#   This program is distributed in the hope that it will be useful, but WITHOUT
#   ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
#   FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
#   more details.
#
#   You should have received a copy of the GNU General Public License along
#   with this program; if not, write to the Free Software Foundation, Inc., 51
#   Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
#

import codecs
import enum
import sys
import traceback

import grumble.property

import bucks.datamodel.account


class ImportStatus(enum.Enum):
    Initial = "Initial"
    Read = "Read"
    InProgress = "In Progress"
    Completed = "Completed"
    Error = "Error"
    Partial = "Partial"


class Import(grumble.model.Model):
    timestamp = grumble.property.DateTimeProperty(auto_now_add=True)
    account = grumble.reference.ReferenceProperty(reference_class=bucks.datamodel.account.Account)
    filename = grumble.property.StringProperty()
    status = grumble.property.EnumProperty(enum=ImportStatus, default=ImportStatus.Initial)
    code = grumble.property.TextProperty(verbose_name="Error code")
    data = grumble.property.StringProperty()
    errors = grumble.property.StringProperty()

    def read(self):
        try:
            with codecs.open(self.filename, encoding="utf-8") as f:
                self.data = f.read()
                self.status = ImportStatus.Read
        except Exception:
            self.errors = traceback.format_exc()
            self.status = ImportStatus.Error
            self.data = None
        finally:
            self.put()
        return self.data

    def log_error(self, what_to_raise: callable = None):
        self.errors = (self.errors + "\n" if self.errors else "") + traceback.format_exc()
        if what_to_raise:
            self.code = what_to_raise.__name__.split(".")[-1]
            raise what_to_raise().with_traceback(sys.exc_info()[2])
