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

from PyQt5.QtCore import QCoreApplication
from PyQt5.QtCore import QTimer

import grumble.model
import grumpy.bridge
import grumpy.model


class BucksForm(grumpy.bridge.FormWidget):
    def __init__(self, tab, kind, buttons=grumpy.bridge.FormButtons.AllButtons, **kwargs):
        super(BucksForm, self).__init__(tab, buttons, **kwargs)
        self.kind(kind)
        self.statusMessage.connect(QCoreApplication.instance().status_message)
        self.exception.connect(QCoreApplication.instance().status_message)
        if hasattr(self, "assigned") and callable(self.assigned):
            self.instanceAssigned.connect(self.assigned)
        if hasattr(self, "new_instance") and callable(self.new_instance):
            self.newInstance.connect(self.new_instance)
        if hasattr(self, "saved") and callable(self.saved):
            self.instanceSaved.connect(self.saved)
        QTimer.singleShot(0, lambda: self.set_instance(None) if self.instance() is None else None)


class MoneyColumn(grumpy.model.TableColumn):
    def __init__(self, attr, header=None, condition=None):
        super(MoneyColumn, self).__init__(attr,
                                          header=header if header is not None else attr.replace('_', ' ').title())
        self._condition = condition \
            if condition is not None \
            else lambda v: True

    def get_value(self, data):
        v = data[self._attr] if self._attr in data else 0.0
        if v:
            v = float(v)
            if abs(v) >= 0.01:
                if self._condition(v):
                    return "{0:.2f}".format(v)
        return None


class BucksTreeItem(grumpy.model.TreeItem):
    def __init__(self, *args, **kwargs):
        super(BucksTreeItem, self).__init__(*args, **kwargs)

    def on_assign(self, obj: grumble.model.Model):
        obj.add_adhoc_property("cum_debit", obj.total_debit if obj.total_debit is not None else 0.0)
        obj.add_adhoc_property("cum_credit", obj.total_credit if obj.total_credit is not None else 0.0)
        obj.add_adhoc_property("cum_total", obj.total if obj.total is not None else 0.0)

    def on_append(self, child):
        obj = child.get_object()
        p = self
        while p and p.get_object():
            p_obj = p.get_object()
            p_obj.cum_debit += obj.cum_debit
            p_obj.cum_credit += obj.cum_credit
            p_obj.cum_total += obj.cum_total
            p = p.parentItem()
