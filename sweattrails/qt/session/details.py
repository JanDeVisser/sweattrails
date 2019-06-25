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

import traceback

from PyQt5.QtCore import QCoreApplication
from PyQt5.QtCore import Qt
from PyQt5.QtWidgets import QPushButton
from PyQt5.QtWidgets import QTabWidget
from PyQt5.QtWidgets import QVBoxLayout
from PyQt5.QtWidgets import QWidget

import gripe
import gripe.conversions
import grumpy.bridge
import grumpy.model
import grumpy.view
import grumpy.bg.job
import sweattrails.qt.graphs
import sweattrails.qt.session.bike
import sweattrails.qt.session.maps
import sweattrails.qt.session.run
import sweattrails.qt.session.tab
import sweattrails.qt.view
import sweattrails.session

logger = gripe.get_logger(__name__)


class MiscDataPage(grumpy.bridge.FormPage):
    def __init__(self, parent, instance):
        super(MiscDataPage, self).__init__(parent)
        self.row = 0
        if instance.geodata:
            self.addProperty(sweattrails.session.GeoData, "geodata.max_elev", self.row, 0,
                             readonly=True,
                             displayconverter=sweattrails.qt.view.MeterFeet)
            self.addProperty(sweattrails.session.GeoData, "geodata.min_elev", self.row + 1, 0,
                             readonly=True,
                             displayconverter=sweattrails.qt.view.MeterFeet)
            self.addProperty(sweattrails.session.GeoData, "geodata.elev_gain", self.row, 2,
                             readonly=True,
                             displayconverter=sweattrails.qt.view.MeterFeet)
            self.addProperty(sweattrails.session.GeoData, "geodata.elev_loss", self.row + 1, 2,
                             readonly=True,
                             displayconverter=sweattrails.qt.view.MeterFeet)
            self.row += 2
        if instance.max_heartrate:
            self.addProperty(sweattrails.session.Interval, "max_heartrate", self.row, 0,
                             readonly=True,
                             suffix="bpm")
            self.addProperty(sweattrails.session.Interval, "average_heartrate", self.row + 1, 0,
                             readonly=True,
                             suffix="bpm")
            self.row += 2
        if parent.plugin and hasattr(parent.plugin, "addMiscData"):
            parent.plugin.addMiscData(self, instance)
        if instance.work:
            self.addProperty(sweattrails.session.Interval, "work", self.row, 0,
                             suffix="kJ",
                             readonly=True)
            self.row += 1
        if instance.calories_burnt:
            self.addProperty(sweattrails.session.Interval, "calories_burnt", self.row, 0,
                             suffix="kcal",
                             readonly=True)
            self.row += 1
        self.addProperty(sweattrails.session.Interval, "interval_id", self.row,
                         0, readonly=True)
        self.row += 1


class Waypoints(sweattrails.qt.graphs.DataSource, sweattrails.qt.graphs.Axis):
    def __init__(self, interval):
        logger.debug("Waypoints.__init__ %s", type(self));
        super(Waypoints, self).__init__(property="distance",
                                        name="Waypoints",
                                        offset=0)
        self.interval = interval

    def fetch(self):
        with gripe.db.Tx.begin():
            return self.interval.waypoints()


class GraphPage(QWidget):
    def __init__(self, parent, instance):
        super(GraphPage, self).__init__(parent)
        self.graphs = sweattrails.qt.graphs.Graph(
            self, Waypoints(instance))
        if instance.max_heartrate:
            self.graphs.addSeries(
                sweattrails.qt.graphs.Series(
                    max=instance.max_heartrate,
                    name="Heartrate",
                    property="heartrate",
                    smooth=0,
                    color=Qt.red))
        if instance.geodata:
            self.graphs.addSeries(sweattrails.qt.graphs.Series(
                min=instance.geodata.min_elev,
                max=instance.geodata.max_elev,
                value=(lambda wp:
                       wp.corrected_elevation
                       if wp.corrected_elevation is not None
                       else wp.elevation if wp.elevation else 0),
                name="elevation",
                smooth=0,
                color="peru",
                shade="sandybrown"))
        if parent.plugin and hasattr(parent.plugin, "addGraphs") and callable(parent.plugin.addGraphs):
            parent.plugin.addGraphs(self.graphs, instance)
        layout = QVBoxLayout(self)
        layout.addWidget(self.graphs)


