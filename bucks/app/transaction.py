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

from PyQt5.QtCore import QCoreApplication, pyqtSignal
from PyQt5.QtCore import QTimer
from PyQt5.QtCore import Qt

from PyQt5.QtGui import QDoubleValidator

from PyQt5.QtWidgets import QComboBox
from PyQt5.QtWidgets import QFileDialog
from PyQt5.QtWidgets import QHBoxLayout
from PyQt5.QtWidgets import QLabel
from PyQt5.QtWidgets import QLineEdit
from PyQt5.QtWidgets import QPushButton
from PyQt5.QtWidgets import QTabWidget
from PyQt5.QtWidgets import QVBoxLayout
from PyQt5.QtWidgets import QWidget

import gripe
import gripe.db
import grumpy.bridge
import grumpy.model
import grumpy.view
import grumble.reference

import bucks.datamodel

from bucks.datamodel.account import Account
from bucks.datamodel.account import Transaction
from bucks.datamodel.account import Transfer
from bucks.datamodel.account import OpeningBalanceTx
from bucks.datamodel.category import Category
from bucks.datamodel.contact import Contact
from bucks.datamodel.project import Project


class TransactionTable(QWidget):
    objectSelected = pyqtSignal(grumble.key.Key)

    def __init__(self, parent):
        super(TransactionTable, self).__init__(parent)
        layout = QVBoxLayout(self)
        l = QHBoxLayout()
        l.addWidget(QLabel("Account: "))
        self.account_combo = QComboBox()
        self.account_combo.setModel(grumpy.model.ListModel(Account.query(keys_only=False),
                                                           "acc_name"))
        self.account_combo.activated[int].connect(self.select_account)
        l.addWidget(self.account_combo)
        self.import_button = QPushButton("Import")
        self.import_button.clicked.connect(self.import_transactions)
        l.addWidget(self.import_button)
        l.addStretch(1)
        l.addWidget(QLabel("Current Balance: "))
        self.balance_label = QLabel("1000000.00")
        l.addWidget(self.balance_label)
        layout.addLayout(l)
        q = Transaction.query(keys_only=False)
        q.add_join(Category, "category", jointype="LEFT")
        q.add_join(Project, "project", jointype="LEFT")
        q.add_join(Contact, "contact", jointype="LEFT")
        self.table = grumpy.view.TableView(q, ["date", "type", "credit", "debit", "description",
                                                   "+category.cat_name", "+project.proj_name",
                                                   "+contact.contact_name"],
                                           self)
        self.table.setMinimumSize(800, 400)
        layout.addWidget(self.table)
        self.setLayout(layout)
        self.table.objectSelected.connect(parent.set_instance)
        QCoreApplication.instance().importer.imported.connect(self.refresh)
        QTimer.singleShot(0, lambda: self.select_account(0))

    def select_account(self, ix):
        with gripe.db.Tx.begin():
            account: Account = self.account_combo.currentData(Qt.UserRole)()
            self.balance_label.setText("{0:10.2f}".format(account.current_balance))
            self.table.query().set_ancestor(account)
            self.table.refresh()

    def set_instance(self, key):
        self.objectSelected.emit(key)

    def refresh(self, *args):
        account: Account = self.account_combo.currentData(Qt.UserRole)
        self.table.refresh()
        self.balance_label.setText("{0:10.2f}".format(account.current_balance))

    def import_transactions(self):
        account: Account = self.account_combo.currentData(Qt.UserRole)
        (file_name, _) = QFileDialog.getOpenFileName(self, "Open Transactions File", "", "CSV Files (*.csv)")
        if file_name:
            QCoreApplication.instance().importer.execute(account, file_name)


class TransactionTab(QWidget):
    def __init__(self, parent):
        super(TransactionTab, self).__init__(parent=parent)
        layout = QVBoxLayout(self)
        self.table = TransactionTable(self)
        self.table.objectSelected.connect(self.set_instance)
        layout.addWidget(self.table)
        self.tabs = QTabWidget()
        self.debit_credit = DebitCreditTab(self)
        self.tabs.addTab(self.debit_credit, "Transaction")
        self.transfer = TransferTab(self)
        self.tabs.addTab(self.transfer, "Transfer")
        self.opening_balance = OpeningBalanceTab(self)
        self.tabs.addTab(self.opening_balance, "Opening Balance")
        layout.addWidget(self.tabs)
        self.setLayout(layout)
        self.debit_credit.refresh.connect(self.table.refresh)
        self.transfer.refresh.connect(self.table.refresh)

    def set_instance(self, instance):
        ix = self.get_tab_index(instance)
        for i in range(0, self.tabs.count()):
            tab: TransactionForm = self.tabs.widget(i)
            tab.set_instance(instance if i == ix else None)
        self.tabs.setCurrentIndex(ix)

    _types = {"C": 0, "D": 0, "T": 1, "O": 2}

    def get_tab_index(self, instance):
        return self._types.get(instance.type) if instance else 0


class TransactionForm(bucks.app.base.BucksForm):
    def __init__(self, parent, kind, **kwargs):
        super(TransactionForm, self).__init__(parent, kind, **kwargs)
        self.addProperty(kind, "^", 0, 1, refclass=Account, label="Account", required=True)
        self.addProperty(kind, "date", 2, 1)

    def tab(self) -> TransactionTab:
        p = self
        while p and not isinstance(p, TransactionTab):
            p = p.parent()
        assert p
        return p

    def new_instance(self):
        account = self.get_property_bridge("^")
        account.set(self.tab().table.account_combo.currentData())


class DebitCreditTab(TransactionForm):
    def __init__(self, parent):
        super(DebitCreditTab, self).__init__(parent, Transaction, init_instance=self.init_tx)
        self.addWidget(QLabel("Amount:"), 3, 1, 1, 1)
        l = QHBoxLayout()
        self.amount = QLineEdit(self)
        self.amount.setValidator(QDoubleValidator(0, 1000000, 2))
        l.addWidget(self.amount)
        self.type = QComboBox(self)
        self.type.addItem("Expense", "D")
        self.type.addItem("Income", "C")
        l.addWidget(self.type)
        self.addLayout(l, 3, 2)
        self.addProperty(Transaction, "description", 4, 1)
        self.addProperty(Transaction, "category", 5, 1)
        self.addProperty(Transaction, "project", 6, 1)
        self.addProperty(Transaction, "contact", 7, 1)

    def init_tx(self):
        ret = Transaction()
        sign = -1.0 if self.type.currentIndex() == 0 else 1.0
        ret.amt = sign * float(self.amount.text())
        return ret

    def assigned(self, key):
        i = self.instance()
        self.type.setCurrentIndex(0 if i.type == "D" else 1)
        sign = -1.0 if i.type == "D" else 1.0
        amt = sign * i.amt
        self.amount.setText("{0:.2f}".format(amt))


class TransferTab(TransactionForm):
    def __init__(self, parent):
        super(TransferTab, self).__init__(parent, Transfer)
        self.addProperty(Transfer, "amt", 3, 1)
        self.addProperty(Transfer, "account", 5, 1)


class OpeningBalanceTab(TransactionForm):
    def __init__(self, parent):
        super(OpeningBalanceTab, self).__init__(parent, OpeningBalanceTx)
        self.addProperty(Transfer, "amt", 3, 1)
