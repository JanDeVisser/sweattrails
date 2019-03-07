#
# Copyright (c) 2017 Jan de Visser (jan@sweattrails.com)
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

import datetime

import grumble
import grumble.model
import grumble.property


@grumble.property.transient
class EntityBalance(grumble.FloatProperty):
    def __init__(self, **kwargs):
        super(EntityBalance, self).__init__(readonly=True, format="$", **kwargs)
        self.entity_prop = kwargs["entity_prop"]
        self.entity_class = kwargs.get("entity_class")

    def getvalue(self, instance):
        if not instance.is_new():
            q = Transaction.query()
            q.add_filter(self.entity_prop, "->", instance)
            q.sum("amt")
            return q.aggregate()


class Category(grumble.model.Model):
    cat_name = grumble.property.TextProperty(verbose_name="Category", is_label=True)
    description = grumble.property.TextProperty()
    current_balance = EntityBalance(verbose_name="Current Balance", entity_prop="category")


class Project(grumble.model.Model):
    proj_name = grumble.property.TextProperty(verbose_name="Project", is_label=True)
    description = grumble.property.TextProperty()
    category = grumble.reference.ReferenceProperty(reference_class=Category, verbose_name="Default Category")
    current_balance = EntityBalance(verbose_name="Current Balance", entity_prop="project")


class Contact(grumble.model.Model):
    contact_name = grumble.property.TextProperty(is_label=True, verbose_name="Contact Name")
    interac_address = grumble.property.TextProperty()
    account_info = grumble.property.TextProperty()
    current_balance = EntityBalance(verbose_name="Current Balance", entity_prop="contact")


class Institution(grumble.model.Model):
    inst_name = grumble.property.TextProperty(is_label=True, verbose_name="Institution")
    description = grumble.property.TextProperty()


@grumble.property.transient
class OpeningDate(grumble.DateProperty):
    def __init__(self, **kwargs):
        super(OpeningDate, self).__init__(**kwargs)

    @staticmethod
    def getvalue(instance):
        q = OpeningBalanceTx.query(parent=instance)
        opening: OpeningBalanceTx = q.get()
        return opening.date if opening is not None else None

    @staticmethod
    def setvalue(instance, value):
        if not instance.is_new():
            q = OpeningBalanceTx.query(parent=instance)
            opening: OpeningBalanceTx = q.get()
            if opening is None:
                opening = OpeningBalanceTx(parent=instance)
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
            q = OpeningBalanceTx.query(parent=instance)
            opening: OpeningBalanceTx = q.get()
            return opening.amt if opening is not None else 0.0
        else:
            return 0

    @staticmethod
    def setvalue(instance, value):
        if not instance.is_new():
            q = OpeningBalanceTx.query(parent=instance)
            opening: OpeningBalanceTx = q.get()
            if opening is None:
                opening = OpeningBalanceTx(parent=instance)
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
            q = Transaction.query(parent=instance)
            q.sum("amt")
            return q.aggregate()
        else:
            return 0


class Account(grumble.model.Model):
    acc_name = grumble.property.TextProperty(is_label=True, verbose_name="Account name")
    acc_nr = grumble.property.TextProperty(verbose_name="Account #")
    description = grumble.property.TextProperty()
    importer = grumble.property.TextProperty()
    opening_date = OpeningDate(verbose_name="Opening Date")
    opening_balance = OpeningBalance(verbose_name="Opening Balance")
    current_balance = Balance(verbose_name="Current Balance")

    def set_opening_balance(self, opening_balance, opening_date):
        if self.is_new():
            self.put()
        opening = OpeningBalanceTx(parent=self)
        opening.amt = opening_balance
        opening.date = opening_date
        opening.description = "Opening Balance"
        opening.put()


@grumble.property.transient
class TransactionType(grumble.TextProperty):
    def __init__(self, **kwargs):
        super(TransactionType, self).__init__(**kwargs)

    @staticmethod
    def getvalue(instance):
        txtype = instance.__class__.config.get("txtype")
        if txtype is None:
            print("instance: ", str(instance), " class ", instance.__class__, " config ", instance.__class__.config)
            assert 0
        if callable(txtype):
            txtype = txtype(instance)
        return txtype


@grumble.property.transient
class DebitAmount(grumble.TextProperty):
    def __init__(self, **kwargs):
        super(DebitAmount, self).__init__(**kwargs)

    @staticmethod
    def getvalue(instance):
        return "{0:10.2f}".format(-instance.amt) if instance.amt < 0 else " " * 10


@grumble.property.transient
class CreditAmount(grumble.TextProperty):
    def __init__(self, **kwargs):
        super(CreditAmount, self).__init__(**kwargs)

    @staticmethod
    def getvalue(instance):
        return "{0:10.2f}".format(instance.amt) if instance.amt > 0 else " " * 10


class Transaction(grumble.model.Model, txtype=lambda i: ("D" if i.amt < 0 else "C")):
    date = grumble.property.DateProperty()
    type = TransactionType()
    amt = grumble.property.IntProperty(verbose_name="Amount", format="$", default=0.0)
    debit = DebitAmount(verbose_name="Out")
    credit = CreditAmount(verbose_name="In")
    description = grumble.property.TextProperty()
    category = grumble.reference.ReferenceProperty(reference_class=Category)
    project = grumble.reference.ReferenceProperty(reference_class=Project)
    contact = grumble.reference.ReferenceProperty(reference_class=Contact)

    @classmethod
    def for_type(cls, account, typ, *args, **kwargs):
        if typ in ("D", "C"):
            c = Transaction
        elif typ == "T":
            c = Transfer
        elif typ == "O":
            c = OpeningBalanceTx
        elif typ == "V":
            c = ValueChange
        else:
            c = None
        kwargs["parent"] = account
        return c(*args, **kwargs) if c else None


@grumble.property.transient
class TransferToAccount(grumble.ReferenceProperty):
    def __init__(self, **kwargs):
        super(TransferToAccount, self).__init__(reference_class=Account, **kwargs)

    @staticmethod
    def getvalue(instance):
        return instance.crosspost.parent() if instance.crosspost else None

    @staticmethod
    def setvalue(instance, value: Account):
        if not value:
            return
        cp = instance.crosspost
        if cp is not None and cp.parent() != value:
            instance.crosspost = None
            grumble.model.delete(cp)
        if instance.crosspost is None:
            cp = Transfer(parent=value, crosspost=instance)
            cp.amt = -1.0 * instance.amt
            cp.date = instance.date
            cp.description = \
                "Transfer {0:s} account {1:s}".format("to" if instance.amt > 0.0 else "from",
                                                      instance.parent().acc_name)
            cp.put()
            instance.crosspost = cp
            instance.description = \
                "Transfer {0:s} account {1:s}".format("from" if instance.amt > 0.0 else "to", value.acc_name)


class Transfer(Transaction, txtype="T"):
    crosspost = grumble.reference.SelfReferenceProperty()
    account = TransferToAccount(verbose_name="Transfer from/to")


class OpeningBalanceTx(Transaction, txtype="O"):
    pass


class ValueChange(Transaction, txtype="V"):
    pass
