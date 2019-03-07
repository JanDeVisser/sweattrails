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

from PyQt5.QtCore import QCoreApplication, pyqtSignal
from PyQt5.QtCore import QTimer
from PyQt5.QtCore import Qt

from PyQt5.QtGui import QDoubleValidator
from PyQt5.QtGui import QPixmap

from PyQt5.QtWidgets import QAction
from PyQt5.QtWidgets import QComboBox
from PyQt5.QtWidgets import QFileDialog
from PyQt5.QtWidgets import QHBoxLayout
from PyQt5.QtWidgets import QLabel
from PyQt5.QtWidgets import QLineEdit
from PyQt5.QtWidgets import QMainWindow
from PyQt5.QtWidgets import QMessageBox
from PyQt5.QtWidgets import QProgressBar
from PyQt5.QtWidgets import QPushButton
from PyQt5.QtWidgets import QSplashScreen
from PyQt5.QtWidgets import QTabWidget
from PyQt5.QtWidgets import QVBoxLayout
from PyQt5.QtWidgets import QWidget

import gripe
import grumble.model
import grumble.property
import grumble.qt.bridge
import grumble.qt.model
import grumble.qt.view
import grumble.reference

import bucks.datamodel


class SplashScreen(QSplashScreen):
    def __init__(self):
        super(SplashScreen, self).__init__(QPixmap("image/splash.png"))


class BucksForm(grumble.qt.bridge.FormWidget):
    def __init__(self, tab, kind, buttons=grumble.qt.bridge.FormButtons.AllButtons, **kwargs):
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


class InstitutionForm(BucksForm):
    def __init__(self, tab):
        super(InstitutionForm, self).__init__(None, bucks.datamodel.Institution)
        self.addProperty(bucks.datamodel.Institution, "inst_name", 0, 1)
        self.addProperty(bucks.datamodel.Institution, "description", 1, 1)
        self.acc_label = QLabel("Accounts:")
        self.addWidget(self.acc_label, 2, 1, 1, 1, Qt.AlignTop)
        self.accounts = grumble.qt.view.TableView(bucks.datamodel.Account.query(keys_only=False), ["acc_name", "description"])
        self.addWidget(self.accounts, 2, 2)

    def assigned(self, key):
        self.accounts.show()
        self.acc_label.show()
        self.accounts.query().set_parent(self.instance())
        self.accounts.refresh()

    def new_instance(self):
        self.accounts.hide()
        self.acc_label.hide()


class InstitutionTab(QWidget):
    def __init__(self, parent):
        super(InstitutionTab, self).__init__(parent=parent)
        layout = QVBoxLayout(self)
        self.table = grumble.qt.view.TableView(bucks.datamodel.Institution.query(keys_only=False), ["inst_name"])
        self.table.setMinimumSize(400, 300)
        layout.addWidget(self.table)
        self.form = InstitutionForm(self)
        layout.addWidget(self.form)
        self.setLayout(layout)
        self.table.objectSelected.connect(self.form.set_instance)
        self.form.refresh.connect(self.table.refresh)


class AccountForm(BucksForm):
    def __init__(self, tab):
        super(AccountForm, self).__init__(tab, bucks.datamodel.Account)
        self.addProperty(bucks.datamodel.Account, "^", 0, 1, refclass=bucks.datamodel.Institution,
                         label="Institution", required=True)
        self.addProperty(bucks.datamodel.Account, "acc_nr", 1, 1)
        self.addProperty(bucks.datamodel.Account, "acc_name", 2, 1)
        self.addProperty(bucks.datamodel.Account, "description", 3, 1)
        self.addProperty(bucks.datamodel.Account, "opening_date", 4, 1)
        self.addProperty(bucks.datamodel.Account, "opening_balance", 5, 1)


class AccountTab(QWidget):
    def __init__(self, parent):
        super(AccountTab, self).__init__(parent=parent)
        layout = QVBoxLayout(self)
        self.table = grumble.qt.view.TableView(bucks.datamodel.Account.query(keys_only=False),
                                               ["acc_name", "description", "current_balance"])
        self.table.setMinimumSize(400, 300)
        layout.addWidget(self.table)
        self.form = AccountForm(self)
        layout.addWidget(self.form)
        self.setLayout(layout)
        self.table.objectSelected.connect(self.form.set_instance)
        self.form.refresh.connect(self.table.refresh)
        QCoreApplication.instance().importer.imported.connect(self.table.refresh)


