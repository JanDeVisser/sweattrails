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

from PyQt5.QtCore import Qt
from PyQt5.QtGui import QGuiApplication
from PyQt5.QtWidgets import QSplitter

import gripe
import sweattrails.qt.session.details
import sweattrails.qt.session.list
import sweattrails.qt.session.maps
import sweattrails.qt.view
import sweattrails.session

logger = gripe.get_logger(__name__)


class SessionTab(QSplitter):
    def __init__(self, parent=None):
        super(SessionTab, self).__init__(Qt.Horizontal, parent)
        s = QGuiApplication.instance().primaryScreen()
        g = s.geometry()
        self.left_pane = QSplitter(Qt.Vertical, self)
        self.left_pane.setMaximumWidth(int(g.width()*0.5))
        self.sessions = sweattrails.qt.session.list.SessionList(parent=self)
        self.map_holder = sweattrails.qt.session.maps.MapHolder(self)
        self.map = self.map_holder.map
        self.map_holder.setMinimumHeight(int(g.height()*0.375))
        self.left_pane.addWidget(self.sessions)
        self.left_pane.addWidget(self.map_holder)
        self.addWidget(self.left_pane)
        self.details = sweattrails.qt.session.details.SessionDetails(self)
        self.details.setMinimumWidth(int(g.width()*0.625))
        self.addWidget(self.details)
        self.sessions.objectSelected.connect(self.details.setSession)

    def setSession(self, session_id):
        session = sweattrails.session.Session.get_by_key(session_id)
        if session:
            self.details.setSession(session)

    def setTab(self, tab):
        self.details.setTab(tab)
