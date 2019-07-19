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

from PyQt5.QtCore import Qt

from PyQt5.QtWidgets import QLabel
from PyQt5.QtWidgets import QVBoxLayout
from PyQt5.QtWidgets import QWidget

import grumpy.bridge
import grumpy.model
import grumpy.view

import bucks.app.base
from bucks.datamodel.institution import Institution
from bucks.datamodel.account import Account


class InstitutionForm(bucks.app.base.BucksForm):
    def __init__(self, tab):
        super(InstitutionForm, self).__init__(None, Institution)
        self.addProperty(Institution, "inst_name", 0, 1)
        self.addProperty(Institution, "description", 1, 1)
        self.acc_label = QLabel("Accounts:")
        self.addWidget(self.acc_label, 2, 1, 1, 1, Qt.AlignTop)
        self.accounts = grumpy.view.TableView(Account.query(keys_only=False), ["acc_name", "description"])
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
        self.table = grumpy.view.TableView(Institution.query(keys_only=False), ["inst_name"])
        self.table.setMinimumSize(400, 300)
        layout.addWidget(self.table)
        self.form = InstitutionForm(self)
        layout.addWidget(self.form)
        self.setLayout(layout)
        self.table.objectSelected.connect(self.form.set_instance)
        self.form.refresh.connect(self.table.refresh)