class CategoryForm(BucksForm):
    def __init__(self, tab):
        super(CategoryForm, self).__init__(tab, bucks.datamodel.Category)
        self.addProperty(bucks.datamodel.Category, "^", 0, 1, refclass=bucks.datamodel.Category,
                         label="Subcategory of", required=False)
        self.addProperty(bucks.datamodel.Category, "cat_name", 2, 1)
        self.addProperty(bucks.datamodel.Category, "description", 3, 1)
        self.addProperty(bucks.datamodel.Category, "current_balance", 4, 1)
        q = bucks.datamodel.Transaction.query(keys_only=False).add_parent_join(bucks.datamodel.Account)
        self.tx_list = grumble.qt.view.TableView(q, ["date", "+p:account.acc_name", "credit", "debit", "description"])
        self.addTab(self.tx_list, "Transactions")
        self.subcategories = grumble.qt.view.TableView(bucks.datamodel.Category.query(keys_only=False), ["cat_name"])
        self.addTab(self.subcategories, "Subcategories")
        QCoreApplication.instance().importer.imported.connect(self.tx_list.refresh)

    def assigned(self, key):
        self.tx_list.query().clear_filters()
        self.tx_list.query().add_filter("category", "->", self.instance())
        self.tx_list.refresh()
        self.subcategories.query().set_parent(self.instance())
        self.subcategories.refresh()

    def new_instance(self):
        pass


class CategoryTab(QWidget):
    def __init__(self, parent):
        super(CategoryTab, self).__init__(parent=parent)
        layout = QVBoxLayout(self)
        self.tree = grumble.qt.view.TreeView(self, bucks.datamodel.Category)
        self.tree.setMinimumSize(400, 300)
        layout.addWidget(self.tree)
        self.form = CategoryForm(self)
        layout.addWidget(self.form)
        self.setLayout(layout)
        self.tree.objectSelected.connect(self.form.set_instance)
        self.form.refresh.connect(self.tree.refresh)
        QCoreApplication.instance().importer.imported.connect(self.tree.refresh)


class ProjectForm(BucksForm):
    def __init__(self, tab):
        super(ProjectForm, self).__init__(tab, bucks.datamodel.Project)
        self.addProperty(bucks.datamodel.Project, "^", 0, 1, refclass=bucks.datamodel.Project,
                         label="Subproject of", required=False)
        self.addProperty(bucks.datamodel.Project, "proj_name", 2, 1)
        self.addProperty(bucks.datamodel.Project, "description", 3, 1)
        self.addProperty(bucks.datamodel.Project, "category", 4, 1)
        self.sub_label = QLabel("Subprojects:")
        self.addWidget(self.sub_label, 5, 1, 1, 1, Qt.AlignTop)
        self.subprojects = grumble.qt.view.TableView(bucks.datamodel.Project.query(keys_only=False), ["proj_name"])
        self.addWidget(self.subprojects, 5, 2)

    def assigned(self, key):
        self.subprojects.show()
        self.sub_label.show()
        self.subprojects.query().set_parent(self.instance())
        self.subprojects.refresh()

    def new_instance(self):
        self.subprojects.hide()
        self.sub_label.hide()


class ProjectTab(QWidget):
    def __init__(self, parent):
        super(ProjectTab, self).__init__(parent=parent)
        layout = QVBoxLayout(self)
        self.tree = grumble.qt.view.TreeView(self, bucks.datamodel.Project)
        self.tree.setMinimumSize(400, 300)
        layout.addWidget(self.tree)
        self.form = ProjectForm(self)
        layout.addWidget(self.form)
        self.setLayout(layout)
        self.tree.objectSelected.connect(self.form.set_instance)
        self.form.refresh.connect(self.tree.refresh)
        QCoreApplication.instance().importer.imported.connect(self.tree.refresh)


class ContactForm(BucksForm):
    def __init__(self, tab):
        super(ContactForm, self).__init__(tab, bucks.datamodel.Contact)
        self.addProperty(bucks.datamodel.Contact, "^", 0, 1, refclass=bucks.datamodel.Contact, query=self.query,
                         label="Alias for", required=False)
        self.addProperty(bucks.datamodel.Contact, "contact_name", 2, 1)
        self.tx_list = grumble.qt.view.TableView(bucks.datamodel.Transaction.query(keys_only=False),
                                                 ["date", "credit", "debit"])
        self.addTab(self.tx_list, "Transactions")

    def assigned(self, key):
        self.get_property_bridge("^").readonly(False)
        self.alias_list.query().set_ancestor(self.instance())
        self.alias_list.refresh()
        self.tx_list.query().clear_filters()
        self.tx_list.query().add_filter("contact", self.instance())
        self.tx_list.refresh()

    def new_instance(self):
        pass

    @staticmethod
    def query():
        return bucks.datamodel.Contact.query(keys_only=False, parent=None)


class ContactTab(QWidget):
    def __init__(self, parent):
        super(ContactTab, self).__init__(parent=parent)
        layout = QVBoxLayout(self)
        self.table = grumble.qt.view.TableView(bucks.datamodel.Contact.query(keys_only=False), ["contact_name", "current_balance"])
        self.table.setMinimumSize(400, 300)
        layout.addWidget(self.table)
        self.form = ContactForm(self)
        layout.addWidget(self.form)
        self.setLayout(layout)
        self.table.objectSelected.connect(self.form.set_instance)
        self.form.refresh.connect(self.table.refresh)
        QCoreApplication.instance().importer.imported.connect(self.table.refresh)


