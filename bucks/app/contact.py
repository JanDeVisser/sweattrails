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
from bucks.datamodel.contact import Contact
from bucks.datamodel.transaction import Transaction


class ContactForm(bucks.app.base.BucksForm):
    def __init__(self, tab):
        super(ContactForm, self).__init__(tab, Contact)
        self.addProperty(Contact, "^", 0, 1, refclass=Contact, query=self.query, label="Alias for", required=False)
        self.addProperty(Contact, "contact_name", 2, 1)
        q = Transaction.query(keys_only=False, include_subclasses=True, raw=True)
        q.add_parent_join(Account)
        q.add_filter("contact", "XX")
        self.tx_list = grumpy.view.TableView(q,
                                             [
                                                grumpy.model.TableColumn(attr='date', header='Date'),
                                                grumpy.model.TableColumn(attr='+p."acc_name"', header='Account'),
                                                bucks.app.base.DebitCreditColumn(attr='amt',
                                                                                 sign=bucks.app.base.DebitCreditColumn.Debit),
                                                bucks.app.base.DebitCreditColumn(attr='amt',
                                                                                 sign=bucks.app.base.DebitCreditColumn.Credit),
                                                grumpy.model.TableColumn(attr='description',
                                                                         header='Description')
                                               ],
                                             key_attr='_key')
        self.addTab(self.tx_list, "Transactions")

    def assigned(self, key):
        self.get_property_bridge("^").readonly(False)
        self.tx_list.query().clear_filters()
        self.tx_list.query().add_filter("contact", self.instance())
        self.tx_list.refresh()

    def new_instance(self):
        pass

    @staticmethod
    def query():
        return Contact.query(keys_only=False, parent=None)


class ContactTab(QWidget):
    def __init__(self, parent):
        super(ContactTab, self).__init__(parent=parent)
        layout = QVBoxLayout(self)
        q = Transaction.query(keys_only=False, include_subclasses=True, raw=True)
        q.add_aggregate("amt", name="total", groupby=Contact, func="SUM")
        q.add_join(Contact, "contact", jointype="RIGHT")
        self.table = grumpy.view.TableView(q,
                                           [
                                                bucks.app.base.DebitCreditColumn(attr='total',
                                                                                 sign=bucks.app.base.DebitCreditColumn.Debit),
                                                bucks.app.DebitCreditColumn(attr='total',
                                                                            sign=bucks.app.base.DebitCreditColumn.Credit),
                                                grumpy.model.TableColumn(attr='contact."contact_name"',
                                                                         header='Name')
                                               ],
                                           key_attr='contact."_key"')
        self.table.setMinimumSize(400, 300)
        layout.addWidget(self.table)
        self.form = ContactForm(self)
        layout.addWidget(self.form)
        self.setLayout(layout)
        self.table.objectSelected.connect(self.form.set_instance)
        self.form.refresh.connect(self.table.refresh)
        QCoreApplication.instance().importer.imported.connect(self.table.refresh)
