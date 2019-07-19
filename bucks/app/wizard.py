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

from PyQt5.QtCore import Qt
from PyQt5.QtGui import QDoubleValidator
from PyQt5.QtWidgets import QDialog, QFormLayout, QLineEdit, QDialogButtonBox, QDateEdit, QFileDialog, QPushButton

import bucks.datamodel
import bucks.schema


class FirstUse(QDialog):
    def __init__(self):
        super(FirstUse, self).__init__()
        layout = QFormLayout(self)
        self.institution = QLineEdit(self)
        fm = self.institution.fontMetrics()
        self.institution.setMaximumWidth(30 * fm.maxWidth() + 11)
        layout.addRow("&Institution:", self.institution)
        self.account = QLineEdit(self)
        fm = self.account.fontMetrics()
        self.account.setMaximumWidth(30 * fm.width('*') + 11)
        layout.addRow("&Account:", self.account)
        self.opened = QDateEdit(self)
        layout.addRow("&Opening Date:", self.opened)
        self.balance = QLineEdit(self)
        self.balance.setValidator(QDoubleValidator(-10000000, 100000000, 2))
        layout.addRow("&Opening Balance:", self.balance)
        self.buttonbox = QDialogButtonBox(
            QDialogButtonBox.Ok | QDialogButtonBox.Cancel,
            Qt.Horizontal, self)
        self.import_button = QPushButton("Import Schema")
        self.buttonbox.addButton(self.import_button, QDialogButtonBox.ActionRole)
        self.buttonbox.accepted.connect(self.create)
        self.buttonbox.rejected.connect(self.reject)
        self.import_button.clicked.connect(self.file_import)
        layout.addRow(self.buttonbox)
        self.setLayout(layout)

    def create(self):
        if not self.institution.text() or not self.account.text():
            return
        institution = bucks.datamodel.Institution()
        institution.inst_name = self.institution.text()
        institution.put()

        account = bucks.datamodel.Account(parent=institution)
        account.acc_name = self.account.text()
        account.put()
        account.set_opening_balance(float(self.balance.text()), self.opened.date().toPyDate())
        self.accept()

    def file_import(self):
        (file_name, _) = QFileDialog.getOpenFileName(self, "Open JSON schema File", "", "JSON Files (*.json)")
        if file_name:
            bucks.schema.SchemaImporter.import_file(file_name)
            self.accept()