class TransactionTable(QWidget):
    objectSelected = pyqtSignal(grumble.key.Key)

    def __init__(self, parent):
        super(TransactionTable, self).__init__(parent)
        layout = QVBoxLayout(self)
        l = QHBoxLayout()
        l.addWidget(QLabel("Account: "))
        self.account_combo = QComboBox()
        self.account_combo.setModel(grumble.qt.model.ListModel(bucks.datamodel.Account.query(keys_only=False),
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
        self.table = grumble.qt.view.TableView(bucks.datamodel.Transaction.query(keys_only=False),
                                               ["date", "type", "credit", "debit", "description", "category.cat_name",
                                                "project.proj_name", "contact.contact_name"],
                                               self)
        self.table.setMinimumSize(800, 400)
        layout.addWidget(self.table)
        self.setLayout(layout)
        self.table.objectSelected.connect(parent.set_instance)
        QCoreApplication.instance().importer.imported.connect(self.refresh)
        QTimer.singleShot(0, lambda: self.select_account(0))

    def select_account(self, ix):
        account: bucks.datamodel.Account = self.account_combo.currentData(Qt.UserRole)
        self.balance_label.setText("{0:10.2f}".format(account.current_balance))
        self.table.query().set_ancestor(account)
        self.table.refresh()

    def set_instance(self, key):
        self.objectSelected.emit(key)

    def refresh(self, *args):
        account: bucks.datamodel.Account = self.account_combo.currentData(Qt.UserRole)
        self.table.refresh()
        self.balance_label.setText("{0:10.2f}".format(account.current_balance))

    def import_transactions(self):
        account: bucks.datamodel.Account = self.account_combo.currentData(Qt.UserRole)
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


class TransactionForm(BucksForm):
    def __init__(self, parent, kind, **kwargs):
        super(TransactionForm, self).__init__(parent, kind, **kwargs)
        self.addProperty(kind, "^", 0, 1, refclass=bucks.datamodel.Account, label="Account", required=True)
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
        super(DebitCreditTab, self).__init__(parent, bucks.datamodel.Transaction, init_instance=self.init_tx)
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
        self.addProperty(bucks.datamodel.Transaction, "description", 4, 1)
        self.addProperty(bucks.datamodel.Transaction, "category", 5, 1)
        self.addProperty(bucks.datamodel.Transaction, "project", 6, 1)
        self.addProperty(bucks.datamodel.Transaction, "contact", 7, 1)

    def init_tx(self):
        ret = bucks.datamodel.Transaction()
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
        super(TransferTab, self).__init__(parent, bucks.datamodel.Transfer)
        self.addProperty(bucks.datamodel.Transfer, "amt", 3, 1)
        self.addProperty(bucks.datamodel.Transfer, "account", 5, 1)


class OpeningBalanceTab(TransactionForm):
    def __init__(self, parent):
        super(OpeningBalanceTab, self).__init__(parent, bucks.datamodel.OpeningBalanceTx)
        self.addProperty(bucks.datamodel.Transfer, "amt", 3, 1)


class MainWindow(QMainWindow):
    def __init__(self, app):
        super(MainWindow, self).__init__()
        self._app = app
        self.table = None
        file_menu = self.menuBar().addMenu(self.tr("&File"))
        file_menu.addAction(
            QAction("E&xit", self, shortcut="Ctrl+Q", statusTip="Exit", triggered=self.close))
        window = QWidget()
        layout = QVBoxLayout()
        self.tabs = QTabWidget()
        self.tabs.addTab(TransactionTab(self), "Transactions")
        self.tabs.addTab(InstitutionTab(self), "Institutions")
        self.tabs.addTab(AccountTab(self), "Accounts")
        self.tabs.addTab(CategoryTab(self), "Categories")
        self.tabs.addTab(ProjectTab(self), "Projects")
        self.tabs.addTab(ContactTab(self), "Contacts")
        layout.addWidget(self.tabs)
        window.setLayout(layout)
        self.message_label = QLabel()
        self.message_label.setMinimumWidth(200)
        self.statusBar().addPermanentWidget(self.message_label)
        self.progressbar = QProgressBar()
        self.progressbar.setMinimumWidth(100)
        self.progressbar.setMinimum(0)
        self.progressbar.setMaximum(100)
        self.statusBar().addPermanentWidget(self.progressbar)
        self.setCentralWidget(window)

    def app(self):
        return self._app

    def status_message(self, msg, *args):
        self.message_label.setText(str(msg).format(*args))

    def error_message(self, msg, e):
        if e:
            msg = str(e) if not msg else "%s: %s" % (msg, str(e))
        if not msg:
            msg = "Unknown error"
        QMessageBox.error(self, "Error", msg)

    def progress_init(self, msg, *args):
        self.progressbar.setValue(0)
        self.status_message(msg, *args)

    def progress(self, percentage):
        self.progressbar.setValue(percentage)

    def progress_done(self):
        self.progressbar.reset()
