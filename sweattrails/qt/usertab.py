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

from PyQt5.QtWidgets import QPushButton
from PyQt5.QtWidgets import QSplitter
from PyQt5.QtWidgets import QVBoxLayout
from PyQt5.QtWidgets import QWidget

import gripe
import grizzle
import grumble.qt.bridge
import grumble.qt.view

logger = gripe.get_logger(__name__)

class UserList(grumble.qt.view.TableView):
    def __init__(self, parent = None):
        super(UserList, self).__init__(parent = parent)
        query = grizzle.User.query(keys_only = False)
        query.add_sort("email")
        self.setQueryAndColumns(query, "email", "display_name", "status")
        #self.setMinimumSize(400, 600)
        QCoreApplication.instance().refresh.connect(self.refresh)
        

class UserDetails(grumble.qt.bridge.FormWidget):
    def __init__(self, user, parent = None):
        super(UserDetails, self).__init__(parent)
        self.addProperty(grizzle.User, "email", 0, 0, readonly = True)
        self.addProperty(grizzle.User, "display_name", 1, 0)
        self.addProperty(grizzle.User, "status", 2, 0)
        self.addProperty(grizzle.User, "has_roles", 3 , 0)
        self.statusMessage.connect(QCoreApplication.instance().status_message)

    def setUser(self, user):
        self.setInstance(user)


class UserTab(QSplitter):
    def __init__(self, parent = None):
        super(UserTab, self).__init__(parent)
        self.leftPanel = QWidget(self)
        self.users = UserList(self)
        vbox = QVBoxLayout(self.leftPanel)
        vbox.addWidget(self.users)
        self.addButton = QPushButton("New...", self)
        self.addButton.clicked.connect(self.newUser)
        vbox.addWidget(self.addButton)
        self.addWidget(self.leftPanel)
        self.details = UserDetails(self)
        self.addWidget(self.details)
        self.users.objectSelected.connect(self.details.setUser)

    def newUser(self):
        pass

