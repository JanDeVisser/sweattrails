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

import grumble.property


@grumble.property.transient
class EntityBalance(grumble.FloatProperty):
    def __init__(self, **kwargs):
        super(EntityBalance, self).__init__(readonly=True, format="$", **kwargs)
        self.entity_prop = kwargs["entity_prop"]
        self.entity_class = kwargs.get("entity_class")

    def getvalue(self, instance):
        if not instance.is_new():
            from bucks.datamodel.transaction import Transaction
            q = Transaction.query()
            q.add_filter(self.entity_prop, "->", instance)
            q.add_aggregate("amt")
            return q.singleton()