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

import datetime

import grumble.property

import bucks.datamodel.transaction


@grumble.property.transient
class OpeningDate(grumble.DateProperty):
    def __init__(self, **kwargs):
        super(OpeningDate, self).__init__(**kwargs)

    @staticmethod
    def getvalue(instance):
        if not instance.is_new():
            q = bucks.datamodel.transaction.OpeningBalanceTx.query(parent=instance)
            opening: bucks.datamodel.transaction.OpeningBalanceTx = q.get()
            return opening.date if opening is not None else None
        else:
            return None

    @staticmethod
    def setvalue(instance, value):
        if not instance.is_new():
            q = bucks.datamodel.transaction.OpeningBalanceTx.query(parent=instance)
            opening: bucks.datamodel.transaction.OpeningBalanceTx = q.get()
            if opening is None:
                opening = bucks.datamodel.transaction.OpeningBalanceTx(parent=instance)
                opening.description = "Opening Balance"
                opening.amt = 0.0
            opening.date = value
            opening.put()


@grumble.property.transient
class OpeningBalance(grumble.FloatProperty):
    def __init__(self, **kwargs):
        super(OpeningBalance, self).__init__(format="$", **kwargs)

    @staticmethod
    def getvalue(instance):
        if not instance.is_new():
            q = bucks.datamodel.transaction.OpeningBalanceTx.query(parent=instance)
            opening: bucks.datamodel.transaction.OpeningBalanceTx = q.get()
            return opening.amt if opening is not None else 0.0
        else:
            return 0

    @staticmethod
    def setvalue(instance, value):
        if not instance.is_new():
            q = bucks.datamodel.transaction.OpeningBalanceTx.query(parent=instance)
            opening: bucks.datamodel.transaction.OpeningBalanceTx = q.get()
            if opening is None:
                opening = bucks.datamodel.transaction.OpeningBalanceTx(parent=instance)
                opening.description = "Opening Balance"
                opening.date = datetime.date.today()
            opening.amt = value
            opening.put()


@grumble.property.transient
class Balance(grumble.FloatProperty):
    def __init__(self, **kwargs):
        super(Balance, self).__init__(readonly=True, format="$", **kwargs)

    @staticmethod
    def getvalue(instance):
        if not instance.is_new():
            q = bucks.datamodel.transaction.Transaction.query(parent=instance)
            q.add_aggregate("amt")
            return q.singleton()
        else:
            return 0


class Account(grumble.model.Model):
    acc_name = grumble.property.TextProperty(is_label=True, verbose_name="Account name")
    acc_nr = grumble.property.TextProperty(verbose_name="Account #")
    description = grumble.property.TextProperty()
    currency = grumble.property.TextProperty(default="CAD")
    importer = grumble.property.TextProperty()
    opening_date = OpeningDate(verbose_name="Opening Date")
    opening_balance = OpeningBalance(verbose_name="Opening Balance")
    current_balance = Balance(verbose_name="Current Balance")

    def set_opening_balance(self, opening_balance, opening_date):
        if self.is_new():
            self.put()
        opening = bucks.datamodel.transaction.OpeningBalanceTx(parent=self)
        opening.amt = opening_balance
        opening.date = opening_date
        opening.description = "Opening Balance"
        opening.put()
