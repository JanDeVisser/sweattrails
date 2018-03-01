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
from PyQt5.QtWidgets import QVBoxLayout
from PyQt5.QtWidgets import QWidget

import gripe.conversions
import grumble.qt.bridge
import grumble.qt.model
import grumble.qt.view
import sweattrails.qt.graphs
import sweattrails.qt.session.list
import sweattrails.qt.session.maps
import sweattrails.qt.view
import sweattrails.session

logger = gripe.get_logger(__name__)


class CriticalPaceList(grumble.qt.view.TableView):
    def __init__(self, parent=None, interval=None):
        super(CriticalPaceList, self).__init__(parent=parent)
        self.interval=interval

        query = sweattrails.session.RunPace.query(keys_only=False)
        query.add_sort("k.distance")
        self.setQueryAndColumns(query,
                                grumble.qt.model.TableColumn("cpdef.name", header="Distance"),
                                sweattrails.qt.view.SecondsColumn("duration", header="Duration"),
                                sweattrails.qt.view.PaceSpeedColumn(interval=interval),
                                sweattrails.qt.view.DistanceColumn("atdistance", header="At distance"),
                                sweattrails.qt.view.TimestampColumn(header="Starting on"))
        self.clicked.connect(self.onClick)

    def resetQuery(self):
        self.query().set_parent(self.interval.intervalpart)

    def onClick(self, ix):
        key = self.model().data(ix, Qt.UserRole)
        if key:
            rp = key()
            p = self.parent()
            while p is not None and not isinstance(p, sweattrails.qt.session.tab.SessionTab):
                p = p.parent()
            if p is not None:
                p.map.drawSegment(rp.timestamp, rp.duration)


class PacesPage(QWidget):
    def __init__(self, parent):
        super(PacesPage, self).__init__(parent)
        self.cplist = CriticalPaceList(self, parent.instance())
        layout = QVBoxLayout(self)
        layout.addWidget(self.cplist)

    def selected(self):
        self.cplist.refresh()


class RunPlugin(object):
    def __init__(self, page, instance):
        self.page = page

    def handle(self, instance):
        logger.debug("Running Run Plugin")
        self.page.addTab(PacesPage(self.page), "Paces")

    def addGraphs(self, graph, interval):
        part = interval.intervalpart
        logger.debug("Pace graph")
        if interval.max_speed:
            graph.addSeries(sweattrails.qt.graphs.Series(property="speed",
                                                         name="Speed",
                                                         max=interval.max_speed,
                                                         smooth=3,
                                                         color=Qt.magenta))
        if part.max_cadence:
            logger.debug("Cadence graph")
            graph.addSeries(sweattrails.qt.graphs.Series(property="cadence",
                                                         name="Cadence",
                                                         max=part.max_cadence,
                                                         smooth=3,
                                                         color=Qt.darkCyan))

    def addMiscData(self, page, interval):
        part = interval.intervalpart
        if part.max_cadence:
            page.addProperty(sweattrails.session.RunPart,
                             "intervalpart.max_cadence",
                             page.row, 0,
                             readonly=True)
            page.addProperty(sweattrails.session.RunPart,
                             "intervalpart.average_cadence",
                             page.row + 1, 0,
                             readonly=True)
            page.row += 2