class MapPage(QWidget):
    def __init__(self, parent, instance):
        super(MapPage, self).__init__(parent)
        layout = QVBoxLayout(self)
        self.map = sweattrails.qt.session.maps.IntervalMap(self)
        self.interval = instance
        layout.addWidget(self.map)


    def selected(self):
        self.map.drawMap(self.interval)


class IntervalList(grumpy.view.TableView):
    def __init__(self, parent, interval):
        super(IntervalList, self).__init__(parent=parent)
        self.interval = interval

        query = sweattrails.session.Interval.query(parent=self.interval, keys_only=False)
        query.add_sort("k.timestamp")
        self.setQueryAndColumns(query,
                                sweattrails.qt.view.TimestampColumn(header="Start Time"),
                                sweattrails.qt.view.TimestampColumn("duration", header="Time"),
                                sweattrails.qt.view.DistanceColumn("distance", header="Distance"),
                                sweattrails.qt.view.PaceSpeedColumn("average_speed", interval=interval))
        self.clicked.connect(self.on_click)


    def resetQuery(self):
        self.query().set_parent(self.interval)

    def on_click(self, ix):
        key = self.model().data(ix, Qt.UserRole)
        if key:
            interval = key()
            p = self.parent()
            while p is not None and not isinstance(p, sweattrails.qt.session.tab.SessionTab):
                p = p.parent()
            if p is not None:
                p.map.draw_segment(interval.timestamp, interval.duration)


class IntervalListPage(QWidget):
    def __init__(self, parent):
        super(IntervalListPage, self).__init__(parent)
        self.list = IntervalList(self, parent.instance())
        layout = QVBoxLayout(self)
        layout.addWidget(self.list)

    def selected(self):
        self.list.refresh()


class RawDataList(grumpy.view.TableView):
    def __init__(self, parent=None, interval=None):
        super(RawDataList, self).__init__(parent=parent)

        query = sweattrails.session.Waypoint.query(parent=interval,
                                                   keys_only=False)
        query.add_sort("timestamp")
        self.setQueryAndColumns(query,
                                grumpy.model.TableColumn("timestamp", header="Timestamp"),
                                grumpy.model.TableColumn("location"),
                                grumpy.model.TableColumn("elevation"),
                                grumpy.model.TableColumn("corrected_elevation", header="Corrected"),
                                grumpy.model.TableColumn("speed"),
                                grumpy.model.TableColumn("distance"),
                                grumpy.model.TableColumn("cadence"),
                                grumpy.model.TableColumn("heartrate"),
                                grumpy.model.TableColumn("power"),
                                grumpy.model.TableColumn("torque"),
                                grumpy.model.TableColumn("temperature"))
        QCoreApplication.instance().refresh.connect(self.refresh)


class RawDataPage(QWidget):
    def __init__(self, parent):
        super(RawDataPage, self).__init__(parent)
        self.list = RawDataList(self, parent.instance())
        layout = QVBoxLayout(self)
        layout.addWidget(self.list)

    def selected(self):
        try:
            self.list.refresh()
        except:
            traceback.print_exc()


class ReanalyzeJob(grumpy.bg.job.Job):
    def __init__(self, interval):
        super(ReanalyzeJob, self).__init__()
        self.interval = interval

    def handle(self):
        self.interval.reanalyze(self)

    def __str__(self):
        return "Re-analyzing %s" % self.interval.interval_id


