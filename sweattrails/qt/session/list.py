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

from PyQt5.QtCore import QCoreApplication

import gripe
import grumble.qt.bridge
import grumble.qt.model
import grumble.qt.view
import sweattrails.session

logger = gripe.get_logger(__name__)


class DescriptionColumn(grumble.qt.model.TableColumn):
    def __init__(self):
        super(DescriptionColumn, self).__init__("description")

    def __call__(self, session):
        if not session.description:
            sessiontype = session.sessiontype
            ret = sessiontype.name
        else:
            ret = session.description
        return ret


class SessionList(grumble.qt.view.TableView):
    def __init__(self, user = None, parent=None):
        super(SessionList, self).__init__(parent=parent)

        if not user:
            user = QCoreApplication.instance().user
        query = sweattrails.session.Session.query(keys_only=False)
        query.add_filter("athlete", "=", user)
        query.add_sort("start_time", False)
        self.setQueryAndColumns(query,
                                grumble.qt.model.TableColumn("start_time", format="%A", header="Day"),
                                grumble.qt.model.TableColumn("start_time", format="%d %B", header="Date"),
                                grumble.qt.model.TableColumn("start_time", format="%H:%M", header="Time"),
                                DescriptionColumn())
        QCoreApplication.instance().refresh.connect(self.refresh)

    def resetQuery(self):
        user = QCoreApplication.instance().user
        self.query().clear_filters()
        self.query().add_filter("athlete", "=", user)
