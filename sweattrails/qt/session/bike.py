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

import gripe.conversions
import grumble.qt.bridge
import grumble.qt.model
import grumble.qt.view
import sweattrails.qt.graphs
import sweattrails.qt.session.maps
import sweattrails.qt.session.tab
import sweattrails.qt.view
import sweattrails.session

logger = gripe.get_logger(__name__)


class CriticalPowerList(grumble.qt.view.TableView):
    def __init__(self, parent=None, interval=None):
        super(CriticalPowerList, self).__init__(parent=parent)
        self.interval = interval

        query = sweattrails.session.CriticalPower.query(keys_only=False)
        query.add_join(sweattrails.config.CriticalPowerInterval, "cpdef", "cpi")
        query.add_sort('cpi."duration"')
        self.setQueryAndColumns(query,
                                grumble.qt.model.TableColumn('+cpi."name"', header="Interval"),
                                grumble.qt.model.TableColumn("power", format="d"),
                                sweattrails.qt.view.TimestampColumn("timestamp"),
                                sweattrails.qt.view.DistanceColumn("atdistance"))
        self.clicked.connect(self.onClick)

    def resetQuery(self):
        self.query().set_parent(self.interval.intervalpart)

    def onClick(self, ix):
        key = self.model().data(ix, Qt.UserRole)
        if key:
            cp = key()
            p = self.parent()
            while p is not None and not isinstance(p, sweattrails.qt.session.tab.SessionTab):
                p = p.parent()
            if p is not None:
                p.map.drawSegment(cp.timestamp, cp.cpdef.duration)


class PowerPage(grumble.qt.bridge.FormPage):
    def __init__(self, parent):
        super(PowerPage, self).__init__(parent)
        logger.debug("Initializing power tab")
        self.addProperty(sweattrails.session.BikePart, "intervalpart.max_power", 0, 0,
                         readonly=True)
        self.addProperty(sweattrails.session.BikePart, "intervalpart.average_power", 1, 0,
                         readonly=True)
        self.addProperty(sweattrails.session.BikePart, "intervalpart.normalized_power", 2, 0,
                         readonly=True)
        self.addProperty(sweattrails.session.BikePart, "intervalpart.vi", 3, 0,
                         readonly=True)
        self.addProperty(sweattrails.session.BikePart, "intervalpart.tss", 3, 2,
                         readonly=True)
        self.addProperty(sweattrails.session.BikePart, "intervalpart.intensity_factor", 4, 0,
                         readonly=True)

        self.addProperty(sweattrails.session.BikePart, "intervalpart.max_cadence", 0, 2,
                         readonly=True)
        self.addProperty(sweattrails.session.BikePart, "intervalpart.average_cadence", 1, 2,
                         readonly=True)
        self.cplist = CriticalPowerList(parent, parent.instance())
        self.addWidget(self.cplist, 6, 0, 1, 4)

    def selected(self):
        self.cplist.refresh()


class BikePlugin(object):
    def __init__(self, page, instance):
        self.page = page

    def handle(self, instance):
        logger.debug("Running Bike Plugin")
        part = instance.intervalpart
        if part.max_power:
            self.page.addTab(PowerPage(self.page), "Power")

    def addGraphs(self, graph, interval):
        part = interval.intervalpart
        graph.addSeries(
            sweattrails.qt.graphs.Series(property="speed",
                                         name="Speed",
                                         smooth=3,
                                         max=interval.max_speed,
                                         color=Qt.magenta))
        if part.max_power:
            series = sweattrails.qt.graphs.Series(property="power",
                                                  name="Power",
                                                  graph=graph,
                                                  smooth=3,
                                                  max=part.max_power,
                                                  color=Qt.blue)
            series.addTrendLine(lambda x : float(part.average_power))
            series.addTrendLine(lambda x : float(part.normalized_power),
                                style=Qt.DashDotLine)
        if part.max_cadence:
            series = sweattrails.qt.graphs.Series(property="cadence",
                                                  name="Cadence",
                                                  graph=graph,
                                                  max=part.max_cadence,
                                                  smooth=3,
                                                  color=Qt.darkCyan)
            series.addTrendLine(lambda x : float(part.average_cadence))