class IntervalPage(grumpy.bridge.FormWidget):
    def __init__(self, interval, parent=None):
        super(IntervalPage, self).__init__(parent,
                                           grumpy.bridge.FormButtons.AllButtons
                                           if interval.basekind() == "session"
                                           else grumpy.bridge.FormButtons.EditButtons)
        self.plugin = None
        with gripe.db.Tx.begin():
            interval = interval()
            self.interval = interval
            if interval.basekind() == "session":
                self.addProperty(sweattrails.session.Session, "sessiontype", 0, 0,
                                 readonly=True, has_label=False, rowspan=3,
                                 bridge=grumpy.bridge.Image, height=64,
                                 displayconverter=sweattrails.qt.view.SessionTypeIcon)
                self.addProperty(sweattrails.session.Session, "start_time", 0, 1, readonly=True)
                self.addProperty(sweattrails.session.Session, "description", 1, 1, colspan=3)
                col = 1
                row = 2
                self.analyzebutton = QPushButton("Re-Analyze", self)
                self.analyzebutton.clicked.connect(self.analyze)
                self.addWidgetToButtonBox(self.analyzebutton)
            else:
                self.addProperty(sweattrails.session.Interval, "timestamp", 0, 0, colspan=3,
                                 readonly=True)
                col = 0
                row = 1
            self.addProperty(sweattrails.session.Interval, "elapsed_time", row, col,
                             readonly=True)
            self.addProperty(sweattrails.session.Interval, "duration", row, col + 2,
                             readonly=True)
            row += 1
            self.addProperty(sweattrails.session.Interval, "distance", row, col,
                             readonly=True,
                             displayconverter=sweattrails.qt.view.Distance)
            row += 1
            self.addProperty(sweattrails.session.Interval, "average_speed", row, col,
                             readonly=True,
                             displayconverter=sweattrails.qt.view.PaceSpeed,
                             labelprefixes="Average")
            self.addProperty(sweattrails.session.Interval, "max_speed", row, col + 2,
                             readonly=True,
                             displayconverter=sweattrails.qt.view.PaceSpeed,
                             labelprefixes={"Pace": "Best", "Speed": "Maximum"})
            row += 1
            self.set_instance(interval)
            intervals = sweattrails.session.Interval.query(parent=interval).fetchall()
            if len(intervals) > 1:
                page = IntervalListPage(self)
                self.addTab(page, "Intervals")
                page.list.objectSelected.connect(parent.addInterval)
            self.part_specific_content(interval)
            self.addTab(GraphPage(self, interval), "Graphs")
            # self.addTab(MapPage(self, interval), "Map")
            self.addTab(MiscDataPage(self, interval), "Other Data")
            if interval.basekind() == "session":
                self.addTab(RawDataPage(self), "Raw Data")

            self.statusMessage.connect(QCoreApplication.instance().status_message)
            self.exception.connect(QCoreApplication.instance().status_message)
            self.instanceSaved.connect(QCoreApplication.instance().status_message)
            self.instanceDeleted.connect(QCoreApplication.instance().status_message)
            self.set_instance(interval)

    def analyze(self):
        job = ReanalyzeJob(self.interval)
        job.submit()

    def part_specific_content(self, instance):
        self.plugin = None
        part = instance.intervalpart
        if not part:
            logger.debug("No part? That's odd")
            return
        plugin_class = self.getPartPluginClass(part)
        if plugin_class:
            self.plugin = plugin_class(self, instance)
            self.plugin.handle(instance)

    _plugins = {
        sweattrails.session.BikePart: sweattrails.qt.session.bike.BikePlugin,
        sweattrails.session.RunPart: sweattrails.qt.session.run.RunPlugin
    }

    @classmethod
    def getPartPluginClass(cls, part):
        if part.__class__ in cls._plugins:
            logger.debug("Hardcoded plugin %s", cls._plugins[part.__class__])
            return cls._plugins[part.__class__]
        plugin_class = None
        plugin_name = gripe.Config.sweattrails.get(part.__class__.__name__)
        if plugin_name:
            logger.debug("Configured plugin %s", plugin_name)
            plugin_class = gripe.resolve(plugin_name)
            cls._plugins[part.__class__] = plugin_class
        else:
            logger.debug("No plugin")
        return plugin_class


class SessionDetails(QWidget):
    def __init__(self, parent=None):
        super(SessionDetails, self).__init__(parent)
        self.tabs = QTabWidget(self)
        layout = QVBoxLayout(self)
        layout.addWidget(self.tabs)
        self.setMinimumSize(600, 600)
        self.tabs.currentChanged.connect(self.selectInterval)
        self.session = None

    def setSession(self, session):
        self.session = session
        self.tabs.clear()
        self.tabs.addTab(IntervalPage(session, self), str(session.start_time))
        self.selectInterval(0)
        if not session.waypoints():
            self.parent().map_holder.hide_map()
        else:
            self.parent().map_holder.show_map()

    def setTab(self, tab):
        t = self.tabs.currentWidget()
        t.setTab(tab)

    def addInterval(self, interval):
        self.tabs.addTab(IntervalPage(interval, self), str(interval.timestamp))
        if interval.waypoints():
            self.parent().map.draw_segment(interval.timestamp, interval.duration)

    def selectInterval(self, ix):
        if self.session.waypoints():
            if ix == 0:
                self.parent().map.draw_map(self.session)
                # pass
            elif ix > 0:
                page = self.tabs.currentWidget()
                self.parent().map.draw_segment(page.interval.timestamp, page.interval.duration)
