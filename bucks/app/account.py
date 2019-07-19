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

from PyQt5.QtWidgets import QVBoxLayout
from PyQt5.QtWidgets import QWidget

import grumpy.bridge
import grumpy.model
import grumpy.view

import bucks.app.base

from bucks.datamodel.account import Account
from bucks.datamodel.institution import Institution
from bucks.datamodel.transaction import Transaction


class AccountForm(bucks.app.base.BucksForm):
    def __init__(self, tab):
        super(AccountForm, self).__init__(tab, Account)
        self.addProperty(Account, "^", 0, 1, refclass=Institution, label="Institution", required=True)
        self.addProperty(Account, "acc_nr", 1, 1)
        self.addProperty(Account, "acc_name", 2, 1)
        self.addProperty(Account, "description", 3, 1)
        self.addProperty(Account, "opening_date", 4, 1)
        self.addProperty(Account, "opening_balance", 5, 1)


class AccountTab(QWidget):
    def __init__(self, parent):
        super(AccountTab, self).__init__(parent=parent)
        layout = QVBoxLayout(self)

        q = Transaction.query(keys_only=False, include_subclasses=True, alias="account")
        q.add_synthetic_column("debit", "(CASE WHEN amt < 0 THEN -amt ELSE 0 END)")
        q.add_synthetic_column("credit", "(CASE WHEN amt > 0 THEN amt ELSE 0 END)")
        q.add_aggregate("k.debit", name="total_debit", groupby=Account, func="SUM")
        q.add_aggregate("k.credit", name="total_credit", groupby=Account, func="SUM")
        q.add_aggregate("amt", name="total", groupby=Account, func="SUM")
        q.add_parent_join(Account, "account")

        self.table = grumpy.view.TableView(q,
                                           ["acc_name", "description", "current_balance",
                                            grumpy.model.TableColumn("debit"),
                                            grumpy.model.TableColumn("credit")],
                                           kind=Account)
        self.table.setMinimumSize(400, 300)
        layout.addWidget(self.table)
        self.form = AccountForm(self)
        layout.addWidget(self.form)
        self.setLayout(layout)
        self.table.objectSelected.connect(self.form.set_instance)
        self.form.refresh.connect(self.table.refresh)
        QCoreApplication.instance().importer.imported.connect(self.table.refresh)
