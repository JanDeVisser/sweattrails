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
from PyQt5.QtCore import QCoreApplication

from PyQt5.QtGui import QValidator

from PyQt5.QtWidgets import QCheckBox
from PyQt5.QtWidgets import QDialog
from PyQt5.QtWidgets import QDialogButtonBox
from PyQt5.QtWidgets import QFormLayout
from PyQt5.QtWidgets import QLineEdit
from PyQt5.QtWidgets import QMessageBox

import gripe
import grizzle
import grumble

logger = gripe.get_logger(__name__)


class SelectUser(QDialog):
    def __init__(self, window = None, **kwargs):
        super(SelectUser, self).__init__(window)
        layout = QFormLayout(self)
        self.email = QLineEdit(self)
        fm = self.email.fontMetrics()
        self.email.setMaximumWidth(30 * fm.maxWidth() + 11)
        self.email.setText(kwargs.get("uid", kwargs.get("email", "")))
        layout.addRow("&User ID:", self.email)
        self.pwd = QLineEdit(self)
        self.pwd.setEchoMode(QLineEdit.Password)
        fm = self.pwd.fontMetrics()
        self.pwd.setMaximumWidth(30 * fm.width('*') + 11)
        self.pwd.setText(kwargs.get("password", ""))
        layout.addRow("&Password:", self.pwd)
        self.savecreds = QCheckBox("&Save Credentials (unsafe)")
        self.savecreds.setChecked(kwargs.get("savecredentials", False))
        layout.addRow(self.savecreds)
        self.buttonbox = QDialogButtonBox(
            QDialogButtonBox.Ok | QDialogButtonBox.Cancel,
            Qt.Horizontal, self)
        self.buttonbox.accepted.connect(self.authenticate)
        self.buttonbox.rejected.connect(self.reject)
        layout.addRow(self.buttonbox)
        self.setLayout(layout)

    def select(self):
        if not QCoreApplication.instance().is_authenticated():
            self.exec_()

    def authenticate(self):
        password = self.pwd.text()
        uid = self.email.text()
        savecreds = self.savecreds.isChecked()
        logger.debug("Authenticating")
        um = grizzle.UserManager()
        with gripe.db.Tx.begin():
            user = um.get(uid)
            if user and user.authenticate(password = password):
                if savecreds:
                    gripe.Config.qtapp.settings["user"] = {
                        "user_id": uid,
                        "password": grumble.property.PasswordProperty.hash(password)
                    }
                    self.config = gripe.Config.set("qtapp", self.config)
                logger.debug("Authenticated. Setting self.user")
                self.user_id = uid
                self.user = user
                self.accept()
            else:
                QMessageBox.critical(self, "Wrong Password",
                    "The user ID and password entered do not match.")


class RepeatPasswordValidator(QValidator):
    def __init__(self, pwdControl):
        super(RepeatPasswordValidator, self).__init__()
        self.pwdControl = pwdControl

    def validate(self, input, pos):
        pwd = self.pwdControl.text()
        if len(input) > len(pwd):
            return QValidator.Invalid
        elif input == pwd:
            return QValidator.Acceptable
        elif pwd.startswith(input):
            return QValidator.Intermediate
        else:
            return QValidator.Invalid


class CreateUser(QDialog):
    def __init__(self, window = None):
        super(CreateUser, self).__init__(window)
        layout = QFormLayout(self)

        self.email = QLineEdit(self)
        fm = self.email.fontMetrics()
        self.email.setMaximumWidth(30 * fm.maxWidth() + 11)
        layout.addRow("&User ID:", self.email)

        self.pwd = QLineEdit(self)
        self.pwd.setEchoMode(QLineEdit.Password)
        fm = self.pwd.fontMetrics()
        self.pwd.setMaximumWidth(30 * fm.width('*') + 11)
        layout.addRow("&Password:", self.pwd)

        self.pwd_again = QLineEdit(self)
        self.pwd_again.setEchoMode(QLineEdit.Password)
        fm = self.pwd_again.fontMetrics()
        self.pwd_again.setMaximumWidth(30 * fm.width('*') + 11)
        validator = RepeatPasswordValidator(self.pwd)
        self.pwd_again.setValidator(validator)
        layout.addRow("Password (&again):", self.pwd_again)

        self.display_name = QLineEdit(self)
        fm = self.display_name.fontMetrics()
        self.display_name.setMaximumWidth(50 * fm.maxWidth() + 11)
        layout.addRow("&Name", self.display_name)

        self.savecreds = QCheckBox("&Save Credentials (unsafe)")
        layout.addRow(self.savecreds)

        self.buttonbox = QDialogButtonBox(
            QDialogButtonBox.Ok | QDialogButtonBox.Cancel, Qt.Horizontal, self)
        self.buttonbox.accepted.connect(self.create)
        self.buttonbox.rejected.connect(self.reject)
        layout.addRow(self.buttonbox)
        self.setLayout(layout)

    def create(self):
        password = self.pwd.text()
        if password != self.pwd_again.text():
            QMessageBox.critical(self, "Passwords don't match",
                "The passwords entered are different")
            self.reject()
        try:
            QCoreApplication.instance().add_user(self.email.text(),
                                                 password,
                                                 self.display_name.text(),
                                                 self.savecreds.isChecked())
            self.accept()
        except Exception as e:
            logger.exception("Exception creating user")
            QMessageBox.critical(self, "Error", str(e))
            self.reject()


def authenticate(**kwargs):
    dialog = SelectUser(QCoreApplication.instance().activeWindow(), **kwargs)
    dialog.exec_()
    ret = dialog.user
    dialog.deleteLater()
    return ret
