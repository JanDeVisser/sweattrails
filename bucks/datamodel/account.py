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

from bucks.datamodel.category import Category
from bucks.datamodel.contact import Contact
from bucks.datamodel.project import Project


@grumble.property.transient
class OpeningDate(grumble.DateProperty):
    def __init__(self, **kwargs):
        super(OpeningDate, self).__init__(**kwargs)

    @staticmethod
    def getvalue(instance):
        if not instance.is_new():
            q = OpeningBalanceTx.query(parent=instance)
            opening: OpeningBalanceTx = q.get()
            return opening.date if opening is not None else None
        else:
            return None

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
        opening = OpeningBalanceTx(parent=self)
        opening.amt = opening_balance
        opening.date = opening_date
        opening.description = "Opening Balance"
        opening.put()

    @classmethod
    def get_accounts(cls):
        q = Transaction.query(keys_only=False, include_subclasses=True, alias="account")
        q.add_synthetic_column("debit", "(CASE WHEN amt < 0 THEN -amt ELSE 0 END)")
        q.add_synthetic_column("credit", "(CASE WHEN amt > 0 THEN amt ELSE 0 END)")
        q.add_aggregate("debit", name="total_debit", groupby=Account, func="SUM", default=0.0)
        q.add_aggregate("credit", name="total_credit", groupby=Account, func="SUM", default=0.0)
        q.add_aggregate("amt", name="total", groupby=Account, func="SUM", default=0.0)
        q.add_parent_join(Account, "account")
        q.add_sort("account.acc_name")
        return q

    def transactions(self):
        q = Transaction.query(keys_only=False)
        q.add_synthetic_column("debit", "(CASE WHEN amt < 0 THEN -amt ELSE 0 END)")
        q.add_synthetic_column("credit", "(CASE WHEN amt > 0 THEN amt ELSE 0 END)")
        q.add_join(Category, "category", jointype="LEFT", alias="cat")
        q.add_join(Project, "project", jointype="LEFT", alias="prj")
        q.add_join(Contact, "contact", jointype="LEFT", alias="ctc")
        q.set_ancestor(self)
        q.add_sort("date")
        return q


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
class DebitAmount(grumble.FloatProperty):
    def __init__(self, **kwargs):
        super(DebitAmount, self).__init__(**kwargs)

    @staticmethod
    def getvalue(instance):
        return -instance.amt if instance.amt < 0 else 0.0


@grumble.property.transient
class CreditAmount(grumble.FloatProperty):
    def __init__(self, **kwargs):
        super(CreditAmount, self).__init__(**kwargs)

    @staticmethod
    def getvalue(instance):
        return instance.amt if instance.amt > 0 else 0.0


class Transaction(grumble.model.Model, txtype=lambda i: ("D" if i.amt is None or i.amt < 0 else "C")):
    date = grumble.property.DateProperty()
    type = TransactionType()
    amt = grumble.property.FloatProperty(verbose_name="Amount", format="$", default=0.0)
    currency = grumble.property.TextProperty(default="CAD")
    foreign_amt = grumble.property.FloatProperty(verbose_name="Amount", format="$", default=0.0)
    debit = DebitAmount(verbose_name="Out")
    credit = CreditAmount(verbose_name="In")
    description = grumble.property.TextProperty()
    consolidated = grumble.property.BooleanProperty()
    category = grumble.reference.ReferenceProperty(reference_class=Category)
    project = grumble.reference.ReferenceProperty(reference_class=Project)
    contact = grumble.reference.ReferenceProperty(reference_class=Contact)

    @classmethod
    def for_type(cls, account, typ, *args, **kwargs):
        if typ in ("D", "C"):
            c = Transaction
        elif typ == "T":
            """
                We should check whether the transfer hasn't posted yet as a result of the import of 
                the counter account:
            """
            if "counter" in kwargs:
                amt = kwargs["amt"]

                # Query transfers of this account:
                q = Transfer.query(parent=account)

                # That have a crosspost in the counter account:
                q.add_join(Transfer, "crosspost", alias="cp", where="cp._parent = %s",
                           value=kwargs["counter"].key())

                # With the given amount:
                q.add_filter("amt", amt)

                # And that are not consolidated yet:
                q.add_filter("consolidated", False)

                """
                    If the amount is negative, it's a transfer *from* this account and the cross post will show
                    up *after* this one. If the amount is positive, the cross post will have posted *before* this
                    one. We allow a week lag; this should be enough for domestic transfers.
                """
                if amt < 0:
                    date_from = kwargs["date"]
                    date_to = date_from + datetime.timedelta(days=7)
                else:
                    date_to = kwargs["date"]
                    date_from = date_to + datetime.timedelta(days=-7)
                q.add_condition("k.date BETWEEN %s AND %s", (date_to, date_from))

                transfer = q.get()
                if transfer:
                    """
                        We found a matching transfer. Mark as consolidated so we won't find it again. If there
                        are more transfers in the given window with the exact same amount, we don't really care 
                        what we match up with what; the outcome is the same anyway.
                    """
                    transfer.consolidated = True
                    transfer.crosspost.consolidated = True
                    transfer.crosspost.put()
                    transfer.put()
                    return None
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
    def setvalue(instance, value):
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
