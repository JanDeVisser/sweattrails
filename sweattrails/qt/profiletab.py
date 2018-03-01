#
# Copyright (c) 2014 Jan de Visser (jan@sweattrails.com)
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

from PyQt5.QtCore import QCoreApplication
from PyQt5.QtWidgets import QCheckBox
from PyQt5.QtWidgets import QGridLayout
from PyQt5.QtWidgets import QGroupBox
from PyQt5.QtWidgets import QLabel
from PyQt5.QtWidgets import QLineEdit
from PyQt5.QtWidgets import QWidget

import gripe
import gripe.db
import grizzle
import grumble.qt.bridge
import sweattrails.qt.stackedpage
import sweattrails.userprofile

logger = gripe.get_logger(__name__)

class SettingsPage(grumble.qt.bridge.FormWidget):
    def __init__(self, parent = None):
        super(SettingsPage, self).__init__(parent)
        self.setMinimumSize(800, 600)
        self.addProperty(grizzle.User, "email", 0, 0)
        self.addProperty(grizzle.User, "display_name", 1, 0)
        self.addProperty(sweattrails.userprofile.UserProfile,
                         "_userprofile.dob", 2, 0)
        self.addProperty(sweattrails.userprofile.UserProfile,
                         "_userprofile.gender", 3, 0,
                         style = "radio")
        self.addProperty(sweattrails.userprofile.UserProfile,
                         "_userprofile.height", 4, 0,
                         min = 100, max = 240, suffix = "cm")
        self.addProperty(sweattrails.userprofile.UserProfile,
                         "_userprofile.units", 5, 0,
                         style = "radio")
        
        withingsB = QGroupBox("Withings Support",  self)
        withingsL = QGridLayout(withingsB)
        self.enableWithings = QCheckBox("Enable Withings",  withingsB)
        self.enableWithings.toggled.connect(self.toggleWithings)
        withingsL.addWidget(self.enableWithings, 0, 0)
        withingsL.addWidget(QLabel("Withings User ID"), 1, 0)
        self.withingsUserID = QLineEdit(withingsB)
        withingsL.addWidget(self.withingsUserID, 1, 1)
        withingsL.addWidget(QLabel("Withings Key"), 2, 0)
        self.withingsKey = QLineEdit(withingsB)
        withingsL.addWidget(self.withingsKey, 2, 1)
        self.addWidget(withingsB, self.form.rowCount(), 0, 1, 2)
        self.addStretch()
        self.statusMessage.connect(QCoreApplication.instance().status_message)
        
    def toggleWithings(self, checked):
        with gripe.db.Tx.begin():
            part = self.instance().get_part("WeightMgmt")
            if not part:
                return
            if checked:
                auth = sweattrails.userprofile.WithingsAuth.query(parent = part).get()
                self.withingsUserID.setEnabled(True)
                self.withingsKey.setEnabled(True)
                if auth:
                    self.withingsUserID.setText(auth.userid)
                    self.withingsKey.setText(auth.public_key)
            else:
                self.withingsUserID.setText("")
                self.withingsUserID.setEnabled(False)
                self.withingsKey.setText("")
                self.withingsKey.setEnabled(False)
            
    def assign(self, user):
        with gripe.db.Tx.begin():
            part = user.get_part("WeightMgmt")
            if not part:
                return
            auth = sweattrails.userprofile.WithingsAuth.query(parent = part).get()
            if auth:
                self.enableWithings.setChecked(bool(auth))

    def retrieve(self, user):
        with gripe.db.Tx.begin():
            part = user.get_part("WeightMgmt")
            if not part:
                return
            auth = sweattrails.userprofile.WithingsAuth.query(parent = part).get()
            if self.enableWithings.isChecked():
                if not auth:
                    auth = sweattrails.userprofile.WithingsAuth(parent = part)
                auth.userid = self.withingsUserID.text()
                auth.public_key = self.withingsKey.text()
                auth.put()
            else:
                if auth:
                    grumble.model.delete(auth)
    
    def refresh(self):
        self.activate()

    def activate(self):
        if QCoreApplication.instance().user:
            self.setInstance(QCoreApplication.instance().user)


class ZonesPage(QWidget):
    def __init__(self, parent = None):
        super(ZonesPage, self).__init__(parent)
        self.setMinimumSize(800, 600)

    def activate(self):
        pass
    
    
class HealthPage(QWidget):
    def __init__(self, parent = None):
        super(HealthPage, self).__init__(parent)
        self.setMinimumSize(800, 600)

    def activate(self):
        pass
    
    
class ProfileTab(sweattrails.qt.stackedpage.StackedPage):
    def __init__(self, parent = None):
        super(ProfileTab, self).__init__(parent)
        self.addPage("Settings", SettingsPage(self))
        self.addPage("Zones and FTP", ZonesPage(self))
        self.addPage("Weight and Health", HealthPage(self))
        QCoreApplication.instance().refresh.connect(self.refresh)
        
    def refresh(self):
        for p in self.pages():
            if hasattr(p, "refresh"):
                p.refresh()
